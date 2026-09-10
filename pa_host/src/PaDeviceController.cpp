#include "PaDeviceController.h"

#include "ILineTransport.h"

#include <cstring>
#include <limits>

namespace {
QString formatVersionWord(quint32 value) {
    return QStringLiteral("%1.%2.%3")
        .arg((value >> 16) & 0xffu)
        .arg((value >> 8) & 0xffu)
        .arg(value & 0xffu);
}

void appendLe16(QByteArray* payload, quint16 value) {
    payload->append(static_cast<char>(value & 0xffu));
    payload->append(static_cast<char>((value >> 8u) & 0xffu));
}

void appendLe32(QByteArray* payload, quint32 value) {
    payload->append(static_cast<char>(value & 0xffu));
    payload->append(static_cast<char>((value >> 8u) & 0xffu));
    payload->append(static_cast<char>((value >> 16u) & 0xffu));
    payload->append(static_cast<char>((value >> 24u) & 0xffu));
}

void appendTlvU32(QByteArray* payload, quint16 type, quint32 value) {
    appendLe16(payload, type);
    appendLe16(payload, 4);
    appendLe32(payload, value);
}

void appendTlvU16(QByteArray* payload, quint16 type, quint16 value) {
    appendLe16(payload, type);
    appendLe16(payload, 2);
    appendLe16(payload, value);
}

void appendTlvU8(QByteArray* payload, quint16 type, quint8 value) {
    appendLe16(payload, type);
    appendLe16(payload, 1);
    payload->append(static_cast<char>(value));
}

void appendTlvBytes(QByteArray* payload, quint16 type, const QByteArray& value) {
    appendLe16(payload, type);
    appendLe16(payload, static_cast<quint16>(value.size()));
    payload->append(value);
}

int commandTimeoutFor(quint16 command, int configuredTimeoutMs) {
    if (command == 0x0501 || command == 0x0301 || command == 0x0305) {
        return qMax(configuredTimeoutMs, 15000);
    }
    if (command == 0x0302 || command == 0x0306) {
        return qMax(configuredTimeoutMs, 60000);
    }
    return configuredTimeoutMs;
}
}

PaDeviceController::PaDeviceController(ILineTransport* transport, QObject* parent)
    : QObject(parent)
    , transport_(transport) {
    qRegisterMetaType<PaDeviceState>("PaDeviceState");
    qRegisterMetaType<PaDeviceStatus>("PaDeviceStatus");
    qRegisterMetaType<PaProtocol::Command>("PaProtocol::Command");

    commandTimer_.setSingleShot(true);
    connect(&commandTimer_, &QTimer::timeout, this, &PaDeviceController::handleCommandTimeout);
    if (transport_ != nullptr) {
        connect(transport_, &ILineTransport::connectionChanged,
            this, &PaDeviceController::handleTransportConnectionChanged);
        connect(transport_, &ILineTransport::errorOccurred,
            this, &PaDeviceController::handleTransportError);
        connect(transport_, &ILineTransport::binaryFrameReceived,
            this, &PaDeviceController::handleBinaryFrame);
        state_ = transport_->isOpen() ? PaDeviceState::Ready : PaDeviceState::Disconnected;
    }
}

bool PaDeviceController::connectDevice(
    const QString& portName,
    int baudRate,
    QString* errorMessage) {
    if (transport_ == nullptr) {
        return rejectOperation(QStringLiteral("控制传输未配置"), errorMessage);
    }
    if (hasPendingCommand()) {
        return rejectOperation(QStringLiteral("设备正在执行命令，不能重新连接"), errorMessage);
    }
    if (transport_->isOpen()) {
        setState(PaDeviceState::Ready);
        return true;
    }

    QString transportError;
    if (!transport_->open(portName, baudRate, &transportError)) {
        const QString message = transportError.isEmpty()
            ? QStringLiteral("串口打开失败")
            : transportError;
        setState(PaDeviceState::Error);
        emit errorOccurred(message);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    }

    // 正式串口和测试传输都应发 connectionChanged；这里保留兜底状态同步。
    setState(PaDeviceState::Ready);
    return true;
}

