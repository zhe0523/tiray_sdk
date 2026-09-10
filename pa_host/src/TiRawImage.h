#pragma once

#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <QMetaType>

/*
 * TiRayRaw 图像文件读取。
 *
 * 从 Windows 上位机样例文件观察到的格式：
 *   0x00: "TiRayRaw" 8 字节魔数
 *   0x08: uint16 版本，样例为 1
 *   0x0A: uint16 每像素字节数，样例为 2
 *   0x0C: uint16 高度
 *   0x0E: uint16 宽度
 *   0x10: uint16 little-endian 灰度像素数据
 *
 * 注意：这是根据当前样例反推的格式，后续拿到正式 SDK 文档后需要再校对。
 */
class TiRawImage {
public:
    struct RoiStats {
        QRect rect;
        int pixelCount = 0;
        double mean = 0.0;
        quint16 min = 0;
        quint16 max = 0;
        double stddev = 0.0;
        double noiseLevel = 0.0;
        double rowNoise = 0.0;
        double rowNoiseStddev = 0.0;
        double rowNoiseRatio = 0.0;
    };

    bool load(const QString& path, QString* errorMessage);
    bool loadPreview(const QString& path, QString* errorMessage);
    bool loadData(const QByteArray& data, const QString& sourceName, QString* errorMessage);
    bool loadRaw16Data(const QByteArray& data, int width, int height, const QString& sourceName, QString* errorMessage);
    bool saveTiRaw(const QString& path, QString* errorMessage) const;
    bool saveRaw16(const QString& path, QString* errorMessage) const;
    bool isValid() const;

    QString path() const;
    quint16 version() const;
    quint16 bytesPerPixel() const;
    int width() const;
    int height() const;
    quint16 minValue() const;
    quint16 maxValue() const;
    int autoWindowCenter() const;
    int autoWindowWidth() const;
    int autoWindowLow() const;
    int autoWindowHigh() const;
    bool pixelValue(int x, int y, quint16* value) const;
    bool roiStats(const QRect& rect, RoiStats* stats) const;

    QImage toDisplayImage(bool autoWindow, int windowCenter, int windowWidth) const;
    QImage toDisplayImage(
        bool autoWindow,
        int windowCenter,
        int windowWidth,
        const QSize& outputSize) const;

private:
    bool savePixelData(const QString& path, bool includeHeader, QString* errorMessage) const;
    bool loadDataInternal(
        const QByteArray& data,
        const QString& sourceName,
        bool previewStats,
        QString* errorMessage);
    static quint16 readLe16(const uchar* p);
    void updateRange();
    void updateAutoWindowLevel();
    void updatePreviewRangeAndAutoWindowLevel(int maxSamples);

    QString path_;
    quint16 version_ = 0;
    quint16 bytesPerPixel_ = 0;
    int width_ = 0;
    int height_ = 0;
    quint16 minValue_ = 0;
    quint16 maxValue_ = 0;
    int autoWindowLow_ = 0;
    int autoWindowHigh_ = 65535;
    int autoWindowCenter_ = 32767;
    int autoWindowWidth_ = 65535;
    QVector<quint16> pixels_;
};

Q_DECLARE_METATYPE(TiRawImage)
