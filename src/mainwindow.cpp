#include "mainwindow.h"

#include "client.h"
#include "devicecontroller.h"
#include "framerecorder.h"
#include "framedecoderworker.h"
#include "frameplayer.h"
#include "parameterpanel.h"
#include "probeadjustmentpanel.h"
#include "remotecontrolserver.h"
#include "robotcontrolpanel.h"
#include "scanwidgets.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_device = new DeviceController(this);
    m_recorder = new FrameRecorder(this);
    m_player = new FramePlayer(this);
    buildUi();
    buildMenus();
    m_remoteControl = new RemoteControlServer(m_device, m_parameters, this, this);
    QString remoteControlError;
    if (!m_remoteControl->start(&remoteControlError)) {
        showMessage(QStringLiteral("远程控制服务启动失败：%1").arg(remoteControlError), true);
    }
    m_auxUiClock.start();

    qRegisterMetaType<DecodedFrame>("DecodedFrame");
    qRegisterMetaType<DecodedFrameBatch>("DecodedFrameBatch");
    qRegisterMetaType<RawPacketBuffer>("RawPacketBuffer");
    m_decoderThread = new QThread(this);
    m_decoder = new FrameDecoderWorker(m_device);
    m_decoder->moveToThread(m_decoderThread);
    connect(m_decoderThread, &QThread::finished, m_decoder, &QObject::deleteLater);
    connect(m_decoderThread, &QThread::started,
            m_decoder, &FrameDecoderWorker::startPolling);
    connect(this, &MainWindow::decoderSettingsChanged,
            m_decoder, &FrameDecoderWorker::updateSettings,
            Qt::QueuedConnection);
    connect(this, &MainWindow::decoderRecordingChanged,
            m_decoder, &FrameDecoderWorker::setRecordingEnabled,
            Qt::QueuedConnection);
    connect(m_decoder, &FrameDecoderWorker::frameDecoded,
            this, &MainWindow::acceptDecodedFrame, Qt::QueuedConnection);
    connect(m_decoder, &FrameDecoderWorker::cScanBatchDecoded,
            this, &MainWindow::acceptCScanBatch, Qt::QueuedConnection);
    connect(m_decoder, &FrameDecoderWorker::decodeFailed, this, [this](const QString &error) {
        statusBar()->showMessage(error, 2000);
    }, Qt::QueuedConnection);
    m_decoderThread->start();
    updateDecoderSettings();
    connect(m_decoder, &FrameDecoderWorker::recordingPacketReady,
            m_recorder, &FrameRecorder::append, Qt::QueuedConnection);
    connect(m_device, &DeviceController::statusChanged, this, [this] {
        updateStatus();
        updateDecoderSettings();
    });
    connect(m_device, &DeviceController::message, this, &MainWindow::showMessage);
    connect(m_parameters, &ParameterPanel::configurationChanged, this, &MainWindow::updateParametersForPlot);
    connect(m_parameters, &ParameterPanel::groupChanged, m_device, &DeviceController::refreshGeometry);
    connect(m_parameters, &ParameterPanel::message, this, &MainWindow::showMessage);
    connect(m_parameters, &ParameterPanel::gatePlacementRequested,
            m_aScan, &AScanWidget::beginGatePlacement);
    connect(m_parameters, &ParameterPanel::surfaceWindowPlacementRequested,
            m_aScan, &AScanWidget::beginSurfaceWindowPlacement);
    connect(m_parameters, &ParameterPanel::gateVisualizationChanged, this, [this] {
        m_aScan->setGates(m_parameters->currentGates());
        m_aScan->setActiveGate(m_parameters->activeGate());
        updateImagingGates();
    });
    connect(m_parameters, &ParameterPanel::surfaceTrackingChanged, this, [this] {
        m_aScan->setSurfaceTracking(m_parameters->surfaceTrackingEnabled(),
                                    m_parameters->surfaceWindowStart(),
                                    m_parameters->surfaceWindowEnd(),
                                    m_parameters->surfaceThreshold(),
                                    m_parameters->surfaceHoldMissing(),
                                    m_parameters->surfaceTrackedGates());
    });
    connect(m_aScan, &AScanWidget::gateSelected,
            m_parameters, &ParameterPanel::selectGate);
    connect(m_aScan, &AScanWidget::gateRangeEdited,
            m_parameters, &ParameterPanel::setGateRangeFromPlot);
    connect(m_aScan, &AScanWidget::gateThresholdEdited,
            m_parameters, &ParameterPanel::setGateThresholdFromPlot);
    connect(m_aScan, &AScanWidget::surfaceWindowEdited,
            m_parameters, &ParameterPanel::setSurfaceWindowFromPlot);
    connect(m_aScan, &AScanWidget::surfaceTrackingUpdated,
            m_parameters, &ParameterPanel::updateSurfaceStatus);
    connect(m_aScan, &AScanWidget::interactionHint, this,
            [this](const QString &text) { showMessage(text, false); });
    connect(m_parameters, &ParameterPanel::applyStarted, this, [this] {
        // 参数写入前先停一次，避免 SDK 在采集中修改配置。
        if (m_device->serviceConnected()) {
            showMessage(QStringLiteral("正在停止采集并应用参数修改…"), false);
            m_device->stop();
        }
    });
    connect(m_parameters, &ParameterPanel::applyFinished, this, [this] {
        m_device->refreshGeometry();
        if (m_device->serviceConnected()) {
            // 自动下发参数并从 SDK 回读后，按安全时序恢复采集。
            m_device->resumeAfterConfigurationChange();
        }
    });
    connect(m_recorder, &FrameRecorder::stateChanged, this, [this](bool recording, const QString &path) {
        emit decoderRecordingChanged(recording);
        m_record->setText(recording ? QStringLiteral("停止保存") : QStringLiteral("保存原始帧"));
        showMessage(recording ? QStringLiteral("正在保存：%1").arg(path)
                              : QStringLiteral("数据已保存：%1").arg(path), false);
    });
    connect(m_recorder, &FrameRecorder::error, this, [this](const QString &text) { showMessage(text, true); });
    connect(m_player, &FramePlayer::frameReady, this, &MainWindow::processPlaybackFrame);
    connect(m_player, &FramePlayer::positionChanged, this, [this](int current, int total) {
        statusBar()->showMessage(QStringLiteral("回放 %1 / %2").arg(current).arg(total));
    });
    connect(m_player, &FramePlayer::finished, this, [this] { showMessage(QStringLiteral("回放完成"), false); });

    setWindowTitle(QStringLiteral("PA-1664 超声检测工作台"));
    resize(1500, 900);
    setMinimumSize(960, 640);
    updateStatus();
    QTimer::singleShot(300, m_parameters, &ParameterPanel::refreshGroups);
    // Do not cycle USB here.  At this point the vendor framework exists, but
    // Client::Connect() has not happened yet.  A simulated arrival in that
    // window is ignored by this SDK and leaves physical_device_id at -1.
    // DeviceController performs the one cold-start cycle only after the first
    // successful SDK connection.
    // In the supported workflow the application starts before USB is inserted.
    // Connect the local SDK service now so its hardware listener is active when
    // the BootLoader/Streamer arrival event occurs.
    QTimer::singleShot(500, this, [this] {
        if (!m_device->serviceConnected())
            connectDevice();
    });
}

