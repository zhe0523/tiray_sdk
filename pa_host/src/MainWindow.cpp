#include "MainWindow.h"

#include "AppLogService.h"
#include "AppSettings.h"
#include "ImageAcquisitionController.h"
#include "ImageExportService.h"
#include "ImageListPanel.h"
#include "ImageTransferWorkflowController.h"
#include "PaDeviceController.h"
#include "PcieImageSource.h"

#include <QAction>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextDocument>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kStaticImageRefreshIntervalMs = 33;
constexpr int kCachedRawFrameCount = 12;
constexpr int kCachedDisplayFrameCount = 12;
constexpr int kLiveDisplayMaxLongEdge = 2048;

QSize scaledDisplaySize(const QSize& sourceSize, int maxLongEdge) {
    if (sourceSize.isEmpty() || maxLongEdge <= 0) {
        return sourceSize;
    }
    const int longEdge = std::max(sourceSize.width(), sourceSize.height());
    if (longEdge <= maxLongEdge) {
        return sourceSize;
    }
    return sourceSize.scaled(QSize(maxLongEdge, maxLongEdge), Qt::KeepAspectRatio);
}

QSize initialWindowSize() {
    auto* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return {1180, 720};
    }

    const QSize available = screen->availableGeometry().size();
    return {
        std::max(1180, std::min(1320, static_cast<int>(available.width() * 0.92))),
        std::max(820, std::min(900, static_cast<int>(available.height() * 0.92))),
    };
}

QPushButton* makeCommandButton(const QString& text) {
    auto* button = new QPushButton(text);
    button->setMinimumHeight(26);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return button;
}

void populateSerialPorts(QComboBox* combo) {
    const QString currentPort = combo->currentText();
    combo->clear();
    combo->setEditable(true);

    auto addPortIfMissing = [combo](const QString& port) {
        if (!port.isEmpty() && combo->findText(port) < 0) {
            combo->addItem(port);
        }
    };

#ifndef Q_OS_WIN
    /*
     * Kylin 实机的 RS422 使用 WCH 多串口卡。Qt/udev 有时只枚举到 ttyS*，
     * 所以这里始终提供 ttyWCH0~3，并放在前面方便现场选择。
     */
    addPortIfMissing(QStringLiteral("/dev/ttyWCH0"));
    addPortIfMissing(QStringLiteral("/dev/ttyWCH1"));
    addPortIfMissing(QStringLiteral("/dev/ttyWCH2"));
    addPortIfMissing(QStringLiteral("/dev/ttyWCH3"));
#endif

    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
#ifdef Q_OS_WIN
        addPortIfMissing(info.portName());
#else
        const QString location = info.systemLocation();
        addPortIfMissing(location);
#endif
    }
    if (combo->count() == 0) {
#ifdef Q_OS_WIN
        addPortIfMissing(QStringLiteral("COM1"));
        addPortIfMissing(QStringLiteral("COM2"));
        addPortIfMissing(QStringLiteral("COM3"));
        addPortIfMissing(QStringLiteral("COM4"));
#endif
    }
    if (!currentPort.isEmpty()) {
        combo->setCurrentText(currentPort);
    }
}

QString defaultImageDirectory(const AppSettings& settings) {
    const QString remembered = settings.lastImageDirectory();
    if (!remembered.isEmpty() && QDir(remembered).exists()) {
        return remembered;
    }

#ifndef Q_OS_WIN
    const QString sampleDirectory = QStringLiteral("/home/zhe/app/windows/tidetector/CollectImage");
    if (QDir(sampleDirectory).exists()) {
        return sampleDirectory;
    }
#endif

    const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return picturesDirectory.isEmpty() ? QDir::homePath() : picturesDirectory;
}

QString imageTransferStateText(
    ImageTransferMode mode,
    ImageTransferState state) {
    switch (state) {
    case ImageTransferState::Disconnected:
        return QStringLiteral("工作模式: 未连接");
    case ImageTransferState::Ready:
        return mode == ImageTransferMode::Manual
            ? QStringLiteral("工作模式: 手动就绪")
            : QStringLiteral("工作模式: 持续就绪");
    case ImageTransferState::StartingSingle:
        return QStringLiteral("工作模式: 单帧上图中");
    case ImageTransferState::StartingContinuous:
        return QStringLiteral("工作模式: 正在启动持续上图");
    case ImageTransferState::ContinuousRunning:
        return QStringLiteral("工作模式: 持续上图中");
    case ImageTransferState::Stopping:
        return QStringLiteral("工作模式: 正在停止上图");
    case ImageTransferState::Error:
        return QStringLiteral("工作模式: 上图控制错误");
    }
    return QStringLiteral("工作模式: 未知");
}

QString normalizedImagePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString deviceStateName(PaDeviceState state) {
    switch (state) {
    case PaDeviceState::Disconnected:
        return QStringLiteral("Disconnected");
    case PaDeviceState::Ready:
        return QStringLiteral("Ready");
    case PaDeviceState::Busy:
        return QStringLiteral("Busy");
    case PaDeviceState::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QPixmap drawAnalysisPreview(const QImage& image, const QRect& roi, const QSize& size) {
    QPixmap pixmap(size);
    pixmap.fill(QColor(8, 9, 10));

    if (image.isNull()) {
        return pixmap;
    }

    const QSize scaledSize = image.size().scaled(size, Qt::KeepAspectRatio);
    const QRect targetRect(
        (size.width() - scaledSize.width()) / 2,
        (size.height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(targetRect, image);

    const qreal scaleX = static_cast<qreal>(targetRect.width()) / image.width();
    const qreal scaleY = static_cast<qreal>(targetRect.height()) / image.height();
    const QRectF roiRect(
        targetRect.left() + roi.left() * scaleX,
        targetRect.top() + roi.top() * scaleY,
        roi.width() * scaleX,
        roi.height() * scaleY);
    painter.setPen(QPen(QColor(255, 110, 0), 1.2));
    painter.drawRect(roiRect);
    return pixmap;
}

QPixmap drawLineChart(
    const QString& title,
    const QString& yLabel,
    const QString& xLabel,
    const QVector<double>& values,
    const QSize& size) {
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect plotRect(56, 28, size.width() - 72, size.height() - 62);
    painter.setPen(QColor(25, 31, 37));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRect(0, 4, size.width(), 20), Qt::AlignCenter, title);

    painter.setFont(QFont());
    painter.setPen(QColor(230, 235, 240));
    for (int i = 0; i <= 5; ++i) {
        const int x = plotRect.left() + plotRect.width() * i / 5;
        painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
        const int y = plotRect.top() + plotRect.height() * i / 5;
        painter.drawLine(plotRect.left(), y, plotRect.right(), y);
    }

    painter.setPen(QColor(30, 35, 40));
    painter.drawRect(plotRect);

    if (!values.isEmpty()) {
        auto range = std::minmax_element(values.begin(), values.end());
        double minValue = *range.first;
        double maxValue = *range.second;
        if (maxValue <= minValue) {
            maxValue = minValue + 1.0;
        }

        QPolygonF polyline;
        polyline.reserve(values.size());
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = plotRect.left() + (values.size() == 1 ? 0.0 : plotRect.width() * static_cast<qreal>(i) / (values.size() - 1));
            const qreal normalized = (values.at(i) - minValue) / (maxValue - minValue);
            const qreal y = plotRect.bottom() - normalized * plotRect.height();
            polyline << QPointF(x, y);
        }
        painter.setPen(QPen(QColor(31, 119, 180), 1.0));
        painter.drawPolyline(polyline);

        painter.setPen(QColor(60, 67, 75));
        painter.drawText(4, plotRect.top() + 4, QString::number(maxValue, 'f', 0));
        painter.drawText(4, plotRect.bottom(), QString::number(minValue, 'f', 0));
    }

    painter.setPen(QColor(25, 31, 37));
    painter.drawText(QRect(plotRect.left(), size.height() - 24, plotRect.width(), 18), Qt::AlignCenter, xLabel);

    painter.save();
    painter.translate(14, plotRect.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-plotRect.height() / 2, 0, plotRect.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();

    return pixmap;
}
}

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(std::make_shared<BuiltinImageAlgorithms>(), parent) {
}

MainWindow::~MainWindow() {
    if (logService_ != nullptr) {
        logService_->info(QStringLiteral("SYSTEM"), QStringLiteral("应用正常退出"));
    }
    if (settings_ != nullptr) {
        settings_->sync();
    }
}

MainWindow::MainWindow(std::shared_ptr<IImageAlgorithms> algorithms, QWidget* parent)
    : QMainWindow(parent) {
    settings_ = std::make_unique<AppSettings>();
    logService_ = new AppLogService(this);
    QString logError;
    if (!logService_->start(QString(), &logError)) {
        qWarning().noquote() << logError;
    }
    imageSession_ = std::make_unique<ImageSession>(std::move(algorithms), this);
    pcieSource_ = new PcieImageSource(this);
    acquisitionController_ = new ImageAcquisitionController(this);
    deviceController_ = new PaDeviceController(&serial_, this);
    imageTransferController_ = new ImageTransferWorkflowController(deviceController_, this);
    imageFrameCache_.setMaxCost(kCachedRawFrameCount);
    frameDisplayCache_.setMaxCost(kCachedDisplayFrameCount);
    setWindowTitle(QStringLiteral("PA Host"));
    setMinimumSize(1180, 820);
    resize(initialWindowSize());

    createMenus();

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("mainRoot"));
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    topBar_ = createTopBar();
    rootLayout->addWidget(topBar_);

    auto* splitter = new QSplitter(Qt::Horizontal, root);
    imageListPanel_ = new ImageListPanel(splitter);
    splitter->addWidget(imageListPanel_);

    imageView_ = new ImageView(splitter);
    imageView_->setObjectName(QStringLiteral("imageCanvas"));
    splitter->addWidget(imageView_);
    rightPanel_ = createRightPanel();
    splitter->addWidget(rightPanel_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({205, 920, 285});
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(root);
    createLogDock();
    createStatusBar();

    connect(logService_, &AppLogService::entryAdded, this, [this](const AppLogEntry& entry) {
        if (logView_ != nullptr) {
            logView_->appendPlainText(entry.formatted());
        }
    });
    connect(logService_, &AppLogService::persistenceError, this, [](const QString& message) {
        qWarning().noquote() << message;
    });

    connect(deviceController_, &PaDeviceController::stateChanged,
        this, &MainWindow::updateDeviceState);
    connect(deviceController_, &PaDeviceController::deviceStatusChanged,
        this, [this](const PaDeviceStatus& status) {
            if (!status.model.isEmpty()) {
                modelLabel_->setText(QStringLiteral("型号: %1").arg(status.model));
            }
            if (!status.serialNumber.isEmpty()) {
                serialLabel_->setText(QStringLiteral("序列号: %1").arg(status.serialNumber));
            }
            if (!status.armVersion.isEmpty()) {
                armProgramVersion_ = status.armVersion;
            }
            if (!status.fpgaVersion.isEmpty()) {
                fpgaVersion_ = status.fpgaVersion;
            }
            logService_->debug(QStringLiteral("RS422"),
                QStringLiteral("设备状态: int_vector=%1 pa_version=%2 com_version=%3 rst_state=%4 wr=%5/%6 corr=%7/%8 arm=%9 fpga=%10")
                    .arg(status.interruptVector)
                    .arg(status.paVersion)
                    .arg(status.communicationVersion)
                    .arg(status.resetState)
                    .arg(status.writeState)
                    .arg(status.writeEnd)
                    .arg(status.correctionState)
                    .arg(status.correctionEnd)
                    .arg(status.armVersion.isEmpty() ? QStringLiteral("--") : status.armVersion)
                    .arg(status.fpgaVersion.isEmpty() ? QStringLiteral("--") : status.fpgaVersion));
        });
    connect(deviceController_, &PaDeviceController::lineTransmitted, this, [this](const QString& line) {
        logService_->info(QStringLiteral("RS422"), QStringLiteral("TX: %1").arg(line));
    });
    connect(deviceController_, &PaDeviceController::errorOccurred, this, [this](const QString& message) {
        logService_->error(QStringLiteral("RS422"), QStringLiteral("控制错误: %1").arg(message));
    });
    connect(deviceController_, &PaDeviceController::commandFinished, this,
        [this](PaProtocol::Command command, bool success, const QString& detail) {
            const QString message = QStringLiteral("命令%1: %2, %3")
                                        .arg(success ? QStringLiteral("完成") : QStringLiteral("失败"))
                                        .arg(PaProtocol::commandName(command), detail);
            logService_->log(success ? AppLogLevel::Info : AppLogLevel::Warning,
                QStringLiteral("RS422"), message);
        });
    connect(deviceController_, &PaDeviceController::binaryCommandFinished,
        this, &MainWindow::handleBinaryCommandFinished);
    connect(imageTransferController_, &ImageTransferWorkflowController::controlsChanged,
        this, &MainWindow::updateImageTransferControls);
    connect(imageTransferController_, &ImageTransferWorkflowController::errorOccurred,
        this, [this](const QString& message) {
            logService_->warning(QStringLiteral("IMAGE_CONTROL"), message);
        });
    connect(imageView_, &ImageView::zoomChanged, this, [this](int percent) {
        progressLabel_->setText(QStringLiteral("缩放: %1%").arg(percent));
    });
    connect(imageView_, &ImageView::pixelHovered, this, &MainWindow::updatePixelInfo);
    connect(imageView_, &ImageView::analysisRoiSelected, this, &MainWindow::handleAnalysisRoi);
    connect(imageView_, &ImageView::windowLevelRoiSelected, this, &MainWindow::applyWindowLevelFromRoi);
    connect(imageView_, &ImageView::roiCleared, this, [this]() {
        activeRoi_ = {};
        updateFullImageInfo();
    });
    connect(imageListPanel_, &ImageListPanel::imageActivated, this, &MainWindow::handleImageListSelection);
    connect(imageListPanel_, &ImageListPanel::imagesRemoved, this, &MainWindow::handleImagesRemoved);
    connect(imageListPanel_, &ImageListPanel::exportRequested, this, &MainWindow::exportImage);
    connect(acquisitionController_, &ImageAcquisitionController::framePresented,
        this, &MainWindow::handlePresentedFrame);
    connect(acquisitionController_, &ImageAcquisitionController::fpsUpdated,
        this, [this](double actualFps, int) {
            fpsLabel_->setText(QStringLiteral("显示 FPS: %1").arg(actualFps, 0, 'f', 2));
        });
    connect(acquisitionController_, &ImageAcquisitionController::stateChanged,
        this, [this]() {
            updateImageUiState();
        });
    connect(acquisitionController_, &ImageAcquisitionController::errorOccurred,
        this, [this](const QString& message) {
        logService_->warning(QStringLiteral("IMAGE"), message);
    });
    connect(acquisitionController_, &ImageAcquisitionController::sessionFinished,
        this, [this](const ImageAcquisitionStats& stats) {
        fpsLabel_->setText(QStringLiteral("显示 FPS: --"));
        updateImageUiState();
        logService_->info(QStringLiteral("IMAGE"),
            QStringLiteral("图像会话结束: 输入 %1 帧，显示 %2 帧，显示丢帧 %3，源失败 %4")
                .arg(stats.source.deliveredFrames)
                .arg(stats.presentation.presentedFrames)
                .arg(stats.presentation.droppedFrames)
            .arg(stats.source.failedFrames));
    });
    connect(pcieSource_, &PcieImageSource::captureInfo,
        this, [this](const QString& message) {
            logService_->info(QStringLiteral("IMAGE"), message);
        });
    connect(pcieSource_, &PcieImageSource::frameFileSaved,
        this, [this](const QString& path, const TiRawImage& image) {
            imageListPanel_->addOrUpdateImage(path, &image);
            imageListPanel_->setCurrentPath(path);
        });
    imageRefreshTimer_.setSingleShot(true);
    imageRefreshTimer_.setInterval(kStaticImageRefreshIntervalMs);
    connect(&imageRefreshTimer_, &QTimer::timeout, this, [this]() {
        refreshImage(resetViewStateOnRefresh_);
        resetViewStateOnRefresh_ = false;
    });
    imageInfoRefreshTimer_.setSingleShot(true);
    connect(&imageInfoRefreshTimer_, &QTimer::timeout, this, &MainWindow::updateCurrentImageInfo);
    updateDeviceState(deviceController_->state());
    updateImageTransferControls();
    updateImageUiState();
    logService_->info(QStringLiteral("SYSTEM"),
        QStringLiteral("应用启动，日志目录: %1").arg(logService_->logDirectory()));
#ifndef Q_OS_WIN
    QTimer::singleShot(0, this, &MainWindow::startPcieCapture);
#endif
}

void MainWindow::openImage() {
    const QString initialDirectory = defaultImageDirectory(*settings_);
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("打开图像（可多选）"),
        initialDirectory,
        QStringLiteral("TiRayRaw (*.tiraw);;All Files (*)"));
    if (paths.isEmpty()) {
        return;
    }

    settings_->setLastImageDirectory(QFileInfo(paths.first()).absolutePath());

    TiRawImage lastImage;
    QString lastPath;
    QStringList failures;
    for (const QString& selectedPath : paths) {
        const QString path = normalizedImagePath(selectedPath);
        TiRawImage image;
        QString error;
        if (!image.load(path, &error)) {
            failures.push_back(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
            continue;
        }

        imageListPanel_->addOrUpdateImage(path, &image);
        lastImage = std::move(image);
        lastPath = path;
    }

    if (!lastPath.isEmpty()) {
        ImageFrame frame;
        frame.image = std::move(lastImage);
        frame.sourceName = QFileInfo(lastPath).fileName();
        frame.contentCacheKey = QFileInfo(lastPath).absoluteFilePath();
        frame.receivedAt = QDateTime::currentDateTimeUtc();
        QString error;
        if (imageSession_->setFrame(frame, &error)) {
            imageListPanel_->setCurrentPath(lastPath);
            showCurrentSessionImage(lastPath, true);
        }
    }

    if (!failures.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("部分图像打开失败"),
            failures.join(QLatin1Char('\n')));
    }
}

void MainWindow::startPcieCapture() {
    stopImageAcquisition();
    clearFrameDisplayCaches();
    stableFrameStatsCache_.clear();
    imageInfoTimer_.invalidate();
    imageListPanel_->clearImages();

    PcieImageSourceOptions options;
    options.width = 3072;
    options.height = 7680;
    const QString appDataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    options.outputDirectory = QDir(appDataDirectory.isEmpty() ? QDir::homePath() : appDataDirectory)
        .filePath(QStringLiteral("pcie_images"));
    pcieSource_->setOptions(options);

    QString error;
    constexpr int kPcieDisplayFps = 30;
    if (!acquisitionController_->start(pcieSource_, kPcieDisplayFps, &error)) {
        imageView_->setPixmapCacheEnabled(false);
        updateImageUiState();
        logService_->warning(QStringLiteral("IMAGE"),
            QStringLiteral("PCIe 自动监听未启动: %1").arg(error));
        return;
    }

    fpsLabel_->setText(QStringLiteral("显示 FPS: 0.00"));
    updateImageUiState();
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("PCIe 自动监听已启动: c2h=%1 event=%2 output=%3 fallback_size=%4x%5 frame_size_source=BAR0[0x00c/0x010]")
            .arg(options.c2hDevice)
            .arg(options.eventDevice)
            .arg(options.outputDirectory)
            .arg(options.width)
            .arg(options.height));
}