void PaDeviceController::disconnectDevice() {
    if (rawBinaryPending_) {
        finishRawBinary(false, {}, QStringLiteral("设备连接已断开，配置命令取消"));
    }
    if (pendingCommand_.has_value()) {
        finishPendingCommand(false, QStringLiteral("设备连接已断开，命令取消"));
    }
    if (transport_ != nullptr) {
        transport_->close();
    }
    setState(PaDeviceState::Disconnected);
}

bool PaDeviceController::sendCommand(
    PaProtocol::Command command,
    QString* errorMessage) {
    if (transport_ == nullptr || !transport_->isOpen()) {
        setState(PaDeviceState::Disconnected);
        return rejectOperation(QStringLiteral("串口未连接"), errorMessage);
    }
    if (hasPendingCommand()) {
        return rejectOperation(QStringLiteral("上一条命令尚未完成"), errorMessage);
    }

    quint16 binaryCommand = 0;
    switch (command) {
        case PaProtocol::Command::Ping:
            binaryCommand = 0x0002;
            break;
        case PaProtocol::Command::Status:
            binaryCommand = 0x0003;
            break;
        case PaProtocol::Command::SendSingle:
            binaryCommand = 0x0200;
            break;
        case PaProtocol::Command::StartContinuous:
            binaryCommand = 0x0210;
            break;
        case PaProtocol::Command::StopTransfer:
        case PaProtocol::Command::StopDynamic:
            binaryCommand = 0x0211;
            break;
        default:
            return rejectOperation(QStringLiteral("该业务命令尚未接入二进制协议"), errorMessage);
    }
    PaBinaryProtocol::Frame frame;
    frame.messageType = PaBinaryProtocol::MessageType::Request;
    frame.command = binaryCommand;
    frame.sequence = nextSequence_++;
    if (nextSequence_ == 0) {
        nextSequence_ = 1;
    }
    pendingCommand_ = command;
    pendingSequence_ = frame.sequence;
    pendingBinaryFrame_ = frame;
    binaryRetryCount_ = 0;
    pendingTimeoutMs_ = commandTimeoutFor(binaryCommand, commandTimeoutMs_);
    setState(PaDeviceState::Busy);

    QString transportError;
    if (!transport_->sendBinaryFrame(frame, &transportError)) {
            pendingCommand_.reset();
            pendingSequence_ = 0;
            pendingBinaryFrame_ = {};
            const QString message = transportError.isEmpty()
                ? QStringLiteral("二进制命令发送失败")
                : transportError;
            setState(PaDeviceState::Error);
            emit errorOccurred(message);
            if (errorMessage != nullptr) {
                *errorMessage = message;
            }
            return false;
    }
    emit lineTransmitted(QStringLiteral("BIN REQ cmd=0x%1 seq=%2")
            .arg(frame.command, 4, 16, QLatin1Char('0'))
            .arg(frame.sequence));
    commandTimer_.start(pendingTimeoutMs_);
    return true;
}

bool PaDeviceController::restartDevice(QString* errorMessage) {
    /* 重启也纳入统一的超时重发；下位机会先返回确认帧，再执行重启。 */
    return sendRawBinaryRequest(0x0005, {}, 0, errorMessage);
}

bool PaDeviceController::requestConfigGroup(quint16 groupId, QString* errorMessage) {
    QByteArray payload;
    payload.append(static_cast<char>(groupId & 0xff));
    payload.append(static_cast<char>((groupId >> 8) & 0xff));
    return sendRawBinaryRequest(0x0103, payload, groupId, errorMessage);
}

