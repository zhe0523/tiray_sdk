#include "MtfAnalysis.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr int kOversampling = 4;
constexpr int kSavitzkyGolayWindow = 35;
constexpr int kSavitzkyGolayOrder = 3;
constexpr double kPi = 3.14159265358979323846;

bool solveLinearSystem(std::array<std::array<double, 4>, 4>* matrix, std::array<double, 4>* values) {
    for (int pivot = 0; pivot < 4; ++pivot) {
        int bestRow = pivot;
        for (int row = pivot + 1; row < 4; ++row) {
            if (std::abs((*matrix)[row][pivot]) > std::abs((*matrix)[bestRow][pivot])) {
                bestRow = row;
            }
        }
        if (std::abs((*matrix)[bestRow][pivot]) < 1e-12) {
            return false;
        }
        std::swap((*matrix)[pivot], (*matrix)[bestRow]);
        std::swap((*values)[pivot], (*values)[bestRow]);

        const double divisor = (*matrix)[pivot][pivot];
        for (int column = pivot; column < 4; ++column) {
            (*matrix)[pivot][column] /= divisor;
        }
        (*values)[pivot] /= divisor;

        for (int row = 0; row < 4; ++row) {
            if (row == pivot) {
                continue;
            }
            const double factor = (*matrix)[row][pivot];
            for (int column = pivot; column < 4; ++column) {
                (*matrix)[row][column] -= factor * (*matrix)[pivot][column];
            }
            (*values)[row] -= factor * (*values)[pivot];
        }
    }
    return true;
}

QVector<double> savitzkyGolayDerivative(const QVector<double>& values, double step) {
    QVector<double> result(values.size());
    if (values.size() < 2 || step <= 0.0) {
        return result;
    }
    if (values.size() < kSavitzkyGolayWindow) {
        result[0] = (values.at(1) - values.at(0)) / step;
        for (int i = 1; i < values.size() - 1; ++i) {
            result[i] = (values.at(i + 1) - values.at(i - 1)) / (2.0 * step);
        }
        result[values.size() - 1] = (values.last() - values.at(values.size() - 2)) / step;
        return result;
    }

    std::array<std::array<double, 4>, 4> normalMatrix{};
    std::array<double, 4> target{};
    target[1] = 1.0;
    const int halfWindow = kSavitzkyGolayWindow / 2;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int offset = -halfWindow; offset <= halfWindow; ++offset) {
                normalMatrix[row][column] += std::pow(static_cast<double>(offset), row + column);
            }
        }
    }
    if (!solveLinearSystem(&normalMatrix, &target)) {
        return result;
    }

    QVector<double> coefficients;
    coefficients.reserve(kSavitzkyGolayWindow);
    for (int offset = -halfWindow; offset <= halfWindow; ++offset) {
        double coefficient = 0.0;
        for (int power = 0; power <= kSavitzkyGolayOrder; ++power) {
            coefficient += target[power] * std::pow(static_cast<double>(offset), power);
        }
        coefficients.push_back(coefficient / step);
    }

    // The native filter leaves samples without a complete convolution window
    // untouched. Keeping those samples at zero also avoids inventing edge
    // energy at the ends of the LSF.
    for (int index = halfWindow; index < values.size() - halfWindow; ++index) {
        double derivative = 0.0;
        for (int coefficientIndex = 0; coefficientIndex < coefficients.size(); ++coefficientIndex) {
            const int sourceIndex = index + coefficientIndex - halfWindow;
            derivative += coefficients.at(coefficientIndex) * values.at(sourceIndex);
        }
        result[index] = derivative;
    }
    return result;
}

struct EdgeFit {
    double slope = 0.0;
    double intercept = 0.0;
    bool xFromY = false;
    double gradientEnergy = 0.0;
};

bool fitLine(const QVector<double>& independent, const QVector<double>& dependent, EdgeFit* fit, bool xFromY) {
    if (fit == nullptr || independent.size() < 2 || independent.size() != dependent.size()) {
        return false;
    }

    double sumIndependent = 0.0;
    double sumDependent = 0.0;
    double sumIndependentSquared = 0.0;
    double sumIndependentDependent = 0.0;
    for (int i = 0; i < independent.size(); ++i) {
        sumIndependent += independent.at(i);
        sumDependent += dependent.at(i);
        sumIndependentSquared += independent.at(i) * independent.at(i);
        sumIndependentDependent += independent.at(i) * dependent.at(i);
    }

    const double count = independent.size();
    const double denominator = count * sumIndependentSquared - sumIndependent * sumIndependent;
    if (std::abs(denominator) < 1e-12) {
        return false;
    }

    fit->slope = (count * sumIndependentDependent - sumIndependent * sumDependent) / denominator;
    fit->intercept = (sumDependent - fit->slope * sumIndependent) / count;
    fit->xFromY = xFromY;
    return std::isfinite(fit->slope) && std::isfinite(fit->intercept);
}

