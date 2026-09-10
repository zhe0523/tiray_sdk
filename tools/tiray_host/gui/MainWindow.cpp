#include "MainWindow.h"

#include "ImageView.h"
#include "SdkWorker.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

QString workModeName(quint32 mode) {
    switch (mode) {
        case TIRAY_WORK_MODE_IDLE: return QStringLiteral("IDLE");
        case TIRAY_WORK_MODE_AED: return QStringLiteral("AED");
        case TIRAY_WORK_MODE_SYNC_OUT: return QStringLiteral("SYNC_OUT");
        case TIRAY_WORK_MODE_SYNC_IN: return QStringLiteral("SYNC_IN");
        case TIRAY_WORK_MODE_PREP: return QStringLiteral("PREP");
        case TIRAY_WORK_MODE_CONTINUOUS: return QStringLiteral("CONTINUOUS");
        case TIRAY_WORK_MODE_INNER: return QStringLiteral("INNER");
        case TIRAY_WORK_MODE_FREE_SYNC: return QStringLiteral("FREE_SYNC");
        case TIRAY_WORK_MODE_DDR: return QStringLiteral("DDR");
        default: return QStringLiteral("UNKNOWN");
    }
}

QString workStateName(quint32 state) {
    switch (state) {
        case TIRAY_WORK_STATE_STOPPED: return QStringLiteral("STOPPED");
        case TIRAY_WORK_STATE_IDLE_WAIT: return QStringLiteral("IDLE_WAIT");
        case TIRAY_WORK_STATE_IDLE_CLEANING: return QStringLiteral("IDLE_CLEANING");
        case TIRAY_WORK_STATE_EXPOSURE_WINDOW: return QStringLiteral("EXPOSURE_WINDOW");
        case TIRAY_WORK_STATE_BRIGHT_CAPTURE: return QStringLiteral("BRIGHT_CAPTURE");
        case TIRAY_WORK_STATE_DARK_WINDOW: return QStringLiteral("DARK_WINDOW");
        case TIRAY_WORK_STATE_DARK_CAPTURE: return QStringLiteral("DARK_CAPTURE");
        case TIRAY_WORK_STATE_DYNAMIC_STARTING: return QStringLiteral("DYNAMIC_STARTING");
        case TIRAY_WORK_STATE_DYNAMIC_RUNNING: return QStringLiteral("DYNAMIC_RUNNING");
        case TIRAY_WORK_STATE_DYNAMIC_STOPPING: return QStringLiteral("DYNAMIC_STOPPING");
        case TIRAY_WORK_STATE_DYNAMIC_COMPLETED: return QStringLiteral("DYNAMIC_COMPLETED");
        case TIRAY_WORK_STATE_ERROR: return QStringLiteral("ERROR");
        default: return QStringLiteral("UNKNOWN");
    }
}

QString calStateName(quint32 state) {
    switch (state) {
        case 0: return QStringLiteral("空闲");
        case 1: return QStringLiteral("运行");
        case 2: return QStringLiteral("停止中");
        case 3: return QStringLiteral("成功");
        case 4: return QStringLiteral("失败");
        case 5: return QStringLiteral("取消");
        default: return QStringLiteral("未知");
    }
}

QString hex32(quint32 value) {
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

/* 十六进制（0x 前缀）或十进制均可。 */
bool parseU32(const QString& text, quint32& value) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    bool ok = false;
    const qulonglong parsed = trimmed.toULongLong(&ok, 0);
    if (!ok || parsed > UINT32_MAX) {
        return false;
    }
    value = static_cast<quint32>(parsed);
    return true;
}

quint16 readU16Le(const char* data) {
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data));
}

QLabel* createValueLabel(const QString& text = QStringLiteral("-")) {
    auto* label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("TiRay 43108 维护上位机（tiray_host_gui）"));
    resize(1280, 800);

    pcieMonitor_ = new PcieMonitor(this);

    workerThread_ = new QThread(this);
    workerThread_->setObjectName(QStringLiteral("SdkWorkerThread"));
    sdkWorker_ = new SdkWorker(); /* 无父对象，随线程结束 deleteLater */
    sdkWorker_->moveToThread(workerThread_);
    connect(workerThread_, &QThread::finished, sdkWorker_, &QObject::deleteLater);
    workerThread_->start();

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(6, 6, 6, 6);
    centralLayout->setSpacing(6);
    centralLayout->addWidget(createTopBar());
    centralLayout->addWidget(createCenterArea(), 1);
    setCentralWidget(central);

    createLogDock();
    createStatusBar();

    /* SdkWorker → 界面（跨线程自动队列） */
    connect(sdkWorker_, &SdkWorker::operationFinished,
            this, &MainWindow::handleOperationFinished);
    connect(sdkWorker_, &SdkWorker::statusReady, this, &MainWindow::handleStatusReady);
    connect(sdkWorker_, &SdkWorker::dynamicStatusReady,
            this, &MainWindow::handleDynamicStatusReady);
    connect(sdkWorker_, &SdkWorker::configGroupReady, this, &MainWindow::handleConfigGroupReady);
    connect(sdkWorker_, &SdkWorker::staticConfigReady, this, &MainWindow::handleStaticConfigReady);
    connect(sdkWorker_, &SdkWorker::dynamicConfigReady,
            this, &MainWindow::handleDynamicConfigReady);
    connect(sdkWorker_, &SdkWorker::calStatusReady, this, &MainWindow::handleCalStatusReady);
    connect(sdkWorker_, &SdkWorker::uploadStatusReady, this, &MainWindow::handleUploadStatusReady);

    /* PcieMonitor → 界面 */
    connect(pcieMonitor_, &PcieMonitor::started, this, &MainWindow::handlePcieStarted);
    connect(pcieMonitor_, &PcieMonitor::stopped, this, &MainWindow::handlePcieStopped);
    connect(pcieMonitor_, &PcieMonitor::frameReceived, this, &MainWindow::handleFrameReceived);

    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::pollStatusTimer);
    calTimer_ = new QTimer(this);
    connect(calTimer_, &QTimer::timeout, this, &MainWindow::pollCalTimer);
    displayTimer_ = new QTimer(this);
    displayTimer_->setInterval(100);
    connect(displayTimer_, &QTimer::timeout, this, &MainWindow::renderDisplayTimer);
    displayTimer_->start();
    fpsTimer_ = new QTimer(this);
    fpsTimer_->setInterval(1000);
    connect(fpsTimer_, &QTimer::timeout, this, [this] {
        fpsLabel_->setText(QStringLiteral("%1 fps").arg(fpsWindowCount_));
        fpsWindowCount_ = 0;
    });
    fpsTimer_->start();

    refreshGroupCombo();
    updateUiState();
    log(QStringLiteral("tiray_host_gui 启动。只调用 tiray_sdk 公共 API；研发自有协议栈请用 pa_host。"));
}

MainWindow::~MainWindow() {
    if (workerThread_ != nullptr && workerThread_->isRunning()) {
        workerThread_->quit();
        workerThread_->wait(2000);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    statusTimer_->stop();
    calTimer_->stop();
    displayTimer_->stop();
    fpsTimer_->stop();

    if (pcieMonitor_->isRunning()) {
        pcieMonitor_->doStop();
    }
    if (connected_) {
        /* 阻塞式断开，确保串口在退出前关闭。 */
        QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doClose(); },
                                  Qt::BlockingQueuedConnection);
        connected_ = false;
    }
    workerThread_->quit();
    workerThread_->wait(2000);
    event->accept();
}

/* ------------------------- 界面搭建 ------------------------- */