bool PaDeviceController::setConfigGroup(quint16 groupId,
                                        const QMap<quint16, quint32>& values,
                                        QString* errorMessage) {
    if (values.isEmpty()) {
        return rejectOperation(QStringLiteral("配置组没有可下发参数"), errorMessage);
    }
    constexpr int kItemsPerFrame = 8;
    QList<QByteArray> chunks;
    QByteArray payload;
    appendLe16(&payload, groupId);
    int itemCount = 0;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        appendTlvU32(&payload, it.key(), it.value());
        ++itemCount;
        if (itemCount == kItemsPerFrame) {
            chunks.append(payload);
            payload.clear();
            appendLe16(&payload, groupId);
            itemCount = 0;
        }
    }
    if (itemCount != 0) chunks.append(payload);
    const QByteArray first = chunks.takeFirst();
    if (!sendRawBinaryRequest(0x0104, first, groupId, errorMessage)) {
        return false;
    }
    pendingRawPayloadChunks_ = chunks;
    return true;
}

bool PaDeviceController::dumpRegisters(QString* errorMessage) {
    return sendRawBinaryRequest(0x0105, {}, 0, errorMessage);
}

bool PaDeviceController::writeRegister(quint16 offset, quint32 value, QString* errorMessage) {
    QByteArray payload;
    payload.append(static_cast<char>(offset & 0xff));
    payload.append(static_cast<char>((offset >> 8) & 0xff));
    payload.append(static_cast<char>(0));
    payload.append(static_cast<char>(0));
    payload.append(static_cast<char>(value & 0xff));
    payload.append(static_cast<char>((value >> 8) & 0xff));
    payload.append(static_cast<char>((value >> 16) & 0xff));
    payload.append(static_cast<char>((value >> 24) & 0xff));
    return sendRawBinaryRequest(0x0106, payload, 0, errorMessage);
}

bool PaDeviceController::queryDynamic(QString* errorMessage) {
    return sendRawBinaryRequest(0x0212, {}, 0, errorMessage);
}

bool PaDeviceController::beginOffsetCalibration(quint32 totalFrames,
                                                quint32 validFrames,
                                                quint32 mode,
                                                QString* errorMessage) {
    if (totalFrames == 0 || validFrames == 0 || validFrames > totalFrames || mode > 1) {
        return rejectOperation(QStringLiteral("暗场模板参数无效"), errorMessage);
    }
    QByteArray payload;
    appendTlvU32(&payload, 0x3000, totalFrames);
    appendTlvU32(&payload, 0x3001, validFrames);
    appendTlvU8(&payload, 0x3002, static_cast<quint8>(mode));
    return sendRawBinaryRequest(0x0300, payload, 0, errorMessage);
}

bool PaDeviceController::captureOffsetCalibration(QString* errorMessage) {
    return sendRawBinaryRequest(0x0301, {}, 0, errorMessage);
}

bool PaDeviceController::buildOffsetCalibration(QString* errorMessage) {
    return sendRawBinaryRequest(0x0302, {}, 0, errorMessage);
}

bool PaDeviceController::cancelOffsetCalibration(QString* errorMessage) {
    return sendRawBinaryRequest(0x0303, {}, 0, errorMessage);
}

bool PaDeviceController::beginGainCalibration(const QList<quint32>& levels,
                                              quint32 framesPerLevel,
                                              float defectThreshold,
                                              QString* errorMessage) {
    if (levels.size() < 2 || levels.size() > 16 || framesPerLevel == 0
        || !(defectThreshold > 0.0f)) {
        return rejectOperation(QStringLiteral("亮场模板参数无效"), errorMessage);
    }
    QByteArray levelBytes;
    levelBytes.reserve(levels.size() * 4);
    for (const quint32 level : levels) {
        appendLe32(&levelBytes, level);
    }
    quint32 thresholdBits = 0;
    static_assert(sizeof(thresholdBits) == sizeof(defectThreshold), "float32 expected");
    std::memcpy(&thresholdBits, &defectThreshold, sizeof(thresholdBits));
    QByteArray payload;
    appendTlvU16(&payload, 0x3100, static_cast<quint16>(levels.size()));
    appendTlvU32(&payload, 0x3101, framesPerLevel);
    appendTlvU32(&payload, 0x3102, thresholdBits);
    appendTlvBytes(&payload, 0x3110, levelBytes);
    return sendRawBinaryRequest(0x0304, payload, 0, errorMessage);
}

