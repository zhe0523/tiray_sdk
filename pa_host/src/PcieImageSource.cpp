#include "PcieImageSource.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>

#ifndef Q_OS_WIN
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
#ifndef Q_OS_WIN
QString imageTypeName(std::uint32_t imageType) {
    if (imageType == 0u) return QStringLiteral("正常图片");
    if (imageType == 1u) return QStringLiteral("模板上传");
    return QStringLiteral("未知类型(%1)").arg(imageType);
}
#endif

#ifndef Q_OS_WIN
constexpr int kBar0MapSize = 4096;
constexpr int kIoChunkSize = 1 << 20;
constexpr int kStopPollSliceMs = 200;
constexpr int kPciVendorDeviceTextSize = 32;
/* 收到 PCIe 中断后、读取 C2H 内存前的等待时间，单位 us。 */
constexpr unsigned int kC2hReadDelayUs = 0;
constexpr std::uint32_t kBar0DmaAddressTableBase = 0x100;
constexpr std::uint32_t kBar0DmaAddressTableStride = 0x8;
constexpr int kBar0DmaAddressTableEntries = 69;

QString errnoMessage(const QString& what) {
    return QStringLiteral("%1: %2 (errno=%3)")
        .arg(what, QString::fromLocal8Bit(std::strerror(errno)))
        .arg(errno);
}

std::uint32_t readLe32(const void* address) {
    const auto* p = reinterpret_cast<const volatile unsigned char*>(address);
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16)
        | (static_cast<std::uint32_t>(p[3]) << 24);
}
QString defaultPcieOutputDirectory() {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(appData.isEmpty() ? QDir::homePath() : appData).filePath(QStringLiteral("pcie_images"));
}
#endif
}

PcieImageSource::PcieImageSource(QObject* parent)
    : IImageSource(parent) {
    qRegisterMetaType<ImageFrame>("ImageFrame");
    qRegisterMetaType<TiRawImage>("TiRawImage");
}

PcieImageSource::~PcieImageSource() {
    stop();
}

void PcieImageSource::setOptions(const PcieImageSourceOptions& options) {
    if (running_) {
        return;
    }
    options_ = options;
}

bool PcieImageSource::start(QString* errorMessage) {
#ifdef Q_OS_WIN
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("PCIe 图像接收只支持 Linux/Kylin 环境");
    }
    return false;
#else
    if (running_) {
        return true;
    }
    if (options_.width <= 0 || options_.height <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 图像尺寸配置无效");
        }
        return false;
    }
    if (options_.outputDirectory.isEmpty()) {
        options_.outputDirectory = defaultPcieOutputDirectory();
    }
    if (!openDevices(errorMessage)) {
        closeDevices();
        return false;
    }

    deliveredFrames_ = 0;
    failedFrames_ = 0;
    stopRequested_ = false;
    saveStopRequested_ = false;
    {
        std::lock_guard<std::mutex> lock(saveMutex_);
        saveQueue_.clear();
    }
    running_ = true;
    emit runningChanged(true);
    saver_ = std::make_unique<std::thread>(&PcieImageSource::runSaver, this);
    worker_ = std::make_unique<std::thread>(&PcieImageSource::run, this);
    return true;
#endif
}

void PcieImageSource::stop() {
    stopRequested_ = true;
    if (worker_ != nullptr && worker_->joinable()) {
        worker_->join();
    }
    worker_.reset();
    saveStopRequested_ = true;
    saveCondition_.notify_all();
    if (saver_ != nullptr && saver_->joinable()) {
        saver_->join();
    }
    saver_.reset();
    closeDevices();
    if (running_.exchange(false)) {
        emit runningChanged(false);
    }
}

bool PcieImageSource::isRunning() const {
    return running_;
}

ImageSourceStats PcieImageSource::stats() const {
    ImageSourceStats result;
    result.deliveredFrames = deliveredFrames_.load();
    result.failedFrames = failedFrames_.load();
    return result;
}