void MainWindow::stopImageAcquisition() {
    imageRefreshTimer_.stop();
    imageInfoRefreshTimer_.stop();
    acquisitionController_->stop();
    resetViewStateOnRefresh_ = false;
    clearFrameDisplayCaches();
    stableFrameStatsCache_.clear();
    imageView_->setPixmapCacheEnabled(false);
    if (fpsLabel_ != nullptr) {
        fpsLabel_->setText(QStringLiteral("显示 FPS: --"));
    }
    updateImageUiState();
}

void MainWindow::saveDisplayImage() {
    if (!imageView_->hasImage()) {
        QMessageBox::information(this, QStringLiteral("无图像"), QStringLiteral("当前没有可保存的显示图像"));
        return;
    }

    QString saveDirectory = settings_->lastSaveDirectory();
    if (saveDirectory.isEmpty() || !QDir(saveDirectory).exists()) {
        saveDirectory = defaultImageDirectory(*settings_);
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存显示图像"),
        QDir(saveDirectory).filePath(QStringLiteral("pa_host_display.png")),
        QStringLiteral("PNG Image (*.png)"));
    if (path.isEmpty()) {
        return;
    }

    if (!imageView_->savePng(path)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("图像保存失败"));
        return;
    }
    settings_->setLastSaveDirectory(QFileInfo(path).absolutePath());
    logService_->info(QStringLiteral("IMAGE"), QStringLiteral("保存显示图像: %1").arg(path));
}

void MainWindow::connectSerial() {
    QString error;
    if (!deviceController_->connectDevice(portCombo_->currentText(), baudSpin_->value(), &error)) {
        QMessageBox::warning(this, QStringLiteral("串口打开失败"), error);
        return;
    }
    settings_->setSerialPort(portCombo_->currentText());
    settings_->setSerialBaudRate(baudSpin_->value());
    logService_->info(QStringLiteral("RS422"),
        QStringLiteral("串口已打开: %1 @ %2")
            .arg(portCombo_->currentText())
            .arg(baudSpin_->value()));
    QString statusError;
    if (!deviceController_->sendCommand(PaProtocol::Command::Status, &statusError)) {
        logService_->warning(QStringLiteral("RS422"),
            QStringLiteral("连接后读取状态失败: %1").arg(statusError));
    }
}

void MainWindow::disconnectSerial() {
    deviceController_->disconnectDevice();
    logService_->info(QStringLiteral("RS422"), QStringLiteral("串口已关闭"));
}

void MainWindow::sendCommand(PaProtocol::Command command) {
    QString error;
    deviceController_->sendCommand(command, &error);
}

void MainWindow::exportDiagnostics() {
    QString directory = settings_->lastDiagnosticDirectory();
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }

    const QString defaultName = QStringLiteral("pa_host_diagnostics_%1.txt")
                                    .arg(QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd-HHmmss")));
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出诊断信息"),
        QDir(directory).filePath(defaultName),
        QStringLiteral("Text File (*.txt)"));
    if (outputPath.isEmpty()) {
        return;
    }
    if (QFileInfo(outputPath).suffix().isEmpty()) {
        outputPath += QStringLiteral(".txt");
    }

    QMap<QString, QString> metadata;
    metadata.insert(QStringLiteral("serial.port"), portCombo_->currentText());
    metadata.insert(QStringLiteral("serial.baud"), QString::number(baudSpin_->value()));
    metadata.insert(QStringLiteral("control.command_timeout_ms"),
        QString::number(deviceController_->commandTimeoutMs()));
    metadata.insert(QStringLiteral("control.max_command_retries"),
        QString::number(deviceController_->maxCommandRetries()));
    metadata.insert(QStringLiteral("control.device_state"),
        deviceStateName(deviceController_->state()));
    metadata.insert(QStringLiteral("log.directory"), logService_->logDirectory());

    QString error;
    if (!logService_->exportDiagnostics(outputPath, metadata, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        logService_->error(QStringLiteral("SYSTEM"),
            QStringLiteral("导出诊断信息失败: %1").arg(error));
        return;
    }
    settings_->setLastDiagnosticDirectory(QFileInfo(outputPath).absolutePath());
    logService_->info(QStringLiteral("SYSTEM"),
        QStringLiteral("导出诊断信息: %1").arg(outputPath));
}

void MainWindow::configureCommandTimeout() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("命令超时设置"));
    auto* form = new QFormLayout(&dialog);

    auto* timeoutSpin = new QSpinBox(&dialog);
    timeoutSpin->setRange(100, 300000);
    timeoutSpin->setSingleStep(100);
    timeoutSpin->setSuffix(QStringLiteral(" ms"));
    timeoutSpin->setValue(deviceController_->commandTimeoutMs());
    form->addRow(QStringLiteral("响应超时"), timeoutSpin);

    auto* retrySpin = new QSpinBox(&dialog);
    retrySpin->setRange(0, 10);
    retrySpin->setValue(deviceController_->maxCommandRetries());
    form->addRow(QStringLiteral("最大重试次数"), retrySpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal,
                                         &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int timeoutMs = timeoutSpin->value();
    const int maxRetries = retrySpin->value();
    deviceController_->setCommandTimeoutMs(timeoutMs);
    deviceController_->setMaxCommandRetries(maxRetries);
    logService_->info(QStringLiteral("SYSTEM"),
        QStringLiteral("命令可靠性设置：响应超时 %1 ms，最大重试 %2 次")
            .arg(timeoutMs)
            .arg(maxRetries));
}

void MainWindow::updateWindowLevel() {
    if (centerSlider_->value() != centerSpin_->value()) {
        centerSpin_->setValue(centerSlider_->value());
    }
    if (widthSlider_->value() != widthSpin_->value()) {
        widthSpin_->setValue(widthSlider_->value());
    }
    if (!autoWindowCheck_->isChecked()) {
        clearFrameDisplayCaches();
        scheduleImageRefresh(false);
    }
}

void MainWindow::toggleImageMaximized() {
    imageMaximized_ = !imageMaximized_;

    if (topBar_ != nullptr) {
        topBar_->setVisible(!imageMaximized_);
    }
    if (imageListPanel_ != nullptr) {
        imageListPanel_->setVisible(!imageMaximized_);
    }
    if (rightPanel_ != nullptr) {
        rightPanel_->setVisible(!imageMaximized_);
    }
    if (imageMaximizeAction_ != nullptr) {
        imageMaximizeAction_->setChecked(imageMaximized_);
        imageMaximizeAction_->setText(imageMaximized_
                                          ? QStringLiteral("退出图像最大化")
                                          : QStringLiteral("图像最大化"));
    }
    if (imageView_ != nullptr && imageView_->hasImage()) {
        imageView_->fitToWindow();
    }
    updateImageUiState();
}

QWidget* MainWindow::createTopBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    idleModeButton_ = new QPushButton(QStringLiteral("Idle"), bar);
    continuousModeButton_ = new QPushButton(QStringLiteral("Continuous"), bar);
    startImageButton_ = new QPushButton(QStringLiteral("手动上图"), bar);
    stopImageButton_ = new QPushButton(QStringLiteral("停止上图"), bar);

    idleModeButton_->setCheckable(true);
    idleModeButton_->setChecked(true);
    continuousModeButton_->setCheckable(true);
    idleModeButton_->setProperty("role", "mode");
    continuousModeButton_->setProperty("role", "mode");
    startImageButton_->setProperty("role", "primary");
    stopImageButton_->setProperty("role", "stop");

    auto* modeGroup = new QButtonGroup(bar);
    modeGroup->setExclusive(true);
    modeGroup->addButton(idleModeButton_);
    modeGroup->addButton(continuousModeButton_);

    connect(idleModeButton_, &QPushButton::clicked, this, [this]() {
        QString error;
        imageTransferController_->setMode(ImageTransferMode::Manual, &error);
    });
    connect(continuousModeButton_, &QPushButton::clicked, this, [this]() {
        QString error;
        imageTransferController_->setMode(ImageTransferMode::Continuous, &error);
    });
    connect(startImageButton_, &QPushButton::clicked, this, [this]() {
        QString error;
        imageTransferController_->startTransfer(&error);
    });
    connect(stopImageButton_, &QPushButton::clicked, this, [this]() {
        QString error;
        imageTransferController_->stopTransfer(&error);
    });

    layout->addWidget(idleModeButton_);
    layout->addWidget(continuousModeButton_);
    layout->addSpacing(18);
    layout->addWidget(startImageButton_);
    layout->addWidget(stopImageButton_);
    layout->addStretch(1);

    return bar;
}

