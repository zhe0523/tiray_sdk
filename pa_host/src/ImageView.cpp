#include "ImageView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
/*
 * 实机内存较大，列表来回切换时允许用一部分内存缓存全尺寸 QPixmap。
 * 这样可以避免每次都把 8-bit QImage 重新转换成窗口系统使用的 pixmap。
 */
constexpr int kPixmapCacheMiB = 512;
constexpr qint64 kBytesPerMiB = 1024 * 1024;
constexpr qreal kMaxZoom = 128.0;
constexpr qreal kMinZoom = 0.02;

int pixmapCostMiB(const QPixmap& pixmap) {
    const int bytesPerPixel = pixmap.depth() <= 8
        ? 1
        : std::max(4, (pixmap.depth() + 7) / 8);
    const qint64 bytes = static_cast<qint64>(pixmap.width())
        * pixmap.height() * bytesPerPixel;
    return static_cast<int>(std::max<qint64>(1, (bytes + kBytesPerMiB - 1) / kBytesPerMiB));
}

QPen roiPen(const QColor& color) {
    QPen pen(color);
    pen.setWidthF(1.0);
    pen.setCosmetic(true);
    return pen;
}
}

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent) {
    pixmapCache_.setMaxCost(kPixmapCacheMiB);
    setScene(&scene_);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFrameShape(QFrame::NoFrame);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState);
    setCacheMode(QGraphicsView::CacheBackground);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
}

void ImageView::setImage(const QImage& image, bool resetViewState, const QSize& logicalImageSize) {
    const bool sizeChanged = image_.size() != image.size();
    image_ = image;
    logicalImageSize_ = logicalImageSize.isValid() ? logicalImageSize : image.size();

    if (resetViewState && roiItem_ != nullptr) {
        clearRoiOverlay();
    }

    if (image_.isNull()) {
        scene_.clear();
        pixmapItem_ = nullptr;
        roiItem_ = nullptr;
        logicalImageSize_ = {};
        return;
    }

    QPixmap pixmap;
    if (pixmapCacheEnabled_) {
        const qint64 cacheKey = image_.cacheKey();
        if (const QPixmap* cached = pixmapCache_.object(cacheKey)) {
            pixmap = *cached;
        } else {
            pixmap = QPixmap::fromImage(image_);
            pixmapCache_.insert(cacheKey, new QPixmap(pixmap), pixmapCostMiB(pixmap));
        }
    } else {
        pixmap = QPixmap::fromImage(image_);
    }

    if (pixmapItem_ == nullptr || sizeChanged) {
        scene_.clear();
        roiItem_ = nullptr;
        pixmapItem_ = scene_.addPixmap(pixmap);
        pixmapItem_->setTransformationMode(Qt::FastTransformation);
        updateSceneRectForPanning();
    } else {
        pixmapItem_->setPixmap(pixmap);
        updateSceneRectForPanning();
    }

    if (resetViewState || sizeChanged) {
        rotation_ = 0;
        flipH_ = false;
        flipV_ = false;
        fitMode_ = true;
    }

    if (fitMode_) {
        fitToWindow();
    } else {
        applyTransform();
    }
}

void ImageView::setPixmapCacheEnabled(bool enabled) {
    if (pixmapCacheEnabled_ == enabled) {
        return;
    }
    pixmapCacheEnabled_ = enabled;
    if (!enabled) {
        pixmapCache_.clear();
    }
}

void ImageView::clearPixmapCache() {
    pixmapCache_.clear();
}

bool ImageView::hasImage() const {
    return !image_.isNull();
}

int ImageView::zoomPercent() const {
    return static_cast<int>(zoom_ * 100.0 + 0.5);
}

void ImageView::zoomIn() {
    fitMode_ = false;
    zoom_ = std::min<qreal>(zoom_ * 1.25, kMaxZoom);
    applyTransform();
}

void ImageView::zoomOut() {
    fitMode_ = false;
    zoom_ = std::max<qreal>(zoom_ / 1.25, kMinZoom);
    applyTransform();
}

