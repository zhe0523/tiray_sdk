#pragma once

#include <QRect>
#include <QString>
#include <QVector>

#include "TiRawImage.h"

struct MtfCurve {
    QVector<double> x;
    QVector<double> y;
};

struct MtfAnalysisResult {
    MtfCurve esf;
    MtfCurve lsf;
    MtfCurve mtf;
};

class MtfAnalysis {
public:
    static bool analyze(
        const TiRawImage& image,
        const QRect& roi,
        MtfAnalysisResult* result,
        double pixelSizeMm = 0.1);

    static bool exportCsv(const QString& directory, const MtfAnalysisResult& result, QString* errorMessage);
};