QWidget* MainWindow::createTopBar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);

    portEdit_ = new QLineEdit(QStringLiteral("/dev/ttyWCH0"), bar);
    portEdit_->setMinimumWidth(160);
    baudSpin_ = new QSpinBox(bar);
    baudSpin_->setRange(9600, 921600);
    baudSpin_->setValue(115200);
    timeoutSpin_ = new QSpinBox(bar);
    timeoutSpin_->setRange(50, 60000);
    timeoutSpin_->setSingleStep(50);
    timeoutSpin_->setValue(500);
    timeoutSpin_->setSuffix(QStringLiteral(" ms"));
    internalCheck_ = new QCheckBox(QStringLiteral("内部版配置"), bar);
    internalCheck_->setToolTip(QStringLiteral(
        "使用 TIRAY_SDK_PROFILE_INTERNAL（需以 -DTIRAY_SDK_INTERNAL_BUILD=ON 构建 SDK），"
        "可读写组 2~4；外部版仅组 1/5。"));
    connect(internalCheck_, &QCheckBox::toggled, this, [this] { refreshGroupCombo(); });

    connectButton_ = new QPushButton(QStringLiteral("连接"), bar);
    disconnectButton_ = new QPushButton(QStringLiteral("断开"), bar);
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::connectDevice);
    connect(disconnectButton_, &QPushButton::clicked, this, &MainWindow::disconnectDevice);

    connectionLabel_ = new QLabel(QStringLiteral("未连接"), bar);

    layout->addWidget(new QLabel(QStringLiteral("串口:"), bar));
    layout->addWidget(portEdit_);
    layout->addWidget(new QLabel(QStringLiteral("波特率:"), bar));
    layout->addWidget(baudSpin_);
    layout->addWidget(new QLabel(QStringLiteral("响应超时:"), bar));
    layout->addWidget(timeoutSpin_);
    layout->addWidget(internalCheck_);
    layout->addWidget(connectButton_);
    layout->addWidget(disconnectButton_);
    layout->addWidget(connectionLabel_);
    layout->addStretch(1);
    return bar;
}

QWidget* MainWindow::createCenterArea() {
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    /* 左侧：图像显示 */
    auto* imageArea = new QWidget(splitter);
    auto* imageLayout = new QVBoxLayout(imageArea);
    imageLayout->setContentsMargins(0, 0, 0, 0);

    auto* imageToolbar = new QHBoxLayout();
    auto* fitButton = new QPushButton(QStringLiteral("适配"), imageArea);
    auto* actualButton = new QPushButton(QStringLiteral("1:1"), imageArea);
    auto* zoomInButton = new QPushButton(QStringLiteral("放大"), imageArea);
    auto* zoomOutButton = new QPushButton(QStringLiteral("缩小"), imageArea);
    savePngButton_ = new QPushButton(QStringLiteral("保存 PNG"), imageArea);
    saveRawButton_ = new QPushButton(QStringLiteral("保存原始帧(.raw)"), imageArea);
    auto* clearButton = new QPushButton(QStringLiteral("清屏"), imageArea);
    connect(savePngButton_, &QPushButton::clicked, this, &MainWindow::saveDisplayImage);
    connect(saveRawButton_, &QPushButton::clicked, this, &MainWindow::saveLatestRaw);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearImageDisplay);

    imageView_ = new ImageView(imageArea);
    connect(fitButton, &QPushButton::clicked, imageView_, &ImageView::fitToWindow);
    connect(actualButton, &QPushButton::clicked, imageView_, &ImageView::zoomActualSize);
    connect(zoomInButton, &QPushButton::clicked, imageView_, &ImageView::zoomIn);
    connect(zoomOutButton, &QPushButton::clicked, imageView_, &ImageView::zoomOut);
    connect(imageView_, &ImageView::pixelHovered, this, &MainWindow::updatePixelInfo);
    connect(imageView_, &ImageView::roiSelected, this, &MainWindow::updateRoiInfo);

    imageToolbar->addWidget(fitButton);
    imageToolbar->addWidget(actualButton);
    imageToolbar->addWidget(zoomInButton);
    imageToolbar->addWidget(zoomOutButton);
    imageToolbar->addWidget(savePngButton_);
    imageToolbar->addWidget(saveRawButton_);
    imageToolbar->addWidget(clearButton);
    imageToolbar->addStretch(1);

    frameInfoLabel_ = createValueLabel(QStringLiteral("暂无图像；请在“图像/PCIe”页启动帧监听"));
    roiInfoLabel_ = createValueLabel(QStringLiteral("右键拖动框选可计算区域统计"));

    imageLayout->addLayout(imageToolbar);
    imageLayout->addWidget(imageView_, 1);
    imageLayout->addWidget(frameInfoLabel_);
    imageLayout->addWidget(roiInfoLabel_);

    /* 右侧：控制页 */
    auto* tabs = new QTabWidget(splitter);
    tabs->addTab(createDeviceTab(), QStringLiteral("设备"));
    tabs->addTab(createConfigTab(), QStringLiteral("配置"));
    tabs->addTab(createCalibrationTab(), QStringLiteral("校准"));
    tabs->addTab(createImageTab(), QStringLiteral("图像/PCIe"));

    splitter->addWidget(imageArea);
    splitter->addWidget(tabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 0);
    return splitter;
}

QWidget* MainWindow::createDeviceTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* basicGroup = new QGroupBox(QStringLiteral("基础"), tab);
    auto* basicLayout = new QFormLayout(basicGroup);
    pingButton_ = new QPushButton(QStringLiteral("PING"), basicGroup);
    connect(pingButton_, &QPushButton::clicked, this, &MainWindow::pingDevice);
    auto* statusRow = new QHBoxLayout();
    auto* statusButton = new QPushButton(QStringLiteral("读取状态"), basicGroup);
    connect(statusButton, &QPushButton::clicked, this, &MainWindow::requestStatus);
    autoStatusCheck_ = new QCheckBox(QStringLiteral("自动轮询"), basicGroup);
    statusIntervalSpin_ = new QSpinBox(basicGroup);
    statusIntervalSpin_->setRange(200, 10000);
    statusIntervalSpin_->setSingleStep(100);
    statusIntervalSpin_->setValue(1000);
    statusIntervalSpin_->setSuffix(QStringLiteral(" ms"));
    connect(autoStatusCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && connected_) {
            statusTimer_->start(statusIntervalSpin_->value());
        } else {
            statusTimer_->stop();
        }
    });
    connect(statusIntervalSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (autoStatusCheck_->isChecked() && connected_) {
            statusTimer_->start(value);
        }
    });
    statusRow->addWidget(statusButton);
    statusRow->addWidget(autoStatusCheck_);
    statusRow->addWidget(statusIntervalSpin_);
    statusRow->addStretch(1);
    basicLayout->addRow(QStringLiteral("通信:"), pingButton_);
    basicLayout->addRow(QStringLiteral("状态:"), statusRow);
    basicLayout->addRow(QStringLiteral("工作模式:"), statusModeLabel_ = createValueLabel());
    basicLayout->addRow(QStringLiteral("工作状态:"), statusStateLabel_ = createValueLabel());
    basicLayout->addRow(QStringLiteral("错误:"), statusErrorLabel_ = createValueLabel());
    basicLayout->addRow(QStringLiteral("帧计数:"), statusFrameLabel_ = createValueLabel());
    basicLayout->addRow(QStringLiteral("地址:"), statusAddrLabel_ = createValueLabel());
    basicLayout->addRow(QStringLiteral("校正:"), statusCorrectionLabel_ = createValueLabel());

    auto* captureGroup = new QGroupBox(QStringLiteral("采集"), tab);
    auto* captureLayout = new QVBoxLayout(captureGroup);
    staticButton_ = new QPushButton(QStringLiteral("触发静态采集"), captureGroup);
    connect(staticButton_, &QPushButton::clicked, this, &MainWindow::requestStaticCapture);
    auto* dynamicRow = new QHBoxLayout();
    dynamicStartButton_ = new QPushButton(QStringLiteral("Dynamic 启动"), captureGroup);
    dynamicQueryButton_ = new QPushButton(QStringLiteral("查询"), captureGroup);
    dynamicStopButton_ = new QPushButton(QStringLiteral("停止"), captureGroup);
    connect(dynamicStartButton_, &QPushButton::clicked, this, &MainWindow::requestDynamicStart);
    connect(dynamicQueryButton_, &QPushButton::clicked, this, &MainWindow::requestDynamicQuery);
    connect(dynamicStopButton_, &QPushButton::clicked, this, &MainWindow::requestDynamicStop);
    dynamicRow->addWidget(dynamicStartButton_);
    dynamicRow->addWidget(dynamicQueryButton_);
    dynamicRow->addWidget(dynamicStopButton_);
    dynamicStatusLabel_ = createValueLabel();
    captureLayout->addWidget(staticButton_);
    captureLayout->addLayout(dynamicRow);
    captureLayout->addWidget(dynamicStatusLabel_);

    auto* systemGroup = new QGroupBox(QStringLiteral("系统"), tab);
    auto* systemLayout = new QVBoxLayout(systemGroup);
    rebootButton_ = new QPushButton(QStringLiteral("重启设备"), systemGroup);
    connect(rebootButton_, &QPushButton::clicked, this, &MainWindow::requestReboot);
    systemLayout->addWidget(rebootButton_);
    systemLayout->addStretch(1);

    layout->addWidget(basicGroup);
    layout->addWidget(captureGroup);
    layout->addWidget(systemGroup);
    layout->addStretch(1);

    serialButtons_ = {pingButton_, statusButton, staticButton_, dynamicStartButton_,
                      dynamicQueryButton_, dynamicStopButton_, rebootButton_};
    return tab;
}

