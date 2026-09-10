#include "ImageSource.h"
#include "TiRawImage.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

namespace {
double elapsedMs(const QElapsedTimer& timer) {
    return timer.nsecsElapsed() / 1000000.0;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    QTextStream errorOutput(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    output.setCodec("UTF-8");
    errorOutput.setCodec("UTF-8");
#endif
    if (argc < 2) {
        errorOutput << QStringLiteral("用法: pa_image_benchmark <frame1.tiraw> [frame2.tiraw ...]\n");
        return 2;
    }

    QStringList paths;
    for (int index = 1; index < argc; ++index) {
        paths.push_back(QString::fromLocal8Bit(argv[index]));
    }

    LocalReplaySource source;
    source.setPlaylist(paths);
    source.setIntervalMs(1000);
    QElapsedTimer timer;
    timer.start();
    QString error;
    if (!source.start(&error)) {
        errorOutput << QStringLiteral("预加载失败: ") << error << '\n';
        return 1;
    }
    const double preloadMs = elapsedMs(timer);
    source.stop();

    const ImageSourceStats stats = source.stats();
    const quint64 selectedFrames = static_cast<quint64>(paths.size());
    const quint64 loadedFrames = selectedFrames > stats.failedFrames
        ? selectedFrames - stats.failedFrames
        : 0;
    output << QStringLiteral("预加载: ") << loadedFrames << '/' << paths.size()
           << QStringLiteral(" 帧, ") << QString::number(preloadMs, 'f', 3) << " ms\n";

    TiRawImage image;
    timer.restart();
    if (!image.load(paths.first(), &error)) {
        errorOutput << QStringLiteral("基准图像加载失败: ") << error << '\n';
        return 1;
    }
    const double loadMs = elapsedMs(timer);
    output << QStringLiteral("图像: ") << QFileInfo(paths.first()).fileName() << ", "
           << image.width() << 'x' << image.height() << QStringLiteral(", 单独加载 ")
           << QString::number(loadMs, 'f', 3) << " ms\n";

    constexpr int kRenderIterations = 10;
    quint64 checksum = 0;
    timer.restart();
    for (int iteration = 0; iteration < kRenderIterations; ++iteration) {
        const QImage display = image.toDisplayImage(
            false, image.autoWindowCenter(), image.autoWindowWidth());
        checksum += display.constScanLine(display.height() / 2)[display.width() / 2];
    }
    output << QStringLiteral("全尺寸显示转换: ")
           << QString::number(elapsedMs(timer) / kRenderIterations, 'f', 3)
           << QStringLiteral(" ms/帧\n");

    constexpr int kThumbnailIterations = 50;
    const QSize thumbnailSize = QSize(image.width(), image.height()).scaled(
        QSize(134, 82), Qt::KeepAspectRatio);
    timer.restart();
    for (int iteration = 0; iteration < kThumbnailIterations; ++iteration) {
        const QImage thumbnail = image.toDisplayImage(
            false, image.autoWindowCenter(), image.autoWindowWidth(), thumbnailSize);
        checksum += thumbnail.constScanLine(thumbnail.height() / 2)[thumbnail.width() / 2];
    }
    output << QStringLiteral("缩略图直接转换: ")
           << QString::number(elapsedMs(timer) / kThumbnailIterations, 'f', 3)
           << QStringLiteral(" ms/张\n");

    TiRawImage::RoiStats roiStats;
    timer.restart();
    const bool roiOk = image.roiStats(QRect(0, 0, image.width(), image.height()), &roiStats);
    const double roiMs = elapsedMs(timer);
    if (!roiOk) {
        errorOutput << QStringLiteral("全图统计失败\n");
        return 1;
    }
    output << QStringLiteral("全图 ROI 统计: ") << QString::number(roiMs, 'f', 3)
           << " ms, mean=" << QString::number(roiStats.mean, 'f', 4) << '\n';
    output << QStringLiteral("校验值: ") << checksum << '\n';
    return 0;
}
