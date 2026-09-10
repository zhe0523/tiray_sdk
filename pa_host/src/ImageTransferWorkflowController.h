#pragma once

#include <QObject>

#include <optional>

#include "PaProtocol.h"

class PaDeviceController;
enum class PaDeviceState;

enum class ImageTransferMode {
    Manual,
    Continuous
};

enum class ImageTransferState {
    Disconnected,
    Ready,
    StartingSingle,
    StartingContinuous,
    ContinuousRunning,
    Stopping,
    Error
};

/*
 * 正式用户上图工作流：把模式选择和客户按钮转换为明确的设备命令。
 * 原始协议菜单不经过本类，仍只用于研发联调。
 */
class ImageTransferWorkflowController final : public QObject {
    Q_OBJECT

public:
    explicit ImageTransferWorkflowController(
        PaDeviceController* deviceController,
        QObject* parent = nullptr);

    ImageTransferMode mode() const;
    ImageTransferState state() const;
    bool setMode(ImageTransferMode mode, QString* errorMessage = nullptr);
    bool startTransfer(QString* errorMessage = nullptr);
    bool stopTransfer(QString* errorMessage = nullptr);

    bool canSelectMode() const;
    bool canStart() const;
    bool canStop() const;

signals:
    void modeChanged(ImageTransferMode mode);
    void stateChanged(ImageTransferState state);
    void controlsChanged();
    void errorOccurred(const QString& message);

private:
    void handleDeviceStateChanged(PaDeviceState state);
    void handleCommandFinished(
        PaProtocol::Command command,
        bool success,
        const QString& detail);
    void setState(ImageTransferState state);
    bool rejectOperation(const QString& message, QString* errorMessage);
    bool deviceCanAcceptCommand() const;

    PaDeviceController* deviceController_ = nullptr;
    ImageTransferMode mode_ = ImageTransferMode::Manual;
    ImageTransferState state_ = ImageTransferState::Disconnected;
    std::optional<PaProtocol::Command> pendingCommand_;
    bool transferMayBeActive_ = false;
};

Q_DECLARE_METATYPE(ImageTransferMode)
Q_DECLARE_METATYPE(ImageTransferState)