QWidget* MainWindow::createConfigTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* groupRow = new QHBoxLayout();
    groupCombo_ = new QComboBox(tab);
    readGroupButton_ = new QPushButton(QStringLiteral("读取配置组"), tab);
    sendGroupButton_ = new QPushButton(QStringLiteral("下发配置组"), tab);
    connect(readGroupButton_, &QPushButton::clicked, this, &MainWindow::requestReadConfigGroup);
    connect(sendGroupButton_, &QPushButton::clicked, this, &MainWindow::requestSendConfigGroup);
    groupRow->addWidget(groupCombo_, 1);
    groupRow->addWidget(readGroupButton_);
    groupRow->addWidget(sendGroupButton_);
    layout->addLayout(groupRow);

    configTable_ = new QTableWidget(0, 2, tab);
    configTable_->setHorizontalHeaderLabels({QStringLiteral("配置项 ID"), QStringLiteral("值")});
    configTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(configTable_, 1);

    auto* tableButtons = new QHBoxLayout();
    auto* addRowButton = new QPushButton(QStringLiteral("增加行"), tab);
    auto* removeRowButton = new QPushButton(QStringLiteral("删除选中行"), tab);
    connect(addRowButton, &QPushButton::clicked, this, [this] {
        const int row = configTable_->rowCount();
        configTable_->insertRow(row);
        configTable_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0x")));
        configTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0x")));
    });
    connect(removeRowButton, &QPushButton::clicked, this, [this] {
        const int row = configTable_->currentRow();
        if (row >= 0) {
            configTable_->removeRow(row);
        }
    });
    tableButtons->addWidget(addRowButton);
    tableButtons->addWidget(removeRowButton);
    tableButtons->addStretch(1);
    layout->addLayout(tableButtons);

    auto* staticGroup = new QGroupBox(QStringLiteral("Static 配置（组 1）"), tab);
    auto* staticForm = new QFormLayout(staticGroup);
    staticIdleSpin_ = new QSpinBox(staticGroup);
    staticExposureSpin_ = new QSpinBox(staticGroup);
    staticDarkSpin_ = new QSpinBox(staticGroup);
    for (QSpinBox* spin : {staticIdleSpin_, staticExposureSpin_, staticDarkSpin_}) {
        spin->setRange(0, 2147483647);
        spin->setSuffix(QStringLiteral(" ms"));
    }
    staticGetButton_ = new QPushButton(QStringLiteral("读取"), staticGroup);
    staticSetButton_ = new QPushButton(QStringLiteral("下发"), staticGroup);
    connect(staticGetButton_, &QPushButton::clicked, this, &MainWindow::requestGetStaticConfig);
    connect(staticSetButton_, &QPushButton::clicked, this, &MainWindow::requestSetStaticConfig);
    auto* staticButtons = new QHBoxLayout();
    staticButtons->addWidget(staticGetButton_);
    staticButtons->addWidget(staticSetButton_);
    staticButtons->addStretch(1);
    staticForm->addRow(QStringLiteral("自清空周期:"), staticIdleSpin_);
    staticForm->addRow(QStringLiteral("曝光窗口:"), staticExposureSpin_);
    staticForm->addRow(QStringLiteral("暗窗口:"), staticDarkSpin_);
    staticForm->addRow(QString(), staticButtons);
    layout->addWidget(staticGroup);

    auto* dynamicGroup = new QGroupBox(QStringLiteral("Dynamic 配置（组 5）"), tab);
    auto* dynamicForm = new QFormLayout(dynamicGroup);
    dynamicCycleSpin_ = new QSpinBox(dynamicGroup);
    dynamicCycleSpin_->setRange(1, 2147483647);
    dynamicCycleSpin_->setValue(1);
    dynamicStartAddrEdit_ = new QLineEdit(dynamicGroup);
    dynamicEndAddrEdit_ = new QLineEdit(dynamicGroup);
    dynamicStartAddrEdit_->setPlaceholderText(QStringLiteral("如 0x26a00000"));
    dynamicEndAddrEdit_->setPlaceholderText(QStringLiteral("如 0x27000000"));
    dynamicStartTimeoutSpin_ = new QSpinBox(dynamicGroup);
    dynamicPollSpin_ = new QSpinBox(dynamicGroup);
    dynamicStopTimeoutSpin_ = new QSpinBox(dynamicGroup);
    for (QSpinBox* spin : {dynamicStartTimeoutSpin_, dynamicPollSpin_, dynamicStopTimeoutSpin_}) {
        spin->setRange(0, 2147483647);
        spin->setSuffix(QStringLiteral(" ms"));
    }
    dynamicStartTimeoutSpin_->setValue(5000);
    dynamicPollSpin_->setValue(50);
    dynamicStopTimeoutSpin_->setValue(5000);
    dynamicGetButton_ = new QPushButton(QStringLiteral("读取"), dynamicGroup);
    dynamicSetButton_ = new QPushButton(QStringLiteral("下发"), dynamicGroup);
    connect(dynamicGetButton_, &QPushButton::clicked, this, &MainWindow::requestGetDynamicConfig);
    connect(dynamicSetButton_, &QPushButton::clicked, this, &MainWindow::requestSetDynamicConfig);
    auto* dynamicButtons = new QHBoxLayout();
    dynamicButtons->addWidget(dynamicGetButton_);
    dynamicButtons->addWidget(dynamicSetButton_);
    dynamicButtons->addStretch(1);
    dynamicForm->addRow(QStringLiteral("cycle(帧数):"), dynamicCycleSpin_);
    dynamicForm->addRow(QStringLiteral("图像起始地址:"), dynamicStartAddrEdit_);
    dynamicForm->addRow(QStringLiteral("图像结束地址:"), dynamicEndAddrEdit_);
    dynamicForm->addRow(QStringLiteral("启动超时:"), dynamicStartTimeoutSpin_);
    dynamicForm->addRow(QStringLiteral("轮询间隔:"), dynamicPollSpin_);
    dynamicForm->addRow(QStringLiteral("停止超时:"), dynamicStopTimeoutSpin_);
    dynamicForm->addRow(QStringLiteral("Step 高/低电平:"),
                        new QLabel(QStringLiteral("请在上方配置组表格中读写 0x21xx"), dynamicGroup));
    dynamicForm->addRow(QString(), dynamicButtons);
    layout->addWidget(dynamicGroup);

    serialButtons_.append({readGroupButton_, sendGroupButton_, staticGetButton_,
                           staticSetButton_, dynamicGetButton_, dynamicSetButton_});
    return tab;
}

