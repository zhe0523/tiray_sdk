#include "ImageAcquisitionController.h"

#include <QElapsedTimer>

ImageAcquisitionController::ImageAcquisitionController(QObject* parent)
    : QObject(parent)
    , presentation_(new FramePresentationController(this)) {
    qRegisterMetaType<ImageAcquisitionState>("ImageAcquisitionState");
    qRegisterMetaType<ImageAcquisitionStats>("ImageAcquisitionStats");

    connect(presentation_, &FramePresentationController::framePresented,
        this, [this](const ImageFrame& frame) {
            if (state_ == ImageAcquisitionState::Running) {
                emit framePresented(frame);
            }
        });
    connect(presentation_, &FramePresentationController::fpsUpdated,
        this, [this](double actualFps, int targetFps) {
            if (state_ == ImageAcquisitionState::Running) {
                emit fpsUpdated(actualFps, targetFps);
            }
        });
}

ImageAcquisitionController::~ImageAcquisitionController() {
    ++generation_;
    disconnectSource();
    presentation_->stop();
    if (source_ != nullptr && source_->isRunning()) {
        source_->stop();
    }
}

bool ImageAcquisitionController::start(
    IImageSource* source,
    int targetFps,
    QString* errorMessage) {
    stop();
    if (source == nullptr) {
        const QString message = QStringLiteral("图像源未配置");
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        setState(ImageAcquisitionState::Error);
        emit errorOccurred(message);
        return false;
    }

    ++generation_;
    source_ = source;
    targetFps_ = qBound(1, targetFps, 120);
    lastStats_ = {};
    startupFrame_ = {};
    startupFramePending_ = false;
    sourceStoppedDuringStart_ = false;
    connectSource(source_, generation_);
    setState(ImageAcquisitionState::Starting);

    QElapsedTimer startTimer;
    startTimer.start();
    QString sourceError;
    const bool started = source_->start(&sourceError);
    lastStats_.sourceStartElapsedMs = startTimer.elapsed();
    lastStats_.source = source_->stats();
    if (!started) {
        const QString message = sourceError.isEmpty()
            ? QStringLiteral("图像源启动失败")
            : sourceError;
        ++generation_;
        disconnectSource();
        if (source_->isRunning()) {
            source_->stop();
        }
        source_ = nullptr;
        startupFrame_ = {};
        startupFramePending_ = false;
        setState(ImageAcquisitionState::Error);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        emit errorOccurred(message);
        return false;
    }

    setState(ImageAcquisitionState::Running);
    presentation_->start(targetFps_);
    if (startupFramePending_) {
        presentation_->submitFrame(startupFrame_);
        startupFrame_ = {};
        startupFramePending_ = false;
    }
    if (sourceStoppedDuringStart_ || !source_->isRunning()) {
        finishNaturally();
    }
    return true;
}

void ImageAcquisitionController::stop() {
    if (source_ == nullptr && !presentation_->isActive()) {
        if (state_ != ImageAcquisitionState::Idle) {
            setState(ImageAcquisitionState::Idle);
        }
        return;
    }

    setState(ImageAcquisitionState::Stopping);
    presentation_->stop();
    IImageSource* completedSource = source_;
    ++generation_;
    disconnectSource();
    if (completedSource != nullptr && completedSource->isRunning()) {
        completedSource->stop();
    }
    if (completedSource != nullptr) {
        lastStats_.source = completedSource->stats();
    }
    lastStats_.presentation = presentation_->stats();
    source_ = nullptr;
    startupFrame_ = {};
    startupFramePending_ = false;
    sourceStoppedDuringStart_ = false;
    setState(ImageAcquisitionState::Idle);
    emit sessionFinished(lastStats_);
}

ImageAcquisitionState ImageAcquisitionController::state() const {
    return state_;
}

