#include "mainwindow.h"

#include "client.h"
#include "clamp_motor_controller.h"
#include "devicecontroller.h"
#include "framerecorder.h"
#include "framedecoderworker.h"
#include "frameplayer.h"
#include "parameterpanel.h"
#include "parameterpresetstore.h"
#include "probeadjustmentpanel.h"
#include "remotecontrolserver.h"
#include "robotcontrolpanel.h"
#include "scanwidgets.h"
#include "scanwaveformcache.h"
#include <QAction>
#include <QActionGroup>
#include <QWidgetAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDesktopServices>
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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUrl>
#include <cmath>

namespace {
QString readableBytes(qint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    double value = qMax<qint64>(0, bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, unit == 0 ? 'f' : 'f', unit == 0 ? 0 : 2)
        .arg(QString::fromLatin1(units[unit]));
}

bool copyDeviceBytes(QIODevice &source, QIODevice &target, qint64 byteCount)
{
    QByteArray buffer;
    buffer.resize(1024 * 1024);
    qint64 remaining = byteCount;
    while (remaining > 0) {
        const qint64 chunk = qMin<qint64>(remaining, buffer.size());
        const qint64 read = source.read(buffer.data(), chunk);
        if (read != chunk || target.write(buffer.constData(), read) != read)
            return false;
        remaining -= read;
    }
    return true;
}

int waveformScore(const GroupScanFrame &item)
{
    if (item.frame.beams.isEmpty())
        return -1;
    int score = 0;
    for (qint16 sample : item.frame.beams.first())
        score = qMax(score, qAbs(int(sample)));
    return score;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    QString presetModeMessage;
    QString presetModeError;
    const bool presetModeReady = ParameterPresetStore::prepare(&presetModeMessage,
                                                                &presetModeError);
    m_activePresetId = ParameterPresetStore::currentPresetId();
    m_device = new DeviceController(this);
    m_recorder = new FrameRecorder(this);
    m_player = new FramePlayer(this);
    m_scanCachePath = QDir(QCoreApplication::applicationDirPath())
                          .filePath(QStringLiteral("scan_cache/latest_scan_ascan.bin"));
    buildUi();
    buildMenus();
    if (!presetModeReady)
        QTimer::singleShot(0, this, [this, presetModeError] {
            showMessage(QStringLiteral("参数预设模式初始化失败：%1").arg(presetModeError), true);
        });
    else if (!presetModeMessage.isEmpty())
        QTimer::singleShot(0, this, [this, presetModeMessage] {
            showMessage(presetModeMessage, false);
        });
    m_remoteControl = new RemoteControlServer(m_device, m_parameters, this, this);
    QString remoteControlError;
    if (!m_remoteControl->start(&remoteControlError)) {
        showMessage(QStringLiteral("远程控制服务启动失败：%1").arg(remoteControlError), true);
    }
    m_auxUiClock.start();

    qRegisterMetaType<DecodedFrame>("DecodedFrame");
    qRegisterMetaType<DecodedFrameBatch>("DecodedFrameBatch");
    qRegisterMetaType<GroupScanFrameBatch>("GroupScanFrameBatch");
    qRegisterMetaType<QVector<GroupScanSettings>>("QVector<GroupScanSettings>");
    qRegisterMetaType<RawPacketBuffer>("RawPacketBuffer");

    auto *scanUiTimer = new QTimer(this);
    scanUiTimer->setTimerType(Qt::CoarseTimer);
    scanUiTimer->setInterval(40);
    connect(scanUiTimer, &QTimer::timeout,
            this, &MainWindow::drainPendingGroupScanBatches);
    scanUiTimer->start();

    m_waveformCacheThread = new QThread(this);
    m_waveformCache = new ScanWaveformCache(m_scanCachePath);
    m_waveformCache->moveToThread(m_waveformCacheThread);
    connect(m_waveformCacheThread, &QThread::started,
            m_waveformCache, &ScanWaveformCache::initialize);
    connect(m_waveformCacheThread, &QThread::finished,
            m_waveformCache, &QObject::deleteLater);
    connect(this, &MainWindow::bWaveformRequested,
            m_waveformCache, &ScanWaveformCache::requestBWaveform,
            Qt::QueuedConnection);
    connect(this, &MainWindow::cWaveformRequested,
            m_waveformCache, &ScanWaveformCache::requestCWaveform,
            Qt::QueuedConnection);
    connect(this, &MainWindow::waveformCacheReset,
            m_waveformCache, &ScanWaveformCache::resetAll,
            Qt::QueuedConnection);
    connect(this, &MainWindow::waveformCacheGroupReset,
            m_waveformCache, &ScanWaveformCache::resetGroupMode,
            Qt::QueuedConnection);
    connect(this, &MainWindow::waveformCacheStatisticsRequested,
            m_waveformCache, &ScanWaveformCache::requestStatistics,
            Qt::QueuedConnection);
    connect(this, &MainWindow::waveformCachingChanged,
            m_waveformCache, &ScanWaveformCache::setCachingEnabled,
            Qt::QueuedConnection);
    connect(m_waveformCache, &ScanWaveformCache::warning, this,
            [this](const QString &text) { showMessage(text, true); },
            Qt::QueuedConnection);
    connect(m_waveformCache, &ScanWaveformCache::waveformReady, this,
            [this](int groupId, QVector<qint16> samples, int amplitudeScale,
                   double rangeStart, double rangeEnd, double cursorRange,
                   const QString &sourceText) {
        if (groupId != currentGroupId())
            return;
        m_device->stop();
        m_reviewingScanWaveform = true;
        m_aScan->setReviewCursor(cursorRange, std::isfinite(cursorRange));
        m_aScan->setWaveform(samples, PacketDecoder::amplitudeScale(amplitudeScale),
                             rangeStart, rangeEnd);
        showMessage(QStringLiteral("正在回看%1对应的 A 扫，采集已自动停止；需要实时波形时请点击右侧“开始采集”。")
                        .arg(sourceText), false);
    }, Qt::QueuedConnection);
    connect(m_waveformCache, &ScanWaveformCache::statisticsChanged, this,
            [this](const QString &path, bool enabled, qint64 bytes,
                   quint64 records, int bPoints, int cPoints,
                   const QString &groups, const QString &updatedAt) {
        if (m_cacheStatus) {
            if (!m_scanSessionActive) {
                m_cacheStatus->setText(QStringLiteral("● 缓存未启动"));
                m_cacheStatus->setStyleSheet(QStringLiteral("color:#77838f;font-weight:700"));
            } else if (m_scanCachingPaused) {
                m_cacheStatus->setText(QStringLiteral("● 缓存已暂停"));
                m_cacheStatus->setStyleSheet(QStringLiteral("color:#e68a00;font-weight:700"));
            } else if (enabled) {
                m_cacheStatus->setText(QStringLiteral("● 正在缓存"));
                m_cacheStatus->setStyleSheet(QStringLiteral("color:#0a9b55;font-weight:700"));
            } else {
                m_cacheStatus->setText(QStringLiteral("● 缓存不可用"));
                m_cacheStatus->setStyleSheet(QStringLiteral("color:#d94841;font-weight:700"));
            }
        }
        if (m_cachePathLabel) {
            m_cachePathLabel->setText(QDir::toNativeSeparators(path));
            m_cachePathLabel->setToolTip(QDir::toNativeSeparators(path));
        }
        if (m_cacheSizeLabel)
            m_cacheSizeLabel->setText(QStringLiteral("%1（%2 条波形记录）")
                                          .arg(readableBytes(bytes)).arg(records));
        if (m_cacheContentLabel)
            m_cacheContentLabel->setText(QStringLiteral("B 扫 %1 点，C 扫 %2 格\n%3")
                                             .arg(bPoints).arg(cPoints).arg(groups));
        if (m_cacheUpdatedLabel)
            m_cacheUpdatedLabel->setText(updatedAt);
    }, Qt::QueuedConnection);
    m_waveformCacheThread->start();

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
    connect(m_decoder, &FrameDecoderWorker::groupScanBatchDecoded,
            this, &MainWindow::enqueueGroupScanBatch, Qt::DirectConnection);
    connect(m_decoder, &FrameDecoderWorker::groupScanBatchDecoded,
            m_waveformCache, &ScanWaveformCache::enqueueBatch,
            Qt::DirectConnection);
    connect(m_decoder, &FrameDecoderWorker::decodeFailed, this, [this](const QString &error) {
        statusBar()->showMessage(error, 2000);
    }, Qt::QueuedConnection);
    m_decoderThread->start();
    updateDecoderSettings();
    connect(m_decoder, &FrameDecoderWorker::recordingPacketReady,
            m_recorder, &FrameRecorder::append, Qt::QueuedConnection);
    connect(m_device, &DeviceController::statusChanged, this, [this] {
        updateStatus();
        const bool decoderReady = m_device->captureReady()
                                  || m_device->capturing()
                                  || m_device->hardwareOnline();
        m_parameters->setEnabled(decoderReady);
        if (!decoderReady) {
            m_decoderSdkReady = false;
            updateDecoderSettings(); // cached controller geometry only
            return;
        }
        m_decoderSdkReady = true;
        if (!m_parameterPresetsLoaded) {
            m_parameterPresetsLoaded = true;
            // Refresh the group selector once, after repository/configuration
            // settling. Doing this directly after Connect races SDK Init.
            m_parameters->refreshGroups();
        } else {
            updateDecoderSettings();
        }
    });
    connect(m_device, &DeviceController::message, this, &MainWindow::showMessage);
    connect(m_parameters, &ParameterPanel::configurationChanged, this, &MainWindow::updateParametersForPlot);
    connect(m_parameters, &ParameterPanel::encoderCalibrationChanged,
            this, &MainWindow::updateEncoderCalibration);
    connect(m_parameters, &ParameterPanel::groupChanged, this, [this] {
        m_activePresetId = m_parameters->currentPresetId();
        selectCurrentGroupViews();
        restoreCurrentGroupControls();
        updateParametersForPlot();
    });
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
        updateImagingGates();
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
        m_captureWasActiveBeforeApply = m_device->capturing()
                                        || m_device->startPending();
        if (m_device->serviceConnected()) {
            showMessage(QStringLiteral("正在停止采集并应用参数修改…"), false);
            m_device->stop();
        }
    });
    connect(m_parameters, &ParameterPanel::applyFinished, this, [this] {
        m_device->refreshGeometry();
        if (m_device->serviceConnected()) {
            // Respect the independent acquisition controls: only resume when
            // acquisition was running before the automatic parameter write.
            m_device->resumeAfterConfigurationChange(m_captureWasActiveBeforeApply);
        }
        m_captureWasActiveBeforeApply = false;
    });
    connect(m_parameters, &ParameterPanel::presetSwitchFinished, this, [this] {
        m_device->refreshGeometry();
        updateDecoderSettings();
        if (m_device->serviceConnected())
            m_device->resumeAfterPresetChange(m_captureWasActiveBeforeApply);
        m_captureWasActiveBeforeApply = false;
    });
    connect(m_recorder, &FrameRecorder::stateChanged, this, [this](bool recording, const QString &path) {
        emit decoderRecordingChanged(recording);
        m_record->setText(recording ? QStringLiteral("停止保存") : QStringLiteral("保存原始帧"));
        showMessage(recording ? QStringLiteral("正在保存：%1").arg(path)
                              : QStringLiteral("数据已保存：%1").arg(path), false);
        if (m_recordingPathLabel) {
            m_recordingPathLabel->setText(QDir::toNativeSeparators(path));
            m_recordingPathLabel->setToolTip(QDir::toNativeSeparators(path));
        }
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
    updateEncoderCalibration();
    updateStatus();
    // Group controls are populated after DeviceController reports its
    // configuration ready. Querying the vendor repository here used to race
    // its asynchronous Init/Connect sequence.
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
            m_device->connectDevice(m_address->text().trimmed(), m_deviceId->value());
        // Hardware acquisition starts automatically, but scan caching remains
        // off until the operator presses the separate right-side Start action.
        m_device->start();
    });
}