QWidget* MainWindow::createCalibrationTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* offsetGroup = new QGroupBox(QStringLiteral("暗场模板（offset）"), tab);
    auto* offsetForm = new QFormLayout(offsetGroup);
    offsetTotalSpin_ = new QSpinBox(offsetGroup);
    offsetTotalSpin_->setRange(1, 4096);
    offsetTotalSpin_->setValue(16);
    offsetValidSpin_ = new QSpinBox(offsetGroup);
    offsetValidSpin_->setRange(1, 4096);
    offsetValidSpin_->setValue(16);
    offsetModeCombo_ = new QComboBox(offsetGroup);
    offsetModeCombo_->addItem(QStringLiteral("0 - 静态"), 0);
    offsetModeCombo_->addItem(QStringLiteral("1 - 动态"), 1);
    offsetBeginButton_ = new QPushButton(QStringLiteral("begin"), offsetGroup);
    offsetCaptureButton_ = new QPushButton(QStringLiteral("capture"), offsetGroup);
    offsetBuildButton_ = new QPushButton(QStringLiteral("build"), offsetGroup);
    offsetCancelButton_ = new QPushButton(QStringLiteral("cancel"), offsetGroup);
    connect(offsetBeginButton_, &QPushButton::clicked, this, &MainWindow::requestOffsetBegin);
    connect(offsetCaptureButton_, &QPushButton::clicked, this, &MainWindow::requestOffsetCapture);
    connect(offsetBuildButton_, &QPushButton::clicked, this, &MainWindow::requestOffsetBuild);
    connect(offsetCancelButton_, &QPushButton::clicked, this, &MainWindow::requestOffsetCancel);
    auto* offsetButtons = new QHBoxLayout();
    offsetButtons->addWidget(offsetBeginButton_);
    offsetButtons->addWidget(offsetCaptureButton_);
    offsetButtons->addWidget(offsetBuildButton_);
    offsetButtons->addWidget(offsetCancelButton_);
    offsetForm->addRow(QStringLiteral("总帧数:"), offsetTotalSpin_);
    offsetForm->addRow(QStringLiteral("有效帧数:"), offsetValidSpin_);
    offsetForm->addRow(QStringLiteral("模式:"), offsetModeCombo_);
    offsetForm->addRow(QString(), offsetButtons);
    layout->addWidget(offsetGroup);

    auto* gainGroup = new QGroupBox(QStringLiteral("亮场模板（gain）"), tab);
    auto* gainForm = new QFormLayout(gainGroup);
    gainLevelsEdit_ = new QLineEdit(QStringLiteral("5000,10000,20000"), gainGroup);
    gainFramesSpin_ = new QSpinBox(gainGroup);
    gainFramesSpin_->setRange(1, 1024);
    gainFramesSpin_->setValue(2);
    gainThresholdSpin_ = new QDoubleSpinBox(gainGroup);
    gainThresholdSpin_->setRange(0.0, 10.0);
    gainThresholdSpin_->setDecimals(3);
    gainThresholdSpin_->setSingleStep(0.05);
    gainThresholdSpin_->setValue(0.3);
    gainBeginButton_ = new QPushButton(QStringLiteral("begin"), gainGroup);
    gainLevelSpin_ = new QSpinBox(gainGroup);
    gainLevelSpin_->setRange(0, 2147483647);
    gainLevelSpin_->setValue(5000);
    gainCaptureButton_ = new QPushButton(QStringLiteral("capture 该灰度级"), gainGroup);
    gainBuildButton_ = new QPushButton(QStringLiteral("build"), gainGroup);
    gainCancelButton_ = new QPushButton(QStringLiteral("cancel"), gainGroup);
    connect(gainBeginButton_, &QPushButton::clicked, this, &MainWindow::requestGainBegin);
    connect(gainCaptureButton_, &QPushButton::clicked, this, &MainWindow::requestGainCapture);
    connect(gainBuildButton_, &QPushButton::clicked, this, &MainWindow::requestGainBuild);
    connect(gainCancelButton_, &QPushButton::clicked, this, &MainWindow::requestGainCancel);
    auto* gainRow1 = new QHBoxLayout();
    gainRow1->addWidget(gainBeginButton_);
    gainRow1->addWidget(gainBuildButton_);
    gainRow1->addWidget(gainCancelButton_);
    auto* gainRow2 = new QHBoxLayout();
    gainRow2->addWidget(gainLevelSpin_);
    gainRow2->addWidget(gainCaptureButton_);
    gainForm->addRow(QStringLiteral("灰度级(逗号分隔):"), gainLevelsEdit_);
    gainForm->addRow(QStringLiteral("每级帧数:"), gainFramesSpin_);
    gainForm->addRow(QStringLiteral("坏点阈值:"), gainThresholdSpin_);
    gainForm->addRow(QString(), gainRow1);
    gainForm->addRow(QStringLiteral("灰度级采集:"), gainRow2);
    layout->addWidget(gainGroup);

    auto* calStatusGroup = new QGroupBox(QStringLiteral("模板任务状态"), tab);
    auto* calStatusLayout = new QVBoxLayout(calStatusGroup);
    auto* calRow = new QHBoxLayout();
    auto* calStatusButton = new QPushButton(QStringLiteral("查询 cal_status"), calStatusGroup);
    connect(calStatusButton, &QPushButton::clicked, this, &MainWindow::requestCalStatus);
    autoCalCheck_ = new QCheckBox(QStringLiteral("自动轮询"), calStatusGroup);
    connect(autoCalCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && connected_) {
            calTimer_->start(800);
        } else {
            calTimer_->stop();
        }
    });
    calRow->addWidget(calStatusButton);
    calRow->addWidget(autoCalCheck_);
    calRow->addStretch(1);
    calStateLabel_ = createValueLabel();
    calStateLabel_->setWordWrap(true);
    calStatusLayout->addLayout(calRow);
    calStatusLayout->addWidget(calStateLabel_);
    layout->addWidget(calStatusGroup);

    auto* uploadGroup = new QGroupBox(QStringLiteral("模板上传"), tab);
    auto* uploadForm = new QFormLayout(uploadGroup);
    uploadKindCombo_ = new QComboBox(uploadGroup);
    uploadKindCombo_->addItem(QStringLiteral("0 - 暗场模板"), 0);
    uploadKindCombo_->addItem(QStringLiteral("1 - 亮场模板"), 1);
    uploadAddrEdit_ = new QLineEdit(uploadGroup);
    uploadAddrEdit_->setPlaceholderText(QStringLiteral("如 0x20000000"));
    uploadRowsSpin_ = new QSpinBox(uploadGroup);
    uploadRowsSpin_->setRange(1, 65535);
    uploadRowsSpin_->setValue(7680);
    uploadColsSpin_ = new QSpinBox(uploadGroup);
    uploadColsSpin_->setRange(1, 65535);
    uploadColsSpin_->setValue(3072);
    uploadPkgsSpin_ = new QSpinBox(uploadGroup);
    uploadPkgsSpin_->setRange(1, 65535);
    uploadPkgsSpin_->setValue(1);
    uploadConfigButton_ = new QPushButton(QStringLiteral("config"), uploadGroup);
    uploadStartButton_ = new QPushButton(QStringLiteral("start"), uploadGroup);
    uploadQueryButton_ = new QPushButton(QStringLiteral("query"), uploadGroup);
    connect(uploadConfigButton_, &QPushButton::clicked, this, &MainWindow::requestUploadConfig);
    connect(uploadStartButton_, &QPushButton::clicked, this, &MainWindow::requestUploadStart);
    connect(uploadQueryButton_, &QPushButton::clicked, this, &MainWindow::requestUploadQuery);
    auto* uploadButtons = new QHBoxLayout();
    uploadButtons->addWidget(uploadConfigButton_);
    uploadButtons->addWidget(uploadStartButton_);
    uploadButtons->addWidget(uploadQueryButton_);
    uploadStateLabel_ = createValueLabel();
    uploadForm->addRow(QStringLiteral("类型:"), uploadKindCombo_);
    uploadForm->addRow(QStringLiteral("模板地址:"), uploadAddrEdit_);
    uploadForm->addRow(QStringLiteral("行数:"), uploadRowsSpin_);
    uploadForm->addRow(QStringLiteral("列数:"), uploadColsSpin_);
    uploadForm->addRow(QStringLiteral("包数:"), uploadPkgsSpin_);
    uploadForm->addRow(QString(), uploadButtons);
    uploadForm->addRow(QStringLiteral("状态:"), uploadStateLabel_);
    layout->addWidget(uploadGroup);

    layout->addStretch(1);
    serialButtons_.append({calStatusButton, offsetBeginButton_, offsetCaptureButton_,
                           offsetBuildButton_, offsetCancelButton_, gainBeginButton_,
                           gainCaptureButton_, gainBuildButton_, gainCancelButton_,
                           uploadConfigButton_, uploadStartButton_, uploadQueryButton_});
    return tab;
}

