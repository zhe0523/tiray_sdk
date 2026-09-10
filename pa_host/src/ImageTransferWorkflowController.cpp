#include "ImageTransferWorkflowController.h"

#include "PaDeviceController.h"

ImageTransferWorkflowController::ImageTransferWorkflowController(
    PaDeviceController* deviceController,
    QObject* parent)
    : QObject(parent)
    , deviceController_(deviceController) {
    qRegisterMetaType<ImageTransferMode>("ImageTransferMode");
    qRegisterMetaType<ImageTransferState>("ImageTransferState");

    if (deviceController_ != nullptr) {
        connect(deviceController_, &PaDeviceController::stateChanged,
            this, &ImageTransferWorkflowController::handleDeviceStateChanged);
        connect(deviceController_, &PaDeviceController::commandFinished,
            this, &ImageTransferWorkflowController::handleCommandFinished);
        state_ = deviceController_->isConnected()
            ? ImageTransferState::Ready
            : ImageTransferState::Disconnected;
    }
}

ImageTransferMode ImageTransferWorkflowController::mode() const {
    return mode_;
}

ImageTransferState ImageTransferWorkflowController::state() const {
    return state_;
}

bool ImageTransferWorkflowController::setMode(
    ImageTransferMode mode,
    QString* errorMessage) {
    if (!canSelectMode()) {
        return rejectOperation(QStringLiteral("当前状态不能切换上图模式"), errorMessage);
    }
    if (mode_ == mode) {
        return true;
    }
    mode_ = mode;
    emit modeChanged(mode_);
    emit controlsChanged();
    return true;
}

bool ImageTransferWorkflowController::startTransfer(QString* errorMessage) {
    if (!canStart()) {
        return rejectOperation(QStringLiteral("当前状态不能开始上图"), errorMessage);
    }

    const PaProtocol::Command command = mode_ == ImageTransferMode::Manual
        ? PaProtocol::Command::SendSingle
        : PaProtocol::Command::StartContinuous;
    pendingCommand_ = command;
    // 上图命令一旦写出，超时也不能断定 FPGA 没有开始，必须保留停止入口。
    transferMayBeActive_ = true;
    if (command == PaProtocol::Command::StartContinuous) {
        setState(ImageTransferState::StartingContinuous);
    } else {
        setState(ImageTransferState::StartingSingle);
    }

    QString commandError;
    if (!deviceController_->sendCommand(command, &commandError)) {
        pendingCommand_.reset();
        // 命令连发送都失败时，不能继续保留“可能正在传输”标志，否则手动上图会永久锁死。
        transferMayBeActive_ = false;
        setState(ImageTransferState::Error);
        const QString message = commandError.isEmpty()
            ? QStringLiteral("上图命令发送失败")
            : commandError;
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }
    return true;
}

bool ImageTransferWorkflowController::stopTransfer(QString* errorMessage) {
    if (!canStop()) {
        return rejectOperation(QStringLiteral("当前没有可停止的上图任务"), errorMessage);
    }

    pendingCommand_ = PaProtocol::Command::StopTransfer;
    setState(ImageTransferState::Stopping);
    // 设备控制器在写出停止命令后同步完成，不为 STOP_TRANSFER 等待回包。
    QString commandError;
    if (!deviceController_->sendCommand(PaProtocol::Command::StopTransfer, &commandError)) {
        pendingCommand_.reset();
        setState(ImageTransferState::Error);
        const QString message = commandError.isEmpty()
            ? QStringLiteral("停止上图命令发送失败")
            : commandError;
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }
    return true;
}

bool ImageTransferWorkflowController::canSelectMode() const {
    return deviceCanAcceptCommand()
        && !transferMayBeActive_
        && (state_ == ImageTransferState::Ready || state_ == ImageTransferState::Error);
}

bool ImageTransferWorkflowController::canStart() const {
    return deviceCanAcceptCommand()
        && !transferMayBeActive_
        && (state_ == ImageTransferState::Ready || state_ == ImageTransferState::Error);
}

