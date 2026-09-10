#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "TiRawImage.h"

struct ImageExportFormat {
    QString id;
    QString label;
    QString suffix;
    QString fileFilter;
    QByteArray writerFormat;
    bool rawPixels = false;
};

/* 无界面导出服务：负责格式能力、后缀和编码，不负责文件对话框。 */
class ImageExportService {
public:
    static const QVector<ImageExportFormat>& formats();
    static bool findFormat(const QString& formatId, ImageExportFormat* format);
    static bool isSupported(const QString& formatId);
    static QString ensureFileSuffix(const QString& path, const QString& formatId);

    static bool exportImage(
        const TiRawImage& image,
        int windowCenter,
        int windowWidth,
        const QString& formatId,
        const QString& outputPath,
        QString* errorMessage);
};
