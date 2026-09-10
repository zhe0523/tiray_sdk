#include "SdkWorker.h"

namespace {

QString formatStatus(const char* operation, tiray_status_t status) {
    return QStringLiteral("%1: %2 (代码 %3)")
        .arg(QString::fromUtf8(operation),
             QString::fromUtf8(tiray_status_string(status)))
        .arg(static_cast<int>(status));
}

} // namespace

SdkWorker::SdkWorker(QObject* parent)
    : QObject(parent) {}

SdkWorker::~SdkWorker() {
    if (sdk_ != nullptr) {
        tiray_sdk_close(sdk_);
        tiray_sdk_destroy(sdk_);
        sdk_ = nullptr;
    }
}

void SdkWorker::doOpen(QString port, quint32 baudrate, quint32 timeoutMs, bool internalProfile) {
    if (sdk_ != nullptr) {
        tiray_sdk_close(sdk_);
        tiray_sdk_destroy(sdk_);
        sdk_ = nullptr;
    }

    portBytes_ = port.toUtf8();
    tiray_sdk_default_config(&config_);
    config_.rs422_device = portBytes_.constData();
    config_.rs422_baudrate = baudrate;
    if (timeoutMs > 0) {
        config_.retry.response_timeout_ms = timeoutMs;
    }
    if (internalProfile) {
        config_.profile = TIRAY_SDK_PROFILE_INTERNAL;
    }

    sdk_ = tiray_sdk_create(&config_);
    if (sdk_ == nullptr) {
        emit operationFinished(QStringLiteral("open"), false,
            QStringLiteral("open: SDK 创建失败（外部版不支持内部配置时请去掉内部版选项）"));
        return;
    }

    const tiray_status_t status = tiray_sdk_open(sdk_);
    if (status != TIRAY_STATUS_OK) {
        tiray_sdk_destroy(sdk_);
        sdk_ = nullptr;
    }
    finish("open", status);
}

void SdkWorker::doClose() {
    if (sdk_ != nullptr) {
        tiray_sdk_close(sdk_);
        tiray_sdk_destroy(sdk_);
        sdk_ = nullptr;
    }
    emit operationFinished(QStringLiteral("close"), true, QStringLiteral("close: 已断开"));
}

bool SdkWorker::requireOpen(const char* operation) {
    if (sdk_ != nullptr && tiray_sdk_is_open(sdk_) != 0) {
        return true;
    }
    emit operationFinished(QString::fromUtf8(operation), false,
        QStringLiteral("%1: 设备未连接，请先打开串口").arg(QString::fromUtf8(operation)));
    return false;
}

void SdkWorker::finish(const char* operation, tiray_status_t status) {
    emit operationFinished(QString::fromUtf8(operation), status == TIRAY_STATUS_OK,
                           formatStatus(operation, status));
}

void SdkWorker::doPing() {
    if (!requireOpen("ping")) return;
    finish("ping", tiray_ping(sdk_));
}

void SdkWorker::doGetStatus() {
    if (!requireOpen("status")) return;
    tiray_device_status_t status{};
    const tiray_status_t result = tiray_get_status(sdk_, &status);
    if (result == TIRAY_STATUS_OK) {
        emit statusReady(status, tiray_sdk_last_device_error(sdk_));
    }
    finish("status", result);
}

void SdkWorker::doReboot() {
    if (!requireOpen("reboot")) return;
    finish("reboot", tiray_reboot(sdk_));
}

void SdkWorker::doStaticCapture() {
    if (!requireOpen("static")) return;
    finish("static", tiray_start_static_capture(sdk_));
}

void SdkWorker::doDynamicStart() {
    if (!requireOpen("dynamic_start")) return;
    finish("dynamic_start", tiray_start_dynamic(sdk_));
}

void SdkWorker::doDynamicQuery() {
    if (!requireOpen("dynamic_query")) return;
    tiray_dynamic_status_t status{};
    const tiray_status_t result = tiray_query_dynamic(sdk_, &status);
    if (result == TIRAY_STATUS_OK) {
        emit dynamicStatusReady(status);
    }
    finish("dynamic_query", result);
}

void SdkWorker::doDynamicStop() {
    if (!requireOpen("dynamic_stop")) return;
    finish("dynamic_stop", tiray_stop_dynamic(sdk_));
}

void SdkWorker::doGetConfigGroup(quint16 group) {
    if (!requireOpen("config_get")) return;
    QVector<tiray_config_item_t> items(256);
    size_t count = 0;
    const tiray_status_t result = tiray_get_config_group(
        sdk_, group, items.data(), static_cast<size_t>(items.size()), &count);
    if (result == TIRAY_STATUS_OK) {
        items.resize(static_cast<int>(count));
        emit configGroupReady(group, items);
    } else {
        items.clear();
    }
    finish("config_get", result);
}

void SdkWorker::doSetConfigGroup(quint16 group, QVector<tiray_config_item_t> items) {
    if (!requireOpen("config_set")) return;
    if (items.isEmpty()) {
        emit operationFinished(QStringLiteral("config_set"), false,
                               QStringLiteral("config_set: 没有可下发的配置项"));
        return;
    }
    finish("config_set",
           tiray_set_config_group(sdk_, group, items.constData(),
                                  static_cast<size_t>(items.size())));
}

void SdkWorker::doGetStaticConfig() {
    if (!requireOpen("static_cfg_get")) return;
    tiray_static_config_t config{};
    const tiray_status_t result = tiray_get_static_config(sdk_, &config);
    if (result == TIRAY_STATUS_OK) {
        emit staticConfigReady(config);
    }
    finish("static_cfg_get", result);
}