MainWindow::~MainWindow()
{
    if (m_remoteDesktopProcess && m_remoteDesktopProcess->state() != QProcess::NotRunning) {
        m_remoteDesktopProcess->terminate();
        if (!m_remoteDesktopProcess->waitForFinished(1500))
            m_remoteDesktopProcess->kill();
    }
    if (m_decoderThread) {
        m_decoderThread->quit();
        m_decoderThread->wait();
    }
}

void MainWindow::buildUi()
{
    // 浅色主题：参考 TOFD 机器人系统的配色与控件样式。
    setStyleSheet(styleSheet() + QStringLiteral(R"(
        QMainWindow { background:#eef4f9; }
        #workspaceTabs::pane { border:1px solid #d7e2ee; background:white; border-radius:8px; }
        #workspaceTabs QTabBar::tab { min-width:126px; min-height:38px; padding:0 16px; color:#405874; font-weight:700; }
        #workspaceTabs QTabBar::tab:selected { background:#1677ff; color:white; border-radius:8px 8px 0 0; }
        #globalBar { background:white; border-bottom:1px solid #d7e2ee; padding:4px 10px; }
        #globalBar QLabel { color:#274967; font-size:13px; font-weight:600; padding:0 5px; }
        #scanCard, #mapCard { border:1px solid #d7e2ee; border-radius:8px; margin-top:12px; padding-top:10px; font-weight:600; }
        #mapTabs::pane { border:1px solid #d7e2ee; border-radius:6px; }
        #mapTabs QTabBar::tab { min-width:92px; min-height:30px; color:#405874; font-weight:600; padding:0 10px; }
        #mapTabs QTabBar::tab:selected { background:#1677ff; color:white; border-radius:6px 6px 0 0; }
        QPushButton { min-height:32px; border:1px solid #b9d5f2; border-radius:6px; background:#eef6ff; color:#1766b6; padding:0 12px; }
        QPushButton:hover { background:#dcecff; }
        QPushButton:checked { background:#1677ff; color:white; }
        #actionRail { spacing:8px; background:#eef4f9; border:0; padding:8px; }
        #actionRail QToolButton { width:66px; height:66px; border-radius:33px; border:1px solid #cbdbea; background:white; color:#123a68; font-size:15px; font-weight:700; }
        #actionRail QToolButton:hover { border:2px solid #1677ff; color:#1677ff; background:#f2f8ff; }
        #actionRail QToolButton:checked { background:#1677ff; color:white; }
        #actionRail QToolButton#startScanButton { background:#13ad58; color:white; border-color:#0f974c; }
        #actionRail QToolButton#stopScanButton { background:#d94141; color:white; border-color:#bd3030; }
        #actionRail QToolButton#closeProgramButton { background:#fff4f4; color:#c73535; border-color:#e9aaaa; }
        #actionRail QToolButton#closeProgramButton:hover { background:#d94141; color:white; border-color:#bd3030; }
        #panelDock { background:white; border:1px solid #cddbe8; }
        #panelTitleBar { background:#f8fbff; border-bottom:1px solid #d7e2ee; }
        #panelTitleBar QLabel { color:#102b4e; font-size:17px; font-weight:700; }
        #collapsePanelButton { border:1px solid #d2dfeb; border-radius:17px; background:white; color:#194c85; font-size:20px; }
    )"));

    // ===== 顶部全局状态栏（连接 / 硬件 / 工作组 / 编码器） =====
    auto *globalBar = new QToolBar(QStringLiteral("状态"), this);
    globalBar->setObjectName(QStringLiteral("globalBar"));
    globalBar->setMovable(false);
    globalBar->setFloatable(false);
    m_serviceStatus = new QLabel;
    m_hardwareStatus = new QLabel;
    m_groupStatus = new QLabel(QStringLiteral("工作组 --"));
    m_encoderStatus = new QLabel(QStringLiteral("编码器 A/B --"));
    globalBar->addWidget(m_serviceStatus);
    globalBar->addSeparator();
    globalBar->addWidget(m_hardwareStatus);
    globalBar->addSeparator();
    globalBar->addWidget(m_groupStatus);
    globalBar->addSeparator();
    globalBar->addWidget(m_encoderStatus);
    addToolBar(Qt::TopToolBarArea, globalBar);

    // ===== 超声检测页：左 A 扫卡片 + 右扫描图谱页签（E/B/C） =====
    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    auto *aCard = new QGroupBox(QStringLiteral("A 扫波形"));
    aCard->setObjectName(QStringLiteral("scanCard"));
    auto *aLayout = new QVBoxLayout(aCard);
    aLayout->setContentsMargins(8, 6, 8, 8);
    aLayout->setSpacing(6);
    auto *aHeader = new QHBoxLayout;
    aHeader->addWidget(new QLabel(QStringLiteral("显示声束")));
    m_beam = new QSpinBox;
    m_beam->setRange(1, 1);
    aHeader->addWidget(m_beam);
    m_measurements = new QLabel(QStringLiteral("Gate A/B/C/I: --"));
    aHeader->addWidget(m_measurements, 1);
    aLayout->addLayout(aHeader);
    m_aScan = new AScanWidget;
    aLayout->addWidget(m_aScan, 1);
    root->addWidget(aCard, 2);

    auto *mapCard = new QGroupBox(QStringLiteral("扫描图谱"));
    mapCard->setObjectName(QStringLiteral("mapCard"));
    auto *mapLayout = new QVBoxLayout(mapCard);
    mapLayout->setContentsMargins(8, 6, 8, 8);
    mapLayout->setSpacing(6);
    auto *mapHeader = new QHBoxLayout;
    mapHeader->addWidget(new QLabel(QStringLiteral("B/C 路径精度")));
    m_cPrecision = new QSpinBox;
    m_cPrecision->setRange(1, 1000000);
    m_cPrecision->setValue(10);
    m_cPrecision->setSuffix(QStringLiteral(" 计数/格"));
    m_cPrecision->setToolTip(QStringLiteral(
        "编码器每变化多少个原始计数生成一个 B 扫列/C 扫网格。数值越小，空间采样越细。"));
    m_cPrecision->setMaximumWidth(165);
    mapHeader->addWidget(m_cPrecision);
    mapHeader->addStretch();
    mapLayout->addLayout(mapHeader);

    // 每个图谱页签内容：页签内启停开关 + 图谱视图。
    const auto buildScanPage = [](QWidget *view, QPushButton *toggle, QComboBox *gateSelector) {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(4, 6, 4, 4);
        layout->setSpacing(4);
        auto *bar = new QHBoxLayout;
        bar->addWidget(toggle);
        bar->addStretch();
        bar->addWidget(new QLabel(QStringLiteral("成像闸门")));
        bar->addWidget(gateSelector);
        layout->addLayout(bar);
        layout->addWidget(view, 1);
        return page;
    };
    m_mapTabs = new QTabWidget;
    m_mapTabs->setObjectName(QStringLiteral("mapTabs"));
    m_mapTabs->setDocumentMode(true);
    m_eScan = new EScanWidget;
    m_eScanToggle = new QPushButton(QStringLiteral("启动 E 扫"));
    m_eScanToggle->setCheckable(true);
    m_eImagingGate = new QComboBox;
    for (const QString &name : {QStringLiteral("Gate A"), QStringLiteral("Gate B"),
                                QStringLiteral("Gate C"), QStringLiteral("Gate I")})
        m_eImagingGate->addItem(name);
    m_mapTabs->addTab(buildScanPage(m_eScan, m_eScanToggle, m_eImagingGate), QStringLiteral("E 扫截面"));
    m_bScan = new BScanWidget;
    m_bScanToggle = new QPushButton(QStringLiteral("启动 B 扫"));
    m_bScanToggle->setCheckable(true);
    m_bImagingGate = new QComboBox;
    for (const QString &name : {QStringLiteral("Gate A"), QStringLiteral("Gate B"),
                                QStringLiteral("Gate C"), QStringLiteral("Gate I")})
        m_bImagingGate->addItem(name);
    m_mapTabs->addTab(buildScanPage(m_bScan, m_bScanToggle, m_bImagingGate), QStringLiteral("编码器 B 扫"));
    m_cScan = new CScanWidget;
    m_cScanToggle = new QPushButton(QStringLiteral("启动 C 扫"));
    m_cScanToggle->setCheckable(true);
    m_cImagingGate = new QComboBox;
    for (const QString &name : {QStringLiteral("Gate A"), QStringLiteral("Gate B"),
                                QStringLiteral("Gate C"), QStringLiteral("Gate I")})
        m_cImagingGate->addItem(name);
    m_mapTabs->addTab(buildScanPage(m_cScan, m_cScanToggle, m_cImagingGate), QStringLiteral("编码器 C 扫"));
    m_bScan->setEncoderPrecision(m_cPrecision->value());
    m_cScan->setEncoderPrecision(m_cPrecision->value());
    mapLayout->addWidget(m_mapTabs, 1);
    root->addWidget(mapCard, 3);

    m_workspace = new QTabWidget;
    m_workspace->setObjectName(QStringLiteral("workspaceTabs"));
    m_workspace->setDocumentMode(true);
    m_workspace->addTab(central, QStringLiteral("超声检测"));
    m_robotPanel = new RobotControlPanel;
    m_workspace->addTab(m_robotPanel, QStringLiteral("小车控制"));
    setCentralWidget(m_workspace);

    // ===== 右侧浮动面板（分区页：连接/状态/超声/数据） =====
    m_parameters = new ParameterPanel;
    m_parameters->setMinimumWidth(370);
    auto *probeAdjust = new ProbeAdjustmentPanel;

    m_panelPages = new QStackedWidget;

    auto *connectPage = new QWidget;
    auto *connectForm = new QFormLayout(connectPage);
    m_address = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_address->setMaximumWidth(160);
    m_deviceId = new QSpinBox;
    m_deviceId->setRange(0, 63);
    m_connect = new QPushButton(QStringLiteral("连接 SDK"));
    auto *connectHint = new QLabel(QStringLiteral(
        "连接本机 SDK 服务后即可开始采集；采集命令会自动排队等待硬件就绪。"));
    connectHint->setWordWrap(true);
    connectHint->setStyleSheet(QStringLiteral("color:#52606d;padding:4px 0 8px 0"));
    connectForm->addRow(QStringLiteral("服务地址"), m_address);
    connectForm->addRow(QStringLiteral("设备 ID"), m_deviceId);
    connectForm->addRow(m_connect);
    connectForm->addRow(connectHint);
    m_panelPages->addWidget(connectPage);

    auto *statusPage = new QWidget;
    auto *statusLayout = new QVBoxLayout(statusPage);
    auto *statusForm = new QFormLayout;
    m_statusCaptureDetail = new QLabel(QStringLiteral("--"));
    m_tailInfo = new QLabel(QStringLiteral("帧号 -- | 编码器 A/B -- | 时间戳 --"));
    m_tailInfo->setWordWrap(true);
    statusForm->addRow(QStringLiteral("采集状态"), m_statusCaptureDetail);
    statusForm->addRow(QStringLiteral("帧尾信息"), m_tailInfo);
    statusLayout->addLayout(statusForm);

    // 车体状态：从小车控制页复用到「状态」分区（数据经 vehicleStatusChanged 同步）。
    auto *vehicleGroup = new QGroupBox(QStringLiteral("车体状态"));
    auto *vehicleForm = new QFormLayout(vehicleGroup);
    const char *vehicleNames[] = {"运行状态", "状态说明", "适配器", "目标运动", "实际运动",
                                  "左轮", "右轮", "反馈看门狗", "双轮同步",
                                  "RIM302 IMU", "MV3DLP 激光", "USB 摄像头"};
    for (const char *name : vehicleNames) {
        auto *value = new QLabel(QStringLiteral("--"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        vehicleForm->addRow(QString::fromUtf8(name), value);
        m_vehicleMetrics.append(value);
    }
    statusLayout->addWidget(vehicleGroup);
    statusLayout->addStretch();
    m_panelPages->addWidget(statusPage);

    auto *paramPage = new QWidget;
    auto *paramLayout = new QVBoxLayout(paramPage);
    paramLayout->setContentsMargins(0, 0, 0, 0);
    auto *paramTabs = new QTabWidget;
    paramTabs->setDocumentMode(true);
    paramTabs->addTab(m_parameters, QStringLiteral("超声设置"));
    paramTabs->addTab(probeAdjust, QStringLiteral("探头调节"));
    paramLayout->addWidget(paramTabs);
    m_panelPages->addWidget(paramPage);

    auto *dataPage = new QWidget;
    auto *dataLayout = new QVBoxLayout(dataPage);
    m_record = new QPushButton(QStringLiteral("保存原始帧"));
    auto *playback = new QPushButton(QStringLiteral("打开原始帧回放…"));
    auto *reset = new QPushButton(QStringLiteral("编码器复位"));
    dataLayout->addWidget(m_record);
    dataLayout->addWidget(playback);
    dataLayout->addWidget(reset);
    dataLayout->addStretch();
    m_panelPages->addWidget(dataPage);

    m_panelDock = new QDockWidget(this);
    m_panelDock->setObjectName(QStringLiteral("panelDock"));
    // 纯浮动面板：不参与停靠，避免 show() 时落到屏幕左下角。
    m_panelDock->setAllowedAreas(Qt::NoDockWidgetArea);
    m_panelDock->setFeatures(QDockWidget::DockWidgetMovable
                             | QDockWidget::DockWidgetFloatable);
    m_panelDock->setMinimumSize(300, 420);
    m_panelDock->resize(390, 640);
    auto *titleBar = new QWidget(m_panelDock);
    titleBar->setObjectName(QStringLiteral("panelTitleBar"));
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(10, 8, 8, 8);
    m_panelTitle = new QLabel(m_panelDock);
    m_panelTitle->setAlignment(Qt::AlignCenter);
    auto *collapse = new QPushButton(QStringLiteral("›"), titleBar);
    collapse->setObjectName(QStringLiteral("collapsePanelButton"));
    collapse->setToolTip(QStringLiteral("收起面板"));
    collapse->setFixedSize(34, 34);
    titleLayout->addWidget(m_panelTitle, 1);
    titleLayout->addWidget(collapse);
    m_panelDock->setTitleBarWidget(titleBar);
    m_panelDock->setWidget(m_panelPages);
    addDockWidget(Qt::RightDockWidgetArea, m_panelDock);
    m_panelDock->setFloating(true);
    connect(collapse, &QPushButton::clicked, this, [this] {
        m_panelDock->hide();
        // 收起面板时同时取消分区选中，保证“再次点击同一分区能重新弹出”。
        if (m_actionRail)
            for (QAction *a : m_actionRail->actions())
                if (a->isCheckable() && a->data().isValid())
                    a->setChecked(false);
    });

    // ===== 右侧圆形导航栏 =====
    m_actionRail = new QToolBar(QStringLiteral("功能导航"), this);
    m_actionRail->setObjectName(QStringLiteral("actionRail"));
    m_actionRail->setMovable(false);
    m_actionRail->setFloatable(false);
    m_actionRail->setOrientation(Qt::Vertical);
    m_actionRail->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_actionRail->setMinimumWidth(88);

    struct Section { const char *label; int index; };
    constexpr Section sections[] = {{"连接", 0}, {"状态", 1},
                                    {"超声", 2}, {"数据", 3}};
    // 分区按钮不做排他分组：点击未选中的分区弹出面板，再次点击当前分区收起面板。
    for (const Section &section : sections) {
        QAction *action = m_actionRail->addAction(QString::fromUtf8(section.label));
        action->setCheckable(true);
        action->setData(section.index);
        connect(action, &QAction::triggered, this, [this, index = section.index](bool checked) {
            if (!checked) {
                if (m_panelDock)
                    m_panelDock->hide();
                return;
            }
            showPanelSection(index);
        });
    }
    m_startAction = m_actionRail->addAction(QStringLiteral("启动"));
    m_stopAction = m_actionRail->addAction(QStringLiteral("停止"));
    m_actionRail->addSeparator();
    QAction *closeAction = m_actionRail->addAction(QStringLiteral("关闭"));
    m_actionRail->widgetForAction(m_startAction)->setObjectName(QStringLiteral("startScanButton"));
    m_actionRail->widgetForAction(m_stopAction)->setObjectName(QStringLiteral("stopScanButton"));
    m_actionRail->widgetForAction(closeAction)->setObjectName(QStringLiteral("closeProgramButton"));
    closeAction->setToolTip(QStringLiteral("关闭程序"));
    m_startAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_stopAction->setShortcut(QKeySequence(Qt::Key_F6));
    connect(m_startAction, &QAction::triggered, m_device, &DeviceController::start);
    connect(m_stopAction, &QAction::triggered, m_device, &DeviceController::stop);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    auto *actionDock = new QDockWidget(this);
    actionDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    actionDock->setAllowedAreas(Qt::RightDockWidgetArea);
    actionDock->setTitleBarWidget(new QWidget(actionDock));
    actionDock->setWidget(m_actionRail);
    addDockWidget(Qt::RightDockWidgetArea, actionDock);

    // ===== 信号连接（沿用原有逻辑，控件位置变化不影响行为） =====
    connect(m_connect, &QPushButton::clicked, this, &MainWindow::connectDevice);
    connect(m_record, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(playback, &QPushButton::clicked, this, &MainWindow::openRecording);
    connect(reset, &QPushButton::clicked, m_device, &DeviceController::resetEncoder);
    connect(reset, &QPushButton::clicked, m_bScan, &BScanWidget::clear);
    connect(reset, &QPushButton::clicked, m_cScan, &CScanWidget::clear);
    connect(m_beam, &QSpinBox::valueChanged, this, [this] {
        updateDecoderSettings();
        if (m_bScanToggle->isChecked())
            m_bScan->clear();
        if (m_cScanToggle->isChecked())
            m_cScan->clear();
        if (!m_lastFrame.beams.isEmpty()) renderCurrentFrame(false);
    });
    // 图谱页签只控制查看内容；各扫描的启停由页签内的开关独立控制。
    connect(m_eScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_eScanToggle->setText(enabled ? QStringLiteral("停止 E 扫")
                                       : QStringLiteral("启动 E 扫"));
        if (enabled)
            m_mapTabs->setCurrentIndex(0);
        updateDecoderSettings();
    });
    connect(m_bScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_bScanToggle->setText(enabled ? QStringLiteral("停止 B 扫")
                                       : QStringLiteral("启动 B 扫"));
        if (enabled) {
            m_bScan->clear();
            m_mapTabs->setCurrentIndex(1);
        }
        updateDecoderSettings();
    });
    connect(m_cScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_cScanToggle->setText(enabled ? QStringLiteral("停止 C 扫")
                                       : QStringLiteral("启动 C 扫"));
        if (enabled) {
            m_cScan->clear();
            m_mapTabs->setCurrentIndex(2);
        }
        updateDecoderSettings();
    });
    connect(m_cPrecision, &QSpinBox::valueChanged, this, [this](int precision) {
        m_bScan->setEncoderPrecision(precision);
        m_cScan->setEncoderPrecision(precision);
        updateDecoderSettings();
    });
    connect(m_eImagingGate, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateImagingGates);
    connect(m_bImagingGate, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateImagingGates);
    connect(m_cImagingGate, &QComboBox::currentIndexChanged,
            this, &MainWindow::updateImagingGates);
    connect(m_robotPanel, &RobotControlPanel::vehicleStatusChanged, this,
            [this](const QStringList &metrics) {
        const int count = qMin(metrics.size(), m_vehicleMetrics.size());
        for (int i = 0; i < count; ++i)
            m_vehicleMetrics[i]->setText(metrics.at(i));
    });

    // 初始定位到「超声」分区，但面板保持隐藏——与参考项目一致：
    // 只有点击右侧导航栏分区时才弹出浮动面板。
    m_panelPages->setCurrentIndex(2);
    if (m_panelTitle)
        m_panelTitle->setText(QStringLiteral("超声"));
    m_panelDock->hide();
}

void MainWindow::showPanelSection(int index)
{
    if (!m_panelPages || index < 0 || index >= m_panelPages->count())
        return;
    static const char *titles[] = {"连接", "状态", "超声", "数据"};
    m_panelPages->setCurrentIndex(index);
    if (m_panelTitle)
        m_panelTitle->setText(QString::fromUtf8(titles[index]));
    if (m_panelDock) {
        // 参考项目 FloatingPanelController::showAtDefaultPosition 的弹出逻辑。
        if (!m_panelDock->isFloating())
            m_panelDock->setFloating(true);
        m_panelDock->show();
        m_panelDock->raise();
        m_panelDock->activateWindow();
        if (!m_panelPositioned) {
            const QPoint topRight = mapToGlobal(rect().topRight());
            m_panelDock->move(topRight.x() - m_panelDock->width() - 100,
                              topRight.y() + 96);
            m_panelPositioned = true;
        }
        // 约束在屏幕可用区域内，避免超出显示器边界。
        QScreen *screen = QGuiApplication::screenAt(m_panelDock->frameGeometry().center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            QPoint pos = m_panelDock->pos();
            pos.setX(qBound(available.left(), pos.x(),
                            available.right() - m_panelDock->width() + 1));
            pos.setY(qBound(available.top(), pos.y(),
                            available.bottom() - m_panelDock->height() + 1));
            m_panelDock->move(pos);
        }
    }
    if (m_actionRail) {
        for (QAction *action : m_actionRail->actions())
            if (action->isCheckable() && action->data().isValid())
                action->setChecked(action->data().toInt() == index);
    }
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(QStringLiteral("文件"));
    auto *load = file->addAction(QStringLiteral("加载设备配置…"));
    load->setShortcut(QKeySequence::Open);
    auto *save = file->addAction(QStringLiteral("保存设备配置…"));
    save->setShortcut(QKeySequence::Save);
    auto *play = file->addAction(QStringLiteral("打开原始帧回放…"));
    file->addSeparator();
    auto *quit = file->addAction(QStringLiteral("退出"));
    connect(load, &QAction::triggered, this, &MainWindow::loadConfig);
    connect(save, &QAction::triggered, this, &MainWindow::saveConfig);
    connect(play, &QAction::triggered, this, &MainWindow::openRecording);
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    auto *tools = menuBar()->addMenu(QStringLiteral("工具"));
    auto *remoteDesktop = tools->addAction(QStringLiteral("打开远程桌面…"));
    remoteDesktop->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(remoteDesktop, &QAction::triggered, this, &MainWindow::openRemoteDesktop);
    auto *robotControl = tools->addAction(QStringLiteral("切换到小车运动控制"));
    robotControl->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    connect(robotControl, &QAction::triggered, this, &MainWindow::openRobotControl);

    auto *information = menuBar()->addMenu(QStringLiteral("信息"));
    auto *robotInformation = information->addAction(QStringLiteral("小车信息"));
    connect(robotInformation, &QAction::triggered, this, [this] {
        if (m_robotPanel)
            m_robotPanel->showInformationDialog(this);
    });
}

void MainWindow::openRemoteDesktop()
{
    if (m_remoteDesktopProcess && m_remoteDesktopProcess->state() != QProcess::NotRunning) {
        showMessage(QStringLiteral("远程桌面已经在运行。"), false);
        return;
    }

    QSettings settings;
    QString executable = settings.value(QStringLiteral("remoteDesktop/executablePath"))
                             .toString().trimmed();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("RemoteDesktop/LanRemoteQt.exe")),
        QDir(appDir).filePath(QStringLiteral("LanRemoteQt.exe")),
        QStandardPaths::findExecutable(QStringLiteral("LanRemoteQt.exe"))
    };
    if (executable.isEmpty() || !QFileInfo::exists(executable)) {
        executable.clear();
        for (const QString &candidate : candidates) {
            if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
                executable = QFileInfo(candidate).absoluteFilePath();
                break;
            }
        }
    }
    if (executable.isEmpty()) {
        executable = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 LanRemoteQt 远程桌面程序"), appDir,
            QStringLiteral("远程桌面 (LanRemoteQt.exe);;可执行程序 (*.exe)"));
    }
    if (executable.isEmpty())
        return;
    if (QFileInfo(executable).fileName().compare(QStringLiteral("LanRemoteQt.exe"),
                                                Qt::CaseInsensitive) != 0) {
        showMessage(QStringLiteral("请选择 LanRemoteQt.exe。"), true);
        return;
    }

    settings.setValue(QStringLiteral("remoteDesktop/executablePath"), executable);
    if (!m_remoteDesktopProcess) {
        m_remoteDesktopProcess = new QProcess(this);
        connect(m_remoteDesktopProcess, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError) {
                    showMessage(QStringLiteral("远程桌面启动失败：%1")
                                    .arg(m_remoteDesktopProcess->errorString()), true);
                });
        connect(m_remoteDesktopProcess,
                qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    if (status == QProcess::CrashExit)
                        showMessage(QStringLiteral("远程桌面异常退出（%1）。").arg(exitCode), true);
                });
    }
    m_remoteDesktopProcess->setProgram(executable);
    m_remoteDesktopProcess->setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_remoteDesktopProcess->start();
    showMessage(QStringLiteral("正在启动远程桌面；PA-1664 本机控制接口已就绪。"), false);
}

