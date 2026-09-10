#include "ImageView.h"

#include <QMouseEvent>
#include <QPen>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>

namespace {
constexpr qreal kMaxZoom = 64.0;
constexpr qreal kMinZoom = 0.01;
}

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent) {
    setScene(&scene_);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFrameShape(QFrame::NoFrame);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setStyleSheet(QStringLiteral("background-color: #202020;"));
}

void ImageView::setImage(const QImage& image, const QSize& logicalSize, bool resetView) {
    image_ = image;
    logicalSize_ = logicalSize.isValid() ? logicalSize : image.size();

    if (image_.isNull()) {
        clearImage();
        return;
    }

    const QPixmap pixmap = QPixmap::fromImage(image_);
    if (pixmapItem_ == nullptr) {
        scene_.clear();
        roiItem_ = nullptr;
        pixmapItem_ = scene_.addPixmap(pixmap);
        pixmapItem_->setTransformationMode(Qt::FastTransformation);
        scene_.setSceneRect(pixmap.rect());
    } else {
        pixmapItem_->setPixmap(pixmap);
        scene_.setSceneRect(pixmap.rect());
    }

    if (resetView) {
        fitMode_ = true;
    }
    if (fitMode_) {
        fitToWindow();
    } else {
        applyTransform();
    }
}

void ImageView::clearImage() {
    scene_.clear();
    pixmapItem_ = nullptr;
    roiItem_ = nullptr;
    image_ = {};
    logicalSize_ = {};
}

bool ImageView::hasImage() const {
    return !image_.isNull();
}

int ImageView::zoomPercent() const {
    return static_cast<int>(zoom_ * 100.0 + 0.5);
}

void ImageView::zoomIn() {
    zoom_ = std::min(kMaxZoom, zoom_ * 1.25);
    fitMode_ = false;
    applyTransform();
}

void ImageView::zoomOut() {
    zoom_ = std::max(kMinZoom, zoom_ / 1.25);
    fitMode_ = false;
    applyTransform();
}

void ImageView::zoomActualSize() {
    zoom_ = 1.0;
    fitMode_ = false;
    applyTransform();
}

void ImageView::fitToWindow() {
    if (pixmapItem_ == nullptr) {
        return;
    }
    fitMode_ = true;
    resetTransform();
    fitInView(pixmapItem_, Qt::KeepAspectRatio);
    zoom_ = transform().m11();
    emit zoomChanged(zoomPercent());
}

bool ImageView::savePng(const QString& path) {
    if (image_.isNull()) {
        return false;
    }
    return image_.save(path, "PNG");
}

void ImageView::applyTransform() {
    QTransform transform;
    transform.scale(zoom_, zoom_);
    setTransform(transform);
    emit zoomChanged(zoomPercent());
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (image_.isNull()) {
        return;
    }
    const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    const qreal next = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    if (next == zoom_) {
        return;
    }
    zoom_ = next;
    fitMode_ = false;
    applyTransform();
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (fitMode_) {
        fitToWindow();
    }
}

QPoint ImageView::logicalPointAt(const QPoint& viewPoint) const {
    if (image_.isNull() || image_.width() <= 0 || logicalSize_.isEmpty()) {
        return {-1, -1};
    }
    const QPointF scenePoint = mapToScene(viewPoint);
    const qreal scaleX = static_cast<qreal>(logicalSize_.width()) / image_.width();
    const qreal scaleY = static_cast<qreal>(logicalSize_.height()) / image_.height();
    const int x = static_cast<int>(scenePoint.x() * scaleX);
    const int y = static_cast<int>(scenePoint.y() * scaleY);
    if (x < 0 || y < 0 || x >= logicalSize_.width() || y >= logicalSize_.height()) {
        return {-1, -1};
    }
    return {x, y};
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && hasImage()) {
        roiSelecting_ = true;
        roiStartScene_ = mapToScene(event->pos());
        clearRoiOverlay();
        QPen pen(QColor(0, 200, 255));
        pen.setCosmetic(true);
        roiItem_ = scene_.addRect(QRectF(roiStartScene_, QSizeF(0, 0)), pen);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    if (roiSelecting_ && roiItem_ != nullptr) {
        const QRectF rect = QRectF(roiStartScene_, mapToScene(event->pos())).normalized();
        roiItem_->setRect(rect);
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
    const QPoint point = logicalPointAt(event->pos());
    if (point.x() >= 0) {
        emit pixelHovered(point);
    }
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && roiSelecting_) {
        roiSelecting_ = false;
        const QRectF sceneRect =
            QRectF(roiStartScene_, mapToScene(event->pos())).normalized();
        clearRoiOverlay();
        if (sceneRect.width() >= 1.0 && sceneRect.height() >= 1.0 &&
            !logicalSize_.isEmpty() && !image_.isNull()) {
            const qreal scaleX = static_cast<qreal>(logicalSize_.width()) / image_.width();
            const qreal scaleY = static_cast<qreal>(logicalSize_.height()) / image_.height();
            const QRect logicalRect(
                static_cast<int>(sceneRect.left() * scaleX),
                static_cast<int>(sceneRect.top() * scaleY),
                std::max(1, static_cast<int>(sceneRect.width() * scaleX)),
                std::max(1, static_cast<int>(sceneRect.height() * scaleY)));
            emit roiSelected(logicalRect.intersected(QRect(QPoint(0, 0), logicalSize_)));
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::clearRoiOverlay() {
    if (roiItem_ != nullptr) {
        scene_.removeItem(roiItem_);
        delete roiItem_;
        roiItem_ = nullptr;
    }
}