void SdkWorker::doSetStaticConfig(tiray_static_config_t config) {
    if (!requireOpen("static_cfg_set")) return;
    finish("static_cfg_set", tiray_set_static_config(sdk_, &config));
}

void SdkWorker::doGetDynamicConfig() {
    if (!requireOpen("dynamic_cfg_get")) return;
    tiray_dynamic_config_t config{};
    const tiray_status_t result = tiray_get_dynamic_config(sdk_, &config);
    if (result == TIRAY_STATUS_OK) {
        emit dynamicConfigReady(config);
    }
    finish("dynamic_cfg_get", result);
}

void SdkWorker::doSetDynamicConfig(tiray_dynamic_config_t config) {
    if (!requireOpen("dynamic_cfg_set")) return;
    if (config.cycle == 0) {
        emit operationFinished(QStringLiteral("dynamic_cfg_set"), false,
                               QStringLiteral("dynamic_cfg_set: cycle 必须 > 0"));
        return;
    }
    /* 先读回设备当前配置，仅覆盖常用 6 项，保留 Step 高/低电平等其余字段，
     * 避免界面未读取过的字段被清零。 */
    tiray_dynamic_config_t current{};
    if (tiray_get_dynamic_config(sdk_, &current) == TIRAY_STATUS_OK) {
        current.cycle = config.cycle;
        current.image_start_addr = config.image_start_addr;
        current.image_end_addr = config.image_end_addr;
        current.start_timeout_ms = config.start_timeout_ms;
        current.state_poll_interval_ms = config.state_poll_interval_ms;
        current.stop_timeout_ms = config.stop_timeout_ms;
        config = current;
    }
    finish("dynamic_cfg_set", tiray_set_dynamic_config(sdk_, &config));
}

void SdkWorker::doOffsetBegin(quint32 totalFrames, quint32 validFrames, quint8 mode) {
    if (!requireOpen("offset_begin")) return;
    if (validFrames == 0 || validFrames > totalFrames) {
        emit operationFinished(QStringLiteral("offset_begin"), false,
                               QStringLiteral("offset_begin: 需满足 0 < 有效帧数 <= 总帧数"));
        return;
    }
    finish("offset_begin", tiray_cal_offset_begin(sdk_, totalFrames, validFrames, mode));
}

void SdkWorker::doOffsetCapture() {
    if (!requireOpen("offset_capture")) return;
    finish("offset_capture", tiray_cal_offset_capture(sdk_));
}

void SdkWorker::doOffsetBuild() {
    if (!requireOpen("offset_build")) return;
    finish("offset_build", tiray_cal_offset_build(sdk_));
}

void SdkWorker::doOffsetCancel() {
    if (!requireOpen("offset_cancel")) return;
    finish("offset_cancel", tiray_cal_offset_cancel(sdk_));
}

void SdkWorker::doGainBegin(QVector<quint32> levels, quint32 framesPerLevel, float defectThreshold) {
    if (!requireOpen("gain_begin")) return;
    if (levels.isEmpty() || levels.size() > 16) {
        emit operationFinished(QStringLiteral("gain_begin"), false,
                               QStringLiteral("gain_begin: 灰度级数量需为 1~16"));
        return;
    }
    if (framesPerLevel == 0) {
        emit operationFinished(QStringLiteral("gain_begin"), false,
                               QStringLiteral("gain_begin: 每级帧数必须 > 0"));
        return;
    }
    finish("gain_begin",
           tiray_cal_gain_begin(sdk_, reinterpret_cast<const uint32_t*>(levels.constData()),
                                static_cast<size_t>(levels.size()), framesPerLevel, defectThreshold));
}

void SdkWorker::doGainCapture(quint32 level) {
    if (!requireOpen("gain_capture")) return;
    finish("gain_capture", tiray_cal_gain_capture(sdk_, level));
}

void SdkWorker::doGainBuild() {
    if (!requireOpen("gain_build")) return;
    finish("gain_build", tiray_cal_gain_build(sdk_));
}

void SdkWorker::doGainCancel() {
    if (!requireOpen("gain_cancel")) return;
    finish("gain_cancel", tiray_cal_gain_cancel(sdk_));
}

void SdkWorker::doGetCalStatus() {
    if (!requireOpen("cal_status")) return;
    tiray_cal_status_t status{};
    const tiray_status_t result = tiray_cal_status(sdk_, &status);
    if (result == TIRAY_STATUS_OK) {
        emit calStatusReady(status);
    }
    finish("cal_status", result);
}

void SdkWorker::doUploadConfig(quint32 templateKind, quint32 imageAddr,
                               quint32 rows, quint32 columns, quint32 packageCount) {
    if (!requireOpen("upload_config")) return;
    finish("upload_config",
           tiray_img_upload_config(sdk_, templateKind, imageAddr, rows, columns, packageCount));
}

void SdkWorker::doUploadStart() {
    if (!requireOpen("upload_start")) return;
    finish("upload_start", tiray_img_upload_start(sdk_));
}

void SdkWorker::doUploadQuery() {
    if (!requireOpen("upload_query")) return;
    tiray_image_upload_status_t status{};
    const tiray_status_t result = tiray_img_upload_query(sdk_, &status);
    if (result == TIRAY_STATUS_OK) {
        emit uploadStatusReady(status);
    }
    finish("upload_query", result);
}
