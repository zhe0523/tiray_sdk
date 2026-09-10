#pragma once

#include <QCache>
#include <QLabel>
#include <QElapsedTimer>
#include <QImage>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QPlainTextEdit>
#include <QPoint>
#include <QRect>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>

#include <memory>

#include "ImageSession.h"
#include "ImageSource.h"
#include "ImageView.h"
#include "PaProtocol.h"
#include "SerialClient.h"
#include "TiRawImage.h"

class QCheckBox;
class QComboBox;
class QDockWidget;
class QMenu;
class QPushButton;
class QSlider;
class QAction;
class ImageAcquisitionController;
class ImageTransferWorkflowController;
class ImageListPanel;
class AppLogService;
class AppSettings;
class PaDeviceController;
class PcieImageSource;
enum class PaDeviceState;

/*
 * 主窗口。
 *
 * 当前界面目标不是复刻旧 Windows 软件，而是保留同类操作习惯：
 *   左侧图像列表，中间图像显示，右侧控制/图像操作，底部状态栏。
 * 新项目只面向单一 RS422 型号，所以不引入旧软件里的 WiFi、TCP、多型号切换。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(std::shared_ptr<IImageAlgorithms> algorithms, QWidget* parent = nullptr);
    ~MainWindow() override;


private slots:
    void openImage();
    void startPcieCapture();
    void saveDisplayImage();
    void connectSerial();
    void disconnectSerial();
    void sendCommand(PaProtocol::Command command);
    void updateWindowLevel();
    void toggleImageMaximized();
    void exportDiagnostics();
    void configureCommandTimeout();
    void showAbout();
    void showOffsetTemplateDialog();
    void showGainTemplateDialog();
    void viewOffsetTemplate();
    void viewGainTemplate();
    void showDynamicConfigDialog();
    void queryDynamicStatus();
    void restartDevice();
    void showDeveloperDialog();
    void showDebugDialog();

private:
    QWidget* createTopBar();
    QWidget* createRightPanel();
    QWidget* createImageOpsPanel();
    QWidget* createWindowLevelPanel();
    QWidget* createImageInfoPanel();
    void createLogDock();
    void createMenus();
    void createStatusBar();
    void handleImageListSelection(const QString& path);
    void handleImagesRemoved(int count, const QString& nextPath);
    void exportImage(const QString& sourcePath, const QString& formatId);
    void showCurrentSessionImage(const QString& source, bool resetViewState);
    void clearCurrentImage();
    void clearFrameDisplayCaches();
    void stopImageAcquisition();
    void scheduleImageRefresh(bool resetViewState = false);
    void refreshImage(bool resetViewState = false);
    void updatePixelInfo(const QPoint& imagePoint);
    void showRoiInfo(const TiRawImage::RoiStats& stats);
    void updateRoiInfo(const QRect& imageRect);
    void updateCurrentImageInfo();
    void updateFullImageInfo();
    void handleAnalysisRoi(const QRect& imageRect);
    void applyWindowLevelFromRoi(const QRect& imageRect);
    void handlePresentedFrame(const ImageFrame& frame);
    void updateImageUiState();
    void updateWindowLevelControlState();
    void updateImageTransferControls();
    void updateDeviceState(PaDeviceState state);
    void handleBinaryCommandFinished(quint16 command,
                                     const QMap<quint16, quint32>& values,
                                     bool success,
                                     const QString& detail);
    void startTemplateUpload(bool gainTemplate);

    SerialClient serial_;
    std::unique_ptr<AppSettings> settings_;
    std::unique_ptr<ImageSession> imageSession_;
    AppLogService* logService_ = nullptr;
    PcieImageSource* pcieSource_ = nullptr;
    ImageAcquisitionController* acquisitionController_ = nullptr;
    PaDeviceController* deviceController_ = nullptr;
    ImageTransferWorkflowController* imageTransferController_ = nullptr;

    QWidget* topBar_ = nullptr;
    ImageListPanel* imageListPanel_ = nullptr;
    QWidget* rightPanel_ = nullptr;
    QWidget* imageOpsPanel_ = nullptr;
    QWidget* windowLevelPanel_ = nullptr;
    ImageView* imageView_ = nullptr;
    QDockWidget* logDock_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QAction* imageMaximizeAction_ = nullptr;
    QAction* saveDisplayAction_ = nullptr;
    QAction* refreshPortsAction_ = nullptr;
    QAction* connectSerialAction_ = nullptr;
    QAction* disconnectSerialAction_ = nullptr;

    QComboBox* portCombo_ = nullptr;
    QSpinBox* baudSpin_ = nullptr;
    QPushButton* idleModeButton_ = nullptr;
    QPushButton* continuousModeButton_ = nullptr;
    QPushButton* startImageButton_ = nullptr;
    QPushButton* stopImageButton_ = nullptr;

    QCheckBox* autoWindowCheck_ = nullptr;
    QSlider* centerSlider_ = nullptr;
    QSlider* widthSlider_ = nullptr;
    QSpinBox* centerSpin_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;

    QLabel* modelLabel_ = nullptr;
    QLabel* serialLabel_ = nullptr;
    QLabel* connectionLabel_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* pixelInfoLabel_ = nullptr;
    QLabel* roiInfoLabel_ = nullptr;

    QTimer imageRefreshTimer_;
    QTimer imageInfoRefreshTimer_;
    /*
     * 历史图像切换性能缓存：
     * - imageFrameCache_ 保存最近若干张 16-bit 原始帧，避免重复从磁盘读取和解析。
     * - frameDisplayCache_ 保存当前窗宽窗位映射后的 8-bit 显示图，避免重复做灰度映射。
     * - stableFrameStatsCache_ 保存稳定文件帧的 ROI/全图统计，避免切回来后再次扫全图。
     */
    QCache<QString, ImageFrame> imageFrameCache_;
    QCache<QString, QImage> frameDisplayCache_;
    QHash<QString, TiRawImage::RoiStats> stableFrameStatsCache_;
    QElapsedTimer imageInfoTimer_;
    QRect activeRoi_;
    QSize lastDisplayRenderSize_;
    QString armProgramVersion_;
    QString fpgaVersion_;
    QString pendingTemplateUploadName_;
    bool awaitingTemplateFrame_ = false;
    bool resetViewStateOnRefresh_ = false;
    bool imageMaximized_ = false;
};
