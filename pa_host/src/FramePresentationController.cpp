#include "FramePresentationController.h"

FramePresentationController::FramePresentationController(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<ImageFrame>("ImageFrame");
    presentationTimer_.setSingleShot(true);
    presentationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&presentationTimer_, &QTimer::timeout, this, &FramePresentationController::presentPendingFrame);
}

void FramePresentationController::start(int targetFps) {
    presentationTimer_.stop();
    scheduler_.setTargetFps(targetFps);
    scheduler_.reset();
    presentationClock_.start();
    fpsClock_.start();
    pendingFrame_ = {};
    stats_ = {};
    fpsFrameCount_ = 0;
    framePending_ = false;
    active_ = true;
    emit fpsUpdated(0.0, scheduler_.targetFps());
}

void FramePresentationController::stop() {
    presentationTimer_.stop();
    if (framePending_) {
        ++stats_.droppedFrames;
    }
    pendingFrame_ = {};
    framePending_ = false;
    active_ = false;
    presentationClock_.invalidate();
    fpsClock_.invalidate();
    scheduler_.reset();
}

void FramePresentationController::flushPendingFrame() {
    if (!active_ || !framePending_) {
        return;
    }
    presentationTimer_.stop();
    presentPendingFrame();
}

bool FramePresentationController::isActive() const {
    return active_;
}

int FramePresentationController::targetFps() const {
    return scheduler_.targetFps();
}

FramePresentationStats FramePresentationController::stats() const {
    return stats_;
}

void FramePresentationController::submitFrame(const ImageFrame& frame) {
    if (!active_) {
        return;
    }

    ++stats_.submittedFrames;
    // 输入追上待显示帧时直接覆盖旧帧，保证界面始终趋近最新数据。
    if (framePending_) {
        ++stats_.droppedFrames;
    }
    pendingFrame_ = frame;
    framePending_ = true;
    schedulePendingFrame();
}

void FramePresentationController::schedulePendingFrame() {
    if (!active_ || !framePending_ || presentationTimer_.isActive()) {
        return;
    }
    const int delayMs = presentationClock_.isValid()
        ? scheduler_.delayMs(presentationClock_.nsecsElapsed())
        : 0;
    presentationTimer_.start(delayMs);
}

void FramePresentationController::presentPendingFrame() {
    if (!active_ || !framePending_) {
        return;
    }

    const ImageFrame frame = pendingFrame_;
    pendingFrame_ = {};
    framePending_ = false;
    emit framePresented(frame);

    ++stats_.presentedFrames;
    ++fpsFrameCount_;
    if (presentationClock_.isValid()) {
        scheduler_.markPresented(presentationClock_.nsecsElapsed());
    }

    const qint64 elapsedMs = fpsClock_.isValid() ? fpsClock_.elapsed() : 0;
    if (elapsedMs >= 500) {
        const double actualFps = fpsFrameCount_ * 1000.0 / elapsedMs;
        emit fpsUpdated(actualFps, scheduler_.targetFps());
        fpsClock_.restart();
        fpsFrameCount_ = 0;
    }

    // framePresented 的接收方可能同步提交新帧，因此呈现后再次检查待显示状态。
    schedulePendingFrame();
}