void MainWindow::openRobotControl()
{
    if (m_workspace && m_robotPanel)
        m_workspace->setCurrentWidget(m_robotPanel);
    showMessage(QStringLiteral("已切换到小车运动控制页面。"), false);
}

void MainWindow::connectDevice()
{
    if (m_device->serviceConnected()) {
        m_device->disconnectDevice();
        return;
    }
    if (m_device->connectDevice(m_address->text().trimmed(), m_deviceId->value())) {
        m_parameters->refreshGroups();
        m_device->refreshGeometry();
    }
}

void MainWindow::acceptDecodedFrame(DecodedFrame frame, int deviceId)
{
    if (deviceId != m_deviceId->value())
        return;
    m_lastFrame = std::move(frame);
    renderCurrentFrame(true);
}

void MainWindow::updateDecoderSettings()
{
    if (!m_device || !m_beam || !m_cPrecision)
        return;
    // Bit 0: E scan, bit 1: B scan, bit 2: C scan.  The modes are independent
    // of the currently visible tab and may be enabled in any combination.
    const int scanModes = (m_eScanToggle->isChecked() ? 0x1 : 0)
                          | (m_bScanToggle->isChecked() ? 0x2 : 0)
                          | (m_cScanToggle->isChecked() ? 0x4 : 0);
    emit decoderSettingsChanged(m_device->groupOffsetWords(),
                                m_device->beamCount(),
                                m_device->pointCount(),
                                scanModes,
                                qMax(0, m_beam->value() - 1),
                                qMax(1, m_device->maxAmplitudeValue()),
                                m_cPrecision ? m_cPrecision->value() : 10);
}

