#pragma once

/*
 * MainWindow —— tiray_host_gui 主窗口。
 *
 * 界面布局参考 pa_host（上方连接栏、中间图像显示、右侧控制、底部日志），
 * 但控制链路只调用 tiray_sdk 公共 API（SdkWorker），不使用私有协议；
 * 图像来自 SDK 的 PCIe 接收接口（PcieMonitor）。
 */

#include <QImage>
#include <QMainWindow>
#include <QSharedPointer>
#include <QVector>

#include "PcieMonitor.h"
#include "tiray_sdk.h"

class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QTimer;
class QThread;
class ImageView;
class SdkWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    /* SdkWorker 结果 */
    void handleOperationFinished(QString operation, bool ok, QString message);
    void handleStatusReady(tiray_device_status_t status, quint32 lastDeviceError);
    void handleDynamicStatusReady(tiray_dynamic_status_t status);
    void handleConfigGroupReady(quint16 group, QVector<tiray_config_item_t> items);
    void handleStaticConfigReady(tiray_static_config_t config);
    void handleDynamicConfigReady(tiray_dynamic_config_t config);
    void handleCalStatusReady(tiray_cal_status_t status);
    void handleUploadStatusReady(tiray_image_upload_status_t status);

    /* PcieMonitor 结果 */
    void handlePcieStarted(bool ok, QString message);
    void handlePcieStopped();
    void handleFrameReceived(QSharedPointer<PcieFrame> frame);

    /* 界面操作 */
    void connectDevice();
    void disconnectDevice();
    void pingDevice();
    void requestStatus();
    void requestReboot();
    void requestStaticCapture();
    void requestDynamicStart();
    void requestDynamicQuery();
    void requestDynamicStop();
    void requestReadConfigGroup();
    void requestSendConfigGroup();
    void requestGetStaticConfig();
    void requestSetStaticConfig();
    void requestGetDynamicConfig();
    void requestSetDynamicConfig();
    void requestOffsetBegin();
    void requestOffsetCapture();
    void requestOffsetBuild();
    void requestOffsetCancel();
    void requestGainBegin();
    void requestGainCapture();
    void requestGainBuild();
    void requestGainCancel();
    void requestCalStatus();
    void requestUploadConfig();
    void requestUploadStart();
    void requestUploadQuery();
    void startPcie();
    void stopPcie();
    void saveDisplayImage();
    void saveLatestRaw();
    void clearImageDisplay();
    void updateWindowControls();
    void updatePixelInfo(const QPoint& logicalPoint);
    void updateRoiInfo(const QRect& logicalRect);
    void renderDisplayTimer();
    void pollStatusTimer();
    void pollCalTimer();