void ImageView::fitToWindow() {
    if (pixmapItem_ == nullptr) {
        return;
    }

    const QSizeF imageSize = pixmapItem_->boundingRect().size();
    qreal targetWidth = imageSize.width();
    qreal targetHeight = imageSize.height();
    if (rotation_ == 90 || rotation_ == 270) {
        std::swap(targetWidth, targetHeight);
    }

    const QSize viewSize = viewport()->size();
    if (targetWidth <= 0 || targetHeight <= 0 || viewSize.isEmpty()) {
        return;
    }

    constexpr qreal margin = 0.96;
    zoom_ = std::max<qreal>(
        kMinZoom,
        std::min(viewSize.width() / targetWidth, viewSize.height() / targetHeight) * margin);
    fitMode_ = true;
    applyTransform();
    centerOn(pixmapItem_);
}

void ImageView::resetView() {
    zoom_ = 1.0;
    rotation_ = 0;
    flipH_ = false;
    flipV_ = false;
    fitMode_ = false;
    applyTransform();
}

void ImageView::rotateLeft() {
    rotation_ = (rotation_ + 270) % 360;
    fitMode_ ? fitToWindow() : applyTransform();
}

void ImageView::rotateRight() {
    rotation_ = (rotation_ + 90) % 360;
    fitMode_ ? fitToWindow() : applyTransform();
}

void ImageView::flipHorizontal() {
    flipH_ = !flipH_;
    fitMode_ ? fitToWindow() : applyTransform();
}

void ImageView::flipVertical() {
    flipV_ = !flipV_;
    fitMode_ ? fitToWindow() : applyTransform();
}