void MainWindow::acceptCScanBatch(DecodedFrameBatch frames, int deviceId)
{
    if (deviceId != m_deviceId->value() || frames.isEmpty())
        return;
    const double scale = PacketDecoder::amplitudeScale(m_device->maxAmplitudeValue());
    if (m_bScanToggle->isChecked())
        m_bScan->appendFrames(frames, scale, 0);
    if (m_cScanToggle->isChecked())
        m_cScan->appendFrames(frames, scale, 0);
    m_lastFrame = frames.constLast();

    if (m_auxUiClock.elapsed() >= 100) {
        m_auxUiClock.restart();
        if (!m_lastFrame.measurements.isEmpty()) {
            const auto &m = m_lastFrame.measurements.first();
            m_measurements->setText(QStringLiteral("幅值 A %1 | B %2 | C %3 | I %4")
                                        .arg(m.amplitudeA).arg(m.amplitudeB)
                                        .arg(m.amplitudeC).arg(m.amplitudeI));
        }
        const auto &t = m_lastFrame.tail;
        m_tailInfo->setText(QStringLiteral("帧号 %1%2 | 编码器 A %3 / B %4 / C %5 | 时间戳 %6")
                                .arg(t.frameNumber)
                                .arg(t.frameError ? QStringLiteral("（帧错误）") : QString())
                                .arg(t.encoder[0]).arg(t.encoder[1])
                                .arg(t.encoder[2]).arg(t.timestamp));
        m_encoderStatus->setText(QStringLiteral("编码器 A %1 / B %2")
                                     .arg(t.encoder[0]).arg(t.encoder[1]));
    }
}

