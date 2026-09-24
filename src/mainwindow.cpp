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
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
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
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("服务地址")));
    m_address = new QLineEdit("127.0.0.1");
    m_address->setMaximumWidth(150);
    top->addWidget(m_address);
    top->addWidget(new QLabel(QStringLiteral("设备 ID")));
    m_deviceId = new QSpinBox;
    m_deviceId->setRange(0, 63);
    top->addWidget(m_deviceId);
    m_connect = new QPushButton(QStringLiteral("连接 SDK"));
    m_start = new QPushButton(QStringLiteral("开始采集  F5"));
    m_stop = new QPushButton(QStringLiteral("停止采集  F6"));
    auto *reset = new QPushButton(QStringLiteral("编码器复位"));
    m_record = new QPushButton(QStringLiteral("保存原始帧"));
    top->addWidget(m_connect);
    top->addWidget(m_start);
    top->addWidget(m_stop);
    top->addWidget(reset);
    top->addWidget(m_record);
    top->addStretch();
    m_serviceStatus = new QLabel;
    m_hardwareStatus = new QLabel;
    top->addWidget(m_serviceStatus);
    top->addWidget(m_hardwareStatus);
    root->addLayout(top);

    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto *viewHeader = new QHBoxLayout;
    viewHeader->addWidget(new QLabel(QStringLiteral("显示声束")));
    m_beam = new QSpinBox;
    m_beam->setRange(1, 1);
    viewHeader->addWidget(m_beam);
    viewHeader->addWidget(new QLabel(QStringLiteral("B/C 路径精度")));
    m_cPrecision = new QSpinBox;
    m_cPrecision->setRange(1, 1000000);
    m_cPrecision->setValue(10);
    m_cPrecision->setSuffix(QStringLiteral(" 计数/格"));
    m_cPrecision->setToolTip(QStringLiteral(
        "编码器每变化多少个原始计数生成一个 B 扫列/C 扫网格。数值越小，空间采样越细。"));
    m_cPrecision->setMaximumWidth(165);
    viewHeader->addWidget(m_cPrecision);
    m_measurements = new QLabel(QStringLiteral("Gate A/B/C/I: --"));
    viewHeader->addWidget(m_measurements, 1);
    m_eScanToggle = new QPushButton(QStringLiteral("启动 E 扫"));
    m_eScanToggle->setCheckable(true);
    m_bScanToggle = new QPushButton(QStringLiteral("启动 B 扫"));
    m_bScanToggle->setCheckable(true);
    m_cScanToggle = new QPushButton(QStringLiteral("启动 C 扫"));
    m_cScanToggle->setCheckable(true);
    viewHeader->addWidget(m_eScanToggle);
    viewHeader->addWidget(m_bScanToggle);
    viewHeader->addWidget(m_cScanToggle);
    leftLayout->addLayout(viewHeader);
    m_views = new QTabWidget;
    m_aScan = new AScanWidget;
    m_eScan = new EScanWidget;
    m_bScan = new BScanWidget;
    m_cScan = new CScanWidget;
    m_bScan->setEncoderPrecision(m_cPrecision->value());
    m_cScan->setEncoderPrecision(m_cPrecision->value());
    m_views->addTab(m_aScan, QStringLiteral("A 扫波形"));
    m_views->addTab(m_eScan, QStringLiteral("E 扫截面"));
    m_views->addTab(m_bScan, QStringLiteral("编码器 B 扫（路径×深度）"));
    m_views->addTab(m_cScan, QStringLiteral("编码器 C 扫（A×B 平面）"));
    // A-scan is the lightweight default. E/B/C processing is opt-in; the
    // three scan modes are independent and may run in any combination.
    m_views->setTabEnabled(1, false);
    m_views->setTabEnabled(2, false);
    m_views->setTabEnabled(3, false);
    leftLayout->addWidget(m_views, 1);
    m_tailInfo = new QLabel(QStringLiteral("帧号 -- | 编码器 A/B -- | 时间戳 --"));
    leftLayout->addWidget(m_tailInfo);

    m_parameters = new ParameterPanel;
    m_parameters->setMinimumWidth(370);
    auto *parameterTabs = new QTabWidget;
    parameterTabs->setDocumentMode(true);
    parameterTabs->setMinimumWidth(370);
    parameterTabs->addTab(m_parameters, QStringLiteral("超声参数"));
    parameterTabs->addTab(new ProbeAdjustmentPanel, QStringLiteral("探头调节"));
    splitter->addWidget(left);
    splitter->addWidget(parameterTabs);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({1000, 440});
    root->addWidget(splitter, 1);
    m_workspace = new QTabWidget;
    m_workspace->setObjectName(QStringLiteral("workspaceTabs"));
    m_workspace->setDocumentMode(true);
    m_workspace->addTab(central, QStringLiteral("超声检测"));
    m_robotPanel = new RobotControlPanel;
    m_workspace->addTab(m_robotPanel, QStringLiteral("小车控制"));
    setCentralWidget(m_workspace);

    m_start->setShortcut(QKeySequence(Qt::Key_F5));
    m_stop->setShortcut(QKeySequence(Qt::Key_F6));
    connect(m_connect, &QPushButton::clicked, this, &MainWindow::connectDevice);
    connect(m_start, &QPushButton::clicked, m_device, &DeviceController::start);
    connect(m_stop, &QPushButton::clicked, m_device, &DeviceController::stop);
    connect(reset, &QPushButton::clicked, m_device, &DeviceController::resetEncoder);
    connect(reset, &QPushButton::clicked, m_bScan, &BScanWidget::clear);
    connect(reset, &QPushButton::clicked, m_cScan, &CScanWidget::clear);
    connect(m_record, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(m_beam, &QSpinBox::valueChanged, this, [this] {
        updateDecoderSettings();
        if (m_bScanToggle->isChecked())
            m_bScan->clear();
        if (m_cScanToggle->isChecked())
            m_cScan->clear();
        if (!m_lastFrame.beams.isEmpty()) renderCurrentFrame(false);
    });
    // Page selection only controls what the operator is looking at. Scan
    // processing is controlled independently by the three toggle buttons.
    connect(m_eScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_eScanToggle->setText(enabled ? QStringLiteral("停止 E 扫")
                                       : QStringLiteral("启动 E 扫"));
        m_views->setTabEnabled(1, enabled);
        if (enabled) {
            m_views->setCurrentIndex(1);
        } else if (m_views->currentIndex() == 1) {
            m_views->setCurrentIndex(0);
        }
        updateDecoderSettings();
    });
    connect(m_bScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_bScanToggle->setText(enabled ? QStringLiteral("停止 B 扫")
                                       : QStringLiteral("启动 B 扫"));
        m_views->setTabEnabled(2, enabled);
        if (enabled) {
            m_bScan->clear();
            m_views->setCurrentIndex(2);
        } else if (m_views->currentIndex() == 2) {
            m_views->setCurrentIndex(0);
        }
        updateDecoderSettings();
    });
    connect(m_cScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_cScanToggle->setText(enabled ? QStringLiteral("停止 C 扫")
                                       : QStringLiteral("启动 C 扫"));
        m_views->setTabEnabled(3, enabled);
        if (enabled) {
            m_cScan->clear();
            m_views->setCurrentIndex(3);
        } else if (m_views->currentIndex() == 3) {
            m_views->setCurrentIndex(0);
        }
        updateDecoderSettings();
    });
    connect(m_cPrecision, &QSpinBox::valueChanged, this, [this](int precision) {
        m_bScan->setEncoderPrecision(precision);
        m_cScan->setEncoderPrecision(precision);
        updateDecoderSettings();
    });
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

    auto *device = menuBar()->addMenu(QStringLiteral("设备"));
    device->addAction(QStringLiteral("连接 SDK"), this, &MainWindow::connectDevice);
    auto *start = device->addAction(QStringLiteral("开始采集"));
    start->setShortcut(QKeySequence(Qt::Key_F5));
    connect(start, &QAction::triggered, m_device, &DeviceController::start);
    auto *stop = device->addAction(QStringLiteral("停止采集"));
    stop->setShortcut(QKeySequence(Qt::Key_F6));
    connect(stop, &QAction::triggered, m_device, &DeviceController::stop);

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
    if (!m_device || !m_views || !m_beam)
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
                                           : (m_device->capturing() ? "color:#e07a00;font-weight:700" : "color:#77838f"));
    m_start->setText(m_device->startPending() ? QStringLiteral("等待自动启动…")
                                              : QStringLiteral("开始采集  F5"));
    m_start->setEnabled(!m_device->capturing() && !m_device->startPending());
    m_stop->setEnabled(m_device->capturing());
}

void MainWindow::updateParametersForPlot()
{
    m_rangeStart = Client::getInstance().getRangeStart();
    m_rangeEnd = Client::getInstance().getRangeEnd();
    m_bScan->setRange(m_rangeStart, m_rangeEnd);
    m_aScan->setBipolar(Client::getInstance().getRectifierMode() == RectifierType::Rectifier_RF);
    m_aScan->setGates(m_parameters->currentGates());
    m_device->refreshGeometry();
    if (!m_lastFrame.beams.isEmpty()) renderCurrentFrame(false);
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
}

void MainWindow::showMessage(const QString &text, bool error)
{
    statusBar()->showMessage(text, error ? 8000 : 5000);
    if (error) statusBar()->setStyleSheet("QStatusBar{color:#b42318}");
    else statusBar()->setStyleSheet("QStatusBar{color:#245c38}");
}