bool fitSlantedEdgeByRows(const TiRawImage& image, const QRect& roi, EdgeFit* fit) {
    if (fit == nullptr || roi.width() < 2 || roi.height() < 2) {
        return false;
    }

    QVector<double> edgeX;
    QVector<double> edgeY;
    double gradientEnergy = 0.0;
    edgeX.reserve(roi.height());
    edgeY.reserve(roi.height());

    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        const int quarter = std::max(1, roi.width() / 4);
        double leftSum = 0.0;
        double rightSum = 0.0;
        for (int i = 0; i < quarter; ++i) {
            quint16 left = 0;
            quint16 right = 0;
            image.pixelValue(roi.left() + i, y, &left);
            image.pixelValue(roi.right() - i, y, &right);
            leftSum += left;
            rightSum += right;
        }

        const double polarity = rightSum >= leftSum ? 1.0 : -1.0;
        double weightedX = 0.0;
        double weightSum = 0.0;
        double linePeak = 0.0;
        for (int x = roi.left(); x < roi.right(); ++x) {
            quint16 left = 0;
            quint16 right = 0;
            image.pixelValue(x, y, &left);
            image.pixelValue(x + 1, y, &right);
            const double weight = polarity * (static_cast<double>(right) - left);
            if (weight <= 0.0) {
                continue;
            }
            weightedX += (x + 0.5) * weight;
            weightSum += weight;
            linePeak = std::max(linePeak, weight);
        }

        if (weightSum <= 1e-6) {
            continue;
        }
        edgeX.push_back(weightedX / weightSum);
        edgeY.push_back(y + 0.5);
        gradientEnergy += linePeak;
    }

    if (edgeX.size() < 2) {
        return false;
    }

    if (!fitLine(edgeY, edgeX, fit, true)) {
        return false;
    }
    fit->gradientEnergy = gradientEnergy;
    return true;
}

bool fitSlantedEdgeByColumns(const TiRawImage& image, const QRect& roi, EdgeFit* fit) {
    if (fit == nullptr || roi.width() < 2 || roi.height() < 2) {
        return false;
    }

    QVector<double> edgeX;
    QVector<double> edgeY;
    double gradientEnergy = 0.0;
    edgeX.reserve(roi.width());
    edgeY.reserve(roi.width());

    for (int x = roi.left(); x < roi.right(); ++x) {
        const int quarter = std::max(1, roi.height() / 4);
        double topSum = 0.0;
        double bottomSum = 0.0;
        for (int i = 0; i < quarter; ++i) {
            quint16 top = 0;
            quint16 bottom = 0;
            image.pixelValue(x, roi.top() + i, &top);
            image.pixelValue(x, roi.bottom() - i, &bottom);
            topSum += top;
            bottomSum += bottom;
        }

        const double polarity = bottomSum >= topSum ? 1.0 : -1.0;
        double weightedY = 0.0;
        double weightSum = 0.0;
        double linePeak = 0.0;
        for (int y = roi.top(); y < roi.bottom(); ++y) {
            quint16 top = 0;
            quint16 bottom = 0;
            image.pixelValue(x, y, &top);
            image.pixelValue(x, y + 1, &bottom);
            const double weight = polarity * (static_cast<double>(bottom) - top);
            if (weight <= 0.0) {
                continue;
            }
            weightedY += (y + 0.5) * weight;
            weightSum += weight;
            linePeak = std::max(linePeak, weight);
        }

        if (weightSum <= 1e-6) {
            continue;
        }
        edgeX.push_back(x + 0.5);
        edgeY.push_back(weightedY / weightSum);
        gradientEnergy += linePeak;
    }

    if (!fitLine(edgeX, edgeY, fit, false)) {
        return false;
    }
    fit->gradientEnergy = gradientEnergy;
    return true;
}