void MainWindow::renderCurrentFrame(bool newlyAcquired)
{
    const int sourceBeamCount = m_lastFrame.sourceBeamCount > 0
                                    ? m_lastFrame.sourceBeamCount
                                    : qMax(m_lastFrame.beams.size(), m_lastFrame.measurements.size());
    if (sourceBeamCount <= 0) return;
    m_beam->setMaximum(sourceBeamCount);
    const int index = m_lastFrame.selectedSourceBeam >= 0
                          ? 0
                          : qBound(0, m_beam->value() - 1, sourceBeamCount - 1);
    const double scale = PacketDecoder::amplitudeScale(m_device->maxAmplitudeValue());
    if (m_aScan->isVisible() && index < m_lastFrame.beams.size())
        m_aScan->setWaveform(m_lastFrame.beams[index], scale, m_rangeStart, m_rangeEnd);
    if (newlyAcquired) {
        // E-scan decoding deliberately produces a ready-to-paint image without
        // retaining every beam waveform.  Requiring beams here discarded those
        // valid E-scan frames and left the widget on "waiting for data".
        if (m_eScanToggle->isChecked()
            && (!m_lastFrame.eScanImage.isNull() || !m_lastFrame.beams.isEmpty()))
            m_eScan->setFrame(m_lastFrame, scale);
    }
    // Text shaping/layout for rapidly changing labels is surprisingly costly
    // on the GUI thread and adds no value at waveform refresh speed.  Keep the
    // waveform at 60 Hz while updating numeric diagnostics at 10 Hz.
    if (newlyAcquired && m_auxUiClock.elapsed() >= 100) {
        m_auxUiClock.restart();
        if (index < m_lastFrame.measurements.size()) {
            const auto &m = m_lastFrame.measurements[index];
            m_measurements->setText(QStringLiteral("幅值 A %1 | B %2 | C %3 | I %4")
                                        .arg(m.amplitudeA).arg(m.amplitudeB).arg(m.amplitudeC).arg(m.amplitudeI));
        }
        const auto &t = m_lastFrame.tail;
        m_tailInfo->setText(QStringLiteral("帧号 %1%2 | 编码器 A %3 / B %4 / C %5 | 时间戳 %6")
                                .arg(t.frameNumber).arg(t.frameError ? QStringLiteral("（帧错误）") : QString())
                                .arg(t.encoder[0]).arg(t.encoder[1]).arg(t.encoder[2]).arg(t.timestamp));
        m_encoderStatus->setText(QStringLiteral("编码器 A %1 / B %2")
                                     .arg(t.encoder[0]).arg(t.encoder[1]));
    }
}