bool PaDeviceController::captureGainCalibration(quint32 level, QString* errorMessage) {
    QByteArray payload;
    appendTlvU32(&payload, 0x3120, level);
    return sendRawBinaryRequest(0x0305, payload, 0, errorMessage);
}

bool PaDeviceController::buildGainCalibration(QString* errorMessage) {
    return sendRawBinaryRequest(0x0306, {}, 0, errorMessage);
}

bool PaDeviceController::cancelGainCalibration(QString* errorMessage) {
    return sendRawBinaryRequest(0x0307, {}, 0, errorMessage);
}

bool PaDeviceController::queryCalibration(QString* errorMessage) {
    return sendRawBinaryRequest(0x0308, {}, 0, errorMessage);
}

bool PaDeviceController::configureTemplateUpload(bool gainTemplate,
                                                 quint32 rows,
                                                 quint32 columns,
                                                 QString* errorMessage) {
    if (rows == 0 || columns == 0) {
        return rejectOperation(QStringLiteral("模板图像尺寸无效"), errorMessage);
    }
    const quint64 bytes = static_cast<quint64>(rows) * columns * 2u;
    const quint64 packages = (bytes + 1023u) / 1024u;
    if (packages > std::numeric_limits<quint32>::max()) {
        return rejectOperation(QStringLiteral("模板图像过大"), errorMessage);
    }
    QByteArray payload;
    appendTlvU32(&payload, 0x5000, gainTemplate ? 1u : 0u);
    appendTlvU32(&payload, 0x5002, rows);
    appendTlvU32(&payload, 0x5003, columns);
    appendTlvU32(&payload, 0x5004, static_cast<quint32>(packages));
    return sendRawBinaryRequest(0x0500, payload, 0, errorMessage);
}

bool PaDeviceController::startTemplateUpload(QString* errorMessage) {
    return sendRawBinaryRequest(0x0501, {}, 0, errorMessage);
}

bool PaDeviceController::queryTemplateUpload(QString* errorMessage) {
    return sendRawBinaryRequest(0x0502, {}, 0, errorMessage);
}

bool PaDeviceController::sendRawBinaryRequest(quint16 command,
                                              const QByteArray& payload,
                                              quint16 groupId,
                                              QString* errorMessage) {
    if (transport_ == nullptr || !transport_->isOpen()) {
        return rejectOperation(QStringLiteral("串口未连接"), errorMessage);
    }
    if (hasPendingCommand()) {
        return rejectOperation(QStringLiteral("上一条命令尚未完成"), errorMessage);
    }
    PaBinaryProtocol::Frame frame;
    frame.messageType = PaBinaryProtocol::MessageType::Request;
    frame.command = command;
    frame.sequence = nextSequence_++;
    if (nextSequence_ == 0) nextSequence_ = 1;
    frame.payload = payload;
    rawBinaryPending_ = true;
    pendingBinaryCommand_ = command;
    pendingConfigGroup_ = groupId;
    pendingBinaryFrame_ = frame;
    binaryRetryCount_ = 0;
    pendingTimeoutMs_ = commandTimeoutFor(command, commandTimeoutMs_);
    setState(PaDeviceState::Busy);
    QString transportError;
    if (!transport_->sendBinaryFrame(frame, &transportError)) {
        rawBinaryPending_ = false;
        pendingBinaryCommand_ = 0;
        pendingConfigGroup_ = 0;
        pendingBinaryFrame_ = {};
        binaryRetryCount_ = 0;
        const QString message = transportError.isEmpty() ? QStringLiteral("二进制配置命令发送失败") : transportError;
        setState(PaDeviceState::Error);
        if (errorMessage) *errorMessage = message;
        emit errorOccurred(message);
        return false;
    }
    pendingSequence_ = frame.sequence;
    emit lineTransmitted(QStringLiteral("BIN REQ cmd=0x%1 seq=%2 group=%3")
        .arg(frame.command, 4, 16, QLatin1Char('0'))
        .arg(frame.sequence)
        .arg(groupId));
    /* 模板上传会在 ARM 端等待 FPGA 完成中断，覆盖其默认 10 秒等待窗口。 */
    commandTimer_.start(pendingTimeoutMs_);
    return true;
}