QWidget* MainWindow::createRightPanel() {
    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("rightPanel"));
    panel->setMinimumWidth(270);
    panel->setMaximumWidth(340);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(6);
    imageOpsPanel_ = createImageOpsPanel();
    windowLevelPanel_ = createWindowLevelPanel();
    layout->addWidget(imageOpsPanel_);
    layout->addWidget(windowLevelPanel_);
    layout->addWidget(createImageInfoPanel());
    layout->addStretch(1);

    return panel;
}

QWidget* MainWindow::createImageOpsPanel() {
    auto* group = new QGroupBox(QStringLiteral("图像操作"), this);
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(4);

    auto* rotateLeftButton = makeCommandButton(QStringLiteral("左转"));
    auto* rotateRightButton = makeCommandButton(QStringLiteral("右转"));
    auto* flipHButton = makeCommandButton(QStringLiteral("水平翻转"));
    auto* flipVButton = makeCommandButton(QStringLiteral("垂直翻转"));
    auto* zoomOutButton = makeCommandButton(QStringLiteral("缩小"));
    auto* zoomInButton = makeCommandButton(QStringLiteral("放大"));
    auto* fitButton = makeCommandButton(QStringLiteral("适应"));
    auto* resetButton = makeCommandButton(QStringLiteral("重置"));
    auto* saveButton = makeCommandButton(QStringLiteral("保存"));
    auto* maximizeButton = makeCommandButton(QStringLiteral("最大化图像"));

    connect(rotateLeftButton, &QPushButton::clicked, imageView_, &ImageView::rotateLeft);
    connect(rotateRightButton, &QPushButton::clicked, imageView_, &ImageView::rotateRight);
    connect(flipHButton, &QPushButton::clicked, imageView_, &ImageView::flipHorizontal);
    connect(flipVButton, &QPushButton::clicked, imageView_, &ImageView::flipVertical);
    connect(zoomOutButton, &QPushButton::clicked, imageView_, &ImageView::zoomOut);
    connect(zoomInButton, &QPushButton::clicked, imageView_, &ImageView::zoomIn);
    connect(fitButton, &QPushButton::clicked, imageView_, &ImageView::fitToWindow);
    connect(resetButton, &QPushButton::clicked, imageView_, &ImageView::resetView);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveDisplayImage);
    connect(maximizeButton, &QPushButton::clicked, this, &MainWindow::toggleImageMaximized);

    layout->addWidget(rotateLeftButton, 0, 0);
    layout->addWidget(rotateRightButton, 0, 1);
    layout->addWidget(flipHButton, 1, 0);
    layout->addWidget(flipVButton, 1, 1);
    layout->addWidget(zoomOutButton, 2, 0);
    layout->addWidget(zoomInButton, 2, 1);
    layout->addWidget(fitButton, 3, 0);
    layout->addWidget(resetButton, 3, 1);
    layout->addWidget(saveButton, 4, 0, 1, 2);
    layout->addWidget(maximizeButton, 5, 0, 1, 2);

    return group;
}

QWidget* MainWindow::createWindowLevelPanel() {
    auto* group = new QGroupBox(QStringLiteral("窗宽窗位"), this);
    auto* layout = new QGridLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(4);

    autoWindowCheck_ = new QCheckBox(QStringLiteral("自动窗宽窗位"), group);
    autoWindowCheck_->setChecked(true);

    centerSlider_ = new QSlider(Qt::Horizontal, group);
    centerSlider_->setRange(0, 65535);
    centerSlider_->setValue(4096);
    widthSlider_ = new QSlider(Qt::Horizontal, group);
    widthSlider_->setRange(1, 65535);
    widthSlider_->setValue(4096);

    centerSpin_ = new QSpinBox(group);
    centerSpin_->setRange(0, 65535);
    centerSpin_->setValue(4096);
    widthSpin_ = new QSpinBox(group);
    widthSpin_->setRange(1, 65535);
    widthSpin_->setValue(4096);

    connect(autoWindowCheck_, &QCheckBox::toggled, this, [this]() {
        updateWindowLevelControlState();
        clearFrameDisplayCaches();
        scheduleImageRefresh(false);
    });
    connect(centerSlider_, &QSlider::valueChanged, this, &MainWindow::updateWindowLevel);
    connect(widthSlider_, &QSlider::valueChanged, this, &MainWindow::updateWindowLevel);
    connect(centerSpin_, QOverload<int>::of(&QSpinBox::valueChanged), centerSlider_, &QSlider::setValue);
    connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), widthSlider_, &QSlider::setValue);

    layout->addWidget(autoWindowCheck_, 0, 0, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("窗位")), 1, 0);
    layout->addWidget(centerSlider_, 1, 1);
    layout->addWidget(centerSpin_, 1, 2);
    layout->addWidget(new QLabel(QStringLiteral("窗宽")), 2, 0);
    layout->addWidget(widthSlider_, 2, 1);
    layout->addWidget(widthSpin_, 2, 2);

    return group;
}

QWidget* MainWindow::createImageInfoPanel() {
    auto* group = new QGroupBox(QStringLiteral("图像信息"), this);
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    pixelInfoLabel_ = new QLabel(QStringLiteral("像素值: --"), group);
    pixelInfoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* separator = new QFrame(group);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet(QStringLiteral("color:#d8e0ea;"));

    roiInfoLabel_ = new QLabel(QStringLiteral("ROI 信息: --"), group);
    roiInfoLabel_->setWordWrap(true);
    roiInfoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(pixelInfoLabel_);
    layout->addWidget(separator);
    layout->addWidget(roiInfoLabel_);
    return group;
}

void MainWindow::createLogDock() {
    logDock_ = new QDockWidget(QStringLiteral("运行日志"), this);
    logDock_->setObjectName(QStringLiteral("runtimeLogDock"));
    logDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    logView_ = new QPlainTextEdit(logDock_);
    logView_->setObjectName(QStringLiteral("runtimeLogView"));
    logView_->setReadOnly(true);
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView_->document()->setMaximumBlockCount(2000);
    logDock_->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    logDock_->hide();

    if (viewMenu_ != nullptr) {
        QAction* toggleAction = logDock_->toggleViewAction();
        toggleAction->setText(QStringLiteral("运行日志"));
        viewMenu_->addAction(toggleAction);
    }
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* openAction = fileMenu->addAction(QStringLiteral("打开图像"));
    saveDisplayAction_ = fileMenu->addAction(QStringLiteral("保存显示图像"));
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(QStringLiteral("退出"));

    connect(openAction, &QAction::triggered, this, &MainWindow::openImage);
    connect(saveDisplayAction_, &QAction::triggered, this, &MainWindow::saveDisplayImage);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* serialMenu = menuBar()->addMenu(QStringLiteral("RS422"));
    auto* portLabelAction = serialMenu->addAction(QStringLiteral("端口"));
    portLabelAction->setEnabled(false);
    portCombo_ = new QComboBox(serialMenu);
    populateSerialPorts(portCombo_);
    if (!settings_->serialPort().isEmpty()) {
        portCombo_->setCurrentText(settings_->serialPort());
    }
    auto* portAction = new QWidgetAction(serialMenu);
    portAction->setDefaultWidget(portCombo_);
    serialMenu->addAction(portAction);

    auto* baudLabelAction = serialMenu->addAction(QStringLiteral("波特率"));
    baudLabelAction->setEnabled(false);
    baudSpin_ = new QSpinBox(serialMenu);
    baudSpin_->setRange(1200, 3000000);
    baudSpin_->setValue(settings_->serialBaudRate());
    auto* baudAction = new QWidgetAction(serialMenu);
    baudAction->setDefaultWidget(baudSpin_);
    serialMenu->addAction(baudAction);

    serialMenu->addSeparator();
    refreshPortsAction_ = serialMenu->addAction(QStringLiteral("刷新端口"), this, [this]() {
        populateSerialPorts(portCombo_);
    });
    connectSerialAction_ = serialMenu->addAction(
        QStringLiteral("连接"), this, &MainWindow::connectSerial);
    disconnectSerialAction_ = serialMenu->addAction(
        QStringLiteral("断开"), this, &MainWindow::disconnectSerial);

    auto* calibrationMenu = menuBar()->addMenu(QStringLiteral("校准"));
    calibrationMenu->addAction(QStringLiteral("制作暗场模板"), this,
                                &MainWindow::showOffsetTemplateDialog);
    calibrationMenu->addAction(QStringLiteral("制作亮场模板"), this,
                                &MainWindow::showGainTemplateDialog);
    calibrationMenu->addSeparator();
    calibrationMenu->addAction(QStringLiteral("查看当前暗场模板"), this,
                                &MainWindow::viewOffsetTemplate);
    calibrationMenu->addAction(QStringLiteral("查看当前亮场模板"), this,
                                &MainWindow::viewGainTemplate);

    viewMenu_ = menuBar()->addMenu(QStringLiteral("视图"));
    imageMaximizeAction_ = viewMenu_->addAction(QStringLiteral("图像最大化"));
    imageMaximizeAction_->setCheckable(true);
    imageMaximizeAction_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(imageMaximizeAction_, &QAction::triggered, this, &MainWindow::toggleImageMaximized);

    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("工具"));
    toolsMenu->addAction(QStringLiteral("命令超时设置"), this, &MainWindow::configureCommandTimeout);
    toolsMenu->addAction(QStringLiteral("导出诊断信息"), this, &MainWindow::exportDiagnostics);
    toolsMenu->addSeparator();
    toolsMenu->addAction(QStringLiteral("Dynamic 配置"), this, &MainWindow::showDynamicConfigDialog);
    toolsMenu->addAction(QStringLiteral("Dynamic 查询"), this, &MainWindow::queryDynamicStatus);
    toolsMenu->addAction(QStringLiteral("重启设备"), this, &MainWindow::restartDevice);
    toolsMenu->addSeparator();
    toolsMenu->addAction(QStringLiteral("开发"), this, &MainWindow::showDeveloperDialog);
    toolsMenu->addAction(QStringLiteral("调试"), this, &MainWindow::showDebugDialog);
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    helpMenu->addAction(QStringLiteral("关于 PA Host"), this, &MainWindow::showAbout);
}

