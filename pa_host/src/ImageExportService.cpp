#include "ImageExportService.h"

#include <QFileInfo>
#include <QImageWriter>

const QVector<ImageExportFormat>& ImageExportService::formats() {
    static const QVector<ImageExportFormat> availableFormats = {
        {
            QStringLiteral("tiraw"),
            QStringLiteral("TiRaw（原始数据）"),
            QStringLiteral("tiraw"),
            QStringLiteral("TiRayRaw (*.tiraw)"),
            {},
            true,
        },
        {
            QStringLiteral("raw"),
            QStringLiteral("RAW16 LE（无文件头）"),
            QStringLiteral("raw"),
            QStringLiteral("RAW 16-bit Little Endian (*.raw)"),
            {},
            true,
        },
        {
            QStringLiteral("png"),
            QStringLiteral("PNG（显示图像）"),
            QStringLiteral("png"),
            QStringLiteral("PNG Image (*.png)"),
            QByteArrayLiteral("png"),
            false,
        },
        {
            QStringLiteral("tiff"),
            QStringLiteral("TIFF（显示图像）"),
            QStringLiteral("tif"),
            QStringLiteral("TIFF Image (*.tif *.tiff)"),
            QByteArrayLiteral("tiff"),
            false,
        },
        {
            QStringLiteral("bmp"),
            QStringLiteral("BMP（显示图像）"),
            QStringLiteral("bmp"),
            QStringLiteral("BMP Image (*.bmp)"),
            QByteArrayLiteral("bmp"),
            false,
        },
        {
            QStringLiteral("jpeg"),
            QStringLiteral("JPEG（显示图像）"),
            QStringLiteral("jpg"),
            QStringLiteral("JPEG Image (*.jpg *.jpeg)"),
            QByteArrayLiteral("jpeg"),
            false,
        },
    };
    return availableFormats;
}

bool ImageExportService::findFormat(const QString& formatId, ImageExportFormat* format) {
    for (const ImageExportFormat& candidate : formats()) {
        if (candidate.id == formatId) {
            if (format != nullptr) {
                *format = candidate;
            }
            return true;
        }
    }
    return false;
}

bool ImageExportService::isSupported(const QString& formatId) {
    ImageExportFormat format;
    if (!findFormat(formatId, &format)) {
        return false;
    }
    return format.rawPixels || QImageWriter::supportedImageFormats().contains(format.writerFormat);
}

QString ImageExportService::ensureFileSuffix(const QString& path, const QString& formatId) {
    if (!QFileInfo(path).suffix().isEmpty()) {
        return path;
    }

    ImageExportFormat format;
    if (!findFormat(formatId, &format) || format.suffix.isEmpty()) {
        return path;
    }
    return path + QLatin1Char('.') + format.suffix;
}

bool ImageExportService::exportImage(
    const TiRawImage& image,
    int windowCenter,
    int windowWidth,
    const QString& formatId,
    const QString& outputPath,
    QString* errorMessage) {
    ImageExportFormat format;
    if (!findFormat(formatId, &format)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("不支持的导出格式: %1").arg(formatId);
        }
        return false;
    }
    if (!isSupported(formatId)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("当前 Qt 环境不支持 %1 导出").arg(format.label);
        }
        return false;
    }

    if (formatId == QStringLiteral("tiraw")) {
        return image.saveTiRaw(outputPath, errorMessage);
    }
    if (formatId == QStringLiteral("raw")) {
        return image.saveRaw16(outputPath, errorMessage);
    }

    QImageWriter writer(outputPath, format.writerFormat);
    if (formatId == QStringLiteral("jpeg")) {
        writer.setQuality(95);
    }
    if (writer.write(image.toDisplayImage(false, windowCenter, windowWidth))) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = writer.errorString();
    }
    return false;
}