void PcieImageSource::run() {
#ifdef Q_OS_WIN
    finishWorker();
#else
    bool haveLastMetadata = false;
    ImageMetadata lastMetadata;

    while (!stopRequested_) {
        std::uint32_t eventValue = 0;
        QString error;
        QElapsedTimer totalTimer;
        QElapsedTimer stageTimer;
        totalTimer.start();
        stageTimer.start();
        const InterruptWaitResult waitResult = waitInterrupt(&eventValue, &error);
        const qint64 waitMs = stageTimer.elapsed();
        if (waitResult == InterruptWaitResult::Idle) {
            continue;
        }
        if (waitResult == InterruptWaitResult::Error) {
            if (!stopRequested_ && !error.isEmpty()) {
                ++failedFrames_;
                emit sourceError(QStringLiteral("PCIe 等待图像中断失败: %1").arg(error));
            }
            continue;
        }
        if (stopRequested_) {
            break;
        }
        const auto interruptAt = std::chrono::steady_clock::now();
        emit captureInfo(QStringLiteral("PCIe 图像中断: event=0x%1 wait=%2ms")
            .arg(eventValue, 0, 16)
            .arg(waitMs));

        /* 中断到达与整帧 DDR 写入完成之间可能存在硬件时序间隔。 */
        if (kC2hReadDelayUs > 0) {
            ::usleep(kC2hReadDelayUs);
        }

        ImageMetadata metadata;
        stageTimer.restart();
        if (!readImageMetadata(&metadata, &error)) {
            ++failedFrames_;
            emit sourceError(QStringLiteral("PCIe BAR0 图像信息读取失败: %1").arg(error));
            continue;
        }
        const qint64 bar0Ms = stageTimer.elapsed();
        if (haveLastMetadata
            && metadata.imageId == lastMetadata.imageId
            && metadata.finalImageAddress == lastMetadata.finalImageAddress
            && metadata.imageType == lastMetadata.imageType
            && metadata.rowCount == lastMetadata.rowCount
            && metadata.columnCount == lastMetadata.columnCount) {
            continue;
        }
        haveLastMetadata = true;
        lastMetadata = metadata;

        QByteArray payload;
        stageTimer.restart();
        if (!readImagePayload(metadata, &payload, &error)) {
            ++failedFrames_;
            emit sourceError(QStringLiteral("PCIe C2H 图像读取失败: %1").arg(error));
            continue;
        }
        const qint64 c2hMs = stageTimer.elapsed();

        TiRawImage image;
        const QString sourceName = QStringLiteral("PCIe #%1 [%2] @0x%3")
            .arg(metadata.imageId)
            .arg(imageTypeName(metadata.imageType))
            .arg(metadata.finalImageAddress, 0, 16);
        stageTimer.restart();
        if (metadata.columnCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            || metadata.rowCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            ++failedFrames_;
            emit sourceError(QStringLiteral("PCIe BAR0 图像尺寸超过 Qt int 范围: row=%1 col=%2")
                .arg(metadata.rowCount)
                .arg(metadata.columnCount));
            continue;
        }
        const int frameWidth = metadata.columnCount > 0 ? static_cast<int>(metadata.columnCount) : options_.width;
        const int frameHeight = metadata.rowCount > 0 ? static_cast<int>(metadata.rowCount) : options_.height;
        if (!image.loadRaw16Data(payload, frameWidth, frameHeight, sourceName, &error)) {
            ++failedFrames_;
            emit sourceError(QStringLiteral("PCIe RAW16 图像解析失败: %1").arg(error));
            continue;
        }
        const qint64 parseMs = stageTimer.elapsed();

        SaveJob saveJob;
        saveJob.image = image;
        saveJob.metadata = metadata;
        saveJob.interruptAt = interruptAt;
        ImageFrame frame;
        frame.image = std::move(image);
        frame.sourceName = sourceName;
        frame.sourceImageType = metadata.imageType;
        frame.sourceImageTypeValid = true;
        frame.sequence = deliveredFrames_.load();
        frame.receivedAt = QDateTime::currentDateTimeUtc();
        ++deliveredFrames_;
        emit frameReady(frame);
        const qint64 emitMs = totalTimer.elapsed();

        emit captureInfo(QStringLiteral(
            "PCIe 图像已进入显示队列: id=%1 type=%2(%3) event=0x%4 final_addr=0x%5 c2h_offset=0x%6 size=%7x%8 bytes=%9 wait=%10ms bar0=%11ms c2h=%12ms parse=%13ms emit_total=%14ms")
                .arg(metadata.imageId)
                .arg(metadata.imageType)
                .arg(imageTypeName(metadata.imageType))
                .arg(eventValue, 0, 16)
                .arg(metadata.finalImageAddress, 0, 16)
                .arg(metadata.c2hOffset, 0, 16)
                .arg(frameWidth)
                .arg(frameHeight)
                .arg(payload.size())
                .arg(waitMs)
                .arg(bar0Ms)
                .arg(c2hMs)
                .arg(parseMs)
                .arg(emitMs));
        saveJob.displayQueuedMs = emitMs;
        enqueueSave(std::move(saveJob));
    }
    finishWorker();
#endif
}