bool PaDeviceController::isConnected() const {
    return transport_ != nullptr && transport_->isOpen();
}

bool PaDeviceController::hasPendingCommand() const {
    return pendingCommand_.has_value() || rawBinaryPending_;
}

PaDeviceState PaDeviceController::state() const {
    return state_;
}

int PaDeviceController::commandTimeoutMs() const {
    return commandTimeoutMs_;
}

int PaDeviceController::maxCommandRetries() const {
    return maxBinaryRetries_;
}

void PaDeviceController::setMaxCommandRetries(int retries) {
    maxBinaryRetries_ = qBound(0, retries, 10);
}

void PaDeviceController::setCommandTimeoutMs(int timeoutMs) {
    commandTimeoutMs_ = qMax(1, timeoutMs);
    if (commandTimer_.isActive()) {
        pendingTimeoutMs_ = commandTimeoutFor(pendingBinaryCommand_, commandTimeoutMs_);
        commandTimer_.start(pendingTimeoutMs_);
    }
}

void PaDeviceController::handleTransportConnectionChanged(bool connected) {
    if (!connected && rawBinaryPending_) {
        finishRawBinary(false, {}, QStringLiteral("设备连接意外断开，配置命令取消"));
    }
    if (!connected && pendingCommand_.has_value()) {
        finishPendingCommand(false, QStringLiteral("设备连接意外断开"));
    }
    setState(connected ? PaDeviceState::Ready : PaDeviceState::Disconnected);
}

void PaDeviceController::handleTransportError(const QString& message) {
    if (rawBinaryPending_) {
        finishRawBinary(false, {}, QStringLiteral("控制传输错误: %1").arg(message));
    }
    if (pendingCommand_.has_value()) {
        finishPendingCommand(false, QStringLiteral("控制传输错误: %1").arg(message));
    }
    setState(isConnected() ? PaDeviceState::Error : PaDeviceState::Disconnected);
    emit errorOccurred(message);
}