void MainWindow::restartDevice() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("重启设备"), QStringLiteral("请先连接 RS422 设备。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("重启设备"),
                              QStringLiteral("确定要重启下位机设备吗？当前任务和串口连接将中断。"))
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!deviceController_->restartDevice(&error)) {
        QMessageBox::warning(this, QStringLiteral("重启设备"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已发送重启指令，设备正在重启。"), 5000);
}

void MainWindow::showOffsetTemplateDialog() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("制作暗场模板"),
                             QStringLiteral("请先连接设备。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("制作暗场模板"));
    auto* layout = new QFormLayout(&dialog);
    auto* totalFrames = new QSpinBox(&dialog);
    totalFrames->setRange(1, 20);
    totalFrames->setValue(4);
    auto* validFrames = new QSpinBox(&dialog);
    validFrames->setRange(1, 20);
    validFrames->setValue(2);
    layout->addRow(QStringLiteral("总采集张数"), totalFrames);
    layout->addRow(QStringLiteral("有效张数（取最后几张均值）"), validFrames);
    auto* hint = new QLabel(QStringLiteral("每张静态图采集完成后再触发下一张，使用最后的有效帧逐像素求均值，结果保存为 offset 模板。"), &dialog);
    hint->setWordWrap(true);
    layout->addRow(hint);
    auto* status = new QLabel(QStringLiteral("状态: 等待开始"), &dialog);
    status->setWordWrap(true);
    layout->addRow(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* start = buttons->addButton(QStringLiteral("开始制作"), QDialogButtonBox::ActionRole);
    auto* cancel = buttons->addButton(QStringLiteral("取消任务"), QDialogButtonBox::ActionRole);
    cancel->setEnabled(false);
    layout->addRow(buttons);
    auto phase = std::make_shared<int>(0); // 1=BEGIN, 2=CAPTURE, 3=轮询, 4=BUILD
    auto* poll = new QTimer(&dialog);
    poll->setInterval(500);
    connect(start, &QPushButton::clicked, &dialog, [this, &dialog, totalFrames, validFrames, status, start, cancel, phase]() {
        if (validFrames->value() > totalFrames->value()) {
            QMessageBox::warning(&dialog, QStringLiteral("参数错误"), QStringLiteral("有效张数不能大于总采集张数。"));
            return;
        }
        QString error;
        *phase = 1;
        if (!deviceController_->beginOffsetCalibration(totalFrames->value(), validFrames->value(),
                                                       1u, &error)) {
            QMessageBox::warning(&dialog, QStringLiteral("发送失败"), error);
            *phase = 0;
            return;
        }
        start->setEnabled(false);
        cancel->setEnabled(true);
        status->setText(QStringLiteral("状态: 正在初始化暗场模板任务"));
    });
    connect(cancel, &QPushButton::clicked, &dialog, [this, status, poll]() {
        poll->stop();
        QString error;
        if (!deviceController_->cancelOffsetCalibration(&error)) {
            status->setText(QStringLiteral("状态: 取消命令发送失败：%1").arg(error));
        }
    });
    connect(poll, &QTimer::timeout, &dialog, [this]() {
        if (!deviceController_->hasPendingCommand()) deviceController_->queryCalibration();
    });
    connect(deviceController_, &PaDeviceController::binaryCommandFinished, &dialog,
        [this, &dialog, status, start, cancel, poll, phase](quint16 command,
                                                           const QMap<quint16, quint32>& values,
                                                           bool success,
                                                           const QString& detail) {
            if (command < 0x0300 || command > 0x0308) return;
            if (!success) {
                poll->stop();
                status->setText(QStringLiteral("状态: 失败：%1").arg(detail));
                start->setEnabled(true);
                cancel->setEnabled(false);
                *phase = 0;
                return;
            }
            if (command == 0x0300 && *phase == 1) {
                *phase = 2;
                status->setText(QStringLiteral("状态: 参数已确认，开始采集"));
                QString error;
                if (!deviceController_->captureOffsetCalibration(&error)) {
                    status->setText(QStringLiteral("状态: 启动采集失败：%1").arg(error));
                }
                return;
            }
            if (command == 0x0301 && *phase == 2) {
                *phase = 3;
                poll->start();
                status->setText(QStringLiteral("状态: 暗场采集与均值计算中"));
                return;
            }
            if (command == 0x0308 && *phase == 3) {
                const quint32 state = values.value(0x3202, 0);
                status->setText(QStringLiteral("状态: %1，进度 %2/%3")
                    .arg(state == 1 ? QStringLiteral("运行中") : state == 2 ? QStringLiteral("停止中")
                         : state == 3 ? QStringLiteral("采集完成") : state == 4 ? QStringLiteral("失败")
                         : state == 5 ? QStringLiteral("已取消") : QStringLiteral("空闲"))
                    .arg(values.value(0x3204)).arg(values.value(0x3205)));
                if (state == 3) {
                    poll->stop();
                    *phase = 4;
                    deviceController_->buildOffsetCalibration();
                } else if (state == 4 || state == 5) {
                    poll->stop();
                    start->setEnabled(true);
                    cancel->setEnabled(false);
                    *phase = 0;
                }
                return;
            }
            if (command == 0x0302 && *phase == 4) {
                status->setText(QStringLiteral("状态: 暗场模板已生成并加载"));
                cancel->setEnabled(false);
                start->setEnabled(true);
                *phase = 0;
            } else if (command == 0x0303) {
                poll->stop();
                status->setText(QStringLiteral("状态: 已请求取消"));
                cancel->setEnabled(false);
                start->setEnabled(true);
                *phase = 0;
            }
        });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::showGainTemplateDialog() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("制作亮场模板"),
                             QStringLiteral("请先连接设备。"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("制作亮场模板"));
    auto* layout = new QFormLayout(&dialog);
    auto* levels = new QLineEdit(QStringLiteral("5000,10000,20000"), &dialog);
    auto* frames = new QSpinBox(&dialog);
    frames->setRange(1, 1000);
    frames->setValue(4);
    auto* threshold = new QDoubleSpinBox(&dialog);
    threshold->setRange(0.0, 1.0);
    threshold->setDecimals(3);
    threshold->setSingleStep(0.01);
    threshold->setValue(0.30);
    layout->addRow(QStringLiteral("灰度级别（逗号分隔）"), levels);
    layout->addRow(QStringLiteral("每级采集张数"), frames);
    layout->addRow(QStringLiteral("坏点判定阈值"), threshold);
    auto* hint = new QLabel(QStringLiteral("每个灰度级先求均值，再生成 gain；坏点通过 gain=0 标记。"), &dialog);
    hint->setWordWrap(true);
    layout->addRow(hint);
    auto* levelSelector = new QComboBox(&dialog);
    levelSelector->setEnabled(false);
    layout->addRow(QStringLiteral("当前采集灰度级"), levelSelector);
    auto* status = new QLabel(QStringLiteral("状态: 等待初始化"), &dialog);
    status->setWordWrap(true);
    layout->addRow(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* begin = buttons->addButton(QStringLiteral("初始化"), QDialogButtonBox::ActionRole);
    auto* capture = buttons->addButton(QStringLiteral("采集当前灰度级"), QDialogButtonBox::ActionRole);
    auto* build = buttons->addButton(QStringLiteral("生成模板"), QDialogButtonBox::ActionRole);
    auto* cancel = buttons->addButton(QStringLiteral("取消任务"), QDialogButtonBox::ActionRole);
    capture->setEnabled(false);
    build->setEnabled(false);
    cancel->setEnabled(false);
    layout->addRow(buttons);
    auto phase = std::make_shared<int>(0); // 1=BEGIN, 2=CAPTURE, 3=BUILD
    auto completedLevels = std::make_shared<QList<quint32>>();
    auto activeLevel = std::make_shared<quint32>(0);
    auto configuredLevels = std::make_shared<QList<quint32>>();
    auto* poll = new QTimer(&dialog);
    poll->setInterval(500);
    connect(begin, &QPushButton::clicked, &dialog,
        [this, &dialog, levels, frames, threshold, levelSelector, status, begin, cancel,
         phase, completedLevels, configuredLevels]() {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            const QStringList tokens = levels->text().split(',', Qt::SkipEmptyParts);
#else
            const QStringList tokens = levels->text().split(',', QString::SkipEmptyParts);
#endif
            QList<quint32> parsed;
            for (const QString& token : tokens) {
                bool ok = false;
                const quint32 value = token.trimmed().toUInt(&ok, 0);
                if (!ok || value == 0 || parsed.contains(value)) {
                    QMessageBox::warning(&dialog, QStringLiteral("参数错误"), QStringLiteral("灰度级列表包含无效或重复数值。"));
                    return;
                }
                parsed.append(value);
            }
            if (parsed.size() < 2 || parsed.size() > 16) {
                QMessageBox::warning(&dialog, QStringLiteral("参数错误"), QStringLiteral("灰度级数量必须为 2~16。"));
                return;
            }
            QString error;
            *phase = 1;
            if (!deviceController_->beginGainCalibration(parsed, frames->value(),
                                                         static_cast<float>(threshold->value()), &error)) {
                QMessageBox::warning(&dialog, QStringLiteral("发送失败"), error);
                *phase = 0;
                return;
            }
            *configuredLevels = parsed;
            completedLevels->clear();
            levelSelector->clear();
            for (quint32 value : parsed) levelSelector->addItem(QString::number(value), value);
            begin->setEnabled(false);
            cancel->setEnabled(true);
            status->setText(QStringLiteral("状态: 正在初始化亮场校准"));
        });
    connect(capture, &QPushButton::clicked, &dialog,
        [this, levelSelector, status, capture, build, activeLevel, phase]() {
            *activeLevel = levelSelector->currentData().toUInt();
            QString error;
            *phase = 2;
            if (!deviceController_->captureGainCalibration(*activeLevel, &error)) {
                status->setText(QStringLiteral("状态: 启动采集失败：%1").arg(error));
                *phase = 0;
                return;
            }
            capture->setEnabled(false);
            build->setEnabled(false);
            status->setText(QStringLiteral("状态: 正在启动灰度级 %1 采集").arg(*activeLevel));
        });
    connect(build, &QPushButton::clicked, &dialog, [this, status, capture, build, phase]() {
        QString error;
        *phase = 3;
        if (!deviceController_->buildGainCalibration(&error)) {
            status->setText(QStringLiteral("状态: 启动生成失败：%1").arg(error));
            *phase = 0;
            return;
        }
        capture->setEnabled(false);
        build->setEnabled(false);
        status->setText(QStringLiteral("状态: 正在生成亮场模板"));
    });
    connect(cancel, &QPushButton::clicked, &dialog, [this, status, poll]() {
        poll->stop();
        QString error;
        if (!deviceController_->cancelGainCalibration(&error)) {
            status->setText(QStringLiteral("状态: 取消命令发送失败：%1").arg(error));
        }
    });
    connect(poll, &QTimer::timeout, &dialog, [this]() {
        if (!deviceController_->hasPendingCommand()) deviceController_->queryCalibration();
    });
    connect(deviceController_, &PaDeviceController::binaryCommandFinished, &dialog,
        [this, status, begin, capture, build, cancel, levelSelector, poll, phase,
         completedLevels, configuredLevels, activeLevel](quint16 command,
                                                         const QMap<quint16, quint32>& values,
                                                         bool success,
                                                         const QString& detail) {
            if (command < 0x0304 || command > 0x0308) return;
            if (!success) {
                poll->stop();
                status->setText(QStringLiteral("状态: 失败：%1").arg(detail));
                capture->setEnabled(*phase == 2);
                build->setEnabled(completedLevels->size() == configuredLevels->size());
                *phase = 0;
                return;
            }
            if (command == 0x0304 && *phase == 1) {
                *phase = 0;
                levelSelector->setEnabled(true);
                capture->setEnabled(true);
                status->setText(QStringLiteral("状态: 初始化完成，请设置光源后逐级采集"));
                return;
            }
            if ((command == 0x0305 && *phase == 2) || (command == 0x0306 && *phase == 3)) {
                poll->start();
                return;
            }
            if (command == 0x0308 && (*phase == 2 || *phase == 3)) {
                const quint32 taskState = values.value(0x3202, 0);
                status->setText(QStringLiteral("状态: %1，进度 %2/%3")
                    .arg(taskState == 1 ? QStringLiteral("运行中") : taskState == 2 ? QStringLiteral("停止中")
                         : taskState == 3 ? QStringLiteral("完成") : taskState == 4 ? QStringLiteral("失败")
                         : taskState == 5 ? QStringLiteral("已取消") : QStringLiteral("空闲"))
                    .arg(values.value(0x3204)).arg(values.value(0x3205)));
                if (taskState == 3) {
                    poll->stop();
                    if (*phase == 2 && !completedLevels->contains(*activeLevel)) completedLevels->append(*activeLevel);
                    if (*phase == 3) status->setText(QStringLiteral("状态: 亮场模板已生成并加载"));
                    *phase = 0;
                    capture->setEnabled(true);
                    build->setEnabled(completedLevels->size() == configuredLevels->size());
                } else if (taskState == 4 || taskState == 5) {
                    poll->stop();
                    *phase = 0;
                    capture->setEnabled(true);
                    build->setEnabled(completedLevels->size() == configuredLevels->size());
                }
                return;
            }
            if (command == 0x0307) {
                poll->stop();
                status->setText(QStringLiteral("状态: 已请求取消"));
                begin->setEnabled(true);
                capture->setEnabled(false);
                build->setEnabled(false);
                cancel->setEnabled(false);
                *phase = 0;
            }
        });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::viewOffsetTemplate() {
    startTemplateUpload(false);
}

void MainWindow::viewGainTemplate() {
    startTemplateUpload(true);
}

void MainWindow::startTemplateUpload(bool gainTemplate) {
    if (!pendingTemplateUploadName_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("查看模板"),
                                 QStringLiteral("上一张模板仍在等待上传，请稍候。"));
        return;
    }
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("查看模板"),
                             QStringLiteral("请先连接设备。"));
        return;
    }
    if (pcieSource_ == nullptr || !pcieSource_->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("查看模板"),
                             QStringLiteral("PCIe 图像监听未运行，无法接收模板图像。"));
        return;
    }
    pendingTemplateUploadName_ = gainTemplate
        ? QStringLiteral("当前亮场模板")
        : QStringLiteral("当前暗场模板");
    awaitingTemplateFrame_ = false;
    QString error;
    if (!deviceController_->configureTemplateUpload(gainTemplate, 7680, 3072, &error)) {
        pendingTemplateUploadName_.clear();
        QMessageBox::warning(this, QStringLiteral("查看模板"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("正在配置%1上传").arg(pendingTemplateUploadName_), 3000);
}

void MainWindow::showDynamicConfigDialog() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Dynamic 配置"),
                             QStringLiteral("请先连接设备。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Dynamic 配置"));
    dialog.resize(620, 720);
    auto* root = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    auto* form = new QFormLayout(content);
    auto fields = std::make_shared<QMap<quint16, QLineEdit*>>();
    auto addField = [form, fields, content](quint16 id, const QString& label, bool hex) {
        auto* edit = new QLineEdit(content);
        edit->setText(hex ? QStringLiteral("0x0") : QStringLiteral("0"));
        edit->setProperty("hex", hex);
        fields->insert(id, edit);
        form->addRow(label, edit);
    };
    addField(0x0600, QStringLiteral("循环次数（0=无限）"), false);
    addField(0x0601, QStringLiteral("图像起始地址"), true);
    addField(0x0602, QStringLiteral("图像结束地址"), true);
    for (int step = 0; step < 10; ++step) {
        addField(static_cast<quint16>(0x2100 + step * 0x10),
                 QStringLiteral("Step %1 High").arg(step), true);
        addField(static_cast<quint16>(0x2101 + step * 0x10),
                 QStringLiteral("Step %1 Low").arg(step), false);
    }
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
    auto* status = new QLabel(QStringLiteral("正在读取当前配置……"), &dialog);
    root->addWidget(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* refresh = buttons->addButton(QStringLiteral("重新读取"), QDialogButtonBox::ActionRole);
    auto* apply = buttons->addButton(QStringLiteral("下发并保存"), QDialogButtonBox::ActionRole);
    apply->setEnabled(false);
    root->addWidget(buttons);
    auto read = [this, status]() {
        QString error;
        if (!deviceController_->requestConfigGroup(5, &error)) status->setText(error);
    };
    connect(refresh, &QPushButton::clicked, &dialog, read);
    connect(apply, &QPushButton::clicked, &dialog, [this, fields, status]() {
        QMap<quint16, quint32> values;
        for (auto it = fields->cbegin(); it != fields->cend(); ++it) {
            bool ok = false;
            const quint32 value = it.value()->text().trimmed().toUInt(&ok, 0);
            if (!ok) {
                status->setText(QStringLiteral("参数格式错误: 0x%1").arg(it.key(), 4, 16, QLatin1Char('0')));
                return;
            }
            values.insert(it.key(), value);
        }
        QString error;
        if (!deviceController_->setConfigGroup(5, values, &error)) status->setText(error);
        else status->setText(QStringLiteral("正在下发并保存 Dynamic 配置……"));
    });
    connect(deviceController_, &PaDeviceController::configGroupReceived, &dialog,
        [fields, status, apply](quint16 group, const QMap<quint16, quint32>& values,
                                bool success, const QString& detail) {
            if (group != 5) return;
            if (!success) {
                status->setText(QStringLiteral("操作失败: %1").arg(detail));
                return;
            }
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                auto field = fields->find(it.key());
                if (field == fields->end()) continue;
                const bool hex = field.value()->property("hex").toBool();
                field.value()->setText(hex
                    ? QStringLiteral("0x%1").arg(it.value(), 0, 16)
                    : QString::number(it.value()));
            }
            apply->setEnabled(true);
            status->setText(QStringLiteral("Dynamic 配置已读取/保存。"));
        });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    read();
    dialog.exec();
}

void MainWindow::queryDynamicStatus() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Dynamic 查询"),
                             QStringLiteral("请先连接设备。"));
        return;
    }
    QString error;
    if (!deviceController_->queryDynamic(&error)) {
        QMessageBox::warning(this, QStringLiteral("Dynamic 查询"), error);
    }
}