void MainWindow::updateStatus()
{
    const bool connected = m_device->serviceConnected();
    const bool online = m_device->hardwareOnline();
    m_connect->setText(connected ? QStringLiteral("断开 SDK") : QStringLiteral("连接 SDK"));
    m_serviceStatus->setText(connected ? QStringLiteral("● SDK 已连接") : QStringLiteral("● SDK 未连接"));
    m_serviceStatus->setStyleSheet(connected ? "color:#1976d2;font-weight:600" : "color:#77838f");
    m_hardwareStatus->setText(online ? QStringLiteral("● USB 硬件在线")
                                    : (m_device->capturing() ? QStringLiteral("● 等待硬件帧")
                                       : (m_device->startPending() ? QStringLiteral("● 开始已排队")
                                          : (connected && !m_device->captureReady() ? QStringLiteral("● SDK 初始化中")
                                                                                   : QStringLiteral("● 硬件未确认")))));
    m_hardwareStatus->setStyleSheet(online ? "color:#159447;font-weight:700"
                                           : (m_device->capturing() ? "color:#07a0a0;font-weight:700" : "color:#77838f"));
    m_startAction->setEnabled(!m_device->capturing() && !m_device->startPending());
    m_stopAction->setEnabled(m_device->capturing());
    m_groupStatus->setText(QStringLiteral("工作组 %1").arg(m_device->currentGroup()));
    m_statusCaptureDetail->setText(
        online ? QStringLiteral("硬件在线，正在采集")
               : (m_device->capturing() ? QStringLiteral("等待硬件帧")
                  : (m_device->startPending() ? QStringLiteral("开始已排队")
                     : (connected && !m_device->captureReady() ? QStringLiteral("SDK 初始化中")
                        : (connected ? QStringLiteral("已连接，未采集") : QStringLiteral("未连接"))))));
}

