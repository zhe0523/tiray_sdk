#include "ImageListPanel.h"

#include "ImageExportService.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kImagePathRole = Qt::UserRole + 1;
constexpr int kThumbnailReadyRole = Qt::UserRole + 2;
constexpr QSize kThumbnailSize(134, 82);
constexpr QSize kImageListItemSize(146, 112);

QString normalizedImagePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QIcon makeImageThumbnail(const TiRawImage& image) {
    QPixmap thumbnail(kThumbnailSize);
    thumbnail.fill(Qt::white);
    if (!image.isValid()) {
        return QIcon(thumbnail);
    }

    const QSize scaledSize = QSize(image.width(), image.height()).scaled(kThumbnailSize, Qt::KeepAspectRatio);
    const QImage scaled = image.toDisplayImage(
        false, image.autoWindowCenter(), image.autoWindowWidth(), scaledSize);
    QPainter painter(&thumbnail);
    painter.drawImage(
        QPoint((thumbnail.width() - scaled.width()) / 2, (thumbnail.height() - scaled.height()) / 2),
        scaled);
    painter.setPen(QColor(145, 156, 168));
    painter.drawRect(thumbnail.rect().adjusted(0, 0, -1, -1));
    return QIcon(thumbnail);
}
}

ImageListPanel::ImageListPanel(QWidget* parent)
    : QGroupBox(QStringLiteral("图像列表"), parent) {
    setObjectName(QStringLiteral("imageListPanel"));
    setMinimumWidth(180);
    setMaximumWidth(300);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    imageList_ = new QListWidget(this);
    imageList_->setObjectName(QStringLiteral("imageList"));
    imageList_->setViewMode(QListView::IconMode);
    imageList_->setFlow(QListView::TopToBottom);
    imageList_->setWrapping(false);
    imageList_->setMovement(QListView::Static);
    imageList_->setResizeMode(QListView::Adjust);
    imageList_->setIconSize(kThumbnailSize);
    imageList_->setGridSize(kImageListItemSize);
    imageList_->setTextElideMode(Qt::ElideMiddle);
    imageList_->setWordWrap(false);
    imageList_->setSpacing(3);
    imageList_->setUniformItemSizes(true);
    imageList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    imageList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageList_->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(imageList_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current != nullptr) {
            emit imageActivated(current->data(kImagePathRole).toString());
        }
    });

    removeButton_ = new QPushButton(QStringLiteral("移除选中图像"), this);
    removeButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    removeButton_->setToolTip(QStringLiteral("从列表移除选中图像，不删除源文件"));
    connect(removeButton_, &QPushButton::clicked, this, &ImageListPanel::removeSelectedImages);

    removeAllButton_ = new QPushButton(QStringLiteral("移除全部图像"), this);
    removeAllButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    removeAllButton_->setToolTip(QStringLiteral("从列表移除全部图像，不删除源文件"));
    connect(removeAllButton_, &QPushButton::clicked, this, &ImageListPanel::removeAllImages);

    removeAction_ = new QAction(QStringLiteral("移除选中图像"), imageList_);
    removeAction_->setShortcut(QKeySequence::Delete);
    removeAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    imageList_->addAction(removeAction_);
    connect(removeAction_, &QAction::triggered, this, &ImageListPanel::removeSelectedImages);
    connect(imageList_, &QListWidget::itemSelectionChanged,
        this, &ImageListPanel::updateRemovalControls);
    connect(imageList_, &QListWidget::customContextMenuRequested, this, &ImageListPanel::showContextMenu);

    layout->addWidget(imageList_);
    layout->addWidget(removeButton_);
    layout->addWidget(removeAllButton_);
    updateRemovalControls();
}

void ImageListPanel::clearImages() {
    const QSignalBlocker blocker(imageList_);
    imageList_->clear();
    updateRemovalControls();
}

void ImageListPanel::addOrUpdateImage(const QString& path, const TiRawImage* image) {
    const QString normalizedPath = normalizedImagePath(path);
    QListWidgetItem* item = findItem(normalizedPath);
    if (item == nullptr) {
        item = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon), QFileInfo(normalizedPath).fileName());
        item->setData(kImagePathRole, normalizedPath);
        item->setData(kThumbnailReadyRole, false);
        item->setToolTip(normalizedPath);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setSizeHint(kImageListItemSize);
        imageList_->addItem(item);
    }
    if (image != nullptr && image->isValid()) {
        item->setIcon(makeImageThumbnail(*image));
        item->setData(kThumbnailReadyRole, true);
    }
    updateRemovalControls();
}

