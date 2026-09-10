#include "ImageAlgorithms.h"

#include <algorithm>

WindowLevelResult BuiltinImageAlgorithms::autoWindowLevel(const TiRawImage& image) const {
    if (!image.isValid()) {
        return {};
    }

    WindowLevelResult result;
    result.low = image.autoWindowLow();
    result.high = image.autoWindowHigh();
    result.center = image.autoWindowCenter();
    result.width = image.autoWindowWidth();
    result.valid = true;
    return result;
}

WindowLevelResult BuiltinImageAlgorithms::roiWindowLevel(const TiRawImage& image, const QRect& roi) const {
    TiRawImage::RoiStats stats;
    if (!image.roiStats(roi, &stats)) {
        return {};
    }

    WindowLevelResult result;
    result.low = stats.min;
    result.high = stats.max;
    result.center = std::max(0, std::min(65535, (result.low + result.high) / 2));
    result.width = std::max(1, std::min(65535, result.high - result.low));
    result.valid = true;
    return result;
}

bool BuiltinImageAlgorithms::analyzeMtf(
    const TiRawImage& image,
    const QRect& roi,
    MtfAnalysisResult* result,
    double pixelSizeMm) const {
    return MtfAnalysis::analyze(image, roi, result, pixelSizeMm);
}

bool BuiltinImageAlgorithms::exportMtf(
    const QString& directory,
    const MtfAnalysisResult& result,
    QString* errorMessage) const {
    return MtfAnalysis::exportCsv(directory, result, errorMessage);
}