bool ImageView::savePng(const QString& path) {
    if (image_.isNull()) {
        return false;
    }
    const QImage output = image_.transformed(imageTransform(1.0), Qt::FastTransformation);
    return output.save(path, "PNG");
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    const bool analysisSelection = (event->modifiers() & Qt::ControlModifier) != 0;
    const bool windowLevelSelection = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (event->button() == Qt::LeftButton && hasImage() && (analysisSelection || windowLevelSelection)) {
        const QPoint imagePoint = imagePointAt(event->pos());
        if (imagePoint.x() >= 0) {
            selectionMode_ = analysisSelection ? SelectionMode::Analysis : SelectionMode::WindowLevel;
            roiStart_ = imagePoint;
            roiStartDisplay_ = displayPointAt(event->pos());
            setDragMode(QGraphicsView::NoDrag);
            if (roiItem_ == nullptr) {
                roiItem_ = scene_.addRect(QRectF(), roiPen(QColor(255, 230, 0)), Qt::NoBrush);
                roiItem_->setZValue(10.0);
            }
            roiItem_->setPen(roiPen(selectionMode_ == SelectionMode::Analysis ? QColor(255, 125, 0) : QColor(36, 170, 84)));
            roiItem_->setRect(QRectF(roiStartDisplay_, QSizeF(1, 1)));
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && hasImage() && roiItem_ != nullptr) {
        clearRoiOverlay();
        emit roiCleared();
    }

    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint imagePoint = imagePointAt(event->pos());
    emit pixelHovered(imagePoint);

    if (selectionMode_ != SelectionMode::None) {
        const QPointF displayPoint = displayPointAt(event->pos());
        if (imagePoint.x() >= 0 && displayPoint.x() >= 0.0 && roiItem_ != nullptr) {
            roiItem_->setRect(QRectF(roiStartDisplay_, displayPoint).normalized());
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (selectionMode_ != SelectionMode::None && event->button() == Qt::LeftButton) {
        const SelectionMode mode = selectionMode_;
        selectionMode_ = SelectionMode::None;
        setDragMode(QGraphicsView::ScrollHandDrag);
        const QPoint imagePoint = imagePointAt(event->pos());
        bool validSelection = false;
        if (imagePoint.x() >= 0) {
            const QRect imageRect(roiStart_, imagePoint);
            const QRect normalized = imageRect.normalized();
            if (normalized.width() > 1 && normalized.height() > 1) {
                validSelection = true;
                if (mode == SelectionMode::Analysis) {
                    emit analysisRoiSelected(normalized);
                } else if (mode == SelectionMode::WindowLevel) {
                    emit windowLevelRoiSelected(normalized);
                }
            }
        }
        if (!validSelection) {
            clearRoiOverlay();
            emit roiCleared();
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (!hasImage()) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    if (event->angleDelta().y() > 0) {
        zoomIn();
    } else {
        zoomOut();
    }
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    updateSceneRectForPanning();
    if (fitMode_ && pixmapItem_ != nullptr) {
        fitToWindow();
    }
}

void ImageView::drawBackground(QPainter* painter, const QRectF& rect) {
    /*
     * 模仿旧 Windows 上位机的透明棋盘背景。
     * 这只是图像查看辅助，不代表图像实际存在透明通道。
     */
    constexpr int cell = 18;
    QPixmap pattern(cell * 2, cell * 2);
    pattern.fill(QColor(255, 255, 255));

    QPainter patternPainter(&pattern);
    patternPainter.fillRect(0, 0, cell, cell, QColor(224, 224, 224));
    patternPainter.fillRect(cell, cell, cell, cell, QColor(224, 224, 224));

    painter->fillRect(rect, QBrush(pattern));
}

QPoint ImageView::imagePointAt(const QPoint& viewPoint) const {
    return logicalPointFromDisplay(displayPointAt(viewPoint));
}

QPointF ImageView::displayPointAt(const QPoint& viewPoint) const {
    if (image_.isNull()) {
        return {-1.0, -1.0};
    }

    const QPointF scenePoint = mapToScene(viewPoint);
    const QRectF imageRect = pixmapItem_ != nullptr
        ? pixmapItem_->boundingRect()
        : QRectF(QPointF(0.0, 0.0), QSizeF(image_.size()));
    if (!imageRect.contains(scenePoint)) {
        return {-1.0, -1.0};
    }
    return scenePoint;
}

QPoint ImageView::logicalPointFromDisplay(const QPointF& displayPoint) const {
    if (image_.isNull() || logicalImageSize_.isEmpty() || displayPoint.x() < 0.0 || displayPoint.y() < 0.0) {
        return {-1, -1};
    }

    const qreal scaleX = static_cast<qreal>(logicalImageSize_.width()) / std::max(1, image_.width());
    const qreal scaleY = static_cast<qreal>(logicalImageSize_.height()) / std::max(1, image_.height());
    const int x = static_cast<int>(std::floor(displayPoint.x() * scaleX));
    const int y = static_cast<int>(std::floor(displayPoint.y() * scaleY));
    if (x < 0 || y < 0 || x >= logicalImageSize_.width() || y >= logicalImageSize_.height()) {
        return {-1, -1};
    }
    return {x, y};
}

void ImageView::clearRoiOverlay() {
    if (roiItem_ != nullptr) {
        scene_.removeItem(roiItem_);
        delete roiItem_;
        roiItem_ = nullptr;
    }
    selectionMode_ = SelectionMode::None;
}

QTransform ImageView::imageTransform(qreal zoom) const {
    QTransform transform;
    transform.scale(zoom * (flipH_ ? -1.0 : 1.0), zoom * (flipV_ ? -1.0 : 1.0));
    transform.rotate(rotation_);
    return transform;
}

void ImageView::updateSceneRectForPanning() {
    if (pixmapItem_ == nullptr) {
        return;
    }

    /*
     * QGraphicsView 的拖拽范围受 sceneRect 限制。sceneRect 如果只等于图像边界，
     * 放大后平移会在图像边缘立刻被卡住。这里给场景保留足够大的空白区，
     * 让用户可以把图像继续拖到视口任意位置附近。
     */
    const QRectF imageRect = pixmapItem_->boundingRect();
    const QRectF visibleSceneRect = mapToScene(viewport()->rect()).boundingRect();
    const qreal viewportExtent = std::max(visibleSceneRect.width(), visibleSceneRect.height());
    const qreal imageExtent = std::max(imageRect.width(), imageRect.height());
    const qreal margin = std::max<qreal>(4096.0, std::max(imageExtent * 20.0, viewportExtent * 20.0));
    scene_.setSceneRect(imageRect.adjusted(-margin, -margin, margin, margin));
}

void ImageView::applyTransform() {
    setTransform(imageTransform(zoom_));
    updateSceneRectForPanning();
    updateZoomLabel();
}

void ImageView::updateZoomLabel() {
    emit zoomChanged(zoomPercent());
}
