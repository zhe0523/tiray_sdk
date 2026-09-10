#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QTimer>

#include <optional>

#include "PaBinaryProtocol.h"
#include "PaProtocol.h"

class ILineTransport;

enum class PaDeviceState {
    Disconnected,
    Ready,
    Busy,
    Error
};

struct PaDeviceStatus {
    bool valid = false;
    quint32 interruptVector = 0;
    quint32 paVersion = 0;
    quint32 communicationVersion = 0;
    quint32 resetState = 0;
    quint32 workMode = 0;
    quint32 workState = 0;
    quint32 lastError = 0;
    quint32 frameCount = 0;
    quint32 dynamicState = 0;
    quint32 imageUploadState = 0;
    int writeState = -1;
    int writeEnd = -1;
    int correctionState = -1;
    int correctionEnd = -1;
    quint32 captureId = 0;
    quint32 outputAddr = 0;
    quint32 offsetAddr = 0;
    QString model;
    QString serialNumber;
    QString armVersion;
    QString fpgaVersion;
    QString rawLine;
};

/*
 * PA 设备控制层：管理串口连接、单条在途命令、响应解析和超时。
 * 界面只消费结构化状态，不直接处理串口帧或 QSerialPort。
 */
class PaDeviceController final : public QObject {
    Q_OBJECT

public:
    explicit PaDeviceController(ILineTransport* transport, QObject* parent = nullptr);

    bool connectDevice(const QString& portName, int baudRate, QString* errorMessage = nullptr);
    void disconnectDevice();
    bool sendCommand(PaProtocol::Command command, QString* errorMessage = nullptr);
    /* 正式协议配置组：组号和 item_id/value 均为固定二进制字段。 */
    bool requestConfigGroup(quint16 groupId, QString* errorMessage = nullptr);
    bool setConfigGroup(quint16 groupId,
                        const QMap<quint16, quint32>& values,
                        QString* errorMessage = nullptr);
    /* 开发调试窗口：读取除 INT_VECTOR 外的安全寄存器快照。 */
    bool dumpRegisters(QString* errorMessage = nullptr);
    /* 开发调试窗口：直接写一个寄存器并等待设备确认。 */
    bool writeRegister(quint16 offset, quint32 value, QString* errorMessage = nullptr);
    /* 发送设备重启指令；下位机确认接收后会执行系统重启。 */
    bool restartDevice(QString* errorMessage = nullptr);
    /* Dynamic 状态查询和模板制作均使用正式二进制业务命令。 */
    bool queryDynamic(QString* errorMessage = nullptr);
    bool beginOffsetCalibration(quint32 totalFrames,
                                quint32 validFrames,
                                quint32 mode,
                                QString* errorMessage = nullptr);
    bool captureOffsetCalibration(QString* errorMessage = nullptr);
    bool buildOffsetCalibration(QString* errorMessage = nullptr);
    bool cancelOffsetCalibration(QString* errorMessage = nullptr);
    bool beginGainCalibration(const QList<quint32>& levels,
                              quint32 framesPerLevel,
                              float defectThreshold,
                              QString* errorMessage = nullptr);
    bool captureGainCalibration(quint32 level, QString* errorMessage = nullptr);
    bool buildGainCalibration(QString* errorMessage = nullptr);
    bool cancelGainCalibration(QString* errorMessage = nullptr);
    bool queryCalibration(QString* errorMessage = nullptr);
    bool configureTemplateUpload(bool gainTemplate,
                                 quint32 rows = 7680,
                                 quint32 columns = 3072,
                                 QString* errorMessage = nullptr);
    bool startTemplateUpload(QString* errorMessage = nullptr);
    bool queryTemplateUpload(QString* errorMessage = nullptr);

    bool isConnected() const;
    bool hasPendingCommand() const;
    PaDeviceState state() const;
    int commandTimeoutMs() const;
    void setCommandTimeoutMs(int timeoutMs);
    int maxCommandRetries() const;
    void setMaxCommandRetries(int retries);

signals:
    void stateChanged(PaDeviceState state);
    void deviceStatusChanged(const PaDeviceStatus& status);
    void commandFinished(PaProtocol::Command command, bool success, const QString& detail);
    void configGroupReceived(quint16 groupId,
                             const QMap<quint16, quint32>& values,
                             bool success,
                             const QString& detail);
    void registerDumpReceived(const QMap<quint16, quint32>& values,
                              bool success,
                              const QString& detail);
    void registerWriteFinished(quint16 offset, quint32 value,
                               bool success,
                               const QString& detail);
    void binaryCommandFinished(quint16 command,
                               const QMap<quint16, quint32>& values,
                               bool success,
                               const QString& detail);
    void lineTransmitted(const QString& line);
    void errorOccurred(const QString& message);

private:
    bool sendRawBinaryRequest(quint16 command,
                              const QByteArray& payload,
                              quint16 groupId,
                              QString* errorMessage);
    void handleTransportConnectionChanged(bool connected);
    void handleTransportError(const QString& message);
    void handleBinaryFrame(const PaBinaryProtocol::Frame& frame);
    void handleCommandTimeout();
    bool retryPendingBinaryRequest();
    void setState(PaDeviceState state);
    void finishPendingCommand(bool success, const QString& detail);
    void finishRawBinary(bool success,
                         const QMap<quint16, quint32>& values,
                         const QString& detail);
    bool parseBinaryDeviceStatus(
        const PaBinaryProtocol::Frame& frame,
        PaDeviceStatus* status,
        QString* errorMessage) const;
    bool rejectOperation(const QString& message, QString* errorMessage);

    ILineTransport* transport_ = nullptr;
    QTimer commandTimer_;
    std::optional<PaProtocol::Command> pendingCommand_;
    bool rawBinaryPending_ = false;
    quint16 pendingBinaryCommand_ = 0;
    quint16 pendingConfigGroup_ = 0;
    quint32 pendingSequence_ = 0;
    quint32 nextSequence_ = 1;
    PaBinaryProtocol::Frame pendingBinaryFrame_;
    int binaryRetryCount_ = 0;
    int maxBinaryRetries_ = 2;
    int pendingTimeoutMs_ = 5000;
    /* SET_CONFIG_GROUP 超过 8 项时按多帧顺序发送，避免大 payload 占满 RS422。 */
    QList<QByteArray> pendingRawPayloadChunks_;
    PaDeviceState state_ = PaDeviceState::Disconnected;
    int commandTimeoutMs_ = 500;
};

Q_DECLARE_METATYPE(PaDeviceState)
Q_DECLARE_METATYPE(PaDeviceStatus)