QWidget* MainWindow::createImageTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);

    auto* pcieGroup = new QGroupBox(QStringLiteral("PCIe 帧接收（SDK 接口）"), tab);
    auto* pcieForm = new QFormLayout(pcieGroup);
    pcieEventEdit_ = new QLineEdit(pcieGroup);
    pcieEventEdit_->setPlaceholderText(QStringLiteral("默认 /dev/idma0_event_0"));
    pcieC2hEdit_ = new QLineEdit(pcieGroup);
    pcieC2hEdit_->setPlaceholderText(QStringLiteral("默认 /dev/idma0_c2h_0"));
    pcieBar0Edit_ = new QLineEdit(pcieGroup);
    pcieBar0Edit_->setPlaceholderText(QStringLiteral("默认自动发现 vendor 0x1b4d / device 0x6667"));
    pcieStartButton_ = new QPushButton(QStringLiteral("启动帧监听"), pcieGroup);
    pcieStopButton_ = new QPushButton(QStringLiteral("停止"), pcieGroup);
    connect(pcieStartButton_, &QPushButton::clicked, this, &MainWindow::startPcie);
    connect(pcieStopButton_, &QPushButton::clicked, this, &MainWindow::stopPcie);
    auto* pcieButtons = new QHBoxLayout();
    pcieButtons->addWidget(pcieStartButton_);
    pcieButtons->addWidget(pcieStopButton_);
    pcieButtons->addStretch(1);
    pcieStatsLabel_ = createValueLabel(QStringLiteral("未接收"));
    pcieForm->addRow(QStringLiteral("event 设备:"), pcieEventEdit_);
    pcieForm->addRow(QStringLiteral("c2h 设备:"), pcieC2hEdit_);
    pcieForm->addRow(QStringLiteral("BAR0 resource:"), pcieBar0Edit_);
    pcieForm->addRow(QString(), pcieButtons);
    pcieForm->addRow(QStringLiteral("统计:"), pcieStatsLabel_);
    layout->addWidget(pcieGroup);

    auto* displayGroup = new QGroupBox(QStringLiteral("显示"), tab);
    auto* displayForm = new QFormLayout(displayGroup);
    autoWindowCheck_ = new QCheckBox(QStringLiteral("自动窗宽窗位（1%~99% 分位）"), displayGroup);
    autoWindowCheck_->setChecked(true);
    connect(autoWindowCheck_, &QCheckBox::toggled, this, [this] {
        displayDirty_ = true;
        updateWindowControls();
    });
    windowCenterSlider_ = new QSlider(Qt::Horizontal, displayGroup);
    windowCenterSlider_->setRange(0, 65535);
    windowCenterSlider_->setValue(32768);
    windowWidthSlider_ = new QSlider(Qt::Horizontal, displayGroup);
    windowWidthSlider_->setRange(1, 65535);
    windowWidthSlider_->setValue(65535);
    connect(windowCenterSlider_, &QSlider::valueChanged, this, [this] {
        if (!autoWindowCheck_->isChecked()) {
            displayDirty_ = true;
        }
    });
    connect(windowWidthSlider_, &QSlider::valueChanged, this, [this] {
        if (!autoWindowCheck_->isChecked()) {
            displayDirty_ = true;
        }
    });
    displayWidthCombo_ = new QComboBox(displayGroup);
    displayWidthCombo_->addItem(QStringLiteral("2048（默认）"), 2048);
    displayWidthCombo_->addItem(QStringLiteral("1024"), 1024);
    displayWidthCombo_->addItem(QStringLiteral("4096"), 4096);
    displayWidthCombo_->addItem(QStringLiteral("全分辨率"), 0);
    connect(displayWidthCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { displayDirty_ = true; });
    displayForm->addRow(QString(), autoWindowCheck_);
    displayForm->addRow(QStringLiteral("窗位:"), windowCenterSlider_);
    displayForm->addRow(QStringLiteral("窗宽:"), windowWidthSlider_);
    displayForm->addRow(QStringLiteral("显示最大宽度:"), displayWidthCombo_);
    layout->addWidget(displayGroup);

    layout->addStretch(1);
    updateWindowControls();
    return tab;
}

void MainWindow::createLogDock() {
    logDock_ = new QDockWidget(QStringLiteral("日志"), this);
    logView_ = new QPlainTextEdit(logDock_);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(5000);
    logDock_->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
}

void MainWindow::createStatusBar() {
    busyLabel_ = new QLabel(QStringLiteral("空闲"), this);
    statusBar()->addPermanentWidget(busyLabel_);
    statusBar()->showMessage(QStringLiteral("就绪"));
}

/* ------------------------- 工具函数 ------------------------- */