void MainWindow::updateParametersForPlot()
{
    m_rangeStart = Client::getInstance().getRangeStart();
    m_rangeEnd = Client::getInstance().getRangeEnd();
    m_bScan->setRange(m_rangeStart, m_rangeEnd);
    m_aScan->setBipolar(Client::getInstance().getRectifierMode() == RectifierType::Rectifier_RF);
    m_aScan->setGates(m_parameters->currentGates());
    m_aScan->setActiveGate(m_parameters->activeGate());
    m_aScan->setSurfaceTracking(m_parameters->surfaceTrackingEnabled(),
                                m_parameters->surfaceWindowStart(),
                                m_parameters->surfaceWindowEnd(),
                                m_parameters->surfaceThreshold(),
                                m_parameters->surfaceHoldMissing(),
                                m_parameters->surfaceTrackedGates());
    updateImagingGates();
    m_device->refreshGeometry();
    if (!m_lastFrame.beams.isEmpty()) renderCurrentFrame(false);
}

void MainWindow::updateImagingGates()
{
    if (!m_parameters || !m_eImagingGate || !m_bImagingGate || !m_cImagingGate)
        return;
    const QVector<QVector<double>> gates = m_parameters->currentGates();
    const auto gateAt = [&gates](int index) {
        return index >= 0 && index < gates.size() && gates[index].size() >= 4
            ? gates[index] : QVector<double>{0.0, 0.0, 0.0, 0.0};
    };
    const QString gateLetters = QStringLiteral("ABCI");
    for (QComboBox *selector : {m_eImagingGate, m_bImagingGate, m_cImagingGate}) {
        for (int i = 0; i < 4; ++i) {
            const QVector<double> gate = gateAt(i);
            selector->setItemText(i, gate[0] >= 0.5
                ? QStringLiteral("Gate %1").arg(gateLetters.mid(i, 1))
                : QStringLiteral("Gate %1（未启用）").arg(gateLetters.mid(i, 1)));
        }
    }

    const int eIndex = m_eImagingGate->currentIndex();
    const QVector<double> eGate = gateAt(eIndex);
    m_eScan->setImagingGate(eGate[0] >= 0.5, eGate[1], eGate[2],
                            m_rangeStart, m_rangeEnd);

    const int bIndex = m_bImagingGate->currentIndex();
    const QVector<double> bGate = gateAt(bIndex);
    m_bScan->setImagingGate(bGate[0] >= 0.5, bGate[1], bGate[2]);

    const int cIndex = m_cImagingGate->currentIndex();
    const QVector<double> cGate = gateAt(cIndex);
    m_cScan->setImagingGate(cIndex, cGate[0] >= 0.5, cGate[3]);
}

