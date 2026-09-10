#pragma once

#include <QGroupBox>
#include <QString>

#include "TiRawImage.h"

class QAction;
class QPushButton;
class QListWidget;
class QListWidgetItem;

/* 左侧图像列表只通过文件路径与外部交互，不向 MainWindow 暴露 Qt 列表项。 */
class ImageListPanel final : public QGroupBox {
    Q_OBJECT

public:
    explicit ImageListPanel(QWidget* parent = nullptr);

    void clearImages();
    void addOrUpdateImage(const QString& path, const TiRawImage* image = nullptr);
    void ensureThumbnail(const QString& path, const TiRawImage& image);
    void setCurrentPath(const QString& path);
    QString currentPath() const;
    int imageCount() const;

signals:
    void imageActivated(const QString& path);
    void imagesRemoved(int count, const QString& nextPath);
    void exportRequested(const QString& sourcePath, const QString& formatId);

private:
    QListWidgetItem* findItem(const QString& path) const;
    void updateRemovalControls();
    void removeSelectedImages();
    void removeAllImages();
    void showContextMenu(const QPoint& position);

    QListWidget* imageList_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* removeAllButton_ = nullptr;
    QAction* removeAction_ = nullptr;
};