void PaDeviceController::handleBinaryFrame(const PaBinaryProtocol::Frame& frame) {
    if (rawBinaryPending_) {
        if (frame.sequence != pendingSequence_ || frame.command != pendingBinaryCommand_) return;
        QMap<quint16, quint32> values;
        const QByteArray& p = frame.payload;
        int pos = 0;
        while (pos + 8 <= p.size()) {
            const auto u16 = [&p](int at) { return static_cast<quint16>(static_cast<quint8>(p.at(at)))
                | static_cast<quint16>(static_cast<quint8>(p.at(at + 1))) << 8; };
            const auto u32 = [&p](int at) { return static_cast<quint32>(static_cast<quint8>(p.at(at)))
                | static_cast<quint32>(static_cast<quint8>(p.at(at + 1))) << 8
                | static_cast<quint32>(static_cast<quint8>(p.at(at + 2))) << 16
                | static_cast<quint32>(static_cast<quint8>(p.at(at + 3))) << 24; };
            if (u16(pos + 2) != 4) break;
            values.insert(u16(pos), u32(pos + 4));
            pos += 8;
        }
        if (frame.messageType == PaBinaryProtocol::MessageType::Error) {
            finishRawBinary(false, values,
                            values.contains(0x0002)
                                ? QStringLiteral("设备返回二进制错误，error_code=0x%1")
                                      .arg(values.value(0x0002), 8, 16, QLatin1Char('0'))
                                : QStringLiteral("设备返回二进制错误帧"));
            return;
        }
        if (frame.messageType != PaBinaryProtocol::MessageType::Done) return;
        if (pendingBinaryCommand_ == 0x0104 && !pendingRawPayloadChunks_.isEmpty()) {
            PaBinaryProtocol::Frame next;
            next.messageType = PaBinaryProtocol::MessageType::Request;
            next.command = pendingBinaryCommand_;
            next.sequence = nextSequence_++;
            if (nextSequence_ == 0) nextSequence_ = 1;
            next.payload = pendingRawPayloadChunks_.takeFirst();
            QString transportError;
            if (!transport_->sendBinaryFrame(next, &transportError)) {
                finishRawBinary(false, values,
                                transportError.isEmpty()
                                    ? QStringLiteral("配置组分帧发送失败")
                                    : transportError);
                return;
            }
            pendingBinaryFrame_ = next;
            binaryRetryCount_ = 0;
            pendingSequence_ = next.sequence;
            emit lineTransmitted(QStringLiteral("BIN REQ cmd=0x%1 seq=%2 group=%3 remaining=%4")
                .arg(next.command, 4, 16, QLatin1Char('0'))
                .arg(next.sequence)
                .arg(pendingConfigGroup_)
                .arg(pendingRawPayloadChunks_.size()));
            commandTimer_.start(pendingTimeoutMs_);
            return;
        }
        if (pendingBinaryCommand_ == 0x0105) {
            finishRawBinary(true, values, QStringLiteral("寄存器读取完成"));
            return;
        }
        if (pendingBinaryCommand_ == 0x0106) {
            finishRawBinary(true, values, QStringLiteral("寄存器写入完成"));
            return;
        }
        QString detail = QStringLiteral("二进制命令已完成");
        if (pendingBinaryCommand_ == 0x0103 || pendingBinaryCommand_ == 0x0104) {
            detail = QStringLiteral("二进制配置组已完成");
        } else if (pendingBinaryCommand_ >= 0x0300 && pendingBinaryCommand_ <= 0x0308) {
            detail = QStringLiteral("模板校准命令已完成");
        } else if (pendingBinaryCommand_ >= 0x0500 && pendingBinaryCommand_ <= 0x0502) {
            detail = QStringLiteral("模板上传命令已完成");
        } else if (pendingBinaryCommand_ == 0x0212) {
            detail = QStringLiteral("Dynamic 状态已读取");
        }
        finishRawBinary(true, values, detail);
        return;
    }
    if (!pendingCommand_.has_value()) {
        return;
    }
    if (frame.sequence != pendingSequence_) {
        return;
    }

    const bool isError = frame.messageType == PaBinaryProtocol::MessageType::Error;
    if (isError) {
        finishPendingCommand(false, QStringLiteral("设备返回二进制错误帧"));
        return;
    }
    if (frame.messageType != PaBinaryProtocol::MessageType::Done) {
        return;
    }

    QString detail = QStringLiteral("二进制命令完成 cmd=0x%1 seq=%2")
        .arg(frame.command, 4, 16, QLatin1Char('0'))
        .arg(frame.sequence);
    if (pendingCommand_.value() == PaProtocol::Command::Status) {
        PaDeviceStatus status;
        QString parseError;
        if (!parseBinaryDeviceStatus(frame, &status, &parseError)) {
            finishPendingCommand(false, parseError);
            return;
        }
        emit deviceStatusChanged(status);
        detail = QStringLiteral("二进制 STATUS 已更新");
    }
    finishPendingCommand(true, detail);
}

void PaDeviceController::handleCommandTimeout() {
    if (retryPendingBinaryRequest()) {
        return;
    }
    if (rawBinaryPending_) {
        finishRawBinary(false, {}, QStringLiteral("等待二进制配置组响应超时"));
        return;
    }
    if (!pendingCommand_.has_value()) {
        return;
    }
    const QString commandName = PaProtocol::commandName(pendingCommand_.value());
    finishPendingCommand(false, QStringLiteral("等待“%1”响应超时").arg(commandName));
}

