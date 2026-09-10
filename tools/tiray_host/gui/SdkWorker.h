#pragma once

/*
 * SdkWorker —— 在独立工作线程中执行 tiray_sdk 公共 API 调用。
 *
 * 只依赖 include/tiray_sdk.h 的公共 C 接口，不接触任何协议细节。
 * 所有 do* 槽通过队列连接在后台线程执行，避免阻塞界面；
 * 结果通过信号回到界面线程。
 */

#include <QObject>
#include <QString>
#include <QVector>

#include "tiray_sdk.h"

class SdkWorker : public QObject {
    Q_OBJECT

public:
    explicit SdkWorker(QObject* parent = nullptr);
    ~SdkWorker() override;

    SdkWorker(const SdkWorker&) = delete;
    SdkWorker& operator=(const SdkWorker&) = delete;

signals:
    /* 每条命令结束都会发出；operation 为命令名，ok 表示成功。 */
    void operationFinished(QString operation, bool ok, QString message);
    void statusReady(tiray_device_status_t status, quint32 lastDeviceError);
    void dynamicStatusReady(tiray_dynamic_status_t status);
    void configGroupReady(quint16 group, QVector<tiray_config_item_t> items);
    void staticConfigReady(tiray_static_config_t config);
    void dynamicConfigReady(tiray_dynamic_config_t config);
    void calStatusReady(tiray_cal_status_t status);
    void uploadStatusReady(tiray_image_upload_status_t status);

public slots:
    void doOpen(QString port, quint32 baudrate, quint32 timeoutMs, bool internalProfile);
    void doClose();
    void doPing();
    void doGetStatus();
    void doReboot();
    void doStaticCapture();
    void doDynamicStart();
    void doDynamicQuery();
    void doDynamicStop();
    void doGetConfigGroup(quint16 group);
    void doSetConfigGroup(quint16 group, QVector<tiray_config_item_t> items);
    void doGetStaticConfig();
    void doSetStaticConfig(tiray_static_config_t config);
    void doGetDynamicConfig();
    void doSetDynamicConfig(tiray_dynamic_config_t config);
    void doOffsetBegin(quint32 totalFrames, quint32 validFrames, quint8 mode);
    void doOffsetCapture();
    void doOffsetBuild();
    void doOffsetCancel();
    void doGainBegin(QVector<quint32> levels, quint32 framesPerLevel, float defectThreshold);
    void doGainCapture(quint32 level);
    void doGainBuild();
    void doGainCancel();
    void doGetCalStatus();
    void doUploadConfig(quint32 templateKind, quint32 imageAddr,
                        quint32 rows, quint32 columns, quint32 packageCount);
    void doUploadStart();
    void doUploadQuery();

private:
    bool requireOpen(const char* operation);
    void finish(const char* operation, tiray_status_t status);

    tiray_sdk_t* sdk_ = nullptr;
    tiray_sdk_config_t config_{};
    QByteArray portBytes_;
};
