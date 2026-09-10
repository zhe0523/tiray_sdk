#pragma once

#include <QMetaObject>
#include <QObject>

#include "FramePresentationController.h"
#include "ImageSource.h"

enum class ImageAcquisitionState {
    Idle,
    Starting,
    Running,
    Stopping,
    Error
};

struct ImageAcquisitionStats {
    ImageSourceStats source;
    FramePresentationStats presentation;
    qint64 sourceStartElapsedMs = 0;
};

/*
 * 图像采集会话边界：连接任意 IImageSource 与统一呈现控制器。
 * 窗口只消费完整帧、状态和统计，不管理源信号或停止顺序。
 */
class ImageAcquisitionController final : public QObject {
    Q_OBJECT

public:
    explicit ImageAcquisitionController(QObject* parent = nullptr);
    ~ImageAcquisitionController() override;

    bool start(IImageSource* source, int targetFps, QString* errorMessage = nullptr);
    void stop();

    ImageAcquisitionState state() const;
    bool isActive() const;
    bool isRunning() const;
    int targetFps() const;
    ImageAcquisitionStats stats() const;

signals:
    void stateChanged(ImageAcquisitionState state);
    void framePresented(const ImageFrame& frame);
    void fpsUpdated(double actualFps, int targetFps);
    void errorOccurred(const QString& message);
    void sessionFinished(const ImageAcquisitionStats& stats);

private:
    void connectSource(IImageSource* source, quint64 generation);
    void disconnectSource();
    void handleSourceFrame(const ImageFrame& frame, quint64 generation);
    void handleSourceRunningChanged(bool running, quint64 generation);
    void finishNaturally();
    void setState(ImageAcquisitionState state);
    bool isCurrentSource(quint64 generation) const;

    FramePresentationController* presentation_ = nullptr;
    IImageSource* source_ = nullptr;
    QMetaObject::Connection frameConnection_;
    QMetaObject::Connection errorConnection_;
    QMetaObject::Connection runningConnection_;
    QMetaObject::Connection destroyedConnection_;
    ImageAcquisitionStats lastStats_;
    ImageFrame startupFrame_;
    quint64 generation_ = 0;
    int targetFps_ = 30;
    ImageAcquisitionState state_ = ImageAcquisitionState::Idle;
    bool startupFramePending_ = false;
    bool sourceStoppedDuringStart_ = false;
};

Q_DECLARE_METATYPE(ImageAcquisitionState)
Q_DECLARE_METATYPE(ImageAcquisitionStats)