private:
    QWidget* createTopBar();
    QWidget* createCenterArea();
    QWidget* createDeviceTab();
    QWidget* createConfigTab();
    QWidget* createCalibrationTab();
    QWidget* createImageTab();
    void createLogDock();
    void createStatusBar();

    void log(const QString& message);
    void logResult(bool ok, const QString& message);
    void changeBusy(int delta);
    void updateUiState();
    void renderDisplay(bool resetView);
    void computeAutoWindow();
    QImage windowedFullImage() const;
    void refreshGroupCombo();

    /* 工作线程与 SDK */
    QThread* workerThread_ = nullptr;
    SdkWorker* sdkWorker_ = nullptr;
    PcieMonitor* pcieMonitor_ = nullptr;

    /* 连接栏 */
    QLineEdit* portEdit_ = nullptr;
    QSpinBox* baudSpin_ = nullptr;
    QSpinBox* timeoutSpin_ = nullptr;
    QCheckBox* internalCheck_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QLabel* connectionLabel_ = nullptr;

    /* 图像区 */
    ImageView* imageView_ = nullptr;
    QLabel* frameInfoLabel_ = nullptr;
    QLabel* pixelInfoLabel_ = nullptr;
    QLabel* roiInfoLabel_ = nullptr;

    /* 设备页 */
    QPushButton* pingButton_ = nullptr;
    QCheckBox* autoStatusCheck_ = nullptr;
    QSpinBox* statusIntervalSpin_ = nullptr;
    QLabel* statusModeLabel_ = nullptr;
    QLabel* statusStateLabel_ = nullptr;
    QLabel* statusErrorLabel_ = nullptr;
    QLabel* statusFrameLabel_ = nullptr;
    QLabel* statusAddrLabel_ = nullptr;
    QLabel* statusCorrectionLabel_ = nullptr;
    QPushButton* staticButton_ = nullptr;
    QPushButton* dynamicStartButton_ = nullptr;
    QPushButton* dynamicQueryButton_ = nullptr;
    QPushButton* dynamicStopButton_ = nullptr;
    QLabel* dynamicStatusLabel_ = nullptr;
    QPushButton* rebootButton_ = nullptr;

    /* 配置页 */
    QComboBox* groupCombo_ = nullptr;
    QPushButton* readGroupButton_ = nullptr;
    QPushButton* sendGroupButton_ = nullptr;
    QTableWidget* configTable_ = nullptr;
    QSpinBox* staticIdleSpin_ = nullptr;
    QSpinBox* staticExposureSpin_ = nullptr;
    QSpinBox* staticDarkSpin_ = nullptr;
    QPushButton* staticGetButton_ = nullptr;
    QPushButton* staticSetButton_ = nullptr;
    QSpinBox* dynamicCycleSpin_ = nullptr;
    QLineEdit* dynamicStartAddrEdit_ = nullptr;
    QLineEdit* dynamicEndAddrEdit_ = nullptr;
    QSpinBox* dynamicStartTimeoutSpin_ = nullptr;
    QSpinBox* dynamicPollSpin_ = nullptr;
    QSpinBox* dynamicStopTimeoutSpin_ = nullptr;
    QPushButton* dynamicGetButton_ = nullptr;
    QPushButton* dynamicSetButton_ = nullptr;

    /* 校准页 */
    QSpinBox* offsetTotalSpin_ = nullptr;
    QSpinBox* offsetValidSpin_ = nullptr;
    QComboBox* offsetModeCombo_ = nullptr;
    QPushButton* offsetBeginButton_ = nullptr;
    QPushButton* offsetCaptureButton_ = nullptr;
    QPushButton* offsetBuildButton_ = nullptr;
    QPushButton* offsetCancelButton_ = nullptr;
    QLineEdit* gainLevelsEdit_ = nullptr;
    QSpinBox* gainFramesSpin_ = nullptr;
    QDoubleSpinBox* gainThresholdSpin_ = nullptr;
    QPushButton* gainBeginButton_ = nullptr;
    QSpinBox* gainLevelSpin_ = nullptr;
    QPushButton* gainCaptureButton_ = nullptr;
    QPushButton* gainBuildButton_ = nullptr;
    QPushButton* gainCancelButton_ = nullptr;
    QLabel* calStateLabel_ = nullptr;
    QCheckBox* autoCalCheck_ = nullptr;
    QComboBox* uploadKindCombo_ = nullptr;
    QLineEdit* uploadAddrEdit_ = nullptr;
    QSpinBox* uploadRowsSpin_ = nullptr;
    QSpinBox* uploadColsSpin_ = nullptr;
    QSpinBox* uploadPkgsSpin_ = nullptr;
    QPushButton* uploadConfigButton_ = nullptr;
    QPushButton* uploadStartButton_ = nullptr;
    QPushButton* uploadQueryButton_ = nullptr;
    QLabel* uploadStateLabel_ = nullptr;

    /* 图像页 */
    QLineEdit* pcieEventEdit_ = nullptr;
    QLineEdit* pcieC2hEdit_ = nullptr;
    QLineEdit* pcieBar0Edit_ = nullptr;
    QPushButton* pcieStartButton_ = nullptr;
    QPushButton* pcieStopButton_ = nullptr;
    QLabel* pcieStatsLabel_ = nullptr;
    QCheckBox* autoWindowCheck_ = nullptr;
    QSlider* windowCenterSlider_ = nullptr;
    QSlider* windowWidthSlider_ = nullptr;
    QComboBox* displayWidthCombo_ = nullptr;
    QPushButton* savePngButton_ = nullptr;
    QPushButton* saveRawButton_ = nullptr;

    /* 日志与状态栏 */
    QDockWidget* logDock_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QLabel* busyLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;

    /* 运行状态 */
    bool connected_ = false;
    int busyCount_ = 0;
    QVector<QPushButton*> serialButtons_;
    QTimer* statusTimer_ = nullptr;
    QTimer* calTimer_ = nullptr;
    QTimer* displayTimer_ = nullptr;
    QTimer* fpsTimer_ = nullptr;

    /* 图像状态 */
    QSharedPointer<PcieFrame> latestFrame_;
    QSharedPointer<PcieFrame> lastRenderedFrame_;
    QImage latestWindowedImage_;
    bool displayDirty_ = false;
    bool autoWindowValid_ = false;
    int autoWindowLow_ = 0;
    int autoWindowHigh_ = 65535;
    quint64 totalFrameCount_ = 0;
    quint64 fpsWindowCount_ = 0;
};