void MainWindow::saveConfig()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存设备配置"),
                                                       QString(), QStringLiteral("JSON 配置 (*.json)"));
    if (path.isEmpty()) return;
    const bool ok = Client::getInstance().saveConfig(path.toStdString());
    showMessage(ok ? QStringLiteral("配置已保存：%1").arg(path) : QStringLiteral("保存配置失败"), !ok);
}

void MainWindow::loadConfig()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载设备配置"),
                                                       QString(), QStringLiteral("JSON 配置 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    const bool ok = Client::getInstance().loadConfig(path.toStdString());
    if (ok) {
        Client::getInstance().pushConfigToDevice();
        m_parameters->refreshGroups();
        m_device->refreshGeometry();
    }
    showMessage(ok ? QStringLiteral("配置已加载并推送：%1").arg(path) : QStringLiteral("加载配置失败"), !ok);
}

void MainWindow::toggleRecording()
{
    if (m_recorder->isRecording()) {
        m_recorder->stop();
        return;
    }
    const QString suggested = QStringLiteral("PA1664_%1.pa16raw").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存原始超声帧"), suggested,
                                                       QStringLiteral("PA1664 原始帧 (*.pa16raw)"));
    if (!path.isEmpty() && m_recorder->start(path, m_device->pointCount(), m_device->beamCount(), m_device->currentGroup()))
        Client::getInstance().saveConfig((path + ".json").toStdString());
}

void MainWindow::openRecording()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开原始超声帧"), QString(),
                                                       QStringLiteral("PA1664 原始帧 (*.pa16raw)"));
    if (path.isEmpty()) return;
    m_device->stop();
    QString error;
    if (!m_player->open(path, &error)) {
        showMessage(QStringLiteral("无法打开回放：%1").arg(error), true);
        return;
    }
    m_bScan->clear();
    m_cScan->clear();
    showMessage(QStringLiteral("已载入 %1 帧，开始回放").arg(m_player->frameCount()), false);
    m_player->play();
}

void MainWindow::processPlaybackFrame(const QByteArray &packet, int pointCount, int beamCount)
{
    QString error;
    DecodedFrame frame;
    if (!PacketDecoder::decode(packet, 0, beamCount, pointCount, frame, &error)) {
        showMessage(QStringLiteral("回放帧解析失败：%1").arg(error), true);
        m_player->pause();
        return;
    }
    m_lastFrame = std::move(frame);
    m_beam->setMaximum(m_lastFrame.beams.size());
    const int index = qBound(0, m_beam->value() - 1, m_lastFrame.beams.size() - 1);
    const double scale = PacketDecoder::amplitudeScale(m_device->maxAmplitudeValue());
    m_aScan->setWaveform(m_lastFrame.beams[index], scale, m_rangeStart, m_rangeEnd);
    m_eScan->setFrame(m_lastFrame, scale);
    m_bScan->appendFrame(m_lastFrame, scale, index);
    m_cScan->appendFrame(m_lastFrame, scale, index);
    const auto &t = m_lastFrame.tail;
    m_tailInfo->setText(QStringLiteral("回放帧 %1 | 编码器 A %2 / B %3 | 时间戳 %4")
                            .arg(t.frameNumber).arg(t.encoder[0]).arg(t.encoder[1]).arg(t.timestamp));
    m_encoderStatus->setText(QStringLiteral("编码器 A %1 / B %2")
                                 .arg(t.encoder[0]).arg(t.encoder[1]));
}

void MainWindow::showMessage(const QString &text, bool error)
{
    statusBar()->showMessage(text, error ? 8000 : 5000);
    if (error) statusBar()->setStyleSheet("QStatusBar{color:#b42318}");
    else statusBar()->setStyleSheet("QStatusBar{color:#245c38}");
}