void MainWindow::handleBinaryCommandFinished(quint16 command,
                                             const QMap<quint16, quint32>& values,
                                             bool success,
                                             const QString& detail) {
    logService_->log(success ? AppLogLevel::Info : AppLogLevel::Warning,
        QStringLiteral("RS422"),
        QStringLiteral("二进制命令 0x%1 %2: %3")
            .arg(command, 4, 16, QLatin1Char('0'))
            .arg(success ? QStringLiteral("完成") : QStringLiteral("失败"), detail));

    if (command == 0x0212) {
        if (!success) {
            QMessageBox::warning(this, QStringLiteral("Dynamic 查询"), detail);
            return;
        }
        const quint32 workState = values.value(0x2003);
        QString stateText;
        if ((workState & 1u) != 0u) stateText = QStringLiteral("运行中");
        else if (values.value(0x2008) != 0u) stateText = QStringLiteral("错误");
        else if (values.value(0x2007) != 0u) stateText = QStringLiteral("已完成");
        else stateText = QStringLiteral("已停止/空闲");
        QMessageBox::information(this, QStringLiteral("Dynamic 查询"),
            QStringLiteral("状态: %1\ndync_state: 0x%2\ndync_end: 0x%3\ndync_debug_out: 0x%4\nfinal_img_addr: 0x%5\n帧缓冲数量: %6")
                .arg(stateText)
                .arg(values.value(0x2003), 8, 16, QLatin1Char('0'))
                .arg(values.value(0x2007), 8, 16, QLatin1Char('0'))
                .arg(values.value(0x2008), 8, 16, QLatin1Char('0'))
                .arg(values.value(0x2004), 8, 16, QLatin1Char('0'))
                .arg(values.value(0x2005)));
        return;
    }

    if (command == 0x0500 && !pendingTemplateUploadName_.isEmpty()) {
        if (!success) {
            QMessageBox::warning(this, QStringLiteral("查看模板"), detail);
            pendingTemplateUploadName_.clear();
            return;
        }
        awaitingTemplateFrame_ = true;
        QString error;
        if (!deviceController_->startTemplateUpload(&error)) {
            awaitingTemplateFrame_ = false;
            pendingTemplateUploadName_.clear();
            QMessageBox::warning(this, QStringLiteral("查看模板"), error);
        } else {
            statusBar()->showMessage(QStringLiteral("正在上传%1……").arg(pendingTemplateUploadName_), 5000);
            const QString expectedName = pendingTemplateUploadName_;
            QTimer::singleShot(15000, this, [this, expectedName]() {
                if (awaitingTemplateFrame_ && pendingTemplateUploadName_ == expectedName) {
                    awaitingTemplateFrame_ = false;
                    pendingTemplateUploadName_.clear();
                    QMessageBox::warning(this, QStringLiteral("查看模板"),
                                         QStringLiteral("控制命令已发送，但 15 秒内未收到 PCIe 模板图像。"));
                }
            });
        }
        return;
    }
    if (command == 0x0501 && !success && !pendingTemplateUploadName_.isEmpty()) {
        awaitingTemplateFrame_ = false;
        QMessageBox::warning(this, QStringLiteral("查看模板"), detail);
        pendingTemplateUploadName_.clear();
    }
}