bool ImageAcquisitionController::isActive() const {
    return state_ == ImageAcquisitionState::Starting
        || state_ == ImageAcquisitionState::Running
        || state_ == ImageAcquisitionState::Stopping;
}

bool ImageAcquisitionController::isRunning() const {
    return state_ == ImageAcquisitionState::Running;
}

int ImageAcquisitionController::targetFps() const {
    return targetFps_;
}

ImageAcquisitionStats ImageAcquisitionController::stats() const {
    ImageAcquisitionStats current = lastStats_;
    if (source_ != nullptr) {
        current.source = source_->stats();
    }
    if (presentation_->isActive()) {
        current.presentation = presentation_->stats();
    }
    return current;
}

void ImageAcquisitionController::connectSource(IImageSource* source, quint64 generation) {
    frameConnection_ = connect(source, &IImageSource::frameReady,
        this, [this, generation](const ImageFrame& frame) {
            handleSourceFrame(frame, generation);
        });
    errorConnection_ = connect(source, &IImageSource::sourceError,
        this, [this, generation](const QString& message) {
            if (isCurrentSource(generation)) {
                emit errorOccurred(message);
            }
        });
    runningConnection_ = connect(source, &IImageSource::runningChanged,
        this, [this, generation](bool running) {
            handleSourceRunningChanged(running, generation);
        });
    destroyedConnection_ = connect(source, &QObject::destroyed,
        this, [this, generation]() {
            if (!isCurrentSource(generation)) {
                return;
            }
            presentation_->stop();
            lastStats_.presentation = presentation_->stats();
            source_ = nullptr;
            ++generation_;
            disconnectSource();
            setState(ImageAcquisitionState::Error);
            emit errorOccurred(QStringLiteral("图像源在会话期间被销毁"));
            emit sessionFinished(lastStats_);
        });
}

void ImageAcquisitionController::disconnectSource() {
    QObject::disconnect(frameConnection_);
    QObject::disconnect(errorConnection_);
    QObject::disconnect(runningConnection_);
    QObject::disconnect(destroyedConnection_);
    frameConnection_ = {};
    errorConnection_ = {};
    runningConnection_ = {};
    destroyedConnection_ = {};
}

void ImageAcquisitionController::handleSourceFrame(
    const ImageFrame& frame,
    quint64 generation) {
    if (!isCurrentSource(generation)) {
        return;
    }
    if (state_ == ImageAcquisitionState::Starting) {
        // 极少数图像源可能在 start() 返回前同步发帧，只保留其最新帧。
        startupFrame_ = frame;
        startupFramePending_ = true;
        return;
    }
    if (state_ == ImageAcquisitionState::Running) {
        presentation_->submitFrame(frame);
    }
}

void ImageAcquisitionController::handleSourceRunningChanged(
    bool running,
    quint64 generation) {
    if (!isCurrentSource(generation) || running) {
        return;
    }
    if (state_ == ImageAcquisitionState::Starting) {
        sourceStoppedDuringStart_ = true;
        return;
    }
    if (state_ == ImageAcquisitionState::Running) {
        finishNaturally();
    }
}

void ImageAcquisitionController::finishNaturally() {
    if (source_ == nullptr) {
        return;
    }
    // 自然结束保留最后一帧；用户主动停止仍通过 stop() 将待显示帧计为丢弃。
    presentation_->flushPendingFrame();
    presentation_->stop();
    lastStats_.source = source_->stats();
    lastStats_.presentation = presentation_->stats();
    ++generation_;
    disconnectSource();
    source_ = nullptr;
    startupFrame_ = {};
    startupFramePending_ = false;
    sourceStoppedDuringStart_ = false;
    setState(ImageAcquisitionState::Idle);
    emit sessionFinished(lastStats_);
}

void ImageAcquisitionController::setState(ImageAcquisitionState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

bool ImageAcquisitionController::isCurrentSource(quint64 generation) const {
    return source_ != nullptr && generation == generation_;
}
