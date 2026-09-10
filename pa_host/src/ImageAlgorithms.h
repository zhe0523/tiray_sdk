#pragma once

#include <QRect>
#include <QString>

#include "MtfAnalysis.h"
#include "TiRawImage.h"

struct WindowLevelResult {
    int low = 0;
    int high = 1;
    int center = 0;
    int width = 1;
    bool valid = false;
};

/*
 * Image algorithm boundary used by the UI/application layer.
 *
 * The built-in implementation keeps the current reverse-engineered behavior.
 * When the original algorithms become available, a replacement implementation
 * can be injected without changing MainWindow or ImageView.
 */
class IImageAlgorithms {
public:
    virtual ~IImageAlgorithms() = default;

    virtual WindowLevelResult autoWindowLevel(const TiRawImage& image) const = 0;
    virtual WindowLevelResult roiWindowLevel(const TiRawImage& image, const QRect& roi) const = 0;
    virtual bool analyzeMtf(
        const TiRawImage& image,
        const QRect& roi,
        MtfAnalysisResult* result,
        double pixelSizeMm = 0.1) const = 0;
    virtual bool exportMtf(
        const QString& directory,
        const MtfAnalysisResult& result,
        QString* errorMessage) const = 0;
};

class BuiltinImageAlgorithms final : public IImageAlgorithms {
public:
    WindowLevelResult autoWindowLevel(const TiRawImage& image) const override;
    WindowLevelResult roiWindowLevel(const TiRawImage& image, const QRect& roi) const override;
    bool analyzeMtf(
        const TiRawImage& image,
        const QRect& roi,
        MtfAnalysisResult* result,
        double pixelSizeMm = 0.1) const override;
    bool exportMtf(
        const QString& directory,
        const MtfAnalysisResult& result,
        QString* errorMessage) const override;
};