void MainWindow::log(const QString& message) {
    logView_->appendPlainText(
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")), message));
}

void MainWindow::logResult(bool ok, const QString& message) {
    log(QStringLiteral("%1 %2").arg(ok ? QStringLiteral("[PASS]") : QStringLiteral("[FAIL]"), message));
}

void MainWindow::changeBusy(int delta) {
    busyCount_ = std::max(0, busyCount_ + delta);
    updateUiState();
}

void MainWindow::updateUiState() {
    const bool serialIdle = connected_ && busyCount_ == 0;
    for (QPushButton* button : serialButtons_) {
        if (button != nullptr) {
            button->setEnabled(serialIdle);
        }
    }
    connectButton_->setEnabled(!connected_ && busyCount_ == 0);
    disconnectButton_->setEnabled(connected_ && busyCount_ == 0);
    portEdit_->setEnabled(!connected_);
    baudSpin_->setEnabled(!connected_);
    timeoutSpin_->setEnabled(!connected_);
    internalCheck_->setEnabled(!connected_);
    connectionLabel_->setText(connected_ ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    busyLabel_->setText(busyCount_ > 0 ? QStringLiteral("执行中…") : QStringLiteral("空闲"));

    if (!(autoStatusCheck_->isChecked() && connected_)) {
        statusTimer_->stop();
    }
    if (!(autoCalCheck_->isChecked() && connected_)) {
        calTimer_->stop();
    }
}

void MainWindow::refreshGroupCombo() {
    const int previous = groupCombo_->currentData().toInt();
    groupCombo_->clear();
    groupCombo_->addItem(QStringLiteral("1 - Static Idle"), 1);
    groupCombo_->addItem(QStringLiteral("5 - Dynamic"), 5);
    if (internalCheck_ != nullptr && internalCheck_->isChecked()) {
        groupCombo_->addItem(QStringLiteral("2 - Correction（内部）"), 2);
        groupCombo_->addItem(QStringLiteral("3 - GIC（内部）"), 3);
        groupCombo_->addItem(QStringLiteral("4 - ROIC（内部）"), 4);
    }
    const int index = groupCombo_->findData(previous);
    groupCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void MainWindow::updateWindowControls() {
    const bool manual = !autoWindowCheck_->isChecked();
    windowCenterSlider_->setEnabled(manual);
    windowWidthSlider_->setEnabled(manual);
}

/* ------------------------- SdkWorker 结果 ------------------------- */

void MainWindow::handleOperationFinished(QString operation, bool ok, QString message) {
    changeBusy(-1);
    logResult(ok, message);
    if (operation == QLatin1String("open")) {
        connected_ = ok;
        if (ok) {
            log(QStringLiteral("已连接 %1").arg(portEdit_->text()));
            if (autoStatusCheck_->isChecked()) {
                statusTimer_->start(statusIntervalSpin_->value());
            }
        }
        updateUiState();
    } else if (operation == QLatin1String("close")) {
        connected_ = false;
        statusTimer_->stop();
        statusModeLabel_->setText(QStringLiteral("-"));
        statusStateLabel_->setText(QStringLiteral("-"));
        statusErrorLabel_->setText(QStringLiteral("-"));
        statusFrameLabel_->setText(QStringLiteral("-"));
        statusAddrLabel_->setText(QStringLiteral("-"));
        statusCorrectionLabel_->setText(QStringLiteral("-"));
        updateUiState();
    }
}

void MainWindow::handleStatusReady(tiray_device_status_t status, quint32 lastDeviceError) {
    statusModeLabel_->setText(QStringLiteral("%1 (%2)")
        .arg(status.work_mode).arg(workModeName(status.work_mode)));
    statusStateLabel_->setText(QStringLiteral("%1 (%2)")
        .arg(status.work_state).arg(workStateName(status.work_state)));
    QString errorText = hex32(status.last_error);
    if (lastDeviceError != 0) {
        errorText += QStringLiteral("（ERROR 帧 TLV 0x0002: %1）").arg(hex32(lastDeviceError));
    }
    statusErrorLabel_->setText(errorText);
    statusFrameLabel_->setText(QStringLiteral("capture_id=%1 frame_count=%2")
        .arg(status.capture_id).arg(status.frame_count));
    statusAddrLabel_->setText(QStringLiteral("output=%1 offset=%2")
        .arg(hex32(status.output_addr), hex32(status.offset_addr)));
    statusCorrectionLabel_->setText(
        QStringLiteral("write=%1/%2 correction=%3/%4")
            .arg(status.write_state).arg(status.write_end)
            .arg(status.correction_state).arg(status.correction_end));
}

void MainWindow::handleDynamicStatusReady(tiray_dynamic_status_t status) {
    dynamicStatusLabel_->setText(
        QStringLiteral("state=%1 (%2) end=%3 debug=%4 addr=%5 frames=%6")
            .arg(status.state)
            .arg(workStateName(status.state))
            .arg(status.end)
            .arg(hex32(status.debug_out))
            .arg(hex32(status.final_image_addr))
            .arg(status.frame_count));
}

void MainWindow::handleConfigGroupReady(quint16 group, QVector<tiray_config_item_t> items) {
    Q_UNUSED(group);
    configTable_->setRowCount(0);
    for (const tiray_config_item_t& item : items) {
        const int row = configTable_->rowCount();
        configTable_->insertRow(row);
        auto* idItem = new QTableWidgetItem(hex32(item.item_id));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        configTable_->setItem(row, 0, idItem);
        configTable_->setItem(row, 1, new QTableWidgetItem(hex32(item.value)));
    }
}

void MainWindow::handleStaticConfigReady(tiray_static_config_t config) {
    staticIdleSpin_->setValue(static_cast<int>(config.idle_clean_interval_ms));
    staticExposureSpin_->setValue(static_cast<int>(config.exposure_window_ms));
    staticDarkSpin_->setValue(static_cast<int>(config.dark_window_ms));
}

void MainWindow::handleDynamicConfigReady(tiray_dynamic_config_t config) {
    dynamicCycleSpin_->setValue(static_cast<int>(config.cycle));
    dynamicStartAddrEdit_->setText(hex32(config.image_start_addr));
    dynamicEndAddrEdit_->setText(hex32(config.image_end_addr));
    dynamicStartTimeoutSpin_->setValue(static_cast<int>(config.start_timeout_ms));
    dynamicPollSpin_->setValue(static_cast<int>(config.state_poll_interval_ms));
    dynamicStopTimeoutSpin_->setValue(static_cast<int>(config.stop_timeout_ms));
}

void MainWindow::handleCalStatusReady(tiray_cal_status_t status) {
    calStateLabel_->setText(QStringLiteral(
        "task_id=%1 kind=%2 state=%3 (%4) last_error=%5\n"
        "progress=%6/%7 gain_level=%8 level_count=%9 levels_ready=%10\n"
        "frames_per_level=%11 defect_threshold=%12 bad_pixel_count=%13")
        .arg(status.task_id).arg(status.task_kind)
        .arg(status.task_state).arg(calStateName(status.task_state))
        .arg(hex32(status.last_error))
        .arg(status.progress_current).arg(status.progress_total)
        .arg(status.gain_level).arg(status.level_count).arg(status.levels_ready)
        .arg(status.frames_per_level)
        .arg(static_cast<double>(status.defect_threshold), 0, 'f', 3)
        .arg(status.bad_pixel_count));

    const bool finished = status.task_state != 1 && status.task_state != 2;
    if (finished && autoCalCheck_->isChecked()) {
        autoCalCheck_->setChecked(false);
        log(QStringLiteral("模板任务结束（状态 %1），自动轮询已停止。").arg(calStateName(status.task_state)));
    }
}

void MainWindow::handleUploadStatusReady(tiray_image_upload_status_t status) {
    uploadStateLabel_->setText(QStringLiteral("state=%1 end=%2 debug=%3")
        .arg(status.state).arg(status.end).arg(hex32(status.debug_out)));
}

/* ------------------------- PcieMonitor 结果 ------------------------- */

void MainWindow::handlePcieStarted(bool ok, QString message) {
    logResult(ok, message);
    pcieStartButton_->setEnabled(!ok);
    pcieStopButton_->setEnabled(ok);
    if (ok) {
        totalFrameCount_ = 0;
        fpsWindowCount_ = 0;
    }
}

void MainWindow::handlePcieStopped() {
    log(QStringLiteral("PCIe 帧监听已停止"));
    pcieStartButton_->setEnabled(true);
    pcieStopButton_->setEnabled(false);
    pcieStatsLabel_->setText(QStringLiteral("已停止，累计接收 %1 帧").arg(totalFrameCount_));
}

void MainWindow::handleFrameReceived(QSharedPointer<PcieFrame> frame) {
    if (frame.isNull()) {
        return;
    }
    latestFrame_ = frame;
    ++totalFrameCount_;
    ++fpsWindowCount_;
    autoWindowValid_ = false;
    displayDirty_ = true;
    pcieStatsLabel_->setText(QStringLiteral("累计 %1 帧；最新 %2×%3 id=%4 type=%5(%6) addr=%7")
        .arg(totalFrameCount_)
        .arg(frame->rows).arg(frame->columns)
        .arg(frame->imageId)
        .arg(frame->imageType)
        .arg(frame->imageType == 1 ? QStringLiteral("模板上传") : QStringLiteral("正常图像"))
        .arg(hex32(static_cast<quint32>(frame->finalAddress))));
}

/* ------------------------- 连接与设备命令 ------------------------- */

void MainWindow::connectDevice() {
    const QString port = portEdit_->text().trimmed();
    if (port.isEmpty()) {
        log(QStringLiteral("[FAIL] 请先填写串口路径"));
        return;
    }
    const quint32 baud = static_cast<quint32>(baudSpin_->value());
    const quint32 timeout = static_cast<quint32>(timeoutSpin_->value());
    const bool internal = internalCheck_->isChecked();
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, port, baud, timeout, internal] {
        sdkWorker_->doOpen(port, baud, timeout, internal);
    }, Qt::QueuedConnection);
}

void MainWindow::disconnectDevice() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doClose(); },
                              Qt::QueuedConnection);
}