bool fitSlantedEdge(const TiRawImage& image, const QRect& roi, EdgeFit* fit) {
    EdgeFit rows;
    EdgeFit columns;
    const bool rowsFitted = fitSlantedEdgeByRows(image, roi, &rows);
    const bool columnsFitted = fitSlantedEdgeByColumns(image, roi, &columns);
    if (rowsFitted && (!columnsFitted || rows.gradientEnergy >= columns.gradientEnergy)) {
        *fit = rows;
        return true;
    }
    if (columnsFitted) {
        *fit = columns;
        return true;
    }
    return false;
}

MtfCurve upsampleColumnProfile(const TiRawImage& image, const QRect& roi, double stepMm) {
    QVector<double> source;
    source.reserve(roi.width());
    for (int x = roi.left(); x <= roi.right(); ++x) {
        double sum = 0.0;
        for (int y = roi.top(); y <= roi.bottom(); ++y) {
            quint16 value = 0;
            image.pixelValue(x, y, &value);
            sum += value;
        }
        source.push_back(sum / roi.height());
    }

    MtfCurve curve;
    if (source.size() < 2) {
        return curve;
    }
    const int sampleCount = (source.size() - 1) * kOversampling;
    curve.x.reserve(sampleCount);
    curve.y.reserve(sampleCount);
    for (int index = 0; index < sampleCount; ++index) {
        const int sourceIndex = index / kOversampling;
        const double fraction = static_cast<double>(index % kOversampling) / kOversampling;
        curve.x.push_back(index * stepMm);
        curve.y.push_back(source.at(sourceIndex) * (1.0 - fraction) + source.at(sourceIndex + 1) * fraction);
    }
    return curve;
}

MtfCurve sampleSlantedEdgeProfile(const TiRawImage& image, const QRect& roi, const EdgeFit& fit, double pixelSizeMm) {
    const double binWidthPixels = 1.0 / kOversampling;
    const double normalScale = std::sqrt(1.0 + fit.slope * fit.slope);
    const double axisScale = fit.xFromY && std::abs(fit.slope) > 1e-6
        ? std::sqrt(1.0 + 1.0 / (fit.slope * fit.slope))
        : normalScale;
    double minDistance = std::numeric_limits<double>::max();
    double maxDistance = std::numeric_limits<double>::lowest();

    QVector<double> distances;
    QVector<double> values;
    distances.reserve(roi.width() * roi.height());
    values.reserve(roi.width() * roi.height());

    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        const double centerY = y + 0.5;
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const double centerX = x + 0.5;
            const double distance = fit.xFromY
                ? (centerX - (fit.slope * centerY + fit.intercept)) / normalScale
                : (centerY - (fit.slope * centerX + fit.intercept)) / normalScale;
            quint16 value = 0;
            image.pixelValue(x, y, &value);
            distances.push_back(distance);
            values.push_back(value);
            minDistance = std::min(minDistance, distance);
            maxDistance = std::max(maxDistance, distance);
        }
    }

    if (distances.isEmpty() || maxDistance <= minDistance) {
        return {};
    }

    minDistance = std::floor(minDistance / binWidthPixels) * binWidthPixels;
    maxDistance = std::ceil(maxDistance / binWidthPixels) * binWidthPixels;
    // The old native implementation uses the ROI width as the profile span.
    // The projected ROI can be wider when the edge is slanted, so keep a
    // stable, comparable output length. Some ROIs do not contain the complete
    // edge; the missing tail is filled below.
    const int binCount = std::max(2, (roi.width() - 1) * kOversampling);
    if (binCount < 2) {
        return {};
    }

    QVector<double> sums(binCount);
    QVector<int> counts(binCount);
    for (int i = 0; i < distances.size(); ++i) {
        const int bin = std::max(
            0,
            std::min(binCount - 1, static_cast<int>(std::floor((distances.at(i) - minDistance) / binWidthPixels))));
        sums[bin] += values.at(i);
        ++counts[bin];
    }

    QVector<double> profile(binCount);
    QVector<int> filled;
    filled.reserve(binCount);
    for (int i = 0; i < binCount; ++i) {
        if (counts.at(i) > 0) {
            profile[i] = sums.at(i) / counts.at(i);
            filled.push_back(i);
        }
    }
    if (filled.size() < 2) {
        return {};
    }

    for (int i = 0; i < filled.first(); ++i) {
        profile[i] = profile.at(filled.first());
    }
    for (int segment = 0; segment < filled.size() - 1; ++segment) {
        const int left = filled.at(segment);
        const int right = filled.at(segment + 1);
        for (int i = left + 1; i < right; ++i) {
            const double t = static_cast<double>(i - left) / (right - left);
            profile[i] = profile.at(left) * (1.0 - t) + profile.at(right) * t;
        }
    }
    for (int i = filled.last() + 1; i < binCount; ++i) {
        profile[i] = profile.at(filled.last());
    }

    MtfCurve curve;
    curve.x.reserve(binCount);
    curve.y.reserve(binCount);
    for (int i = 0; i < binCount; ++i) {
        curve.x.push_back(i * pixelSizeMm * axisScale / kOversampling);
        curve.y.push_back(profile.at(i));
    }
    return curve;
}