void MainWindow::showDeveloperDialog() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("开发参数"), QStringLiteral("请先连接 RS422 设备。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("开发参数"));
    dialog.resize(760, 720);
    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* fields = new QHash<quint16, QLineEdit*>();
    auto* hexModes = new QHash<quint16, QCheckBox*>();
    const auto addGroup = [form, fields, hexModes](const QString& title,
                                         quint16 group,
                                         const QList<QPair<quint16, QString>>& items) {
        auto* box = new QGroupBox(title);
        auto* groupForm = new QFormLayout(box);
        for (const auto& item : items) {
            auto* row = new QWidget(box);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto* edit = new QLineEdit(row);
            const bool defaultHex = (item.first >= 0x0500 && item.first <= 0x0515)
                || item.first == 0x0601 || item.first == 0x0602
                || (item.first >= 0x2100 && item.first <= 0x2190
                    && ((item.first - 0x2100) % 0x10) == 0);
            auto* hex = new QCheckBox(QStringLiteral("Hex"), row);
            hex->setChecked(defaultHex);
            edit->setText(defaultHex ? QStringLiteral("0x0") : QStringLiteral("0"));
            QObject::connect(hex, &QCheckBox::toggled, edit, [edit](bool checked) {
                bool ok = false;
                const quint32 value = edit->text().toUInt(&ok, 0);
                if (!ok) {
                    return;
                }
                edit->setText(checked
                    ? QStringLiteral("0x%1").arg(value, 0, 16)
                    : QString::number(value));
            });
            rowLayout->addWidget(edit, 1);
            rowLayout->addWidget(hex);
            (*fields)[item.first] = edit;
            (*hexModes)[item.first] = hex;
            groupForm->addRow(item.second, row);
        }
        form->addRow(box);
        Q_UNUSED(group);
    };
    addGroup(QStringLiteral("静态时序"), 1, {
        {0x1000, QStringLiteral("自清空间隔(ms)")}, {0x1001, QStringLiteral("曝光窗口(ms)")},
        {0x1002, QStringLiteral("暗场窗口(ms)")}});
    addGroup(QStringLiteral("校正"), 2, {
        {0x0300, QStringLiteral("Offset 使能")}, {0x0301, QStringLiteral("Gain 使能")},
        {0x0302, QStringLiteral("Defect 使能")}, {0x0303, QStringLiteral("Offset 加法值")},
        {0x0304, QStringLiteral("Gain 截止值")} });
    addGroup(QStringLiteral("GIC"), 3, {
        {0x0400, QStringLiteral("请求码")}, {0x0401, QStringLiteral("数据输出使能")},
        {0x0402, QStringLiteral("行时间(ns)")}, {0x0403, QStringLiteral("起始行")},
        {0x0404, QStringLiteral("结束行")} });
    addGroup(QStringLiteral("ROIC"), 4, {
        {0x0500, QStringLiteral("起始列")}, {0x0501, QStringLiteral("结束列")},
        {0x0502, QStringLiteral("Binning")},
        {0x0503, QStringLiteral("ROIC Reg 0x00")}, {0x0504, QStringLiteral("ROIC Reg 0x02")},
        {0x0505, QStringLiteral("ROIC Reg 0x05")}, {0x0506, QStringLiteral("ROIC Reg 0x06")},
        {0x0507, QStringLiteral("ROIC Reg 0x07")}, {0x0508, QStringLiteral("ROIC Reg 0x09")},
        {0x0509, QStringLiteral("ROIC Reg 0x0A")}, {0x050A, QStringLiteral("ROIC Reg 0x0B")},
        {0x050B, QStringLiteral("ROIC Reg 0x0C")}, {0x050C, QStringLiteral("ROIC Reg 0x0D")},
        {0x050D, QStringLiteral("ROIC Reg 0x0E")}, {0x050E, QStringLiteral("ROIC Reg 0x0F")},
        {0x050F, QStringLiteral("ROIC Reg 0x10")}, {0x0510, QStringLiteral("ROIC Reg 0x11")},
        {0x0511, QStringLiteral("ROIC Reg 0x17")}, {0x0512, QStringLiteral("ROIC Reg 0x24")},
        {0x0513, QStringLiteral("ROIC Reg 0x28")}, {0x0514, QStringLiteral("ROIC Reg 0x2D")},
        {0x0515, QStringLiteral("ROIC Reg 0x3B")} });
    addGroup(QStringLiteral("动态模式"), 5, {
        {0x0600, QStringLiteral("循环次数")}, {0x0601, QStringLiteral("图像起始地址")},
        {0x0602, QStringLiteral("图像结束地址")}, {0x0603, QStringLiteral("启动等待(ms)")},
        {0x0604, QStringLiteral("状态轮询间隔(ms)")}, {0x0605, QStringLiteral("停止等待(ms)")}});
    for (int i = 0; i < 10; ++i) {
        const quint16 base = static_cast<quint16>(0x2100 + i * 0x10);
        addGroup(QStringLiteral("动态步骤 %1").arg(i), 5, {
            {base, QStringLiteral("Step %1 High").arg(i)},
            {static_cast<quint16>(base + 1), QStringLiteral("Step %1 Low").arg(i)}});
    }
    auto* scroll = new QScrollArea(&dialog);
    auto* panel = new QWidget(scroll);
    panel->setLayout(form);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    root->addWidget(scroll, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* readButton = buttons->addButton(QStringLiteral("重新读取"), QDialogButtonBox::ActionRole);
    auto* writeButton = buttons->addButton(QStringLiteral("下发全部"), QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    const QList<quint16> groups = {1, 2, 3, 4, 5};
    const QMap<quint16, QList<quint16>> groupItems = {
        {1, {0x1000, 0x1001, 0x1002}},
        {2, {0x0300, 0x0301, 0x0302, 0x0303, 0x0304}},
        {3, {0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405}},
        {4, {0x0500, 0x0501, 0x0502, 0x0503, 0x0504, 0x0505, 0x0506, 0x0507,
             0x0508, 0x0509, 0x050A, 0x050B, 0x050C, 0x050D, 0x050E, 0x050F,
             0x0510, 0x0511, 0x0512, 0x0513, 0x0514, 0x0515}},
        {5, {0x0600, 0x0601, 0x0602, 0x0603, 0x0604, 0x0605,
             0x2100, 0x2101, 0x2110, 0x2111, 0x2120, 0x2121,
             0x2130, 0x2131, 0x2140, 0x2141, 0x2150, 0x2151,
             0x2160, 0x2161, 0x2170, 0x2171, 0x2180, 0x2181,
             0x2190, 0x2191}}
    };
    auto* index = new std::shared_ptr<int>(std::make_shared<int>(0));
    auto* writing = new std::shared_ptr<bool>(std::make_shared<bool>(false));
    const auto startRead = [this, &dialog, fields, groups, index, writing]() {
        *(*index) = 0;
        *(*writing) = false;
        if (!groups.isEmpty()) {
            deviceController_->requestConfigGroup(groups.at(0));
        }
        Q_UNUSED(dialog);
    };
    connect(deviceController_, &PaDeviceController::configGroupReceived, &dialog,
            [this, fields, hexModes, groups, groupItems, index, writing](quint16 group,
                                                               const QMap<quint16, quint32>& values,
                                                               bool success,
                                                               const QString& detail) {
        if (!success) {
            emit deviceController_->errorOccurred(detail);
            return;
        }
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (fields->contains(it.key())) {
                const bool asHex = hexModes->contains(it.key()) && hexModes->value(it.key())->isChecked();
                fields->value(it.key())->setText(asHex
                    ? QStringLiteral("0x%1").arg(it.value(), 0, 16)
                    : QString::number(it.value()));
            }
        }
        const int current = groups.indexOf(group);
        if (current < 0) return;
        if (!*(*writing)) {
            const int next = current + 1;
            if (next < groups.size()) deviceController_->requestConfigGroup(groups.at(next));
        } else {
            const int next = current + 1;
            if (next < groups.size()) {
                QMap<quint16, quint32> sendValues;
                for (const quint16 itemId : groupItems.value(groups.at(next))) {
                    auto it = fields->constFind(itemId);
                    if (it == fields->cend()) continue;
                    bool ok = false;
                    const quint32 value = it.value()->text().toUInt(&ok, 0);
                    if (ok) sendValues.insert(itemId, value);
                }
                deviceController_->setConfigGroup(groups.at(next), sendValues);
            } else {
                *(*writing) = false;
                QMessageBox::information(nullptr, QStringLiteral("开发参数"), QStringLiteral("全部参数已下发并保存。"));
            }
        }
    });
    connect(readButton, &QPushButton::clicked, &dialog, startRead);
    connect(writeButton, &QPushButton::clicked, &dialog, [this, fields, groups, groupItems, writing]() {
        *(*writing) = true;
        QMap<quint16, quint32> values;
        for (const quint16 itemId : groupItems.value(groups.first())) {
            auto it = fields->constFind(itemId);
            if (it == fields->cend()) continue;
            bool ok = false;
            const quint32 value = it.value()->text().toUInt(&ok, 0);
            if (ok) values.insert(itemId, value);
        }
        if (!groups.isEmpty()) deviceController_->setConfigGroup(groups.first(), values);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    startRead();
    dialog.exec();
    delete fields;
    delete hexModes;
    delete index;
    delete writing;
}

void MainWindow::showDebugDialog() {
    if (deviceController_ == nullptr || !deviceController_->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("调试"), QStringLiteral("请先连接 RS422 设备。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("寄存器调试"));
    dialog.resize(780, 720);
    auto* root = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QStringLiteral("INT_VECTOR 不在列表中，因为读取它会清除中断；其他寄存器允许直接修改。"), &dialog);
    hint->setWordWrap(true);
    root->addWidget(hint);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("寄存器"), QStringLiteral("偏移"), QStringLiteral("值")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table, 1);

    auto* status = new QLabel(&dialog);
    root->addWidget(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* refresh = buttons->addButton(QStringLiteral("重新读取全部"), QDialogButtonBox::ActionRole);
    auto* write = buttons->addButton(QStringLiteral("写入选中寄存器"), QDialogButtonBox::ActionRole);
    root->addWidget(buttons);

    const QMap<quint16, QString> registerNames = {
        {0x0008, QStringLiteral("pa_version")},
        {0x0010, QStringLiteral("pa_build_information")},
        {0x0018, QStringLiteral("adapted_main_board_version")},
        {0x0020, QStringLiteral("adapted_gic_board_version")},
        {0x0028, QStringLiteral("adapted_roic_board_version")},
        {0x0030, QStringLiteral("adapted_reserved_board_0_version")},
        {0x0038, QStringLiteral("adapted_reserved_board_1_version")},
        {0x0040, QStringLiteral("adapted_reserved_board_2_version")},
        {0x0048, QStringLiteral("pa_pu_com_version")},
        {0x0050, QStringLiteral("rst_init_state")},
        {0x0200, QStringLiteral("gic_str")}, {0x0208, QStringLiteral("gic_stop")},
        {0x0210, QStringLiteral("gic_req_code")}, {0x0218, QStringLiteral("gic_dout_en")},
        {0x0220, QStringLiteral("gic_line_time")}, {0x0228, QStringLiteral("gic_oe_raising_edge")},
        {0x0230, QStringLiteral("gic_oe_falling_edge")}, {0x0238, QStringLiteral("gic_str_row_num")},
        {0x0240, QStringLiteral("gic_end_row_num")}, {0x0248, QStringLiteral("gic_binning_mode")},
        {0x03a0, QStringLiteral("gic_state")}, {0x03a8, QStringLiteral("gic_end")},
        {0x03c0, QStringLiteral("gic_dfx")}, {0x03c8, QStringLiteral("gic_debug_in")},
        {0x03d0, QStringLiteral("gic_debug_out")},
        {0x0400, QStringLiteral("roic_str")}, {0x0408, QStringLiteral("roic_req_code")},
        {0x0410, QStringLiteral("roic_reg_00")}, {0x0418, QStringLiteral("roic_reg_02")},
        {0x0420, QStringLiteral("roic_reg_05")}, {0x0428, QStringLiteral("roic_reg_06")},
        {0x0430, QStringLiteral("roic_reg_07")}, {0x0438, QStringLiteral("roic_reg_09")},
        {0x0440, QStringLiteral("roic_reg_0a")}, {0x0448, QStringLiteral("roic_reg_0b")},
        {0x0450, QStringLiteral("roic_reg_0c")}, {0x0458, QStringLiteral("roic_reg_0d")},
        {0x0460, QStringLiteral("roic_reg_0e")}, {0x0468, QStringLiteral("roic_reg_0f")},
        {0x0470, QStringLiteral("roic_reg_10")}, {0x0478, QStringLiteral("roic_reg_11")},
        {0x0480, QStringLiteral("roic_reg_17")}, {0x0488, QStringLiteral("roic_reg_24")},
        {0x0490, QStringLiteral("roic_reg_28")}, {0x0498, QStringLiteral("roic_reg_2d")},
        {0x04a0, QStringLiteral("roic_reg_3b")}, {0x04a8, QStringLiteral("roic_str_col_num")},
        {0x04b0, QStringLiteral("roic_end_col_num")}, {0x04b8, QStringLiteral("roic_binning_mode")},
        {0x05a0, QStringLiteral("roic_state")}, {0x05a8, QStringLiteral("roic_end")},
        {0x05c0, QStringLiteral("roic_dfx")}, {0x05c8, QStringLiteral("roic_debug_in")},
        {0x05d0, QStringLiteral("roic_debug_out")},
        {0x0600, QStringLiteral("img_wr_str")}, {0x0608, QStringLiteral("img_wr_str_addr")},
        {0x07a0, QStringLiteral("img_wr_state")}, {0x07a8, QStringLiteral("img_wr_end")},
        {0x07b0, QStringLiteral("img_wr_final_img_addr")}, {0x07c0, QStringLiteral("img_wr_dfx")},
        {0x07c8, QStringLiteral("img_wr_debug_in")}, {0x07d0, QStringLiteral("img_wr_debug_out")},
        {0x0800, QStringLiteral("img_corr_str")}, {0x0808, QStringLiteral("img_pkg_num")},
        {0x0810, QStringLiteral("img_row_num")}, {0x0818, QStringLiteral("img_col_num")},
        {0x0820, QStringLiteral("img_corr_offset_en")}, {0x0828, QStringLiteral("img_corr_offset_temp_str_addr")},
        {0x0830, QStringLiteral("img_corr_offset_adder_value")}, {0x0838, QStringLiteral("img_corr_gain_en")},
        {0x0840, QStringLiteral("img_corr_gain_temp_str_addr")}, {0x0848, QStringLiteral("img_corr_gain_clipping_value")},
        {0x0850, QStringLiteral("img_corr_defect_en")}, {0x0858, QStringLiteral("img_offset_corr_mode")},
        {0x09a0, QStringLiteral("img_corr_state")}, {0x09a8, QStringLiteral("img_corr_end")},
        {0x09c0, QStringLiteral("img_corr_dfx")}, {0x09c8, QStringLiteral("img_corr_debug_in")},
        {0x09d0, QStringLiteral("img_corr_debug_out")},
        {0x0a00, QStringLiteral("dync_str")}, {0x0a08, QStringLiteral("dync_stop")},
        {0x0a10, QStringLiteral("dync_cycle_num")}, {0x0a18, QStringLiteral("dync_img_str_addr")},
        {0x0a20, QStringLiteral("dync_img_end_addr")},
        {0x0a28, QStringLiteral("dync_step_0_cfg_h")}, {0x0a30, QStringLiteral("dync_step_0_cfg_l")},
        {0x0a38, QStringLiteral("dync_step_1_cfg_h")}, {0x0a40, QStringLiteral("dync_step_1_cfg_l")},
        {0x0a48, QStringLiteral("dync_step_2_cfg_h")}, {0x0a50, QStringLiteral("dync_step_2_cfg_l")},
        {0x0a58, QStringLiteral("dync_step_3_cfg_h")}, {0x0a60, QStringLiteral("dync_step_3_cfg_l")},
        {0x0a68, QStringLiteral("dync_step_4_cfg_h")}, {0x0a70, QStringLiteral("dync_step_4_cfg_l")},
        {0x0a78, QStringLiteral("dync_step_5_cfg_h")}, {0x0a80, QStringLiteral("dync_step_5_cfg_l")},
        {0x0a88, QStringLiteral("dync_step_6_cfg_h")}, {0x0a90, QStringLiteral("dync_step_6_cfg_l")},
        {0x0a98, QStringLiteral("dync_step_7_cfg_h")}, {0x0aa0, QStringLiteral("dync_step_7_cfg_l")},
        {0x0aa8, QStringLiteral("dync_step_8_cfg_h")}, {0x0ab0, QStringLiteral("dync_step_8_cfg_l")},
        {0x0ab8, QStringLiteral("dync_step_9_cfg_h")}, {0x0ac0, QStringLiteral("dync_step_9_cfg_l")},
        {0x0ba0, QStringLiteral("dync_state")}, {0x0ba8, QStringLiteral("dync_end")},
        {0x0bc0, QStringLiteral("dync_debug_in")}, {0x0bc8, QStringLiteral("dync_debug_out")},
        {0x0c00, QStringLiteral("img_upload_str")}, {0x0c08, QStringLiteral("img_upload_str_addr")},
        {0x0c10, QStringLiteral("img_upload_pkg_num")}, {0x0c18, QStringLiteral("img_upload_row_num")},
        {0x0c20, QStringLiteral("img_upload_col_num")}, {0x0da0, QStringLiteral("img_upload_state")},
        {0x0da8, QStringLiteral("img_upload_end")}, {0x0dc0, QStringLiteral("img_upload_dfx")}
    };
    const auto fillTable = [table, registerNames](const QMap<quint16, quint32>& values) {
        table->setRowCount(0);
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto* name = new QTableWidgetItem(registerNames.value(it.key(),
                QStringLiteral("reg_0x%1").arg(it.key(), 4, 16, QLatin1Char('0'))));
            name->setData(Qt::UserRole, it.key());
            table->setItem(row, 0, name);
            table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0x%1").arg(it.key(), 4, 16, QLatin1Char('0'))));
            table->setCellWidget(row, 2, new QLineEdit(QStringLiteral("0x%1").arg(it.value(), 8, 16, QLatin1Char('0'))));
        }
    };
    connect(deviceController_, &PaDeviceController::registerDumpReceived, &dialog,
            [fillTable, status](const QMap<quint16, quint32>& values, bool success, const QString& detail) {
        if (!success) {
            status->setText(detail);
            return;
        }
        fillTable(values);
        status->setText(QStringLiteral("已读取 %1 个寄存器").arg(values.size()));
    });
    connect(deviceController_, &PaDeviceController::registerWriteFinished, &dialog,
            [status](quint16 offset, quint32 value, bool success, const QString& detail) {
        status->setText(success
            ? QStringLiteral("写入完成 offset=0x%1 value=0x%2").arg(offset, 4, 16, QLatin1Char('0')).arg(value, 8, 16, QLatin1Char('0'))
            : detail);
    });
    connect(refresh, &QPushButton::clicked, &dialog, [this, status]() {
        status->setText(QStringLiteral("正在读取寄存器..."));
        deviceController_->dumpRegisters();
    });
    connect(write, &QPushButton::clicked, &dialog, [this, table, status]() {
        const int row = table->currentRow();
        auto* valueEdit = row >= 0 ? qobject_cast<QLineEdit*>(table->cellWidget(row, 2)) : nullptr;
        if (valueEdit == nullptr || table->item(row, 0) == nullptr) {
            status->setText(QStringLiteral("请先选择一个寄存器"));
            return;
        }
        bool ok = false;
        const quint32 value = valueEdit->text().toUInt(&ok, 0);
        if (!ok) {
            status->setText(QStringLiteral("寄存器值格式错误，请使用十进制或 0x 十六进制"));
            return;
        }
        const quint16 offset = table->item(row, 0)->data(Qt::UserRole).toUInt();
        status->setText(QStringLiteral("正在写入..."));
        deviceController_->writeRegister(offset, value);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    deviceController_->dumpRegisters();
    dialog.exec();
}

void MainWindow::showAbout() {
    const bool connected = deviceController_ != nullptr && deviceController_->isConnected();
    const QString unavailableVersion = connected
        ? QStringLiteral("未获取（设备未返回）")
        : QStringLiteral("未获取（设备未连接）");

    QMessageBox dialog(this);
    dialog.setWindowTitle(QStringLiteral("关于 PA Host"));
    dialog.setIcon(QMessageBox::Information);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QStringLiteral("PA Host"));
    dialog.setInformativeText(
        QStringLiteral("软件版本: %1\n编译时间: %2\nARM 程序版本: %3\nFPGA 版本: %4")
            .arg(QCoreApplication::applicationVersion(),
                QStringLiteral(PA_HOST_BUILD_TIME),
                armProgramVersion_.isEmpty() ? unavailableVersion : armProgramVersion_,
                fpgaVersion_.isEmpty() ? unavailableVersion : fpgaVersion_));
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
}

void MainWindow::createStatusBar() {
    modelLabel_ = new QLabel(QStringLiteral("型号: PA 专用"));
    serialLabel_ = new QLabel(QStringLiteral("序列号: --"));
    connectionLabel_ = new QLabel(QStringLiteral("RS422: 未连接"));
    modeLabel_ = new QLabel(QStringLiteral("工作模式: Idle"));
    imageLabel_ = new QLabel(QStringLiteral("图像尺寸: --"));
    progressLabel_ = new QLabel(QStringLiteral("缩放: --"));
    fpsLabel_ = new QLabel(QStringLiteral("显示 FPS: --"));

    const auto configureStatusLabel = [](QLabel* label) {
        label->setObjectName(QStringLiteral("statusLabel"));
        label->setContentsMargins(6, 2, 6, 2);
    };
    configureStatusLabel(modelLabel_);
    configureStatusLabel(serialLabel_);
    configureStatusLabel(connectionLabel_);
    configureStatusLabel(modeLabel_);
    configureStatusLabel(imageLabel_);
    configureStatusLabel(progressLabel_);
    configureStatusLabel(fpsLabel_);

    statusBar()->addWidget(modelLabel_);
    statusBar()->addWidget(serialLabel_);
    statusBar()->addWidget(connectionLabel_);
    statusBar()->addWidget(modeLabel_);
    statusBar()->addWidget(imageLabel_, 1);
    statusBar()->addWidget(progressLabel_);
    statusBar()->addWidget(fpsLabel_);
}

void MainWindow::handleImageListSelection(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    QElapsedTimer totalTimer;
    QElapsedTimer stageTimer;
    totalTimer.start();
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("切换图像开始: %1").arg(path));

    const bool keepPcieListening = pcieSource_ != nullptr && pcieSource_->isRunning();
    if (!keepPcieListening) {
        stopImageAcquisition();
    }
    const QString displayedPath = imageSession_->hasImage()
        ? imageSession_->image().path()
        : QString();
    QString error;
    stageTimer.start();
    const QString cacheKey = normalizedImagePath(path);
    bool frameCacheHit = false;
    if (const ImageFrame* cachedFrame = imageFrameCache_.object(cacheKey)) {
        frameCacheHit = true;
        if (!imageSession_->setFrame(*cachedFrame, &error)) {
            imageFrameCache_.remove(cacheKey);
            frameCacheHit = false;
        }
    }
    if (!error.isEmpty() || !imageSession_->hasImage()
        || normalizedImagePath(imageSession_->image().path()) != cacheKey) {
        error.clear();
        if (!imageSession_->loadFilePreview(path, &error)) {
            if (!displayedPath.isEmpty()) {
                imageListPanel_->setCurrentPath(displayedPath);
            }
            QMessageBox::warning(this, QStringLiteral("打开失败"), error);
            logService_->error(QStringLiteral("IMAGE"),
                QStringLiteral("切换图像失败: %1, %2").arg(path, error));
            return;
        }
        imageFrameCache_.insert(cacheKey, new ImageFrame(imageSession_->currentFrame()));
    }
    if (!error.isEmpty()) {
        if (!displayedPath.isEmpty()) {
            imageListPanel_->setCurrentPath(displayedPath);
        }
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
        logService_->error(QStringLiteral("IMAGE"),
            QStringLiteral("切换图像失败: %1, %2").arg(path, error));
        return;
    }
    const qint64 loadMs = stageTimer.elapsed();

    stageTimer.restart();
    imageListPanel_->ensureThumbnail(path, imageSession_->image());
    const qint64 thumbnailMs = stageTimer.elapsed();

    stageTimer.restart();
    showCurrentSessionImage(path, true);
    const qint64 displayMs = stageTimer.elapsed();
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("切换图像完成: cache=%1 load=%2ms thumbnail=%3ms display=%4ms total=%5ms path=%6")
            .arg(frameCacheHit ? 1 : 0)
            .arg(loadMs)
            .arg(thumbnailMs)
            .arg(displayMs)
            .arg(totalTimer.elapsed())
            .arg(path));
}

void MainWindow::handleImagesRemoved(int count, const QString& nextPath) {
    /*
     * 删除列表项只表示“从历史列表移除”，不能影响正在运行的 PCIe 中断监听。
     * PCIe 监听是常驻图像入口，不能被列表、显示或 FPGA 控制命令间接停止。
     */
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("从图像列表移除 %1 项（源文件未删除）").arg(count));
    if (nextPath.isEmpty()) {
        clearCurrentImage();
    } else {
        handleImageListSelection(nextPath);
    }
}

void MainWindow::exportImage(const QString& sourcePath, const QString& formatId) {
    if (sourcePath.isEmpty()) {
        return;
    }

    TiRawImage exportImage;
    const TiRawImage* imageToExport = nullptr;
    if (imageSession_->hasImage()
        && normalizedImagePath(imageSession_->image().path()) == normalizedImagePath(sourcePath)) {
        imageToExport = &imageSession_->image();
    } else {
        QString loadError;
        if (!exportImage.load(sourcePath, &loadError)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), loadError);
            return;
        }
        imageToExport = &exportImage;
    }

    QString exportDirectory = settings_->lastExportDirectory();
    if (exportDirectory.isEmpty() || !QDir(exportDirectory).exists()) {
        exportDirectory = QFileInfo(sourcePath).absolutePath();
    }

    ImageExportFormat format;
    if (!ImageExportService::findFormat(formatId, &format)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("不支持的导出格式"));
        return;
    }
    const QString defaultName = QFileInfo(sourcePath).completeBaseName()
        + QStringLiteral("_export.") + format.suffix;
    QString outputPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出当前图像"),
        QDir(exportDirectory).filePath(defaultName),
        format.fileFilter);
    if (outputPath.isEmpty()) {
        return;
    }
    outputPath = ImageExportService::ensureFileSuffix(outputPath, formatId);

    QString error;
    if (!ImageExportService::exportImage(
            *imageToExport,
            centerSpin_->value(),
            widthSpin_->value(),
            formatId,
            outputPath,
            &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    settings_->setLastExportDirectory(QFileInfo(outputPath).absolutePath());
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("导出图像: %1 -> %2").arg(sourcePath, outputPath));
}