void MainWindow::pingDevice() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doPing(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestStatus() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGetStatus(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestReboot() {
    const auto answer = QMessageBox::question(this, QStringLiteral("重启设备"),
        QStringLiteral("确认向设备发送重启命令？"));
    if (answer != QMessageBox::Yes) {
        return;
    }
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doReboot(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestStaticCapture() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doStaticCapture(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestDynamicStart() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doDynamicStart(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestDynamicQuery() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doDynamicQuery(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestDynamicStop() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doDynamicStop(); },
                              Qt::QueuedConnection);
}

/* ------------------------- 配置 ------------------------- */

void MainWindow::requestReadConfigGroup() {
    const quint16 group = static_cast<quint16>(groupCombo_->currentData().toInt());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, group] {
        sdkWorker_->doGetConfigGroup(group);
    }, Qt::QueuedConnection);
}

void MainWindow::requestSendConfigGroup() {
    const quint16 group = static_cast<quint16>(groupCombo_->currentData().toInt());
    QVector<tiray_config_item_t> items;
    items.reserve(configTable_->rowCount());
    for (int row = 0; row < configTable_->rowCount(); ++row) {
        const QString idText = configTable_->item(row, 0) != nullptr
            ? configTable_->item(row, 0)->text() : QString();
        const QString valueText = configTable_->item(row, 1) != nullptr
            ? configTable_->item(row, 1)->text() : QString();
        quint32 id = 0;
        quint32 value = 0;
        if (idText.trimmed().isEmpty() && valueText.trimmed().isEmpty()) {
            continue; /* 跳过空行 */
        }
        if (!parseU32(idText, id) || id > UINT16_MAX || !parseU32(valueText, value)) {
            log(QStringLiteral("[FAIL] 第 %1 行配置项格式错误（ID 需在 0~0xFFFF）").arg(row + 1));
            return;
        }
        tiray_config_item_t item{};
        item.item_id = static_cast<uint16_t>(id);
        item.value = value;
        items.append(item);
    }
    if (items.isEmpty()) {
        log(QStringLiteral("[FAIL] 配置表为空，无可下发项"));
        return;
    }
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, group, items] {
        sdkWorker_->doSetConfigGroup(group, items);
    }, Qt::QueuedConnection);
}

void MainWindow::requestGetStaticConfig() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGetStaticConfig(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestSetStaticConfig() {
    tiray_static_config_t config{};
    config.idle_clean_interval_ms = static_cast<uint32_t>(staticIdleSpin_->value());
    config.exposure_window_ms = static_cast<uint32_t>(staticExposureSpin_->value());
    config.dark_window_ms = static_cast<uint32_t>(staticDarkSpin_->value());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, config] {
        sdkWorker_->doSetStaticConfig(config);
    }, Qt::QueuedConnection);
}

void MainWindow::requestGetDynamicConfig() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGetDynamicConfig(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestSetDynamicConfig() {
    tiray_dynamic_config_t config{};
    config.cycle = static_cast<uint32_t>(dynamicCycleSpin_->value());
    if (!parseU32(dynamicStartAddrEdit_->text(), config.image_start_addr) ||
        !parseU32(dynamicEndAddrEdit_->text(), config.image_end_addr)) {
        log(QStringLiteral("[FAIL] 图像起止地址格式错误（支持 0x 前缀十六进制）"));
        return;
    }
    config.start_timeout_ms = static_cast<uint32_t>(dynamicStartTimeoutSpin_->value());
    config.state_poll_interval_ms = static_cast<uint32_t>(dynamicPollSpin_->value());
    config.stop_timeout_ms = static_cast<uint32_t>(dynamicStopTimeoutSpin_->value());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, config] {
        sdkWorker_->doSetDynamicConfig(config);
    }, Qt::QueuedConnection);
}

/* ------------------------- 校准 ------------------------- */

void MainWindow::requestOffsetBegin() {
    const quint32 total = static_cast<quint32>(offsetTotalSpin_->value());
    const quint32 valid = static_cast<quint32>(offsetValidSpin_->value());
    const quint8 mode = static_cast<quint8>(offsetModeCombo_->currentData().toInt());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, total, valid, mode] {
        sdkWorker_->doOffsetBegin(total, valid, mode);
    }, Qt::QueuedConnection);
}

void MainWindow::requestOffsetCapture() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doOffsetCapture(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestOffsetBuild() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doOffsetBuild(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestOffsetCancel() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doOffsetCancel(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestGainBegin() {
    QVector<quint32> levels;
    const QStringList parts = gainLevelsEdit_->text().split(QLatin1Char(','),
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        Qt::SkipEmptyParts);
#else
        QString::SkipEmptyParts);
#endif
    for (const QString& part : parts) {
        quint32 level = 0;
        if (!parseU32(part, level)) {
            log(QStringLiteral("[FAIL] 灰度级格式错误: %1").arg(part));
            return;
        }
        levels.append(level);
    }
    const quint32 frames = static_cast<quint32>(gainFramesSpin_->value());
    const float threshold = static_cast<float>(gainThresholdSpin_->value());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, levels, frames, threshold] {
        sdkWorker_->doGainBegin(levels, frames, threshold);
    }, Qt::QueuedConnection);
}

void MainWindow::requestGainCapture() {
    const quint32 level = static_cast<quint32>(gainLevelSpin_->value());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, level] {
        sdkWorker_->doGainCapture(level);
    }, Qt::QueuedConnection);
}

void MainWindow::requestGainBuild() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGainBuild(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestGainCancel() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGainCancel(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestCalStatus() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGetCalStatus(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestUploadConfig() {
    quint32 addr = 0;
    if (!parseU32(uploadAddrEdit_->text(), addr)) {
        log(QStringLiteral("[FAIL] 模板地址格式错误（支持 0x 前缀十六进制）"));
        return;
    }
    const quint32 kind = static_cast<quint32>(uploadKindCombo_->currentData().toInt());
    const quint32 rows = static_cast<quint32>(uploadRowsSpin_->value());
    const quint32 cols = static_cast<quint32>(uploadColsSpin_->value());
    const quint32 pkgs = static_cast<quint32>(uploadPkgsSpin_->value());
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this, kind, addr, rows, cols, pkgs] {
        sdkWorker_->doUploadConfig(kind, addr, rows, cols, pkgs);
    }, Qt::QueuedConnection);
}

void MainWindow::requestUploadStart() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doUploadStart(); },
                              Qt::QueuedConnection);
}

void MainWindow::requestUploadQuery() {
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doUploadQuery(); },
                              Qt::QueuedConnection);
}

/* ------------------------- PCIe ------------------------- */

void MainWindow::startPcie() {
    const QString event = pcieEventEdit_->text().trimmed();
    const QString c2h = pcieC2hEdit_->text().trimmed();
    const QString bar0 = pcieBar0Edit_->text().trimmed();
    pcieMonitor_->doStart(event, c2h, bar0);
}

void MainWindow::stopPcie() {
    pcieMonitor_->doStop();
}

/* ------------------------- 图像显示 ------------------------- */

void MainWindow::computeAutoWindow() {
    const QSharedPointer<PcieFrame>& frame = latestFrame_;
    if (frame.isNull() || frame->data.isEmpty()) {
        autoWindowLow_ = 0;
        autoWindowHigh_ = 65535;
        autoWindowValid_ = true;
        return;
    }

    const int pixelCount = static_cast<int>(frame->data.size() / 2);
    constexpr int kBins = 4096;
    QVector<quint64> histogram(kBins, 0);
    const int stride = std::max(1, pixelCount / 2000000);
    const char* data = frame->data.constData();
    for (int i = 0; i < pixelCount; i += stride) {
        const quint32 bin = readU16Le(data + i * 2) >> 4;
        ++histogram[static_cast<int>(bin)];
    }

    quint64 sampled = 0;
    for (quint64 count : histogram) {
        sampled += count;
    }
    const quint64 lowTarget = sampled / 100;        /* 1% 分位 */
    const quint64 highTarget = sampled - sampled / 100; /* 99% 分位 */
    quint64 cumulative = 0;
    int lowBin = 0;
    int highBin = kBins - 1;
    bool lowFound = false;
    for (int bin = 0; bin < kBins; ++bin) {
        cumulative += histogram[bin];
        if (!lowFound && cumulative >= lowTarget) {
            lowBin = bin;
            lowFound = true;
        }
        if (cumulative >= highTarget) {
            highBin = bin;
            break;
        }
    }
    autoWindowLow_ = lowBin << 4;
    autoWindowHigh_ = std::max(autoWindowLow_ + 1, (highBin << 4) | 0x0F);
    autoWindowValid_ = true;
}

