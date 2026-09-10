#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

#include <memory>

#include "ImageAlgorithms.h"
#include "ImageSource.h"

/*
 * Application-level owner of the current image frame. UI and future hardware
 * sources meet here; neither side needs to know the other side's details.
 */
class ImageSession final : public QObject {
    Q_OBJECT

public:
    explicit ImageSession(std::shared_ptr<IImageAlgorithms> algorithms, QObject* parent = nullptr);

    bool loadFile(const QString& path, QString* errorMessage);
    bool loadFilePreview(const QString& path, QString* errorMessage);
    bool setFrame(const ImageFrame& frame, QString* errorMessage);
    void clear();
    bool hasImage() const;
    const TiRawImage& image() const;
    ImageFrame currentFrame() const;

    WindowLevelResult autoWindowLevel() const;
    bool roiStats(const QRect& roi, TiRawImage::RoiStats* stats) const;
    WindowLevelResult roiWindowLevel(const QRect& roi) const;
    QImage render(int center, int width) const;
    QImage render(int center, int width, const QSize& outputSize) const;
    bool analyzeMtf(const QRect& roi, MtfAnalysisResult* result, double pixelSizeMm = 0.1) const;
    bool exportMtf(const QString& directory, const MtfAnalysisResult& result, QString* errorMessage) const;

signals:
    void frameChanged(const ImageFrame& frame);

private:
    std::shared_ptr<IImageAlgorithms> algorithms_;
    ImageFrame currentFrame_;
};