void MainWindow::showCurrentSessionImage(const QString& source, bool resetViewState) {
    if (!imageSession_->hasImage()) {
        return;
    }

    QElapsedTimer totalTimer;
    QElapsedTimer stageTimer;
    totalTimer.start();
    stageTimer.start();
    const TiRawImage& image = imageSession_->image();
    /*
     * 这里不能清显示缓存。历史图像切换时 contentCacheKey 是文件路径，
     * 保留 QImage/QPixmap/ROI 统计缓存可以把重复切换的耗时压到很低。
     * 窗宽窗位变化、清空图像等真正会让显示内容失效的路径会单独清缓存。
     */
    imageView_->setPixmapCacheEnabled(!imageSession_->currentFrame().contentCacheKey.isEmpty());
    const qint64 cacheSetupMs = stageTimer.elapsed();

    stageTimer.restart();
    imageLabel_->setText(QStringLiteral("图像尺寸: %1 x %2")
                             .arg(image.width())
                             .arg(image.height()));
    fpsLabel_->setText(QStringLiteral("显示 FPS: --"));
    pixelInfoLabel_->setText(QStringLiteral("像素值: --"));
    updateImageUiState();
    activeRoi_ = {};
    const qint64 controlsMs = stageTimer.elapsed();

    if (roiInfoLabel_ != nullptr) {
        roiInfoLabel_->setText(QStringLiteral("ROI 信息: 统计中..."));
    }
    imageInfoRefreshTimer_.stop();
    const qint64 statsMs = 0;

    stageTimer.restart();
    const WindowLevelResult autoWindow = imageSession_->autoWindowLevel();
    const qint64 autoWindowMs = stageTimer.elapsed();
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("打开图像: %1, %2x%3, min=%4 max=%5, auto center=%6 width=%7")
            .arg(source)
            .arg(image.width())
            .arg(image.height())
            .arg(image.minValue())
            .arg(image.maxValue())
            .arg(autoWindow.center)
            .arg(autoWindow.width));
    stageTimer.restart();
    refreshImage(resetViewState);
    const qint64 refreshMs = stageTimer.elapsed();
    imageInfoRefreshTimer_.start(1);
    logService_->info(QStringLiteral("IMAGE"),
        QStringLiteral("图像显示刷新耗时: cache_setup=%1ms controls=%2ms stats=%3ms auto=%4ms refresh=%5ms total=%6ms reset=%7 source=%8")
            .arg(cacheSetupMs)
            .arg(controlsMs)
            .arg(statsMs)
            .arg(autoWindowMs)
            .arg(refreshMs)
            .arg(totalTimer.elapsed())
            .arg(resetViewState ? 1 : 0)
            .arg(source));
}

void MainWindow::clearCurrentImage() {
    imageRefreshTimer_.stop();
    imageInfoRefreshTimer_.stop();
    imageSession_->clear();
    imageView_->setImage(QImage());
    imageLabel_->setText(QStringLiteral("图像尺寸: --"));
    progressLabel_->setText(QStringLiteral("缩放: --"));
    fpsLabel_->setText(QStringLiteral("显示 FPS: --"));
    pixelInfoLabel_->setText(QStringLiteral("像素值: --"));
    roiInfoLabel_->setText(QStringLiteral("ROI 信息: --"));
    activeRoi_ = {};
    clearFrameDisplayCaches();
    stableFrameStatsCache_.clear();
    updateImageUiState();
}

void MainWindow::clearFrameDisplayCaches() {
    frameDisplayCache_.clear();
    if (imageView_ != nullptr) {
        imageView_->clearPixmapCache();
    }
}

void MainWindow::scheduleImageRefresh(bool resetViewState) {
    resetViewStateOnRefresh_ = resetViewStateOnRefresh_ || resetViewState;
    if (!imageRefreshTimer_.isActive()) {
        imageRefreshTimer_.start(kStaticImageRefreshIntervalMs);
    }
}

void MainWindow::refreshImage(bool resetViewState) {
    if (!imageSession_->hasImage()) {
        return;
    }

    if (autoWindowCheck_->isChecked()) {
        const WindowLevelResult autoWindow = imageSession_->autoWindowLevel();
        if (!autoWindow.valid) {
            return;
        }
        const QSignalBlocker blockCenterSlider(centerSlider_);
        const QSignalBlocker blockWidthSlider(widthSlider_);
        const QSignalBlocker blockCenterSpin(centerSpin_);
        const QSignalBlocker blockWidthSpin(widthSpin_);

        centerSlider_->setValue(autoWindow.center);
        centerSpin_->setValue(autoWindow.center);
        widthSlider_->setValue(autoWindow.width);
        widthSpin_->setValue(autoWindow.width);
    }

    QImage display;
    const ImageFrame currentFrame = imageSession_->currentFrame();
    const QString contentCacheKey = currentFrame.contentCacheKey;
    const QSize logicalImageSize(imageSession_->image().width(), imageSession_->image().height());
    /*
     * PCIe 实时帧没有稳定文件缓存键。这里对实时显示做降采样，减少 16-bit->8-bit
     * 映射和 QPixmap 创建成本；原始 16-bit 帧仍保留在 ImageSession 中，像素值/ROI 按原图坐标计算。
     */
    const QSize renderSize = contentCacheKey.isEmpty()
        ? scaledDisplaySize(logicalImageSize, kLiveDisplayMaxLongEdge)
        : logicalImageSize;
    if (!contentCacheKey.isEmpty()) {
        const QString cacheKey = contentCacheKey
            + QStringLiteral("\n%1\n%2").arg(centerSpin_->value()).arg(widthSpin_->value());
        if (const QImage* cached = frameDisplayCache_.object(cacheKey)) {
            display = *cached;
        } else {
            display = imageSession_->render(centerSpin_->value(), widthSpin_->value());
            frameDisplayCache_.insert(cacheKey, new QImage(display));
        }
    } else {
        display = imageSession_->render(centerSpin_->value(), widthSpin_->value(), renderSize);
    }
    lastDisplayRenderSize_ = display.size();
    imageView_->setImage(display, resetViewState, logicalImageSize);
}

void MainWindow::updatePixelInfo(const QPoint& imagePoint) {
    if (pixelInfoLabel_ == nullptr) {
        return;
    }

    quint16 value = 0;
    if (!imageSession_->image().pixelValue(imagePoint.x(), imagePoint.y(), &value)) {
        pixelInfoLabel_->setText(QStringLiteral("像素值: --"));
        return;
    }

    pixelInfoLabel_->setText(QStringLiteral("像素值 (%1, %2): %3")
                                 .arg(imagePoint.x())
                                 .arg(imagePoint.y())
                                 .arg(value));
}

void MainWindow::showRoiInfo(const TiRawImage::RoiStats& stats) {
    if (roiInfoLabel_ == nullptr) {
        return;
    }

    const TiRawImage& image = imageSession_->image();
    const bool fullImage = stats.rect == QRect(0, 0, image.width(), image.height());
    roiInfoLabel_->setText(QStringLiteral(
                               "ROI 信息: %1\n"
                               "区域: (%2, %3) - (%4, %5)\n"
                               "像素数量: %6\n"
                               "均值: %7\n"
                               "最大值: %8\n"
                               "最小值: %9\n"
                               "标准差: %10\n"
                               "行噪声: %11")
                               .arg(fullImage ? QStringLiteral("全图") : QStringLiteral("当前框选"))
                               .arg(stats.rect.left())
                               .arg(stats.rect.top())
                               .arg(stats.rect.right())
                               .arg(stats.rect.bottom())
                               .arg(stats.pixelCount)
                               .arg(QString::number(stats.mean, 'f', 4))
                               .arg(stats.max)
                               .arg(stats.min)
                               .arg(QString::number(stats.stddev, 'f', 4))
                               .arg(QString::number(stats.rowNoise, 'f', 4)));
}

void MainWindow::updateRoiInfo(const QRect& imageRect) {
    TiRawImage::RoiStats stats;
    if (!imageSession_->roiStats(imageRect, &stats)) {
        if (roiInfoLabel_ != nullptr) {
            roiInfoLabel_->setText(QStringLiteral("ROI 信息: --"));
        }
        return;
    }
    showRoiInfo(stats);
}

void MainWindow::updateCurrentImageInfo() {
    QElapsedTimer timer;
    timer.start();
    if (activeRoi_.isValid() && !activeRoi_.isEmpty()) {
        updateRoiInfo(activeRoi_);
        const qint64 elapsedMs = timer.elapsed();
        if (elapsedMs >= 50) {
            logService_->info(QStringLiteral("IMAGE"),
                QStringLiteral("ROI 信息统计耗时: roi=1 elapsed=%1ms").arg(elapsedMs));
        }
        return;
    }
    updateFullImageInfo();
    const qint64 elapsedMs = timer.elapsed();
    if (elapsedMs >= 50) {
        logService_->info(QStringLiteral("IMAGE"),
            QStringLiteral("ROI 信息统计耗时: roi=0 elapsed=%1ms").arg(elapsedMs));
    }
}

