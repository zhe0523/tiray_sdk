#include "ImageSession.h"

#include <QFileInfo>

#include <utility>

ImageSession::ImageSession(std::shared_ptr<IImageAlgorithms> algorithms, QObject* parent)
    : QObject(parent)
    , algorithms_(std::move(algorithms)) {
    if (algorithms_ == nullptr) {
        algorithms_ = std::make_shared<BuiltinImageAlgorithms>();
    }
}

bool ImageSession::loadFile(const QString& path, QString* errorMessage) {
    TiRawImage image;
    if (!image.load(path, errorMessage)) {
        return false;
    }

    ImageFrame frame;
    frame.image = std::move(image);
    frame.sourceName = QFileInfo(path).fileName();
    frame.contentCacheKey = QFileInfo(path).absoluteFilePath();
    frame.receivedAt = QDateTime::currentDateTimeUtc();
    return setFrame(frame, errorMessage);
}

bool ImageSession::loadFilePreview(const QString& path, QString* errorMessage) {
    TiRawImage image;
    if (!image.loadPreview(path, errorMessage)) {
        return false;
    }

    ImageFrame frame;
    frame.image = std::move(image);
    frame.sourceName = QFileInfo(path).fileName();
    frame.contentCacheKey = QFileInfo(path).absoluteFilePath();
    frame.receivedAt = QDateTime::currentDateTimeUtc();
    return setFrame(frame, errorMessage);
}

bool ImageSession::setFrame(const ImageFrame& frame, QString* errorMessage) {
    if (!frame.image.isValid()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("图像帧无效");
        }
        return false;
    }

    currentFrame_ = frame;
    emit frameChanged(currentFrame_);
    return true;
}

void ImageSession::clear() {
    currentFrame_ = {};
    emit frameChanged(currentFrame_);
}

bool ImageSession::hasImage() const {
    return currentFrame_.image.isValid();
}

const TiRawImage& ImageSession::image() const {
    return currentFrame_.image;
}

ImageFrame ImageSession::currentFrame() const {
    return currentFrame_;
}

WindowLevelResult ImageSession::autoWindowLevel() const {
    return algorithms_->autoWindowLevel(currentFrame_.image);
}

bool ImageSession::roiStats(const QRect& roi, TiRawImage::RoiStats* stats) const {
    return currentFrame_.image.roiStats(roi, stats);
}

WindowLevelResult ImageSession::roiWindowLevel(const QRect& roi) const {
    return algorithms_->roiWindowLevel(currentFrame_.image, roi);
}

QImage ImageSession::render(int center, int width) const {
    return currentFrame_.image.toDisplayImage(false, center, width);
}

QImage ImageSession::render(int center, int width, const QSize& outputSize) const {
    return currentFrame_.image.toDisplayImage(false, center, width, outputSize);
}

bool ImageSession::analyzeMtf(const QRect& roi, MtfAnalysisResult* result, double pixelSizeMm) const {
    return algorithms_->analyzeMtf(currentFrame_.image, roi, result, pixelSizeMm);
}

bool ImageSession::exportMtf(const QString& directory, const MtfAnalysisResult& result, QString* errorMessage) const {
    return algorithms_->exportMtf(directory, result, errorMessage);
}
