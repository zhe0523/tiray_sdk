// EXPERIMENTAL ONLY: intentionally excluded from CMakeLists.txt.

#include "PcieFrameSource.h"

#include <QDateTime>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {
constexpr int kDmaReadBlockBytes = 4 * 1024 * 1024;
constexpr int kPollTimeoutMs = 100;

quint32 readLe32(const char* bytes) {
    const auto* source = reinterpret_cast<const uchar*>(bytes);
    return static_cast<quint32>(source[0])
        | (static_cast<quint32>(source[1]) << 8)
        | (static_cast<quint32>(source[2]) << 16)
        | (static_cast<quint32>(source[3]) << 24);
}

quint64 readLe64(const char* bytes) {
    quint64 value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<quint64>(static_cast<uchar>(bytes[index])) << (index * 8);
    }
    return value;
}

quint64 currentTimeUs() {
    return static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
}
}

bool PcieFrameLayout::isValid() const {
    return syncWord.size() >= 4
        && headerSize >= syncWord.size()
        && frameNumberOffset >= 0
        && frameNumberOffset + 4 <= headerSize
        && payloadLengthOffset >= 0
        && payloadLengthOffset + 4 <= headerSize
        && timestampOffset >= 0
        && timestampOffset + 8 <= headerSize
        && imageSize.width() > 0
        && imageSize.height() > 0
        && bitsPerPixel == 12
        && expectedPayloadBytes > 0
        && maximumPayloadBytes >= expectedPayloadBytes;
}

PcieFrameParser::PcieFrameParser(PcieFrameLayout layout)
    : layout_(std::move(layout)) {
}

void PcieFrameParser::append(const QByteArray& bytes) {
    buffer_.append(bytes);
}

bool PcieFrameParser::takeNext(PcieRawFrame* frame, QString* errorMessage) {
    if (frame == nullptr || !layout_.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 帧布局无效");
        }
        return false;
    }

    discardBeforeSync();
    if (buffer_.size() < layout_.headerSize) {
        return false;
    }

    PcieFrameMetadata metadata;
    quint32 payloadLength = 0;
    if (!parseHeader(&metadata, &payloadLength, errorMessage)) {
        buffer_.remove(0, 1);
        ++syncErrors_;
        return false;
    }

    const qint64 frameBytes = static_cast<qint64>(layout_.headerSize) + payloadLength;
    if (buffer_.size() < frameBytes) {
        return false;
    }

    if (hasLastFrameNumber_) {
        const quint32 expected = lastFrameNumber_ + 1;
        metadata.droppedBeforeThisFrame = metadata.frameNumber - expected;
        protocolDrops_ += metadata.droppedBeforeThisFrame;
    }
    lastFrameNumber_ = metadata.frameNumber;
    hasLastFrameNumber_ = true;
    metadata.receivedTimestampUs = currentTimeUs();

    frame->metadata = metadata;
    frame->payload = buffer_.mid(layout_.headerSize, payloadLength);
    buffer_.remove(0, static_cast<int>(frameBytes));
    return true;
}

void PcieFrameParser::reset() {
    buffer_.clear();
    hasLastFrameNumber_ = false;
    lastFrameNumber_ = 0;
    syncErrors_ = 0;
    protocolDrops_ = 0;
}

quint64 PcieFrameParser::syncErrorCount() const {
    return syncErrors_;
}

quint64 PcieFrameParser::protocolDropCount() const {
    return protocolDrops_;
}

bool PcieFrameParser::parseHeader(
    PcieFrameMetadata* metadata,
    quint32* payloadLength,
    QString* errorMessage) const {
    if (!buffer_.startsWith(layout_.syncWord)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("未对齐到 PCIe 帧同步字");
        }
        return false;
    }

    const char* header = buffer_.constData();
    *payloadLength = readLe32(header + layout_.payloadLengthOffset);
    if (*payloadLength != layout_.expectedPayloadBytes || *payloadLength > layout_.maximumPayloadBytes) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 帧载荷长度不符合已确认协议");
        }
        return false;
    }

    metadata->frameNumber = readLe32(header + layout_.frameNumberOffset);
    metadata->timestampUs = readLe64(header + layout_.timestampOffset);
    return true;
}