bool writeCurve(const QString& path, const MtfCurve& curve, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    QTextStream output(&file);
    output.setRealNumberNotation(QTextStream::FixedNotation);
    output.setRealNumberPrecision(12);
    for (int index = 0; index < curve.x.size() && index < curve.y.size(); ++index) {
        output << curve.x.at(index) << ',' << curve.y.at(index) << '\n';
    }
    return true;
}
}

bool MtfAnalysis::analyze(
    const TiRawImage& image,
    const QRect& rect,
    MtfAnalysisResult* result,
    double pixelSizeMm) {
    if (!image.isValid() || result == nullptr || pixelSizeMm <= 0.0) {
        return false;
    }

    const QRect roi = rect.normalized().intersected(QRect(0, 0, image.width(), image.height()));
    if (roi.width() < 2 || roi.height() < 1) {
        return false;
    }

    MtfAnalysisResult analysis;
    const double esfStep = pixelSizeMm / kOversampling;
    EdgeFit edgeFit;
    if (fitSlantedEdge(image, roi, &edgeFit)) {
        analysis.esf = sampleSlantedEdgeProfile(image, roi, edgeFit, pixelSizeMm);
    }
    if (analysis.esf.y.isEmpty()) {
        analysis.esf = upsampleColumnProfile(image, roi, esfStep);
    }
    if (analysis.esf.y.isEmpty()) {
        return false;
    }

    const double curveStep = analysis.esf.x.size() > 1
        ? analysis.esf.x.at(1) - analysis.esf.x.at(0)
        : esfStep;
    analysis.lsf.x = analysis.esf.x;
    analysis.lsf.y = savitzkyGolayDerivative(analysis.esf.y, curveStep);

    const int mtfCount = analysis.lsf.y.size() / kOversampling + 1;
    analysis.mtf.x.reserve(mtfCount);
    analysis.mtf.y.reserve(mtfCount);
    double dc = 0.0;
    for (const double value : analysis.lsf.y) {
        dc += value;
    }
    dc = std::abs(dc);
    if (dc < 1e-12) {
        dc = 1.0;
    }
    for (int frequencyIndex = 0; frequencyIndex < mtfCount; ++frequencyIndex) {
        double real = 0.0;
        double imaginary = 0.0;
        for (int sampleIndex = 0; sampleIndex < analysis.lsf.y.size(); ++sampleIndex) {
            const double angle = -2.0 * kPi * frequencyIndex * sampleIndex / analysis.lsf.y.size();
            real += analysis.lsf.y.at(sampleIndex) * std::cos(angle);
            imaginary += analysis.lsf.y.at(sampleIndex) * std::sin(angle);
        }
        // Keep the same index spacing as the native exported curve. The
        // frequency values themselves are still calculated from the LSF DFT.
        analysis.mtf.x.push_back(frequencyIndex * curveStep);
        analysis.mtf.y.push_back(std::sqrt(real * real + imaginary * imaginary) / dc);
    }

    *result = std::move(analysis);
    return true;
}

bool MtfAnalysis::exportCsv(const QString& directory, const MtfAnalysisResult& result, QString* errorMessage) {
    QDir outputDirectory(directory);
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建导出目录");
        }
        return false;
    }

    return writeCurve(outputDirectory.filePath(QStringLiteral("esf.csv")), result.esf, errorMessage)
        && writeCurve(outputDirectory.filePath(QStringLiteral("lsf.csv")), result.lsf, errorMessage)
        && writeCurve(outputDirectory.filePath(QStringLiteral("mtf.csv")), result.mtf, errorMessage);
}
