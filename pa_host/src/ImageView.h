#pragma once

#include <QCache>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QTransform>

/*
 * 图像显示控件。
 *
 * 当前只负责单帧 8-bit 显示图像的缩放、旋转、翻转和保存。
 * 原始 16-bit 数据由 TiRawImage 保存，窗宽窗位转换后再传入这里。
 */
class ImageView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image, bool resetViewState = false, const QSize& logicalImageSize = QSize());
    void setPixmapCacheEnabled(bool enabled);
    void clearPixmapCache();
    bool hasImage() const;
    int zoomPercent() const;

public slots:
    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void resetView();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    bool savePng(const QString& path);

signals:
    void zoomChanged(int percent);
    void pixelHovered(const QPoint& imagePoint);
    void analysisRoiSelected(const QRect& imageRect);
    void windowLevelRoiSelected(const QRect& imageRect);
    void roiCleared();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    QPoint imagePointAt(const QPoint& viewPoint) const;
    QPointF displayPointAt(const QPoint& viewPoint) const;
    QPoint logicalPointFromDisplay(const QPointF& displayPoint) const;
    QTransform imageTransform(qreal zoom) const;
    void clearRoiOverlay();
    void updateSceneRectForPanning();
    void applyTransform();
    void updateZoomLabel();

    enum class SelectionMode {
        None,
        Analysis,
        WindowLevel,
    };

    QGraphicsScene scene_;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QGraphicsRectItem* roiItem_ = nullptr;
    QCache<qint64, QPixmap> pixmapCache_;
    QImage image_;
    /*
     * 实时 PCIe 上图可以用降采样图像显示，但鼠标像素和 ROI 必须仍然回到
     * 原始 16-bit 图像坐标。logicalImageSize_ 记录原始帧尺寸，image_ 是当前显示尺寸。
     */
    QSize logicalImageSize_;
    qreal zoom_ = 1.0;
    int rotation_ = 0;
    bool flipH_ = false;
    bool flipV_ = false;
    bool fitMode_ = true;
    bool pixmapCacheEnabled_ = false;
    SelectionMode selectionMode_ = SelectionMode::None;
    QPoint roiStart_;
    QPointF roiStartDisplay_;
};