bool PaDeviceController::retryPendingBinaryRequest() {
    if ((!rawBinaryPending_ && !pendingCommand_.has_value()) || transport_ == nullptr ||
        !transport_->isOpen() || binaryRetryCount_ >= maxBinaryRetries_) {
        return false;
    }

    QString transportError;
    if (!transport_->sendBinaryFrame(pendingBinaryFrame_, &transportError)) {
        const QString detail = transportError.isEmpty()
            ? QStringLiteral("二进制命令重发失败")
            : QStringLiteral("二进制命令重发失败: %1").arg(transportError);
        if (rawBinaryPending_) {
            finishRawBinary(false, {}, detail);
        } else {
            finishPendingCommand(false, detail);
        }
        return true;
    }

    ++binaryRetryCount_;
    emit lineTransmitted(QStringLiteral("BIN RETRY cmd=0x%1 seq=%2 attempt=%3/%4")
        .arg(pendingBinaryFrame_.command, 4, 16, QLatin1Char('0'))
        .arg(pendingBinaryFrame_.sequence)
        .arg(binaryRetryCount_)
        .arg(maxBinaryRetries_));
    commandTimer_.start(pendingTimeoutMs_);
    return true;
}

void PaDeviceController::setState(PaDeviceState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void PaDeviceController::finishPendingCommand(bool success, const QString& detail) {
    if (!pendingCommand_.has_value()) {
        return;
    }
    commandTimer_.stop();
    const PaProtocol::Command command = pendingCommand_.value();
    pendingCommand_.reset();
    pendingBinaryFrame_ = {};
    binaryRetryCount_ = 0;
    pendingSequence_ = 0;
    setState(success
            ? (isConnected() ? PaDeviceState::Ready : PaDeviceState::Disconnected)
            : PaDeviceState::Error);
    emit commandFinished(command, success, detail);
}

void PaDeviceController::finishRawBinary(bool success,
                                          const QMap<quint16, quint32>& values,
                                          const QString& detail) {
    if (!rawBinaryPending_) return;
    commandTimer_.stop();
    const quint16 group = pendingConfigGroup_;
    const quint16 command = pendingBinaryCommand_;
    rawBinaryPending_ = false;
    pendingBinaryCommand_ = 0;
    pendingConfigGroup_ = 0;
    pendingSequence_ = 0;
    pendingBinaryFrame_ = {};
    binaryRetryCount_ = 0;
    pendingRawPayloadChunks_.clear();
    setState(success ? (isConnected() ? PaDeviceState::Ready : PaDeviceState::Disconnected)
                     : PaDeviceState::Error);
    emit binaryCommandFinished(command, values, success, detail);
    if (command == 0x0105) {
        emit registerDumpReceived(values, success, detail);
        return;
    }
    if (command == 0x0106) {
        const quint16 offset = values.isEmpty() ? 0 : values.firstKey();
        const quint32 value = values.isEmpty() ? 0 : values.first();
        emit registerWriteFinished(offset, value, success, detail);
        return;
    }
    if (command == 0x0103 || command == 0x0104) {
        emit configGroupReceived(group, values, success, detail);
    }
}

bool PaDeviceController::parseBinaryDeviceStatus(
    const PaBinaryProtocol::Frame& frame,
    PaDeviceStatus* status,
    QString* errorMessage) const {
    if (status == nullptr) {
        return false;
    }
    QMap<quint16, QByteArray> fields;
    int offset = 0;
    while (offset + 4 <= frame.payload.size()) {
        const auto readLe16 = [&frame](int at) {
            return static_cast<quint16>(static_cast<quint8>(frame.payload.at(at)))
                | static_cast<quint16>(static_cast<quint8>(frame.payload.at(at + 1))) << 8;
        };
        const quint16 type = readLe16(offset);
        const quint16 length = readLe16(offset + 2);
        offset += 4;
        if (offset + length > frame.payload.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("二进制 STATUS TLV 长度错误");
            }
            return false;
        }
        fields.insert(type, frame.payload.mid(offset, length));
        offset += length;
    }
    if (offset != frame.payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("二进制 STATUS TLV 尾部不完整");
        }
        return false;
    }

    const auto readU32 = [&fields](quint16 type, quint32* value, bool required) {
        const auto it = fields.constFind(type);
        if (it == fields.cend()) {
            return !required;
        }
        if (it.value().size() != 4) {
            return false;
        }
        const QByteArray& data = it.value();
        *value = static_cast<quint32>(static_cast<quint8>(data.at(0)))
            | static_cast<quint32>(static_cast<quint8>(data.at(1))) << 8
            | static_cast<quint32>(static_cast<quint8>(data.at(2))) << 16
            | static_cast<quint32>(static_cast<quint8>(data.at(3))) << 24;
        return true;
    };

    PaDeviceStatus parsed;
    const auto readString = [&fields](quint16 type, QString* value) {
        const auto it = fields.constFind(type);
        if (it == fields.cend()) {
            return false;
        }
        *value = QString::fromUtf8(it.value()).trimmed();
        return true;
    };
    quint32 value = 0;
    bool valid = readU32(0x0209, &value, true);
    parsed.writeState = static_cast<int>(value);
    valid = readU32(0x020A, &value, true) && valid;
    parsed.writeEnd = static_cast<int>(value);
    valid = readU32(0x020B, &value, true) && valid;
    parsed.correctionState = static_cast<int>(value);
    valid = readU32(0x020C, &value, true) && valid;
    parsed.correctionEnd = static_cast<int>(value);
    readU32(0x0200, &parsed.workMode, false);
    readU32(0x0201, &parsed.workState, false);
    readU32(0x0202, &parsed.lastError, false);
    readU32(0x0205, &parsed.frameCount, false);
    readU32(0x0206, &parsed.outputAddr, false);
    readU32(0x0207, &parsed.offsetAddr, false);
    readU32(0x0204, &parsed.captureId, false);
    readU32(0x0213, &parsed.dynamicState, false);
    readU32(0x0214, &parsed.imageUploadState, false);
    readString(0x0300, &parsed.armVersion);
    quint32 paBuildInformation = 0;
    quint32 mainBoardVersion = 0;
    quint32 gicBoardVersion = 0;
    quint32 roicBoardVersion = 0;
    quint32 reservedBoard0Version = 0;
    quint32 reservedBoard1Version = 0;
    quint32 reservedBoard2Version = 0;
    readU32(0x0302, &parsed.paVersion, false);
    readU32(0x0303, &paBuildInformation, false);
    readU32(0x0304, &mainBoardVersion, false);
    readU32(0x0305, &gicBoardVersion, false);
    readU32(0x0306, &roicBoardVersion, false);
    readU32(0x0307, &reservedBoard0Version, false);
    readU32(0x0308, &reservedBoard1Version, false);
    readU32(0x0309, &reservedBoard2Version, false);
    readU32(0x030A, &parsed.communicationVersion, false);
    if (fields.contains(0x0302)) {
        parsed.fpgaVersion = QStringLiteral("PA %1 (build 0x%2), Main %3, GIC %4, ROIC %5, COM %6")
            .arg(formatVersionWord(parsed.paVersion))
            .arg(paBuildInformation, 8, 16, QLatin1Char('0'))
            .arg(formatVersionWord(mainBoardVersion))
            .arg(formatVersionWord(gicBoardVersion))
            .arg(formatVersionWord(roicBoardVersion))
            .arg(formatVersionWord(parsed.communicationVersion));
    }
    parsed.valid = valid;
    parsed.rawLine = QStringLiteral("binary STATUS");
    if (!valid && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("二进制 STATUS 缺少必要字段");
    }
    if (valid) {
        *status = parsed;
    }
    return valid;
}

bool PaDeviceController::rejectOperation(const QString& message, QString* errorMessage) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    emit errorOccurred(message);
    return false;
}
