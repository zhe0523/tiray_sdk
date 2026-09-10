#pragma once

/*
 * ImageView —— 图像显示控件（参考 pa_host/src/ImageView 的交互方式裁剪）。
 *
 * 负责单帧显示图的缩放、平移和适配显示；原始 16-bit 数据由主窗口保存。
 * 逻辑坐标按原始帧尺寸映射（显示图可能是降采样后的结果）。
 *   左键拖动 = 平移；右键拖动 = 框选分析区域；滚轮 = 以鼠标为中心缩放。
 */

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

class ImageView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    /* image 为显示用 8-bit 图；logicalSize 为原始帧尺寸（用于坐标映射）。 */
    void setImage(const QImage& image, const QSize& logicalSize, bool resetView = false);
    void clearImage();
    bool hasImage() const;
    int zoomPercent() const;

public slots:
    void zoomIn();
    void zoomOut();
    void zoomActualSize();
    void fitToWindow();
    bool savePng(const QString& path);

signals:
    void zoomChanged(int percent);
    void pixelHovered(const QPoint& logicalPoint);
    void roiSelected(const QRect& logicalRect);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QPoint logicalPointAt(const QPoint& viewPoint) const;
    void applyTransform();
    void clearRoiOverlay();

    QGraphicsScene scene_;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QGraphicsRectItem* roiItem_ = nullptr;
    QImage image_;
    QSize logicalSize_;
    qreal zoom_ = 1.0;
    bool fitMode_ = true;
    bool roiSelecting_ = false;
    QPointF roiStartScene_;
};
