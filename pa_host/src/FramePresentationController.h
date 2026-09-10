#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "ImageSource.h"
#include "ReplayPresentationScheduler.h"

struct FramePresentationStats {
    quint64 submittedFrames = 0;
    quint64 presentedFrames = 0;
    quint64 droppedFrames = 0;
};

/*
 * 将图像源的输入帧限速为界面实际呈现帧。
 * 控制器只保留最新待显示帧，避免输入速度高于界面时形成延迟队列。
 */
class FramePresentationController final : public QObject {
    Q_OBJECT

public:
    explicit FramePresentationController(QObject* parent = nullptr);

    void start(int targetFps);
    void stop();
    void flushPendingFrame();
    bool isActive() const;
    int targetFps() const;
    FramePresentationStats stats() const;

public slots:
    void submitFrame(const ImageFrame& frame);

signals:
    void framePresented(const ImageFrame& frame);
    void fpsUpdated(double actualFps, int targetFps);

private:
    void schedulePendingFrame();
    void presentPendingFrame();

    ReplayPresentationScheduler scheduler_;
    QElapsedTimer presentationClock_;
    QElapsedTimer fpsClock_;
    QTimer presentationTimer_;
    ImageFrame pendingFrame_;
    FramePresentationStats stats_;
    quint64 fpsFrameCount_ = 0;
    bool framePending_ = false;
    bool active_ = false;
};
