#pragma once

/*
 * PcieMonitor —— 通过 tiray_sdk 公共 PCIe 接口接收图像帧。
 *
 * 帧回调运行在 SDK 线程，回调内复制帧数据后立即返回，
 * 通过队列信号把帧送到界面线程。启动/停止均在界面线程调用。
 */

#include <QByteArray>
#include <QObject>
#include <QSharedPointer>

#include <atomic>

#include "tiray_sdk.h"

struct PcieFrame {
    QByteArray data;      /* rows * columns * 2，16-bit 灰度，小端 */
    quint32 rows = 0;
    quint32 columns = 0;
    quint32 imageId = 0;
    quint32 imageType = 0; /* 0=正常图像 1=模板上传 */
    quint64 finalAddress = 0;
};

Q_DECLARE_METATYPE(QSharedPointer<PcieFrame>)

class PcieMonitor : public QObject {
    Q_OBJECT

public:
    explicit PcieMonitor(QObject* parent = nullptr);
    ~PcieMonitor() override;

    PcieMonitor(const PcieMonitor&) = delete;
    PcieMonitor& operator=(const PcieMonitor&) = delete;

    bool isRunning() const { return running_.load(); }

signals:
    void started(bool ok, QString message);
    void stopped();
    void frameReceived(QSharedPointer<PcieFrame> frame);

public slots:
    /* 空字符串使用 SDK 默认设备路径与 BAR0 自动发现。 */
    void doStart(QString eventDevice, QString c2hDevice, QString bar0Resource);
    void doStop();

private:
    static void frameCallback(const tiray_image_frame_t* frame, void* userData);
    void cleanup();

    tiray_pcie_receiver_t* receiver_ = nullptr;
    std::atomic<bool> running_{false};
    QByteArray eventBytes_;
    QByteArray c2hBytes_;
    QByteArray bar0Bytes_; /* 指针须在接收器生命周期内有效 */
};