void ImageListPanel::ensureThumbnail(const QString& path, const TiRawImage& image) {
    QListWidgetItem* item = findItem(path);
    if (item == nullptr) {
        addOrUpdateImage(path, &image);
        return;
    }
    if (!item->data(kThumbnailReadyRole).toBool() && image.isValid()) {
        item->setIcon(makeImageThumbnail(image));
        item->setData(kThumbnailReadyRole, true);
    }
}

void ImageListPanel::setCurrentPath(const QString& path) {
    QListWidgetItem* item = findItem(path);
    if (item == nullptr) {
        return;
    }
    const QSignalBlocker blocker(imageList_);
    imageList_->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
    imageList_->scrollToItem(item);
    updateRemovalControls();
}

QString ImageListPanel::currentPath() const {
    const QListWidgetItem* item = imageList_->currentItem();
    return item == nullptr ? QString() : item->data(kImagePathRole).toString();
}

int ImageListPanel::imageCount() const {
    return imageList_->count();
}

QListWidgetItem* ImageListPanel::findItem(const QString& path) const {
    const QString normalizedPath = normalizedImagePath(path);
    for (int row = 0; row < imageList_->count(); ++row) {
        QListWidgetItem* item = imageList_->item(row);
        if (item->data(kImagePathRole).toString() == normalizedPath) {
            return item;
        }
    }
    return nullptr;
}

void ImageListPanel::updateRemovalControls() {
    const bool canRemove = imageList_ != nullptr
        && (!imageList_->selectedItems().isEmpty() || imageList_->currentItem() != nullptr);
    if (removeButton_ != nullptr) {
        removeButton_->setEnabled(canRemove);
    }
    if (removeAction_ != nullptr) {
        removeAction_->setEnabled(canRemove);
    }
    if (removeAllButton_ != nullptr) {
        removeAllButton_->setEnabled(imageList_ != nullptr && imageList_->count() > 0);
    }
}

void ImageListPanel::removeSelectedImages() {
    QList<QListWidgetItem*> selected = imageList_->selectedItems();
    if (selected.isEmpty() && imageList_->currentItem() != nullptr) {
        selected.push_back(imageList_->currentItem());
    }
    if (selected.isEmpty()) {
        return;
    }

    int nextRow = imageList_->currentRow();
    {
        const QSignalBlocker blocker(imageList_);
        for (QListWidgetItem* item : selected) {
            delete imageList_->takeItem(imageList_->row(item));
        }
        nextRow = std::min(nextRow, imageList_->count() - 1);
        if (nextRow >= 0) {
            imageList_->setCurrentRow(nextRow);
        }
    }
    updateRemovalControls();
    emit imagesRemoved(selected.size(), currentPath());
}

void ImageListPanel::removeAllImages() {
    const int count = imageList_->count();
    if (count == 0) {
        return;
    }

    {
        const QSignalBlocker blocker(imageList_);
        imageList_->clear();
    }
    updateRemovalControls();
    emit imagesRemoved(count, QString());
}

void ImageListPanel::showContextMenu(const QPoint& position) {
    QListWidgetItem* clickedItem = imageList_->itemAt(position);
    if (clickedItem == nullptr) {
        return;
    }
    imageList_->setCurrentItem(clickedItem, QItemSelectionModel::ClearAndSelect);
    const QString sourcePath = clickedItem->data(kImagePathRole).toString();

    QMenu menu(imageList_);
    QMenu* exportMenu = menu.addMenu(QStringLiteral("导出当前图像"));
    for (const ImageExportFormat& format : ImageExportService::formats()) {
        QAction* action = exportMenu->addAction(format.label);
        action->setEnabled(ImageExportService::isSupported(format.id));
        connect(action, &QAction::triggered, this, [this, sourcePath, format]() {
            emit exportRequested(sourcePath, format.id);
        });
    }
    menu.addSeparator();
    menu.addAction(removeAction_);
    menu.exec(imageList_->viewport()->mapToGlobal(position));
}