void PcieImageSource::runSaver() {
#ifdef Q_OS_WIN
    return;
#else
    while (true) {
        SaveJob job;
        {
            std::unique_lock<std::mutex> lock(saveMutex_);
            saveCondition_.wait(lock, [this]() {
                return saveStopRequested_ || !saveQueue_.empty();
            });
            if (saveQueue_.empty()) {
                if (saveStopRequested_) {
                    break;
                }
                continue;
            }
            job = std::move(saveQueue_.front());
            saveQueue_.pop_front();
        }

        QString framePath;
        QString error;
        QElapsedTimer stageTimer;
        stageTimer.start();
        if (!saveFrameFile(job.image, job.metadata, &framePath, &error)) {
            ++failedFrames_;
            emit sourceError(QStringLiteral("PCIe 图像保存失败: %1").arg(error));
            continue;
        }
        const qint64 saveMs = stageTimer.elapsed();
        // 保存线程复用已解析的内存图像，避免主线程为缩略图和信息统计再次读盘。
        emit frameFileSaved(framePath, job.image);
        const qint64 totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - job.interruptAt).count();
        const qint64 queueMs = std::max<qint64>(0, totalMs - job.displayQueuedMs - saveMs);
        emit captureInfo(QStringLiteral(
            "PCIe 图像文件已保存: id=%1 type=%2(%3) queue=%4ms save=%5ms total=%6ms path=%7")
                .arg(job.metadata.imageId)
                .arg(job.metadata.imageType)
                .arg(imageTypeName(job.metadata.imageType))
                .arg(queueMs)
                .arg(saveMs)
                .arg(totalMs)
                .arg(framePath));
    }
#endif
}

void PcieImageSource::enqueueSave(SaveJob job) {
    {
        std::lock_guard<std::mutex> lock(saveMutex_);
        saveQueue_.push_back(std::move(job));
    }
    saveCondition_.notify_one();
}

bool PcieImageSource::openDevices(QString* errorMessage) {
#ifdef Q_OS_WIN
    Q_UNUSED(errorMessage);
    return false;
#else
    if (options_.bar0Resource.isEmpty()
        && !findBar0Resource(&options_.bar0Resource, errorMessage)) {
        return false;
    }

    eventFd_ = ::open(options_.eventDevice.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (eventFd_ < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = errnoMessage(options_.eventDevice);
        }
        return false;
    }

    c2hFd_ = ::open(options_.c2hDevice.toLocal8Bit().constData(), O_RDONLY);
    if (c2hFd_ < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = errnoMessage(options_.c2hDevice);
        }
        return false;
    }
    return true;
#endif
}

void PcieImageSource::closeDevices() {
#ifndef Q_OS_WIN
    if (eventFd_ >= 0) {
        ::close(eventFd_);
        eventFd_ = -1;
    }
    if (c2hFd_ >= 0) {
        ::close(c2hFd_);
        c2hFd_ = -1;
    }
#endif
}

