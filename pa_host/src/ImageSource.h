#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "TiRawImage.h"

struct ImageFrame {
    TiRawImage image;
    QString sourceName;
    // 非空表示内容在当前会话内稳定，可安全复用显示图和统计缓存。
    QString contentCacheKey;
    quint64 sequence = 0;
    /* PCIe BAR0[0x018]：0=正常图片，1=模板上传。非 PCIe 图像保持 false。 */
    quint32 sourceImageType = 0;
    bool sourceImageTypeValid = false;
    QDateTime receivedAt;
};

struct ImageSourceStats {
    quint64 deliveredFrames = 0;
    quint64 failedFrames = 0;
};

class IImageSource : public QObject {
    Q_OBJECT

public:
    explicit IImageSource(QObject* parent = nullptr);
    ~IImageSource() override = default;

    virtual bool start(QString* errorMessage) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual ImageSourceStats stats() const = 0;

signals:
    void frameReady(const ImageFrame& frame);
    void sourceError(const QString& message);
    void runningChanged(bool running);
};

Q_DECLARE_METATYPE(ImageFrame)