MainWindow::~MainWindow()
{
    if (m_recorder)
        m_recorder->stop();
    if (m_remoteDesktopProcess && m_remoteDesktopProcess->state() != QProcess::NotRunning) {
        m_remoteDesktopProcess->terminate();
        if (!m_remoteDesktopProcess->waitForFinished(1500))
            m_remoteDesktopProcess->kill();
    }
    if (m_decoderThread) {
        m_decoderThread->quit();
        m_decoderThread->wait();
    }
    if (m_waveformCacheThread) {
        QMetaObject::invokeMethod(m_waveformCache, "close",
                                  Qt::BlockingQueuedConnection);
        m_waveformCacheThread->quit();
        m_waveformCacheThread->wait();
    }
    // 关闭软件即清掉本次扫描缓存文件，下次启动从空缓存开始。
    QFile::remove(m_scanCachePath);
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
        #actionRail QToolButton#startScanButton:disabled { background:#c9ced4; color:#8a939c; border-color:#c9ced4; }
        #actionRail QToolButton#stopScanButton { background:#d94141; color:white; border-color:#bd3030; }
        #actionRail QToolButton#stopScanButton:disabled { background:#c9ced4; color:#8a939c; border-color:#c9ced4; }
        #actionRail QToolButton:disabled { background:#c9ced4; color:#8a939c; border-color:#c9ced4; }
        #actionRail QToolButton#startCaptureButton { background:#1677ff; color:white; border-color:#0f63d5; font-size:13px; }
        #actionRail QToolButton#stopCaptureButton { background:#f59e0b; color:white; border-color:#d98600; font-size:13px; }
        #actionRail QToolButton#closeProgramButton { background:#fff4f4; color:#c73535; border-color:#e9aaaa; }
        #actionRail QToolButton#closeProgramButton:hover { background:#d94141; color:white; border-color:#bd3030; }
        #panelDock { background:white; border:1px solid #cddbe8; }
        #panelTitleBar { background:#f8fbff; border-bottom:1px solid #d7e2ee; }
        #panelTitleBar QLabel { color:#102b4e; font-size:17px; font-weight:700; }
        #collapsePanelButton { border:1px solid #d2dfeb; border-radius:17px; background:white; color:#194c85; font-size:20px; }
    )"));

    // ===== 顶部全局状态栏（连接 / 硬件 / 参数预设 / 编码器） =====
    auto *globalBar = new QToolBar(QStringLiteral("状态"), this);
    globalBar->setObjectName(QStringLiteral("globalBar"));
    globalBar->setMovable(false);
    globalBar->setFloatable(false);
    m_serviceStatus = new QLabel;
    m_hardwareStatus = new QLabel;
    m_groupStatus = new QLabel(QStringLiteral("参数预设 -- | 硬件组 0"));
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
    aHeader->addWidget(new QLabel(QStringLiteral("采集")));
    m_captureToggle = new QSlider(Qt::Horizontal);
    m_captureToggle->setRange(0, 1);
    m_captureToggle->setFixedSize(52, 24);
    m_captureToggle->setToolTip(QStringLiteral("采集开关：右拨开始采集，左拨停止采集"));
    m_captureToggle->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height:20px; border-radius:10px; background:#c0c4cc; }"
        "QSlider::sub-page:horizontal { border-radius:10px; background:#13ad58; }"
        "QSlider::handle:horizontal { width:16px; height:16px; margin:2px 2px; border-radius:8px; background:white; }"));
    aHeader->addWidget(m_captureToggle);
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
    m_cPrecision = new QDoubleSpinBox;
    m_cPrecision->setRange(0.01, 100000.0);
    m_cPrecision->setDecimals(2);
    m_cPrecision->setValue(1.0);
    m_cPrecision->setSuffix(QStringLiteral(" mm/格"));
    m_cPrecision->setToolTip(QStringLiteral(
        "B 扫列/C 扫网格的空间分辨率（毫米/格）。数值越小，空间采样越细。"));
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
    m_bScanStack = new QStackedWidget;
    m_bScan = ensureBScan(currentGroupId());
    m_bScanToggle = new QPushButton(QStringLiteral("启动 B 扫"));
    m_bScanToggle->setCheckable(true);
    m_bImagingGate = new QComboBox;
    for (const QString &name : {QStringLiteral("Gate A"), QStringLiteral("Gate B"),
                                QStringLiteral("Gate C"), QStringLiteral("Gate I")})
        m_bImagingGate->addItem(name);
    auto *bPage = new QWidget;
    auto *bLayout = new QVBoxLayout(bPage);
    bLayout->setContentsMargins(4, 6, 4, 4);
    bLayout->setSpacing(4);

    // B 扫显示配置控件（统一收进「配置」下拉菜单，避免工具栏拥挤）。
    m_bViewMode = new QComboBox;
    m_bViewMode->addItem(QStringLiteral("幅值"), BScanWidget::AmplitudeView);
    m_bViewMode->addItem(QStringLiteral("轮廓/厚度"), BScanWidget::ProfileView);
    m_bPaletteMode = new QComboBox;
    m_bPaletteMode->addItem(QStringLiteral("彩色"), BScanWidget::HeatPalette);
    m_bPaletteMode->addItem(QStringLiteral("灰度"), BScanWidget::GrayscalePalette);
    m_bPaletteMode->addItem(QStringLiteral("RF 双极性"), BScanWidget::RfPalette);
    m_bScaleMode = new QComboBox;
    m_bScaleMode->addItem(QStringLiteral("固定 100%"), BScanWidget::FixedFullScale);
    m_bScaleMode->addItem(QStringLiteral("动态"), BScanWidget::DynamicScale);
    m_bDisplayReject = new QDoubleSpinBox;
    m_bDisplayReject->setRange(0.0, 99.0);
    m_bDisplayReject->setSuffix(QStringLiteral(" %"));
    m_bDisplayReject->setToolTip(QStringLiteral("只影响颜色显示，不修改原始幅值和闸门判定"));
    m_bDisplayLower = new QDoubleSpinBox;
    m_bDisplayLower->setRange(0.0, 99.0);
    m_bDisplayLower->setSuffix(QStringLiteral(" %"));
    m_bDisplayUpper = new QDoubleSpinBox;
    m_bDisplayUpper->setRange(1.0, 100.0);
    m_bDisplayUpper->setValue(100.0);
    m_bDisplayUpper->setSuffix(QStringLiteral(" %"));
    m_bGamma = new QDoubleSpinBox;
    m_bGamma->setRange(0.2, 3.0);
    m_bGamma->setDecimals(2);
    m_bGamma->setSingleStep(0.1);
    m_bGamma->setValue(1.0);
    m_bInterpolateMissing = new QCheckBox(QStringLiteral("显示插补小间隙"));
    m_bInterpolateMissing->setToolTip(QStringLiteral(
        "仅对后续不超过 64 格的小间隙插值，并用黄色透明层标记；原始漏点仍计入质量统计。"));

    auto *bBar = new QHBoxLayout;
    bBar->addWidget(m_bScanToggle);
    auto *bConfigButton = new QToolButton;
    bConfigButton->setText(QStringLiteral("配置"));
    bConfigButton->setPopupMode(QToolButton::InstantPopup);
    bConfigButton->setToolTip(QStringLiteral("B 扫显示参数"));
    bConfigButton->setStyleSheet(QStringLiteral(
        "QToolButton::menu-indicator { image: none; } padding:0 6px;"));
    auto *bConfigMenu = new QMenu(bConfigButton);
    bConfigButton->setMenu(bConfigMenu);
    bBar->addWidget(bConfigButton);
    bBar->addWidget(new QLabel(QStringLiteral("闸门")));
    bBar->addWidget(m_bImagingGate);
    bBar->addStretch();
    auto *resetBView = new QPushButton(QStringLiteral("复位视图"));
    bBar->addWidget(resetBView);
    bLayout->addLayout(bBar);

    // 「配置」下拉菜单：QFormLayout 承载全部显示配置控件。
    auto *configWidget = new QWidget;
    auto *configForm = new QFormLayout(configWidget);
    configForm->setContentsMargins(12, 8, 12, 8);
    configForm->setSpacing(6);
    configForm->addRow(QStringLiteral("视图"), m_bViewMode);
    configForm->addRow(QStringLiteral("色带"), m_bPaletteMode);
    configForm->addRow(QStringLiteral("刻度"), m_bScaleMode);
    configForm->addRow(QStringLiteral("显示低切"), m_bDisplayReject);
    auto *rangeWidget = new QWidget;
    auto *rangeLayout = new QHBoxLayout(rangeWidget);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->addWidget(m_bDisplayLower);
    rangeLayout->addWidget(new QLabel(QStringLiteral("～")));
    rangeLayout->addWidget(m_bDisplayUpper);
    configForm->addRow(QStringLiteral("色阶"), rangeWidget);
    configForm->addRow(QStringLiteral("Gamma"), m_bGamma);
    configForm->addRow(QStringLiteral("显示插补小间隙"), m_bInterpolateMissing);
    auto *configAction = new QWidgetAction(bConfigMenu);
    configAction->setDefaultWidget(configWidget);
    bConfigMenu->addAction(configAction);

    bLayout->addWidget(m_bScanStack, 1);
    m_mapTabs->addTab(bPage, QStringLiteral("编码器 B 扫"));
    m_cScanStack = new QStackedWidget;
    m_cScan = ensureCScan(currentGroupId());
    m_cScanToggle = new QPushButton(QStringLiteral("启动 C 扫"));
    m_cScanToggle->setCheckable(true);
    m_cImagingGate = new QComboBox;
    for (const QString &name : {QStringLiteral("Gate A"), QStringLiteral("Gate B"),
                                QStringLiteral("Gate C"), QStringLiteral("Gate I")})
        m_cImagingGate->addItem(name);
    m_mapTabs->addTab(buildScanPage(m_cScanStack, m_cScanToggle, m_cImagingGate), QStringLiteral("编码器 C 扫"));
    m_bScan->setEncoderPrecision(m_cPrecision->value());
    m_cScan->setEncoderPrecision(m_cPrecision->value());
    mapLayout->addWidget(m_mapTabs, 1);
    root->addWidget(mapCard, 3);

    m_workspace = new QTabWidget;
    m_workspace->setObjectName(QStringLiteral("workspaceTabs"));
    m_workspace->setDocumentMode(true);
    m_workspace->addTab(central, QStringLiteral("超声检测"));
    m_clampController = new crawling::ClampMotorController(m_workspace);
    m_robotPanel = new RobotControlPanel(m_clampController, nullptr);
    m_workspace->addTab(m_robotPanel, QStringLiteral("小车控制"));
    setCentralWidget(m_workspace);

    // ===== 右侧浮动面板（分区页：连接/状态/超声/数据） =====
    m_parameters = new ParameterPanel;
    m_parameters->setMinimumWidth(370);
    m_parameters->setEnabled(false);
    auto *probeAdjust = new ProbeAdjustmentPanel(m_clampController, nullptr);

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

    auto *cacheGroup = new QGroupBox(QStringLiteral("扫描点 A 扫缓存（随“启动/停止”控制）"));
    auto *cacheLayout = new QVBoxLayout(cacheGroup);
    auto *cacheForm = new QFormLayout;
    m_cacheStatus = new QLabel(QStringLiteral("● 正在初始化"));
    m_cacheStatus->setStyleSheet(QStringLiteral("color:#66788a;font-weight:700"));
    m_cachePathLabel = new QLabel(QDir::toNativeSeparators(m_scanCachePath));
    m_cachePathLabel->setWordWrap(true);
    m_cachePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cacheSizeLabel = new QLabel(QStringLiteral("0 B"));
    m_cacheContentLabel = new QLabel(QStringLiteral("尚未缓存扫描点"));
    m_cacheContentLabel->setWordWrap(true);
    m_cacheUpdatedLabel = new QLabel(QStringLiteral("--"));
    cacheForm->addRow(QStringLiteral("状态"), m_cacheStatus);
    cacheForm->addRow(QStringLiteral("文件"), m_cachePathLabel);
    cacheForm->addRow(QStringLiteral("大小"), m_cacheSizeLabel);
    cacheForm->addRow(QStringLiteral("内容"), m_cacheContentLabel);
    cacheForm->addRow(QStringLiteral("更新时间"), m_cacheUpdatedLabel);
    cacheLayout->addLayout(cacheForm);

    auto *cacheActions = new QHBoxLayout;
    auto *openCacheFolder = new QPushButton(QStringLiteral("打开缓存目录"));
    auto *copyCachePath = new QPushButton(QStringLiteral("复制文件路径"));
    auto *refreshCache = new QPushButton(QStringLiteral("刷新统计"));
    cacheActions->addWidget(openCacheFolder);
    cacheActions->addWidget(copyCachePath);
    cacheActions->addWidget(refreshCache);
    cacheLayout->addLayout(cacheActions);
    dataLayout->addWidget(cacheGroup);

    auto *rawGroup = new QGroupBox(QStringLiteral("原始帧文件（手动保存）"));
    auto *rawLayout = new QVBoxLayout(rawGroup);
    m_record = new QPushButton(QStringLiteral("保存原始帧"));
    auto *playback = new QPushButton(QStringLiteral("打开原始帧回放…"));
    m_recordingPathLabel = new QLabel(QStringLiteral("尚未选择原始帧文件"));
    m_recordingPathLabel->setWordWrap(true);
    m_recordingPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rawLayout->addWidget(m_record);
    rawLayout->addWidget(playback);
    rawLayout->addWidget(m_recordingPathLabel);
    dataLayout->addWidget(rawGroup);

    auto *scanGroup = new QGroupBox(QStringLiteral("扫描管理"));
    auto *scanLayout = new QVBoxLayout(scanGroup);
    auto *saveScan = new QPushButton(QStringLiteral("保存当前扫查"));
    saveScan->setToolTip(QStringLiteral("把当前 B/C 扫图像和点选回看缓存保存成一份独立会话"));
    auto *openScan = new QPushButton(QStringLiteral("打开已保存扫查"));
    openScan->setToolTip(QStringLiteral("载入一份已保存的扫查会话，直接查看，无需重新扫"));
    auto *reset = new QPushButton(QStringLiteral("编码器复位"));
    scanLayout->addWidget(saveScan);
    scanLayout->addWidget(openScan);
    scanLayout->addWidget(reset);
    dataLayout->addWidget(scanGroup);
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
    m_pauseAction = m_actionRail->addAction(QStringLiteral("暂停"));
    m_stopAction = m_actionRail->addAction(QStringLiteral("停止"));
    m_actionRail->addSeparator();
    QAction *closeAction = m_actionRail->addAction(QStringLiteral("关闭"));
    m_actionRail->widgetForAction(m_startAction)->setObjectName(QStringLiteral("startScanButton"));
    m_actionRail->widgetForAction(m_pauseAction)->setObjectName(QStringLiteral("pauseScanButton"));
    m_actionRail->widgetForAction(m_stopAction)->setObjectName(QStringLiteral("stopScanButton"));
    m_actionRail->widgetForAction(closeAction)->setObjectName(QStringLiteral("closeProgramButton"));
    closeAction->setToolTip(QStringLiteral("关闭程序"));
    m_startAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_stopAction->setShortcut(QKeySequence(Qt::Key_F6));
    m_startAction->setToolTip(QStringLiteral("使能小车、启动自动纠偏，并开始 B/C 扫描及点选 A 扫缓存"));
    m_pauseAction->setToolTip(QStringLiteral("暂停/继续当前扫描缓存，不覆盖已有数据"));
    m_stopAction->setToolTip(QStringLiteral("停止自动纠偏和 B/C 扫描缓存，硬件继续采集实时 A 扫"));
    connect(m_startAction, &QAction::triggered, this, [this] {
        setScanSessionActive(true);
        if (m_scanSessionActive && m_robotPanel) {
            m_robotPanel->startAutoCorrection();
        } else if (m_scanSessionActive && m_waitingForTrackingStart) {
            // No robot panel or auto-correction unavailable, start ultrasound immediately
            m_waitingForTrackingStart = false;
            emit waveformCachingChanged(true);
            showMessage(QStringLiteral("扫描缓存已启动（手动模式），B/C 扫数据和对应 A 扫开始保存。"), false);
        }
    });
    connect(m_pauseAction, &QAction::triggered, this, [this] {
        setScanCachingPaused(!m_scanCachingPaused);
    });
    connect(m_stopAction, &QAction::triggered, this, [this] {
        setScanSessionActive(false);
    });
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
    connect(openCacheFolder, &QPushButton::clicked, this, [this] {
        const QString directory = QFileInfo(m_scanCachePath).absolutePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
            showMessage(QStringLiteral("无法打开缓存目录：%1").arg(directory), true);
    });
    connect(copyCachePath, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(m_scanCachePath));
        showMessage(QStringLiteral("缓存文件路径已复制"), false);
    });
    connect(refreshCache, &QPushButton::clicked,
            this, &MainWindow::waveformCacheStatisticsRequested);
    connect(saveScan, &QPushButton::clicked, this, &MainWindow::saveScanSession);
    connect(openScan, &QPushButton::clicked, this, &MainWindow::loadScanSession);
    connect(reset, &QPushButton::clicked, m_device, &DeviceController::resetEncoder);
    connect(reset, &QPushButton::clicked, this, [this] {
        clearAllScanHistories();
        emit waveformCacheReset();
    });
    connect(m_captureToggle, &QSlider::valueChanged, this, [this](int value) {
        if (value != 0)
            startAcquisitionFromUi();
        else
            m_device->stop();
    });
    connect(m_beam, &QSpinBox::valueChanged, this, [this] {
        if (m_restoringGroupControls)
            return;
        const int groupId = currentGroupId();
        m_groupSelectedBeam[groupId] = qMax(0, m_beam->value() - 1);
        updateDecoderSettings();
        if (m_bScanToggle->isChecked()) {
            m_bScan->clear();
            emit waveformCacheGroupReset(groupId, 0x2);
        }
        if (m_cScanToggle->isChecked()) {
            m_cScan->clear();
            emit waveformCacheGroupReset(groupId, 0x4);
        }
        if (!m_lastFrame.beams.isEmpty()) renderCurrentFrame(false);
    });
    // 图谱页签只控制查看内容；各扫描的启停由页签内的开关独立控制。
    connect(m_eScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_eScanToggle->setText(enabled ? QStringLiteral("停止 E 扫")
                                       : QStringLiteral("启动 E 扫"));
        if (m_restoringGroupControls)
            return;
        const int groupId = currentGroupId();
        int modes = m_groupScanModes.value(groupId, 0);
        m_groupScanModes[groupId] = enabled ? (modes | 0x1) : (modes & ~0x1);
        if (enabled)
            m_mapTabs->setCurrentIndex(0);
        updateDecoderSettings();
    });
    connect(m_bScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_bScanToggle->setText(enabled ? QStringLiteral("停止 B 扫")
                                       : QStringLiteral("启动 B 扫"));
        if (m_restoringGroupControls)
            return;
        const int groupId = currentGroupId();
        int modes = m_groupScanModes.value(groupId, 0);
        m_groupScanModes[groupId] = enabled ? (modes | 0x2) : (modes & ~0x2);
        if (enabled) {
            m_bScan->clear();
            emit waveformCacheGroupReset(groupId, 0x2);
            m_mapTabs->setCurrentIndex(1);
        }
        updateDecoderSettings();
    });
    connect(m_cScanToggle, &QPushButton::toggled, this, [this](bool enabled) {
        m_cScanToggle->setText(enabled ? QStringLiteral("停止 C 扫")
                                       : QStringLiteral("启动 C 扫"));
        if (m_restoringGroupControls)
            return;
        const int groupId = currentGroupId();
        int modes = m_groupScanModes.value(groupId, 0);
        m_groupScanModes[groupId] = enabled ? (modes | 0x4) : (modes & ~0x4);
        if (enabled) {
            m_cScan->clear();
            emit waveformCacheGroupReset(groupId, 0x4);
            m_mapTabs->setCurrentIndex(2);
        }
        updateDecoderSettings();
    });
    connect(m_cPrecision, &QDoubleSpinBox::valueChanged, this, [this](double mmPerCell) {
        if (m_restoringGroupControls)
            return;
        const int groupId = currentGroupId();
        m_groupEncoderPrecision[groupId] = mmPerCell;
        const int counts = encoderCountsPerCell(groupId);
        m_bScan->setEncoderPrecision(counts);
        m_cScan->setEncoderPrecision(counts);
        emit waveformCacheGroupReset(groupId, 0x6);
        updateDecoderSettings();
    });
    connect(m_eImagingGate, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_restoringGroupControls)
            m_groupEImagingGate[currentGroupId()] = index;
        updateImagingGates();
    });
    connect(m_bImagingGate, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_restoringGroupControls)
            m_groupBImagingGate[currentGroupId()] = index;
        updateImagingGates();
    });
    connect(m_bViewMode, &QComboBox::currentIndexChanged, this, [this] {
        const int mode = m_bViewMode->currentData().toInt();
        for (BScanWidget *widget : qAsConst(m_bScans))
            widget->setViewMode(mode);
    });
    connect(m_bPaletteMode, &QComboBox::currentIndexChanged, this, [this] {
        const int mode = m_bPaletteMode->currentData().toInt();
        for (BScanWidget *widget : qAsConst(m_bScans))
            widget->setPaletteMode(mode);
    });
    connect(m_bScaleMode, &QComboBox::currentIndexChanged, this, [this] {
        const int mode = m_bScaleMode->currentData().toInt();
        for (BScanWidget *widget : qAsConst(m_bScans))
            widget->setScaleMode(mode);
    });
    const auto applyBDisplayLevels = [this] {
        const double lower = qMin(m_bDisplayLower->value(),
                                  m_bDisplayUpper->value() - 1.0);
        for (BScanWidget *widget : qAsConst(m_bScans))
            widget->setDisplayLevels(lower, m_bDisplayUpper->value(),
                                     m_bGamma->value());
    };
    connect(m_bDisplayLower, &QDoubleSpinBox::valueChanged, this,
            [applyBDisplayLevels](double) { applyBDisplayLevels(); });
    connect(m_bDisplayUpper, &QDoubleSpinBox::valueChanged, this,
            [applyBDisplayLevels](double) { applyBDisplayLevels(); });
    connect(m_bGamma, &QDoubleSpinBox::valueChanged, this,
            [applyBDisplayLevels](double) { applyBDisplayLevels(); });
    connect(m_bDisplayReject, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        for (BScanWidget *widget : qAsConst(m_bScans))
            widget->setDisplayReject(value);
    });
    connect(m_bInterpolateMissing, &QCheckBox::toggled, this, [this](bool enabled) {
        for (BScanWidget *widget : qAsConst(m_bScans))
            widget->setInterpolateMissing(enabled);
    });
    connect(resetBView, &QPushButton::clicked, this, [this] {
        if (m_bScan)
            m_bScan->resetView();
    });
    connect(m_cImagingGate, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_restoringGroupControls)
            m_groupCImagingGate[currentGroupId()] = index;
        updateImagingGates();
    });
    connect(m_robotPanel, &RobotControlPanel::vehicleStatusChanged, this,
            [this](const QStringList &metrics) {
        const int count = qMin(metrics.size(), m_vehicleMetrics.size());
        for (int i = 0; i < count; ++i)
            m_vehicleMetrics[i]->setText(metrics.at(i));
    });
    connect(m_robotPanel, &RobotControlPanel::autoCorrectionPausedChanged,
            this, [this](bool paused) {
        if (m_scanSessionActive)
            setScanCachingPaused(paused);
    });
    connect(m_robotPanel, &RobotControlPanel::autoCorrectionTrackingStarted,
            this, [this]() {
        if (m_waitingForTrackingStart) {
            m_waitingForTrackingStart = false;
            emit waveformCachingChanged(true);
            showMessage(QStringLiteral("自动纠偏跟踪已开始，超声数据采集同步启动。"), false);
        }
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
    if (index == 3 && m_waveformCache)
        emit waveformCacheStatisticsRequested();
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
    m_device->connectDevice(m_address->text().trimmed(), m_deviceId->value());
}

void MainWindow::setScanSessionActive(bool active)
{
    m_waitingForTrackingStart = false;
    if (!active && m_robotPanel)
        m_robotPanel->stopAutoCorrection();
    if (active && !m_device->capturing()) {
        showMessage(QStringLiteral("请先点击“开始采集”，硬件产生实时数据后再启动扫描缓存。"), true);
        return;
    }
    if (m_scanSessionActive == active) {
        updateStatus();
        return;
    }

    {
        QMutexLocker locker(&m_pendingScanMutex);
        m_pendingScanFrames.clear();
        m_pendingScanScores.clear();
        m_pendingScanOrder.clear();
        m_pendingScanDeviceId = -1;
    }

    m_scanSessionActive = active;
    m_scanCachingPaused = false;
    if (m_robotPanel)
        m_robotPanel->setAutoCorrectionPaused(false);
    if (active) {
        // 新的一次缓存会话：先清掉上一次的 B/C 扫图像和 A 扫缓存，再启用缓存，
        // 避免新数据叠加在旧图像/旧缓存之上。
        clearAllScanHistories();
        emit waveformCacheReset();
        // Delay ultrasound start until auto-correction tracking phase begins
        m_waitingForTrackingStart = true;
        emit waveformCachingChanged(false);
        updateDecoderSettings();
        showMessage(QStringLiteral("已准备扫描缓存，等待小车自动纠偏跟踪开始后同步启动超声采集。"), false);
    } else {
        updateDecoderSettings();
        emit waveformCachingChanged(false);
        showMessage(QStringLiteral("扫描缓存已停止，实时 A 扫采集继续运行。"), false);
    }
    updateStatus();
}

void MainWindow::setScanCachingPaused(bool paused)
{
    if (!m_scanSessionActive)
        return;
    if (m_scanCachingPaused == paused) {
        updateStatus();
        return;
    }
    m_scanCachingPaused = paused;
    if (m_robotPanel)
        m_robotPanel->setAutoCorrectionPaused(paused);
    
    // 如果正在等待跟踪阶段开始，即使用户点击继续也不立即启动超声采集
    // 需要等待 autoCorrectionTrackingStarted 信号触发
    if (!paused && m_waitingForTrackingStart) {
        showMessage(QStringLiteral("扫描缓存已继续，等待自动纠偏进入跟踪阶段后同步启动超声采集。"), false);
    } else {
        emit waveformCachingChanged(!paused);
        showMessage(paused ? QStringLiteral("扫描缓存已暂停，点击【继续】接着当前会话追加保存。")
                           : QStringLiteral("扫描缓存已继续，接着当前会话追加保存。"), false);
    }
    updateStatus();
}

void MainWindow::startAcquisitionFromUi()
{
    m_reviewingScanWaveform = false;
    if (m_aScan)
        m_aScan->setReviewCursor(0.0, false);
    m_device->start();
    showMessage(QStringLiteral("已请求开始超声采集；扫描缓存需另行点击“启动”。"), false);
}

void MainWindow::acceptDecodedFrame(DecodedFrame frame, int groupId, int deviceId)
{
    if (deviceId != m_deviceId->value())
        return;
    m_groupLastFrames.insert(groupId, frame);
    if (groupId != currentGroupId())
        return;
    m_lastFrame = std::move(frame);
    renderCurrentFrame(true);
}

void MainWindow::updateDecoderSettings()
{
    if (!m_device || !m_beam || !m_cPrecision)
        return;

    // The vendor framework builds its config repository asynchronously at
    // startup.  Querying it (getGroupsNo/getPointQuantity/...) from the GUI
    // thread while that init is still running races with repository Init and
    // can leave the SDK stuck before ultrasound-device/service.  Until the SDK
    // session is actually established there is no data to decode, so emit a
    // single default group from cached controller state instead.
    if (!m_device->serviceConnected()
        || (!m_device->captureReady() && !m_device->capturing()
            && !m_device->hardwareOnline())) {
        const int groupId = currentGroupId();
        GroupScanSettings group;
        group.groupId = groupId;
        group.groupOffsetWords = 0;
        group.pointCount = qMax(1, m_device->pointCount());
        group.beamCount = qMax(1, m_device->beamCount());
        const int requestedModes = m_groupScanModes.value(groupId, 0);
        group.scanModes = m_scanSessionActive ? requestedModes
                                              : (requestedModes & 0x1);
        group.selectedBeam = qBound(0, m_groupSelectedBeam.value(groupId, 0),
                                    group.beamCount - 1);
        group.amplitudeScale = qMax(1, m_device->maxAmplitudeValue());
        group.encoderPrecision = encoderCountsPerCell(groupId);
        group.rangeStart = m_rangeStart;
        group.rangeEnd = m_rangeEnd;
        QVector<GroupScanSettings> settings{group};
        emit decoderSettingsChanged(std::move(settings), groupId);
        return;
    }

    // Parameter-preset mode deliberately exposes one logical settings entry.
    // The SDK packet always contains hardware group 0 at word offset 0; the
    // logical preset id only selects the matching UI/cache history.
    Client &sdk = Client::getInstance();
    constexpr int hardwareGroupId = ParameterPresetStore::HardwareGroupId;
    const int logicalPresetId = currentGroupId();
    const int pointCount = qMax(1, sdk.getPointQuantity(hardwareGroupId));
    const int beamCount = qMax(1, sdk.getBeamCounts(hardwareGroupId));
    GroupScanSettings group;
    group.groupId = logicalPresetId;
    group.groupOffsetWords = 0;
    group.pointCount = pointCount;
    group.beamCount = beamCount;
    const int requestedModes = m_groupScanModes.value(logicalPresetId, 0);
    group.scanModes = m_scanSessionActive ? requestedModes
                                          : (requestedModes & 0x1);
    group.selectedBeam = qBound(0, m_groupSelectedBeam.value(logicalPresetId, 0),
                                beamCount - 1);
    group.amplitudeScale = qMax(1, int(sdk.getMaxAmplitude(hardwareGroupId)));
    group.encoderPrecision = encoderCountsPerCell(logicalPresetId);
    group.rangeStart = sdk.getRangeStart(hardwareGroupId);
    group.rangeEnd = sdk.getRangeEnd(hardwareGroupId);
    QVector<GroupScanSettings> settings{group};
    emit decoderSettingsChanged(std::move(settings), logicalPresetId);
}

void MainWindow::acceptGroupScanBatch(GroupScanFrameBatch frames, int deviceId)
{
    if (!m_scanSessionActive || deviceId != m_deviceId->value() || frames.isEmpty())
        return;
    QSet<BScanWidget *> changedBScans;
    QSet<CScanWidget *> changedCScans;
    for (const GroupScanFrame &item : qAsConst(frames)) {
        const double scale = PacketDecoder::amplitudeScale(item.amplitudeScale);
        if (item.triggeredModes & 0x2) {
            BScanWidget *bScan = ensureBScan(item.groupId);
            bScan->setRange(item.rangeStart, item.rangeEnd);
            if (bScan->appendFrameAtColumnDeferred(item.frame, scale,
                                                   item.bColumn, 0))
                changedBScans.insert(bScan);
        }
        if (item.triggeredModes & 0x4) {
            CScanWidget *cScan = ensureCScan(item.groupId);
            if (cScan->appendFrameDeferred(item.frame, scale, 0,
                                           item.cGateMask))
                changedCScans.insert(cScan);
        }
        m_groupLastFrames.insert(item.groupId, item.frame);
        if (item.groupId == currentGroupId())
            m_lastFrame = item.frame;
    }

    for (BScanWidget *scan : qAsConst(changedBScans)) {
        if (scan->isVisible())
            scan->update();
    }
    for (CScanWidget *scan : qAsConst(changedCScans)) {
        if (scan->isVisible())
            scan->update();
    }

    if (m_lastFrame.beams.isEmpty())
        return;

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

void MainWindow::enqueueGroupScanBatch(const GroupScanFrameBatch &frames,
                                       int deviceId)
{
    if (frames.isEmpty())
        return;
    QMutexLocker locker(&m_pendingScanMutex);
    if (m_pendingScanDeviceId != -1 && m_pendingScanDeviceId != deviceId) {
        m_pendingScanFrames.clear();
        m_pendingScanScores.clear();
        m_pendingScanOrder.clear();
    }
    m_pendingScanDeviceId = deviceId;

    const auto merge = [this](const QString &key, GroupScanFrame item, int score) {
        if (m_pendingScanFrames.contains(key)) {
            if (score > m_pendingScanScores.value(key, -1)) {
                m_pendingScanFrames.insert(key, std::move(item));
                m_pendingScanScores.insert(key, score);
            }
            return;
        }
        while (m_pendingScanFrames.size() >= 4096
               && !m_pendingScanOrder.isEmpty()) {
            const QString oldest = m_pendingScanOrder.dequeue();
            m_pendingScanFrames.remove(oldest);
            m_pendingScanScores.remove(oldest);
        }
        m_pendingScanOrder.enqueue(key);
        m_pendingScanFrames.insert(key, std::move(item));
        m_pendingScanScores.insert(key, score);
    };

    for (const GroupScanFrame &source : frames) {
        if ((source.triggeredModes & 0x2) != 0 && source.bColumn >= 0) {
            GroupScanFrame item = source;
            item.triggeredModes = 0x2;
            item.cGateMask = 0;
            merge(QStringLiteral("B:%1:%2").arg(item.groupId).arg(item.bColumn),
                  std::move(item), waveformScore(source));
        }
        if ((source.triggeredModes & 0x4) == 0
            || source.frame.measurements.isEmpty())
            continue;
        const BeamMeasurement &measurement = source.frame.measurements.first();
        const int amplitudes[4] = {int(measurement.amplitudeA),
                                   int(measurement.amplitudeB),
                                   int(measurement.amplitudeC),
                                   int(measurement.amplitudeI)};
        for (int gate = 0; gate < 4; ++gate) {
            if ((source.cGateMask & (1 << gate)) == 0)
                continue;
            GroupScanFrame item = source;
            item.triggeredModes = 0x4;
            item.bColumn = -1;
            item.cGateMask = 1 << gate;
            merge(QStringLiteral("C:%1:%2:%3:%4")
                      .arg(item.groupId).arg(gate)
                      .arg(item.cColumn).arg(item.cRow),
                  std::move(item), amplitudes[gate]);
        }
    }
}

void MainWindow::drainPendingGroupScanBatches()
{
    GroupScanFrameBatch batch;
    int deviceId = -1;
    {
        QMutexLocker locker(&m_pendingScanMutex);
        batch.reserve(m_pendingScanFrames.size());
        while (!m_pendingScanOrder.isEmpty()) {
            const QString key = m_pendingScanOrder.dequeue();
            const auto it = m_pendingScanFrames.find(key);
            if (it == m_pendingScanFrames.end())
                continue;
            batch.append(std::move(it.value()));
            m_pendingScanFrames.erase(it);
            m_pendingScanScores.remove(key);
        }
        deviceId = m_pendingScanDeviceId;
        m_pendingScanDeviceId = -1;
    }
    if (!batch.isEmpty())
        acceptGroupScanBatch(std::move(batch), deviceId);
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
    if (m_startAction)
        m_startAction->setEnabled(m_device->capturing() && !m_scanSessionActive);
    if (m_pauseAction) {
        m_pauseAction->setEnabled(m_scanSessionActive);
        m_pauseAction->setText(m_scanCachingPaused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    }
    if (m_stopAction)
        m_stopAction->setEnabled(m_scanSessionActive);
    if (m_captureToggle) {
        QSignalBlocker blocker(m_captureToggle);
        m_captureToggle->setValue(m_device->capturing() || m_device->startPending() ? 1 : 0);
    }
    m_groupStatus->setText(QStringLiteral("参数预设 %1 | 硬件组 0")
                               .arg(currentGroupId()));
    m_statusCaptureDetail->setText(
        online ? (m_scanSessionActive
                      ? QStringLiteral("硬件在线，正在采集；扫描缓存已启动")
                      : QStringLiteral("硬件在线，正在采集；扫描缓存未启动"))
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
    m_bScan->setImagingGate(bGate[0] >= 0.5, bGate[1], bGate[2], bGate[3]);
    const QVector<bool> trackedGates = m_parameters->surfaceTrackedGates();
    m_bScan->setSurfaceTracking(m_parameters->surfaceTrackingEnabled(),
                                m_parameters->surfaceWindowStart(),
                                m_parameters->surfaceWindowEnd(),
                                m_parameters->surfaceThreshold(),
                                m_parameters->surfaceHoldMissing(),
                                bIndex >= 0 && bIndex < trackedGates.size()
                                    && trackedGates[bIndex]);

    const int cIndex = m_cImagingGate->currentIndex();
    const QVector<double> cGate = gateAt(cIndex);
    m_cScan->setImagingGate(cIndex, cGate[0] >= 0.5, cGate[3]);
}

int MainWindow::currentGroupId() const
{
    return m_activePresetId;
}

int MainWindow::encoderCountsPerCell(int groupId) const
{
    const double mmPerCell = m_groupEncoderPrecision.value(groupId, 1.0);
    return qMax(1, qRound(mmPerCell / m_encoderMmPerCount));
}

BScanWidget *MainWindow::ensureBScan(int groupId)
{
    BScanWidget *&slot = m_bScans[groupId];
    if (slot)
        return slot;
    auto *widget = new BScanWidget;
    widget->setEncoderPrecision(encoderCountsPerCell(groupId));
    widget->setMmPerCount(m_encoderMmPerCount);
    if (m_bViewMode)
        widget->setViewMode(m_bViewMode->currentData().toInt());
    if (m_bPaletteMode)
        widget->setPaletteMode(m_bPaletteMode->currentData().toInt());
    if (m_bScaleMode)
        widget->setScaleMode(m_bScaleMode->currentData().toInt());
    if (m_bDisplayLower && m_bDisplayUpper && m_bGamma)
        widget->setDisplayLevels(m_bDisplayLower->value(),
                                 m_bDisplayUpper->value(), m_bGamma->value());
    if (m_bDisplayReject)
        widget->setDisplayReject(m_bDisplayReject->value());
    if (m_bInterpolateMissing)
        widget->setInterpolateMissing(m_bInterpolateMissing->isChecked());
    if (m_bScanStack)
        m_bScanStack->addWidget(widget);
    connect(widget, &BScanWidget::columnClicked, this,
            [this, groupId](int column, double depth) {
        emit bWaveformRequested(groupId, column, depth);
    });
    slot = widget;
    return widget;
}

CScanWidget *MainWindow::ensureCScan(int groupId)
{
    CScanWidget *&slot = m_cScans[groupId];
    if (slot)
        return slot;
    auto *widget = new CScanWidget;
    widget->setEncoderPrecision(encoderCountsPerCell(groupId));
    widget->setMmPerCount(m_encoderMmPerCount);
    if (m_cScanStack)
        m_cScanStack->addWidget(widget);
    connect(widget, &CScanWidget::cellClicked, this,
            [this, groupId, widget](int x, int y) {
        emit cWaveformRequested(groupId, widget->imagingGate(), x, y);
    });
    slot = widget;
    return widget;
}

void MainWindow::selectCurrentGroupViews()
{
    const int groupId = currentGroupId();
    m_bScan = ensureBScan(groupId);
    m_cScan = ensureCScan(groupId);
    if (m_bScanStack)
        m_bScanStack->setCurrentWidget(m_bScan);
    if (m_cScanStack)
        m_cScanStack->setCurrentWidget(m_cScan);
    if (m_groupLastFrames.contains(groupId)) {
        m_lastFrame = m_groupLastFrames.value(groupId);
        renderCurrentFrame(false);
    } else {
        m_lastFrame = DecodedFrame();
        m_aScan->setWaveform(QVector<qint16>(), 1.0, m_rangeStart, m_rangeEnd);
    }
}

void MainWindow::restoreCurrentGroupControls()
{
    const int groupId = currentGroupId();
    m_restoringGroupControls = true;
    m_beam->setValue(m_groupSelectedBeam.value(groupId, 0) + 1);
    m_cPrecision->setValue(m_groupEncoderPrecision.value(groupId, 1.0));
    const int modes = m_groupScanModes.value(groupId, 0);
    m_eScanToggle->setChecked((modes & 0x1) != 0);
    m_bScanToggle->setChecked((modes & 0x2) != 0);
    m_cScanToggle->setChecked((modes & 0x4) != 0);
    m_eImagingGate->setCurrentIndex(m_groupEImagingGate.value(groupId, 0));
    m_bImagingGate->setCurrentIndex(m_groupBImagingGate.value(groupId, 0));
    m_cImagingGate->setCurrentIndex(m_groupCImagingGate.value(groupId, 0));
    m_restoringGroupControls = false;
}

void MainWindow::clearAllScanHistories()
{
    for (BScanWidget *widget : qAsConst(m_bScans))
        if (widget)
            widget->clear();
    for (CScanWidget *widget : qAsConst(m_cScans))
        if (widget)
            widget->clear();
    m_lastFrame = DecodedFrame();
    m_groupLastFrames.clear();
}

void MainWindow::updateEncoderCalibration()
{
    if (!m_parameters)
        return;
    const double diameter = qMax(0.001, m_parameters->wheelDiameter());
    const int resolution = qMax(1, m_parameters->encoderResolution());
    m_encoderMmPerCount = 3.14159265358979323846 * diameter / double(resolution);
    ParameterPresetStore::saveEncoderCalibration(diameter, resolution);
    for (BScanWidget *widget : qAsConst(m_bScans))
        if (widget)
            widget->setMmPerCount(m_encoderMmPerCount);
    for (CScanWidget *widget : qAsConst(m_cScans))
        if (widget)
            widget->setMmPerCount(m_encoderMmPerCount);
}

void MainWindow::saveScanSession()
{
    if (m_bScans.isEmpty() && m_cScans.isEmpty()) {
        showMessage(QStringLiteral("当前没有可保存的扫描数据"), true);
        return;
    }
    const QString sessionsDir = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("scan_sessions"));
    QDir().mkpath(sessionsDir);
    const QString defaultPath = sessionsDir + QStringLiteral("/scan_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".scn");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存扫查"), defaultPath, QStringLiteral("扫查会话 (*.scn)"));
    if (path.isEmpty())
        return;

    // 用固定临时文件名（保存前先删除残留），避免 QTemporaryFile 在 Windows
    // 关闭时按 DELETE_ON_CLOSE 语义删掉文件，导致后续快照/读取落空。
    const QString tempAscan = QDir::tempPath() + QStringLiteral("/pa1664_ascan_snapshot.bin");
    QFile::remove(tempAscan);

    bool snapshotOk = false;
    const bool invoked = m_waveformCache
        && QMetaObject::invokeMethod(m_waveformCache, "saveSnapshot",
                                     Qt::BlockingQueuedConnection,
                                     Q_RETURN_ARG(bool, snapshotOk),
                                     Q_ARG(QString, tempAscan));
    if (!invoked || !snapshotOk) {
        showMessage(QStringLiteral("保存扫查失败：A 扫缓存快照未生成"), true);
        return;
    }
    QFile cacheFile(tempAscan);
    if (!cacheFile.open(QIODevice::ReadOnly)) {
        showMessage(QStringLiteral("保存扫查失败：无法读取 A 扫缓存快照"), true);
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        showMessage(QStringLiteral("无法创建扫查文件：%1").arg(file.errorString()), true);
        return;
    }
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint32(0x5041534E) << quint32(2); // "PASN" version 2: streamed cache

    QSet<int> groupIds;
    for (auto it = m_bScans.constBegin(); it != m_bScans.constEnd(); ++it)
        groupIds.insert(it.key());
    for (auto it = m_cScans.constBegin(); it != m_cScans.constEnd(); ++it)
        groupIds.insert(it.key());

    out << quint32(groupIds.size());
    for (int groupId : groupIds) {
        const BScanWidget *b = m_bScans.value(groupId, nullptr);
        const CScanWidget *c = m_cScans.value(groupId, nullptr);
        out << qint32(groupId) << bool(b != nullptr) << bool(c != nullptr);
        if (b) b->saveTo(out);
        if (c) c->saveTo(out);
    }
    out << qint64(cacheFile.size());
    if (out.status() != QDataStream::Ok
        || !copyDeviceBytes(cacheFile, file, cacheFile.size())) {
        file.cancelWriting();
        showMessage(QStringLiteral("保存扫查失败：写入缓存数据时磁盘异常"), true);
        return;
    }
    if (!file.commit()) {
        showMessage(QStringLiteral("保存扫查失败：%1").arg(file.errorString()), true);
        return;
    }
    showMessage(QStringLiteral("已保存扫查：%1").arg(QDir::toNativeSeparators(path)), false);
}

void MainWindow::loadScanSession()
{
    const QString sessionsDir = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("scan_sessions"));
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开已保存扫查"), sessionsDir,
        QStringLiteral("扫查会话 (*.scn)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showMessage(QStringLiteral("无法打开扫查文件：%1").arg(file.errorString()), true);
        return;
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    in >> magic >> version;
    if (magic != 0x5041534E || (version != 1 && version != 2)) {
        showMessage(QStringLiteral("扫查会话文件格式不识别"), true);
        return;
    }

    quint32 groupCount = 0;
    in >> groupCount;
    if (in.status() != QDataStream::Ok || groupCount > 1024) {
        showMessage(QStringLiteral("扫查会话中的预设数量异常"), true);
        return;
    }
    QSet<int> loadedGroupIds;
    for (quint32 i = 0; i < groupCount; ++i) {
        qint32 groupId = 0;
        bool hasB = false, hasC = false;
        in >> groupId >> hasB >> hasC;
        if (in.status() != QDataStream::Ok || groupId < 0 || groupId > 1000000
            || loadedGroupIds.contains(int(groupId))) {
            showMessage(QStringLiteral("扫查会话中的预设索引损坏"), true);
            return;
        }
        loadedGroupIds.insert(int(groupId));
        BScanWidget bValidator;
        CScanWidget cValidator;
        if ((hasB && !bValidator.loadFrom(in))
            || (hasC && !cValidator.loadFrom(in))) {
            showMessage(QStringLiteral("扫查会话中的 B/C 扫数据不完整"), true);
            return;
        }
    }

    QTemporaryFile cacheSnapshot(
        QDir::tempPath() + QStringLiteral("/pa1664_ascan_XXXXXX.bin"));
    if (!cacheSnapshot.open()) {
        showMessage(QStringLiteral("无法建立扫查缓存临时文件：%1")
                        .arg(cacheSnapshot.errorString()), true);
        return;
    }
    qint64 cacheLength = 0;
    bool cacheExtracted = true;
    if (version == 1) {
        QByteArray legacyCache;
        in >> legacyCache;
        cacheLength = legacyCache.size();
        cacheExtracted = in.status() == QDataStream::Ok
                         && cacheSnapshot.write(legacyCache) == legacyCache.size();
    } else {
        in >> cacheLength;
        cacheExtracted = in.status() == QDataStream::Ok && cacheLength >= 0
                         && cacheLength <= file.bytesAvailable()
                         && copyDeviceBytes(file, cacheSnapshot, cacheLength);
    }
    cacheSnapshot.flush();
    cacheSnapshot.close();
    if (!cacheExtracted) {
        showMessage(QStringLiteral("扫查会话中的 A 扫缓存数据不完整"), true);
        return;
    }

    // Opening a completed scan is a review operation. Freeze live acquisition
    // and caching so the loaded image/cache cannot be modified underneath it.
    setScanSessionActive(false);
    m_device->stop();

    if (m_waveformCache) {
        if (cacheLength > 0) {
            bool cacheLoaded = false;
            const bool cacheInvoked = QMetaObject::invokeMethod(
                m_waveformCache, "loadSnapshot", Qt::BlockingQueuedConnection,
                Q_RETURN_ARG(bool, cacheLoaded),
                Q_ARG(QString, cacheSnapshot.fileName()));
            if (!cacheInvoked || !cacheLoaded) {
                showMessage(QStringLiteral("扫查会话的 A 扫缓存校验失败，未替换当前数据"), true);
                return;
            }
        } else {
            QMetaObject::invokeMethod(m_waveformCache, "resetAll",
                                      Qt::BlockingQueuedConnection);
        }
    }

    // Validation and cache restoration succeeded. Re-read the small B/C part
    // into the live views only now, so a damaged file never clears the current scan.
    in.setDevice(nullptr);
    if (!file.seek(0)) {
        showMessage(QStringLiteral("无法重新读取扫查会话"), true);
        return;
    }
    QDataStream liveIn(&file);
    liveIn.setByteOrder(QDataStream::LittleEndian);
    liveIn >> magic >> version >> groupCount;
    clearAllScanHistories();
    for (quint32 i = 0; i < groupCount; ++i) {
        qint32 groupId = 0;
        bool hasB = false, hasC = false;
        liveIn >> groupId >> hasB >> hasC;
        if ((hasB && !ensureBScan(int(groupId))->loadFrom(liveIn))
            || (hasC && !ensureCScan(int(groupId))->loadFrom(liveIn))) {
            showMessage(QStringLiteral("恢复 B/C 扫视图失败"), true);
            return;
        }
    }
    selectCurrentGroupViews();
    showMessage(QStringLiteral("已加载扫查会话，可在 B/C 扫图像上点选回看 A 扫"), false);
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
    QString error;
    const QList<int> imported = ParameterPresetStore::importConfiguration(path, &error);
    if (imported.isEmpty()) {
        showMessage(error.isEmpty() ? QStringLiteral("导入参数预设失败") : error, true);
        return;
    }
    m_parameters->refreshGroups();
    QStringList ids;
    for (int id : imported)
        ids << QString::number(id);
    showMessage(QStringLiteral("已从配置文件导入 %1 个软件参数预设 [%2]；"
                               "硬件工作组仍保持为 0，请在参数预设下拉框中选择后下发。")
                    .arg(imported.size()).arg(ids.join(',')), false);
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
    const double encoderAMm = double(t.encoder[0]) * m_encoderMmPerCount;
    const double encoderBMm = double(t.encoder[1]) * m_encoderMmPerCount;
    m_tailInfo->setText(QStringLiteral("回放帧 %1 | 位置 A %2 mm / B %3 mm | 时间戳 %4")
                            .arg(t.frameNumber).arg(encoderAMm, 0, 'f', 2)
                            .arg(encoderBMm, 0, 'f', 2).arg(t.timestamp));
    m_encoderStatus->setText(QStringLiteral("位置 A %1 mm / B %2 mm")
                                 .arg(encoderAMm, 0, 'f', 2)
                                 .arg(encoderBMm, 0, 'f', 2));
}

void MainWindow::showMessage(const QString &text, bool error)
{
    statusBar()->showMessage(text, error ? 8000 : 5000);
    if (error) statusBar()->setStyleSheet("QStatusBar{color:#b42318}");
    else statusBar()->setStyleSheet("QStatusBar{color:#245c38}");
}