QImage MainWindow::windowedFullImage() const {
    const QSharedPointer<PcieFrame>& frame = latestFrame_;
    if (frame.isNull() || frame->rows == 0 || frame->columns == 0) {
        return {};
    }
    const qint64 required = static_cast<qint64>(frame->rows) * frame->columns * 2;
    if (frame->data.size() < required) {
        return {};
    }

    int low = autoWindowLow_;
    int high = autoWindowHigh_;
    if (!autoWindowCheck_->isChecked()) {
        const int center = windowCenterSlider_->value();
        const int width = std::max(1, windowWidthSlider_->value());
        low = center - width / 2;
        high = center + width / 2;
    }
    const int range = std::max(1, high - low);

    QImage image(static_cast<int>(frame->columns), static_cast<int>(frame->rows),
                 QImage::Format_Grayscale8);
    const char* data = frame->data.constData();
    for (int y = 0; y < image.height(); ++y) {
        uchar* line = image.scanLine(y);
        const char* row = data + static_cast<qint64>(y) * frame->columns * 2;
        for (int x = 0; x < image.width(); ++x) {
            int value = static_cast<int>(readU16Le(row + x * 2));
            value = (value - low) * 255 / range;
            line[x] = static_cast<uchar>(std::clamp(value, 0, 255));
        }
    }
    return image;
}

void MainWindow::renderDisplay(bool resetView) {
    if (latestFrame_.isNull()) {
        return;
    }
    if (autoWindowCheck_->isChecked() && !autoWindowValid_) {
        computeAutoWindow();
    }

    const QSharedPointer<PcieFrame>& frame = latestFrame_;
    latestWindowedImage_ = windowedFullImage();
    if (latestWindowedImage_.isNull()) {
        frameInfoLabel_->setText(QStringLiteral("帧数据不完整（%1 字节），等待下一帧")
            .arg(frame->data.size()));
        return;
    }

    QImage displayImage = latestWindowedImage_;
    const int maxWidth = displayWidthCombo_->currentData().toInt();
    if (maxWidth > 0 && displayImage.width() > maxWidth) {
        const int targetHeight = std::max(1, displayImage.height() * maxWidth / displayImage.width());
        displayImage = displayImage.scaled(maxWidth, targetHeight, Qt::KeepAspectRatio,
                                           Qt::FastTransformation);
    }

    imageView_->setImage(displayImage,
                         QSize(static_cast<int>(frame->columns), static_cast<int>(frame->rows)),
                         resetView);
    lastRenderedFrame_ = frame;
    displayDirty_ = false;
    frameInfoLabel_->setText(QStringLiteral("%1×%2 id=%3 type=%4(%5) addr=%6 | 显示窗位 [%7, %8]")
        .arg(frame->rows).arg(frame->columns)
        .arg(frame->imageId)
        .arg(frame->imageType)
        .arg(frame->imageType == 1 ? QStringLiteral("模板上传") : QStringLiteral("正常图像"))
        .arg(hex32(static_cast<quint32>(frame->finalAddress)))
        .arg(autoWindowCheck_->isChecked() ? autoWindowLow_ : windowCenterSlider_->value() - windowWidthSlider_->value() / 2)
        .arg(autoWindowCheck_->isChecked() ? autoWindowHigh_ : windowCenterSlider_->value() + windowWidthSlider_->value() / 2));
}

void MainWindow::renderDisplayTimer() {
    if (!displayDirty_ || latestFrame_.isNull()) {
        return;
    }
    const bool resetView = lastRenderedFrame_.isNull();
    renderDisplay(resetView);
}

void MainWindow::clearImageDisplay() {
    latestFrame_.clear();
    lastRenderedFrame_.clear();
    latestWindowedImage_ = QImage();
    displayDirty_ = false;
    imageView_->clearImage();
    frameInfoLabel_->setText(QStringLiteral("暂无图像"));
    roiInfoLabel_->setText(QStringLiteral("右键拖动框选可计算区域统计"));
}

void MainWindow::saveDisplayImage() {
    if (latestWindowedImage_.isNull()) {
        log(QStringLiteral("[FAIL] 当前没有可保存的图像"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存显示图像"),
        QStringLiteral("tiray_display.png"), QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    if (latestWindowedImage_.save(path, "PNG")) {
        log(QStringLiteral("[PASS] 已保存显示图像: %1").arg(path));
    } else {
        log(QStringLiteral("[FAIL] 保存失败: %1").arg(path));
    }
}

void MainWindow::saveLatestRaw() {
    const QSharedPointer<PcieFrame>& frame = latestFrame_;
    if (frame.isNull() || frame->data.isEmpty()) {
        log(QStringLiteral("[FAIL] 当前没有可保存的原始帧"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存原始帧"),
        QStringLiteral("tiray_frame.raw"), QStringLiteral("RAW (*.raw)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log(QStringLiteral("[FAIL] 无法写入 %1").arg(path));
        return;
    }
    const qint64 expected = static_cast<qint64>(frame->rows) * frame->columns * 2;
    const qint64 length = std::min<qint64>(expected, frame->data.size());
    file.write(frame->data.constData(), length);
    log(QStringLiteral("[PASS] 已保存原始帧: %1（%2×%3，%4 字节）")
        .arg(path).arg(frame->rows).arg(frame->columns).arg(length));
}

void MainWindow::updatePixelInfo(const QPoint& logicalPoint) {
    if (latestFrame_.isNull() || logicalPoint.x() < 0) {
        return;
    }
    const qint64 index = (static_cast<qint64>(logicalPoint.y()) * latestFrame_->columns
                          + logicalPoint.x()) * 2;
    if (index < 0 || index + 2 > latestFrame_->data.size()) {
        return;
    }
    statusBar()->showMessage(QStringLiteral("像素 (%1, %2) = %3")
        .arg(logicalPoint.x()).arg(logicalPoint.y())
        .arg(readU16Le(latestFrame_->data.constData() + index)), 3000);
}

void MainWindow::updateRoiInfo(const QRect& logicalRect) {
    if (latestFrame_.isNull() || logicalRect.isEmpty()) {
        return;
    }
    const int width = static_cast<int>(latestFrame_->columns);
    const int height = static_cast<int>(latestFrame_->rows);
    const QRect roi = logicalRect.intersected(QRect(0, 0, width, height));
    if (roi.isEmpty() || latestFrame_->data.size() < width * height * 2) {
        return;
    }

    const char* data = latestFrame_->data.constData();
    double sum = 0.0;
    double sumSq = 0.0;
    quint16 minValue = 65535;
    quint16 maxValue = 0;
    const qint64 count = static_cast<qint64>(roi.width()) * roi.height();
    for (int y = roi.top(); y <= roi.bottom(); ++y) {
        const char* row = data + static_cast<qint64>(y) * width * 2;
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const quint16 value = readU16Le(row + x * 2);
            sum += value;
            sumSq += static_cast<double>(value) * value;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
    }
    const double mean = sum / count;
    const double variance = std::max(0.0, sumSq / count - mean * mean);
    roiInfoLabel_->setText(QStringLiteral(
        "ROI (%1,%2)-(%3,%4) %5 像素：均值 %6 最小 %7 最大 %8 标准差 %9")
        .arg(roi.left()).arg(roi.top()).arg(roi.right()).arg(roi.bottom())
        .arg(count)
        .arg(mean, 0, 'f', 2).arg(minValue).arg(maxValue)
        .arg(std::sqrt(variance), 0, 'f', 2));
}

/* ------------------------- 轮询 ------------------------- */

void MainWindow::pollStatusTimer() {
    if (!connected_ || busyCount_ > 0) {
        return;
    }
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGetStatus(); },
                              Qt::QueuedConnection);
}

void MainWindow::pollCalTimer() {
    if (!connected_ || busyCount_ > 0) {
        return;
    }
    changeBusy(1);
    QMetaObject::invokeMethod(sdkWorker_, [this] { sdkWorker_->doGetCalStatus(); },
                              Qt::QueuedConnection);
}
