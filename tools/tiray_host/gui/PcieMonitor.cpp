#include "PcieMonitor.h"

PcieMonitor::PcieMonitor(QObject* parent)
    : QObject(parent) {}

PcieMonitor::~PcieMonitor() {
    doStop();
}

void PcieMonitor::doStart(QString eventDevice, QString c2hDevice, QString bar0Resource) {
    if (running_.load()) {
        emit started(false, QStringLiteral("PCIe 监听已在运行"));
        return;
    }

    tiray_pcie_config_t config;
    tiray_pcie_default_config(&config);

    eventBytes_ = eventDevice.toUtf8();
    c2hBytes_ = c2hDevice.toUtf8();
    bar0Bytes_ = bar0Resource.toUtf8();
    if (!eventBytes_.isEmpty()) {
        config.event_device = eventBytes_.constData();
    }
    if (!c2hBytes_.isEmpty()) {
        config.c2h_device = c2hBytes_.constData();
    }
    if (!bar0Bytes_.isEmpty()) {
        config.bar0_resource = bar0Bytes_.constData();
    }

    receiver_ = tiray_pcie_create(&config);
    if (receiver_ == nullptr) {
        emit started(false, QStringLiteral("PCIe 接收器创建失败"));
        return;
    }

    tiray_status_t status = tiray_pcie_open(receiver_);
    if (status != TIRAY_STATUS_OK) {
        emit started(false,
            QStringLiteral("PCIe 打开失败: %1（请确认 event/c2h 设备节点和 BAR0 自动发现）")
                .arg(QString::fromUtf8(tiray_status_string(status))));
        cleanup();
        return;
    }

    status = tiray_pcie_start(receiver_, &PcieMonitor::frameCallback, this);
    if (status != TIRAY_STATUS_OK) {
        emit started(false,
            QStringLiteral("PCIe 启动失败: %1")
                .arg(QString::fromUtf8(tiray_status_string(status))));
        cleanup();
        return;
    }

    running_ = true;
    emit started(true, QStringLiteral("PCIe 帧监听已启动"));
}

void PcieMonitor::doStop() {
    if (receiver_ == nullptr) {
        return;
    }
    if (running_.load()) {
        tiray_pcie_stop(receiver_);
    }
    cleanup();
    running_ = false;
    emit stopped();
}

void PcieMonitor::cleanup() {
    if (receiver_ != nullptr) {
        tiray_pcie_close(receiver_);
        tiray_pcie_destroy(receiver_);
        receiver_ = nullptr;
    }
}

void PcieMonitor::frameCallback(const tiray_image_frame_t* frame, void* userData) {
    auto* self = static_cast<PcieMonitor*>(userData);
    if (frame == nullptr || self == nullptr) {
        return;
    }

    /* frame->data 仅回调期间有效，必须在此复制。 */
    auto copy = QSharedPointer<PcieFrame>::create();
    copy->rows = frame->rows;
    copy->columns = frame->columns;
    copy->imageId = frame->image_id;
    copy->imageType = frame->image_type;
    copy->finalAddress = frame->final_image_address;
    if (frame->data != nullptr && frame->data_length > 0) {
        copy->data = QByteArray(reinterpret_cast<const char*>(frame->data),
                                static_cast<int>(frame->data_length));
    }

    /* 跨线程发射自动按队列方式投递到界面线程。 */
    emit self->frameReceived(copy);
}