bool ImageTransferWorkflowController::canStop() const {
    if (deviceController_ == nullptr
        || !deviceController_->isConnected()) {
        return false;
    }
    // Idle/手动上图是单帧请求，不提供“停止上图”入口；停止只属于持续上图模式。
    return mode_ == ImageTransferMode::Continuous
        && transferMayBeActive_
        && (state_ == ImageTransferState::StartingContinuous
            || state_ == ImageTransferState::ContinuousRunning
            || state_ == ImageTransferState::Error);
}

void ImageTransferWorkflowController::handleDeviceStateChanged(PaDeviceState state) {
    switch (state) {
    case PaDeviceState::Disconnected:
        pendingCommand_.reset();
        transferMayBeActive_ = false;
        setState(ImageTransferState::Disconnected);
        break;
    case PaDeviceState::Ready:
        if (!pendingCommand_.has_value()
            && (state_ == ImageTransferState::Disconnected || state_ == ImageTransferState::Error)) {
            setState(transferMayBeActive_
                    ? ImageTransferState::Error
                    : ImageTransferState::Ready);
        } else {
            emit controlsChanged();
        }
        break;
    case PaDeviceState::Busy:
        emit controlsChanged();
        break;
    case PaDeviceState::Error:
        if (!pendingCommand_.has_value()) {
            setState(ImageTransferState::Error);
        } else {
            emit controlsChanged();
        }
        break;
    }
}

void ImageTransferWorkflowController::handleCommandFinished(
    PaProtocol::Command command,
    bool success,
    const QString& detail) {
    if (!pendingCommand_.has_value()) {
        // 研发菜单可能直接发送上图命令，仍要同步正式按钮状态。
        if (success && command == PaProtocol::Command::StartContinuous) {
            transferMayBeActive_ = true;
            mode_ = ImageTransferMode::Continuous;
            emit modeChanged(mode_);
            setState(ImageTransferState::ContinuousRunning);
        } else if (success && command == PaProtocol::Command::StopTransfer) {
            transferMayBeActive_ = false;
            setState(ImageTransferState::Ready);
        } else if (!success && command == PaProtocol::Command::StartContinuous) {
            transferMayBeActive_ = false;
            mode_ = ImageTransferMode::Continuous;
            emit modeChanged(mode_);
            setState(ImageTransferState::Error);
        } else if (!success && command == PaProtocol::Command::SendSingle) {
            transferMayBeActive_ = false;
            mode_ = ImageTransferMode::Manual;
            emit modeChanged(mode_);
            setState(ImageTransferState::Error);
        } else {
            emit controlsChanged();
        }
        return;
    }
    if (pendingCommand_.value() != command) {
        emit controlsChanged();
        return;
    }
    pendingCommand_.reset();

    if (!success) {
        // 单帧或启动持续上图失败后，允许用户直接重试；停止失败则保留停止入口，便于再次发送停止命令。
        if (command == PaProtocol::Command::SendSingle
            || command == PaProtocol::Command::StartContinuous) {
            transferMayBeActive_ = false;
        }
        setState(ImageTransferState::Error);
        emit errorOccurred(QStringLiteral("%1失败: %2")
                               .arg(PaProtocol::commandName(command), detail));
        return;
    }

    switch (command) {
    case PaProtocol::Command::SendSingle:
        transferMayBeActive_ = false;
        setState(ImageTransferState::Ready);
        break;
    case PaProtocol::Command::StartContinuous:
        transferMayBeActive_ = true;
        setState(ImageTransferState::ContinuousRunning);
        break;
    case PaProtocol::Command::StopTransfer:
        transferMayBeActive_ = false;
        setState(ImageTransferState::Ready);
        break;
    default:
        setState(ImageTransferState::Error);
        emit errorOccurred(QStringLiteral("收到非上图命令的工作流响应"));
        break;
    }
}

void ImageTransferWorkflowController::setState(ImageTransferState state) {
    if (state_ == state) {
        emit controlsChanged();
        return;
    }
    state_ = state;
    emit stateChanged(state_);
    emit controlsChanged();
}

bool ImageTransferWorkflowController::rejectOperation(
    const QString& message,
    QString* errorMessage) {
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    emit errorOccurred(message);
    return false;
}

bool ImageTransferWorkflowController::deviceCanAcceptCommand() const {
    return deviceController_ != nullptr
        && deviceController_->isConnected()
        && !deviceController_->hasPendingCommand();
}