PcieImageSource::InterruptWaitResult PcieImageSource::waitInterrupt(std::uint32_t* eventValue, QString* errorMessage) {
#ifdef Q_OS_WIN
    Q_UNUSED(eventValue);
    Q_UNUSED(errorMessage);
    return InterruptWaitResult::Error;
#else
    int elapsedMs = 0;
    while (!stopRequested_) {
        const int timeoutMs = options_.interruptTimeoutMs <= 0
            ? kStopPollSliceMs
            : std::min(kStopPollSliceMs, options_.interruptTimeoutMs - elapsedMs);
        struct pollfd pfd = {eventFd_, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, timeoutMs);
        if (ready > 0 && (pfd.revents & POLLIN)) {
            const ssize_t got = ::read(eventFd_, eventValue, sizeof(*eventValue));
            if (got == static_cast<ssize_t>(sizeof(*eventValue))) {
                return InterruptWaitResult::Event;
            }
            if (errorMessage != nullptr) {
                *errorMessage = errnoMessage(options_.eventDevice);
            }
            return InterruptWaitResult::Error;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errorMessage != nullptr) {
                *errorMessage = errnoMessage(options_.eventDevice);
            }
            return InterruptWaitResult::Error;
        }
        if (options_.interruptTimeoutMs > 0) {
            elapsedMs += timeoutMs;
            if (elapsedMs >= options_.interruptTimeoutMs) {
                return InterruptWaitResult::Idle;
            }
        }
    }
    return InterruptWaitResult::Idle;
#endif
}

bool PcieImageSource::readImageMetadata(ImageMetadata* metadata, QString* errorMessage) {
#ifdef Q_OS_WIN
    Q_UNUSED(metadata);
    Q_UNUSED(errorMessage);
    return false;
#else
    const int fd = ::open(options_.bar0Resource.toLocal8Bit().constData(), O_RDONLY);
    if (fd < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = errnoMessage(options_.bar0Resource);
        }
        return false;
    }
    void* map = ::mmap(nullptr, kBar0MapSize, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        if (errorMessage != nullptr) {
            *errorMessage = errnoMessage(options_.bar0Resource);
        }
        return false;
    }

    const auto* base = static_cast<const unsigned char*>(map);
    metadata->rowCount = readLe32(base + 0x0c);
    metadata->columnCount = readLe32(base + 0x10);
    metadata->imageId = readLe32(base + 0x14);
    metadata->imageType = readLe32(base + 0x18);
    const std::uint32_t finalAddrLo = readLe32(base + 0x1c);
    const std::uint32_t finalAddrHi = readLe32(base + 0x20);
    metadata->finalImageAddress = (static_cast<std::uint64_t>(finalAddrHi) << 32)
        | static_cast<std::uint64_t>(finalAddrLo);
    const std::uint32_t blockSize = readLe32(base + 0x08);
    if (blockSize == 0) {
        ::munmap(map, kBar0MapSize);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("BAR0 C2H 块大小为 0");
        }
        return false;
    }
    bool addressMatched = false;
    for (int index = 0; index < kBar0DmaAddressTableEntries; ++index) {
        const std::uint32_t addressOffset = kBar0DmaAddressTableBase
            + static_cast<std::uint32_t>(index) * kBar0DmaAddressTableStride;
        const std::uint32_t dmaLo = readLe32(base + addressOffset);
        const std::uint32_t dmaHi = readLe32(base + addressOffset + 4);
        const std::uint64_t dmaAddress = (static_cast<std::uint64_t>(dmaHi) << 32)
            | static_cast<std::uint64_t>(dmaLo);
        if (metadata->finalImageAddress >= dmaAddress
            && metadata->finalImageAddress - dmaAddress < blockSize) {
            metadata->c2hOffset = static_cast<std::uint64_t>(index) * blockSize
                + (metadata->finalImageAddress - dmaAddress);
            addressMatched = true;
            break;
        }
    }
    if (!addressMatched) {
        ::munmap(map, kBar0MapSize);
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("BAR0 最终图像地址未在 C2H DMA 地址表中找到: 0x%1")
                .arg(metadata->finalImageAddress, 0, 16);
        }
        return false;
    }
    ::munmap(map, kBar0MapSize);
    return true;
#endif
}