void MainWindow::updateFullImageInfo() {
    if (!imageSession_->hasImage()) {
        if (roiInfoLabel_ != nullptr) {
            roiInfoLabel_->setText(QStringLiteral("ROI 信息: --"));
        }
        return;
    }

    const TiRawImage& image = imageSession_->image();
    const QString contentCacheKey = imageSession_->currentFrame().contentCacheKey;
    if (!contentCacheKey.isEmpty()) {
        auto cached = stableFrameStatsCache_.constFind(contentCacheKey);
        if (cached == stableFrameStatsCache_.constEnd()) {
            TiRawImage::RoiStats stats;
            if (!imageSession_->roiStats(QRect(0, 0, image.width(), image.height()), &stats)) {
                return;
            }
            cached = stableFrameStatsCache_.insert(contentCacheKey, stats);
        }
        showRoiInfo(cached.value());
        return;
    }
    updateRoiInfo(QRect(0, 0, image.width(), image.height()));
}

void MainWindow::handleAnalysisRoi(const QRect& imageRect) {
    activeRoi_ = imageRect;
    updateRoiInfo(imageRect);

    TiRawImage::RoiStats stats;
    if (!imageSession_->roiStats(imageRect, &stats)) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("分析测试"));
    dialog.resize(1280, 720);

    auto* root = new QHBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 12, 12);
    root->setSpacing(12);

    auto* previewLabel = new QLabel(&dialog);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumSize(700, 640);
    previewLabel->setStyleSheet(QStringLiteral("background:#08090a;"));

    const QImage display = imageSession_->render(centerSpin_->value(), widthSpin_->value());
    previewLabel->setPixmap(drawAnalysisPreview(display, stats.rect, previewLabel->minimumSize()));

    auto* rightPanel = new QWidget(&dialog);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 24, 0, 0);
    rightLayout->setSpacing(18);

    MtfAnalysisResult analysis;
    if (!imageSession_->analyzeMtf(stats.rect, &analysis)) {
        QMessageBox::warning(this, QStringLiteral("分析失败"), QStringLiteral("当前 ROI 无法生成 MTF 分析结果"));
        return;
    }

    constexpr int chartWidth = 430;
    constexpr int chartHeight = 185;
    auto* esfLabel = new QLabel(&dialog);
    auto* lsfLabel = new QLabel(&dialog);
    auto* mtfLabel = new QLabel(&dialog);
    esfLabel->setPixmap(drawLineChart(QStringLiteral("Edge Spread Function"), QStringLiteral("ESF (ADC/mm)"), QStringLiteral("Distance (mm)"), analysis.esf.y, {chartWidth, chartHeight}));
    lsfLabel->setPixmap(drawLineChart(QStringLiteral("Line Spread Function"), QStringLiteral("LSF (ADC/mm)"), QStringLiteral("Distance (mm)"), analysis.lsf.y, {chartWidth, chartHeight}));
    mtfLabel->setPixmap(drawLineChart(QStringLiteral("Modulation Transfer Function (Pixel Size:100um)"), QStringLiteral("MTF"), QStringLiteral("Spatial Frequency (lp/mm)"), analysis.mtf.y, {chartWidth, chartHeight}));

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* exportButton = closeButtons->addButton(QStringLiteral("导出 CSV"), QDialogButtonBox::ActionRole);
    connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(exportButton, &QPushButton::clicked, &dialog, [this, analysis]() {
        const QString directory = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("导出分析曲线"),
            defaultImageDirectory(*settings_));
        if (directory.isEmpty()) {
            return;
        }

        QString error;
        if (!imageSession_->exportMtf(directory, analysis, &error)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), error);
            return;
        }
        logService_->info(QStringLiteral("ANALYSIS"),
            QStringLiteral("导出分析曲线: %1").arg(directory));
    });

    rightLayout->addWidget(esfLabel);
    rightLayout->addWidget(lsfLabel);
    rightLayout->addWidget(mtfLabel);
    rightLayout->addStretch(1);
    rightLayout->addWidget(closeButtons);

    root->addWidget(previewLabel, 1);
    root->addWidget(rightPanel);

    dialog.exec();
}

void MainWindow::applyWindowLevelFromRoi(const QRect& imageRect) {
    activeRoi_ = imageRect;
    updateRoiInfo(imageRect);

    TiRawImage::RoiStats stats;
    if (!imageSession_->roiStats(imageRect, &stats)) {
        return;
    }

    const WindowLevelResult windowLevel = imageSession_->roiWindowLevel(stats.rect);
    if (!windowLevel.valid) {
        return;
    }

    {
        const QSignalBlocker blockAuto(autoWindowCheck_);
        const QSignalBlocker blockCenterSlider(centerSlider_);
        const QSignalBlocker blockWidthSlider(widthSlider_);
        const QSignalBlocker blockCenterSpin(centerSpin_);
        const QSignalBlocker blockWidthSpin(widthSpin_);

        autoWindowCheck_->setChecked(false);
        centerSlider_->setValue(windowLevel.center);
        centerSpin_->setValue(windowLevel.center);
        widthSlider_->setValue(windowLevel.width);
        widthSpin_->setValue(windowLevel.width);
    }

    updateWindowLevelControlState();
    clearFrameDisplayCaches();
    refreshImage(false);
}

void MainWindow::handlePresentedFrame(const ImageFrame& frame) {
    QElapsedTimer uiTimer;
    QElapsedTimer stageTimer;
    uiTimer.start();
    stageTimer.start();
    ImageFrame presentedFrame = frame;
    const bool isPcieFrame = presentedFrame.sourceName.startsWith(QStringLiteral("PCIe #"));
    if (isPcieFrame && awaitingTemplateFrame_ && !pendingTemplateUploadName_.isEmpty()) {
        presentedFrame.sourceName = pendingTemplateUploadName_;
        awaitingTemplateFrame_ = false;
        statusBar()->showMessage(QStringLiteral("%1已接收并显示").arg(pendingTemplateUploadName_), 5000);
        pendingTemplateUploadName_.clear();
    }
    const bool resetViewState = !imageSession_->hasImage()
        || imageSession_->image().width() != presentedFrame.image.width()
        || imageSession_->image().height() != presentedFrame.image.height();
    QString error;
    if (!imageSession_->setFrame(presentedFrame, &error)) {
        logService_->error(QStringLiteral("IMAGE"),
            QStringLiteral("接收图像帧失败: %1").arg(error));
        return;
    }
    const qint64 setFrameMs = stageTimer.elapsed();
    if (resetViewState) {
        activeRoi_ = {};
    }

    stageTimer.restart();
    QString imageTypeText;
    if (presentedFrame.sourceImageTypeValid) {
        imageTypeText = presentedFrame.sourceImageType == 1u
            ? QStringLiteral("模板上传")
            : presentedFrame.sourceImageType == 0u
                ? QStringLiteral("正常图片")
                : QStringLiteral("未知类型(%1)").arg(presentedFrame.sourceImageType);
    }
    imageLabel_->setText(imageTypeText.isEmpty()
        ? QStringLiteral("图像尺寸: %1 x %2")
              .arg(presentedFrame.image.width())
              .arg(presentedFrame.image.height())
        : QStringLiteral("图像尺寸: %1 x %2  类型: %3")
              .arg(presentedFrame.image.width())
              .arg(presentedFrame.image.height())
              .arg(imageTypeText));
    updateImageUiState();
    const bool stableContent = !presentedFrame.contentCacheKey.isEmpty();
    imageView_->setPixmapCacheEnabled(stableContent);
    if (stableContent && !presentedFrame.image.path().isEmpty()) {
        imageListPanel_->ensureThumbnail(presentedFrame.image.path(), presentedFrame.image);
        imageListPanel_->setCurrentPath(presentedFrame.image.path());
    }
    const qint64 controlsMs = stageTimer.elapsed();

    // 采集控制器已经完成会话隔离、限速和丢帧处理，这里只同步界面状态。
    imageRefreshTimer_.stop();
    const bool shouldResetView = resetViewStateOnRefresh_ || resetViewState;
    resetViewStateOnRefresh_ = false;
    stageTimer.restart();
    refreshImage(shouldResetView);
    const qint64 refreshMs = stageTimer.elapsed();
    const bool shouldRefreshInfo = !isPcieFrame || (activeRoi_.isValid() && !activeRoi_.isEmpty());
    if (shouldRefreshInfo
        && (!imageInfoTimer_.isValid() || imageInfoTimer_.elapsed() >= 1000)
        && !imageInfoRefreshTimer_.isActive()) {
        imageInfoTimer_.restart();
        imageInfoRefreshTimer_.start(1);
    } else if (isPcieFrame && (activeRoi_.isNull() || activeRoi_.isEmpty()) && roiInfoLabel_ != nullptr) {
        /*
         * 实时连续上图时，全图 ROI 统计需要遍历 2359 万像素，容易和显示刷新抢 UI。
         * 未框选 ROI 时先暂停全图统计；如果 1 秒内没有新帧，再只计算最后一帧。
         * 用户框选 ROI 后仍按原始 16-bit 数据计算该 ROI。
         */
        roiInfoLabel_->setText(QStringLiteral("ROI 信息: 实时上图中"));
        imageInfoRefreshTimer_.start(1000);
    }
    if (isPcieFrame) {
        logService_->info(QStringLiteral("IMAGE"),
            QStringLiteral("PCIe 图像显示完成: source=%1 set_frame=%2ms controls=%3ms refresh=%4ms ui_total=%5ms reset=%6 display=%7x%8")
                .arg(presentedFrame.sourceName)
                .arg(setFrameMs)
                .arg(controlsMs)
                .arg(refreshMs)
                .arg(uiTimer.elapsed())
                .arg(shouldResetView ? 1 : 0)
                .arg(lastDisplayRenderSize_.width())
                .arg(lastDisplayRenderSize_.height()));
    }
}

void MainWindow::updateImageUiState() {
    const bool hasImage = imageSession_ != nullptr && imageSession_->hasImage();
    if (imageOpsPanel_ != nullptr) {
        imageOpsPanel_->setEnabled(hasImage);
    }
    if (windowLevelPanel_ != nullptr) {
        windowLevelPanel_->setEnabled(hasImage);
    }
    if (saveDisplayAction_ != nullptr) {
        saveDisplayAction_->setEnabled(hasImage);
    }
    if (imageMaximizeAction_ != nullptr) {
        imageMaximizeAction_->setEnabled(hasImage || imageMaximized_);
    }
    updateWindowLevelControlState();
}

void MainWindow::updateWindowLevelControlState() {
    const bool manualEnabled = imageSession_ != nullptr
        && imageSession_->hasImage()
        && autoWindowCheck_ != nullptr
        && !autoWindowCheck_->isChecked();
    if (centerSlider_ != nullptr) {
        centerSlider_->setEnabled(manualEnabled);
    }
    if (widthSlider_ != nullptr) {
        widthSlider_->setEnabled(manualEnabled);
    }
    if (centerSpin_ != nullptr) {
        centerSpin_->setEnabled(manualEnabled);
    }
    if (widthSpin_ != nullptr) {
        widthSpin_->setEnabled(manualEnabled);
    }
}

void MainWindow::updateImageTransferControls() {
    if (imageTransferController_ == nullptr) {
        return;
    }
    const ImageTransferMode mode = imageTransferController_->mode();
    idleModeButton_->setChecked(mode == ImageTransferMode::Manual);
    continuousModeButton_->setChecked(mode == ImageTransferMode::Continuous);
    idleModeButton_->setEnabled(imageTransferController_->canSelectMode());
    continuousModeButton_->setEnabled(imageTransferController_->canSelectMode());
    startImageButton_->setText(mode == ImageTransferMode::Manual
            ? QStringLiteral("手动上图")
            : QStringLiteral("开始上图"));
    startImageButton_->setEnabled(imageTransferController_->canStart());
    stopImageButton_->setEnabled(imageTransferController_->canStop());
    modeLabel_->setText(imageTransferStateText(mode, imageTransferController_->state()));
}

void MainWindow::updateDeviceState(PaDeviceState state) {
    const bool connected = deviceController_->isConnected();
    const bool commandEnabled = connected && state != PaDeviceState::Busy;

    portCombo_->setEnabled(!connected);
    baudSpin_->setEnabled(!connected);
    refreshPortsAction_->setEnabled(!connected);
    connectSerialAction_->setEnabled(!connected);
    disconnectSerialAction_->setEnabled(connected);
    Q_UNUSED(commandEnabled);

    switch (state) {
    case PaDeviceState::Disconnected:
        awaitingTemplateFrame_ = false;
        pendingTemplateUploadName_.clear();
        armProgramVersion_.clear();
        fpgaVersion_.clear();
        modelLabel_->setText(QStringLiteral("型号: PA 专用"));
        serialLabel_->setText(QStringLiteral("序列号: --"));
        connectionLabel_->setText(QStringLiteral("RS422: 未连接"));
        break;
    case PaDeviceState::Ready:
        connectionLabel_->setText(QStringLiteral("RS422: 已连接"));
        break;
    case PaDeviceState::Busy:
        connectionLabel_->setText(QStringLiteral("RS422: 执行中"));
        break;
    case PaDeviceState::Error:
        connectionLabel_->setText(QStringLiteral("RS422: 错误"));
        break;
    }
}