void PcieFrameParser::discardBeforeSync() {
    const int syncOffset = buffer_.indexOf(layout_.syncWord);
    if (syncOffset > 0) {
        buffer_.remove(0, syncOffset);
        ++syncErrors_;
    } else if (syncOffset < 0 && buffer_.size() >= layout_.syncWord.size()) {
        const int retained = layout_.syncWord.size() - 1;
        buffer_ = buffer_.right(retained);
        ++syncErrors_;
    }
}

bool Raw12Unpacker::unpack(
    const QByteArray& payload,
    const QSize& imageSize,
    Raw12Packing packing,
    QVector<quint16>* pixels,
    QString* errorMessage) {
    if (pixels == nullptr || imageSize.width() <= 0 || imageSize.height() <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("RAW12 图像尺寸无效");
        }
        return false;
    }

    const qint64 pixelCount = static_cast<qint64>(imageSize.width()) * imageSize.height();
    if ((pixelCount % 2) != 0 || payload.size() != pixelCount * 3 / 2) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("RAW12 数据长度与图像尺寸不匹配");
        }
        return false;
    }
    if (packing != Raw12Packing::MsbFirstTwoPixelsThreeBytes) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("尚未实现此 RAW12 打包方式");
        }
        return false;
    }

    pixels->resize(static_cast<int>(pixelCount));
    const auto* source = reinterpret_cast<const uchar*>(payload.constData());
    for (int pixel = 0, byte = 0; pixel < pixels->size(); pixel += 2, byte += 3) {
        (*pixels)[pixel] = static_cast<quint16>((source[byte] << 4) | (source[byte + 1] & 0x0f));
        (*pixels)[pixel + 1] = static_cast<quint16>(((source[byte + 1] & 0xf0) << 4) | source[byte + 2]);
    }
    return true;
}

PcieFrameSource::PcieFrameSource(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<PcieRawFrame>("PcieRawFrame");
}

PcieFrameSource::~PcieFrameSource() {
    stop();
}

bool PcieFrameSource::start(const QString& devicePath, const PcieFrameLayout& layout, QString* errorMessage) {
    if (running_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 图像源已在运行");
        }
        return false;
    }
    if (!layout.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("PCIe 帧布局未确认或无效");
        }
        return false;
    }

    running_ = true;
    readerThread_ = std::thread(&PcieFrameSource::readLoop, this, devicePath, layout);
    return true;
}

void PcieFrameSource::stop() {
    running_ = false;
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
}

bool PcieFrameSource::isRunning() const {
    return running_;
}

void PcieFrameSource::readLoop(QString devicePath, PcieFrameLayout layout) {
    const int fd = ::open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        emit sourceError(QStringLiteral("打开 PCIe C2H 设备失败: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        running_ = false;
        emit sourceStopped();
        return;
    }

    PcieFrameParser parser(std::move(layout));
    QByteArray chunk(kDmaReadBlockBytes, Qt::Uninitialized);
    while (running_) {
        pollfd descriptor{fd, POLLIN, 0};
        const int pollResult = ::poll(&descriptor, 1, kPollTimeoutMs);
        if (pollResult < 0) {
            if (errno != EINTR) {
                emit sourceError(QStringLiteral("等待 PCIe 数据失败: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
                break;
            }
            continue;
        }
        if (pollResult == 0) {
            continue;
        }

        const ssize_t readBytes = ::read(fd, chunk.data(), static_cast<size_t>(chunk.size()));
        if (readBytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            emit sourceError(QStringLiteral("读取 PCIe 数据失败: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
            break;
        }
        if (readBytes == 0) {
            emit sourceError(QStringLiteral("PCIe C2H 设备返回 EOF"));
            break;
        }

        parser.append(chunk.left(static_cast<int>(readBytes)));
        while (running_) {
            PcieRawFrame frame;
            QString parseError;
            if (!parser.takeNext(&frame, &parseError)) {
                // A rejected header has already advanced the parser by one
                // byte. Keep draining buffered data to resynchronize without
                // waiting for another DMA read.
                if (!parseError.isEmpty()) {
                    continue;
                }
                break;
            }
            emit frameReady(frame);
        }
    }

    ::close(fd);
    running_ = false;
    emit sourceStopped();
}
