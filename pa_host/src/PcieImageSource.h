#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "ImageSource.h"

struct PcieImageSourceOptions {
    QString c2hDevice = QStringLiteral("/dev/idma0_c2h_0");
    QString eventDevice = QStringLiteral("/dev/idma0_event_0");
    QString bar0Resource;
    QString outputDirectory;
    /* BAR0 未提供有效行列时使用的备用尺寸；正常情况下每帧尺寸来自 BAR0[0x00c/0x010]。 */
    int width = 3072;
    int height = 7680;
    int interruptTimeoutMs = 1000;
};

/*
 * PCIe 光口图像源。
 *
 * 该类把 tools/pcie_read/c2h_capture_loop.c 的 C2H 读取流程接入上位机：
 *   1. 等待 /dev/idma0_event_0 图像中断；
 *   2. 从 BAR0 读取 image_id、实际行列和最终 DDR 地址；
 *   3. 按 BAR0 行列从 /dev/idma0_c2h_0 读取一帧 RAW16；
 *   4. 先把内存图像送入统一显示链路，再由保存线程写 .tiraw 文件。
 *
 * Windows 只保留接口以便工程继续编译；实际 PCIe 采集只在 Linux/Kylin 上启用。
 */
class PcieImageSource final : public IImageSource {
    Q_OBJECT

public:
    explicit PcieImageSource(QObject* parent = nullptr);
    ~PcieImageSource() override;

    void setOptions(const PcieImageSourceOptions& options);

    bool start(QString* errorMessage) override;
    void stop() override;
    bool isRunning() const override;
    ImageSourceStats stats() const override;

signals:
    void captureInfo(const QString& message);
    void frameFileSaved(const QString& path, const TiRawImage& image);

private:
    enum class InterruptWaitResult {
        Event,
        Idle,
        Error
    };

    struct ImageMetadata {
        std::uint32_t imageId = 0;
        std::uint32_t imageType = 0;
        /* BAR0[0x00c] = 源图行数，BAR0[0x010] = 源图列数。 */
        std::uint32_t rowCount = 0;
        std::uint32_t columnCount = 0;
        std::uint64_t finalImageAddress = 0;
        /* BAR0 DMA 地址表对应的 C2H 字符设备逻辑偏移。 */
        std::uint64_t c2hOffset = 0;
    };

    struct SaveJob {
        TiRawImage image;
        ImageMetadata metadata;
        std::chrono::steady_clock::time_point interruptAt;
        qint64 displayQueuedMs = 0;
    };

    void run();
    void runSaver();
    void enqueueSave(SaveJob job);
    bool openDevices(QString* errorMessage);
    void closeDevices();
    InterruptWaitResult waitInterrupt(std::uint32_t* eventValue, QString* errorMessage);
    bool readImageMetadata(ImageMetadata* metadata, QString* errorMessage);
    bool readImagePayload(const ImageMetadata& metadata, QByteArray* payload, QString* errorMessage);
    bool saveFrameFile(const TiRawImage& image, const ImageMetadata& metadata, QString* path, QString* errorMessage);
    void finishWorker();
    static bool findBar0Resource(QString* resource, QString* errorMessage);

    PcieImageSourceOptions options_;
    std::unique_ptr<std::thread> worker_;
    std::unique_ptr<std::thread> saver_;
    std::mutex saveMutex_;
    std::condition_variable saveCondition_;
    std::deque<SaveJob> saveQueue_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_bool saveStopRequested_ {false};
    std::atomic_bool running_ {false};
    mutable std::atomic<quint64> deliveredFrames_ {0};
    mutable std::atomic<quint64> failedFrames_ {0};
    int eventFd_ = -1;
    int c2hFd_ = -1;
};