bool PcieImageSource::readImagePayload(const ImageMetadata& metadata,
                                       QByteArray* payload,
                                       QString* errorMessage) {
#ifdef Q_OS_WIN
    Q_UNUSED(metadata);
    Q_UNUSED(payload);
    Q_UNUSED(errorMessage);
    return false;
#else
    const std::uint32_t rows = metadata.rowCount > 0 ? metadata.rowCount : static_cast<std::uint32_t>(options_.height);
    const std::uint32_t columns = metadata.columnCount > 0 ? metadata.columnCount : static_cast<std::uint32_t>(options_.width);
    const qint64 imageBytes = static_cast<qint64>(rows) * static_cast<qint64>(columns) * 2;
    if (rows == 0 || columns == 0 || imageBytes <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 图像尺寸无效: row=%1 col=%2").arg(rows).arg(columns);
        }
        return false;
    }
    if (imageBytes > std::numeric_limits<int>::max()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 图像过大，超过当前 Qt QByteArray 单帧容量");
        }
        return false;
    }
    payload->resize(static_cast<int>(imageBytes));
    if (::lseek(c2hFd_, static_cast<off_t>(metadata.c2hOffset), SEEK_SET) < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = errnoMessage(options_.c2hDevice);
        }
        return false;
    }

    qint64 total = 0;
    while (total < imageBytes && !stopRequested_) {
        const int want = static_cast<int>(std::min<qint64>(kIoChunkSize, imageBytes - total));
        const ssize_t got = ::read(c2hFd_, payload->data() + total, want);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            if (errorMessage != nullptr) {
                *errorMessage = got == 0
                    ? QStringLiteral("%1: 读到 EOF，offset=0x%2").arg(options_.c2hDevice).arg(total, 0, 16)
                    : errnoMessage(options_.c2hDevice);
            }
            return false;
        }
        total += got;
    }
    return total == imageBytes && !stopRequested_;
#endif
}

bool PcieImageSource::saveFrameFile(
    const TiRawImage& image,
    const ImageMetadata& metadata,
    QString* path,
    QString* errorMessage) {
#ifdef Q_OS_WIN
    Q_UNUSED(image);
    Q_UNUSED(metadata);
    Q_UNUSED(path);
    Q_UNUSED(errorMessage);
    return false;
#else
    QDir directory(options_.outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建目录: %1").arg(directory.absolutePath());
        }
        return false;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString fileName = QStringLiteral("pcie_%1_id%2_type%3_%4x%5_addr0x%6.tiraw")
        .arg(timestamp)
        .arg(metadata.imageId)
        .arg(metadata.imageType)
        .arg(image.width())
        .arg(image.height())
        .arg(metadata.finalImageAddress, 0, 16);
    const QString filePath = directory.filePath(fileName);
    if (!image.saveTiRaw(filePath, errorMessage)) {
        return false;
    }
    if (path != nullptr) {
        *path = QFileInfo(filePath).absoluteFilePath();
    }
    return true;
#endif
}

void PcieImageSource::finishWorker() {
    if (running_.exchange(false)) {
        emit runningChanged(false);
    }
}

bool PcieImageSource::findBar0Resource(QString* resource, QString* errorMessage) {
#ifdef Q_OS_WIN
    Q_UNUSED(resource);
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("PCIe 图像接收只支持 Linux/Kylin 环境");
    }
    return false;
#else
    DIR* directory = ::opendir("/sys/bus/pci/devices");
    if (directory == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = errnoMessage(QStringLiteral("/sys/bus/pci/devices"));
        }
        return false;
    }

    while (dirent* entry = ::readdir(directory)) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        const QString deviceDir = QStringLiteral("/sys/bus/pci/devices/%1")
            .arg(QString::fromLocal8Bit(entry->d_name));
        QFile vendorFile(deviceDir + QStringLiteral("/vendor"));
        QFile deviceFile(deviceDir + QStringLiteral("/device"));
        if (!vendorFile.open(QIODevice::ReadOnly) || !deviceFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray vendor = vendorFile.read(kPciVendorDeviceTextSize).trimmed();
        const QByteArray device = deviceFile.read(kPciVendorDeviceTextSize).trimmed();
        if (::strcasecmp(vendor.constData(), "0x1b4d") == 0
            && ::strcasecmp(device.constData(), "0x6667") == 0) {
            *resource = deviceDir + QStringLiteral("/resource0");
            ::closedir(directory);
            return true;
        }
    }

    ::closedir(directory);
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("未找到 vendor=0x1b4d device=0x6667 的 PCIe BAR0 resource0");
    }
    return false;
#endif
}
