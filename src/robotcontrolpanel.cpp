#include "robotcontrolpanel.h"

#include "robot_profile_view.h"
#include "robot_app_logger.h"
#include "profile_weld_detector.h"
#include "robot_sensor_controller.h"
#include "robot_usb_camera_controller.h"
#include "clamp_motor_controller.h"
#include "clamp_motor_widgets.h"
#include "synchronized_drive_controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QMetaObject>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kMillimetersPerMeter = 1000.0;
constexpr double kDegreesPerRadian = 57.29577951308232;
constexpr double kRadiansPerDegree = 0.017453292519943295;

QPushButton *actionButton(const QString &text, const char *kind = "secondary")
{
    auto *button = new QPushButton(text);
    button->setProperty("kind", kind);
    button->setMinimumHeight(40);
    return button;
}

QLabel *metricLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("--"), parent);
    label->setObjectName(QStringLiteral("robotMetric"));
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QDoubleSpinBox *makeDouble(double minimum, double maximum, double step,
                           int decimals, QWidget *parent = nullptr)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    return box;
}

QWidget *wrapWithHint(QDoubleSpinBox *box, const QString &hint)
{
    auto *container = new QWidget(box->parentWidget());
    auto *stack = new QStackedLayout(container);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(box);
    const QString range = QStringLiteral("%1~%2")
                              .arg(box->minimum(), 0, 'f', box->decimals())
                              .arg(box->maximum(), 0, 'f', box->decimals());
    auto *label = new QLabel(QStringLiteral("范围 %1；%2").arg(range, hint), container);
    label->setObjectName(QStringLiteral("spinHint"));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setContentsMargins(0, 0, 26, 0);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setStyleSheet(QStringLiteral("color:#8a94a6;background:transparent;font-size:11px;"));
    stack->addWidget(label);
    // StackAll raises the current widget above the others; keep the hint on top.
    stack->setCurrentIndex(1);
    return container;
}

void populateBaudRates(QComboBox *box)
{
    for (int baud : {115200, 500000, 1000000, 1500000, 2500000})
        box->addItem(QString::number(baud), baud);
}

void populateCombo(QComboBox *box, std::initializer_list<int> values)
{
    for (int value : values)
        box->addItem(QString::number(value), value);
}

void setComboValue(QComboBox *box, const QVariant &value)
{
    const int index = box->findData(value);
    if (index >= 0)
        box->setCurrentIndex(index);
}

void setComboText(QComboBox *box, const QString &value)
{
    if (!box || value.isEmpty())
        return;
    int index = box->findData(value);
    if (index < 0) {
        box->addItem(value, value);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

} // namespace

RobotControlPanel::RobotControlPanel(QWidget *parent)
    : RobotControlPanel(nullptr, parent)
{
}

RobotControlPanel::RobotControlPanel(crawling::ClampMotorController *clampController,
                                     QWidget *parent)
    : QWidget(parent), m_clampController(clampController)
{
    qRegisterMetaType<crawling::DriveSettings>("crawling::DriveSettings");
    qRegisterMetaType<crawling::DriveState>("crawling::DriveState");
    qRegisterMetaType<crawling::DriveTelemetry>("crawling::DriveTelemetry");
    qRegisterMetaType<crawling::ImuSample>("crawling::ImuSample");
    qRegisterMetaType<QVector<QVector3D>>("QVector<QVector3D>");
    qRegisterMetaType<crawling::LaserGapDetection>(
        "crawling::LaserGapDetection");
    qRegisterMetaType<crawling::LaserCorrectionSettings>(
        "crawling::LaserCorrectionSettings");
    qRegisterMetaType<crawling::LaserCorrectionStatus>(
        "crawling::LaserCorrectionStatus");

    if (!m_clampController)
        m_clampController = new crawling::ClampMotorController(this);
    connect(m_clampController,
            &crawling::ClampMotorController::connectionChanged,
            this, &RobotControlPanel::updateClampConnection,
            Qt::QueuedConnection);

    buildUi();
    loadSettings();
    refreshPorts();

    m_driveThread = new QThread(this);
    m_controller = new crawling::SynchronizedDriveController;
    m_controller->moveToThread(m_driveThread);
    connect(m_driveThread, &QThread::started, m_controller,
            &crawling::SynchronizedDriveController::startControlLoop);
    connect(m_driveThread, &QThread::finished, m_controller, &QObject::deleteLater);
    connect(m_controller, &crawling::SynchronizedDriveController::telemetryChanged,
            this, &RobotControlPanel::updateTelemetry, Qt::QueuedConnection);
    connect(m_controller, &crawling::SynchronizedDriveController::stateChanged,
            this, &RobotControlPanel::updateState, Qt::QueuedConnection);
    connect(m_controller, &crawling::SynchronizedDriveController::connectionChanged,
            this, &RobotControlPanel::updateConnection, Qt::QueuedConnection);
    connect(m_controller, &crawling::SynchronizedDriveController::logMessage,
            this, &RobotControlPanel::appendLog, Qt::QueuedConnection);
    m_driveThread->start(QThread::HighPriority);

    m_sensorThread = new QThread(this);
    m_sensorController = new crawling::RobotSensorController;
    m_sensorController->moveToThread(m_sensorThread);
    connect(m_sensorThread, &QThread::finished, m_sensorController, &QObject::deleteLater);
    connect(m_sensorController, &crawling::RobotSensorController::imuSampleChanged,
            this, &RobotControlPanel::updateImu, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::imuConnectionChanged,
            this, &RobotControlPanel::updateImuConnection, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::deviceDetectionChanged,
            this, &RobotControlPanel::updateSensorDetection, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::cameraDevicesChanged,
            this, &RobotControlPanel::updateLaserDevices, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::cameraConnectionChanged,
            this, &RobotControlPanel::updateLaserConnection, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::cameraFrameChanged,
            this, &RobotControlPanel::updateLaserFrame, Qt::QueuedConnection);
    connect(m_sensorController,
            &crawling::RobotSensorController::cameraProfileModeChanged,
            this, [this](bool active) {
                if (m_laserView && active && m_correctionStatus)
                    m_correctionStatus->setText(QStringLiteral("SDK X/Z 轮廓流已切换"));
            }, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::logMessage,
            this, &RobotControlPanel::appendLog, Qt::QueuedConnection);
    m_sensorThread->start();

    m_correctionThread = new QThread(this);
    m_correctionController = new crawling::LaserCorrectionController;
    if (m_profileDetectionMode) {
        m_correctionController->setProfileDetectionMode(
            static_cast<crawling::ProfileDetectionMode>(
                m_profileDetectionMode->currentData().toInt()));
    }
    m_correctionController->setProfileTuning(m_profileWeldTuning);
    m_correctionController->setProfileTemplates(m_profileTemplates);
    if (m_laserInputTimeout) {
        m_correctionController->setProfileInputTimeout(m_laserInputTimeout->value());
        m_laserView->setInputTimeoutMs(m_laserInputTimeout->value());
        connect(m_laserInputTimeout, QOverload<int>::of(&QSpinBox::valueChanged),
                m_correctionController,
                &crawling::LaserCorrectionController::setProfileInputTimeout,
                Qt::QueuedConnection);
    }
    connect(m_correctionController,
            &crawling::LaserCorrectionController::profileInputTimeoutChanged,
            m_laserView, &RobotProfileView::setInputTimeoutMs, Qt::QueuedConnection);
    m_correctionController->moveToThread(m_correctionThread);
    connect(m_correctionThread, &QThread::finished, m_correctionController,
            &QObject::deleteLater);
    m_profileDetectionLogThread = new QThread(this);
    m_profileDetectionLogWriter = new crawling::ProfileDetectionLogWriter;
    m_profileDetectionLogWriter->moveToThread(m_profileDetectionLogThread);
    connect(m_profileDetectionLogThread, &QThread::finished,
            m_profileDetectionLogWriter, &QObject::deleteLater);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::profileDetectionTraceBatch,
            m_profileDetectionLogWriter,
            &crawling::ProfileDetectionLogWriter::appendBatch,
            Qt::QueuedConnection);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::profileDiagnosticBatch,
            m_profileDetectionLogWriter,
            &crawling::ProfileDetectionLogWriter::appendDiagnosticBatch,
            Qt::QueuedConnection);
    m_profileDetectionLogThread->start(QThread::LowPriority);
    QMetaObject::invokeMethod(
        m_correctionController, "setProfileDetectionTraceEnabled",
        Qt::QueuedConnection, Q_ARG(bool, false));
    connect(m_sensorController,
            &crawling::RobotSensorController::correctionProfileFrameReady,
            m_correctionController,
            &crawling::LaserCorrectionController::submitProfileFrame,
            Qt::DirectConnection);
    connect(m_controller, &crawling::SynchronizedDriveController::telemetryChanged,
            m_correctionController,
            &crawling::LaserCorrectionController::processDriveTelemetry,
            Qt::QueuedConnection);
    connect(m_sensorController,
            &crawling::RobotSensorController::correctionProfilePrepared,
            this, [this](bool ready) {
                if (!m_autoCorrectionStartPending)
                    return;
                crawling::AppLogger::write(
                    QStringLiteral("CORRECTION.START"),
                    QStringLiteral(
                        "event=profile_prepare_complete ready=%1 pending=%2 executable=%3")
                        .arg(ready ? 1 : 0)
                        .arg(m_autoCorrectionStartPending ? 1 : 0)
                        .arg(QCoreApplication::applicationFilePath()));
                if (ready) {
                    m_autoCorrectionStartPending = false;
                    QMetaObject::invokeMethod(
                        m_correctionController, "setEnabled", Qt::QueuedConnection,
                        Q_ARG(bool, true));
                    if (m_autoCorrectionPaused)
                        QMetaObject::invokeMethod(
                            m_correctionController, "setPaused", Qt::QueuedConnection,
                            Q_ARG(bool, true));
                    appendLog(QStringLiteral("激光轮廓流已就绪，自动纠偏已启动"));
                } else {
                    m_autoCorrectionStartPending = false;
                    m_autoCorrectionActive = false;
                    setAutoCorrectionPaused(false);
                    finishProfileDetectionLogSession(
                        QStringLiteral("profile_prepare_failed"));
                    m_autoCorrectionStart->setEnabled(true);
                    m_autoCorrectionStop->setEnabled(false);
                    appendLog(QStringLiteral("激光轮廓流准备失败，自动纠偏未启动"));
                    m_correctionStatus->setText(QStringLiteral("启动失败：轮廓流准备失败"));
                }
            }, Qt::QueuedConnection);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::profileObservationReady,
            this, [this](const QVector<QVector3D> &points,
                         const crawling::LaserGapDetection &detection,
                         quint32 frameNumber, qint64 receivedAtEpochMs) {
                if (m_laserView)
                    m_laserView->setProfileObservation(
                        points, detection, frameNumber, receivedAtEpochMs);
            }, Qt::QueuedConnection);
    connect(m_correctionController, &crawling::LaserCorrectionController::commandChanged,
            m_controller, &crawling::SynchronizedDriveController::setCorrectionCommand,
            Qt::QueuedConnection);
    connect(m_correctionController, &crawling::LaserCorrectionController::statusChanged,
            this, &RobotControlPanel::updateCorrectionStatus, Qt::QueuedConnection);
    connect(m_correctionController, &crawling::LaserCorrectionController::trackingPhaseStarted,
            this, &RobotControlPanel::autoCorrectionTrackingStarted, Qt::QueuedConnection);
    connect(m_correctionController, &crawling::LaserCorrectionController::logMessage,
            this, &RobotControlPanel::appendLog, Qt::QueuedConnection);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::diagnosticLogMessage,
            this, [](const QString &message) {
                crawling::AppLogger::write(QStringLiteral("CORRECTION.RAW_IMAGE"),
                                           message);
            }, Qt::QueuedConnection);
    m_trajectoryThread = new QThread(this);
    m_trajectoryWriter = new crawling::LaserTrajectoryWriter;
    m_trajectoryWriter->moveToThread(m_trajectoryThread);
    connect(m_trajectoryThread, &QThread::finished, m_trajectoryWriter,
            &QObject::deleteLater);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::trajectorySessionRequested,
            m_trajectoryWriter, &crawling::LaserTrajectoryWriter::beginSession,
            Qt::QueuedConnection);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::trajectorySegmentReady,
            m_trajectoryWriter, &crawling::LaserTrajectoryWriter::saveSegment,
            Qt::QueuedConnection);
    connect(m_correctionController,
            &crawling::LaserCorrectionController::rawFrameReady,
            m_trajectoryWriter, &crawling::LaserTrajectoryWriter::saveRawFrame,
            Qt::QueuedConnection);
    connect(m_trajectoryWriter, &crawling::LaserTrajectoryWriter::sessionStarted,
            m_correctionController,
            &crawling::LaserCorrectionController::trajectorySessionStarted,
            Qt::QueuedConnection);
    connect(m_trajectoryWriter, &crawling::LaserTrajectoryWriter::imageSaved,
            m_correctionController,
            &crawling::LaserCorrectionController::trajectoryImageSaved,
            Qt::QueuedConnection);
    connect(m_trajectoryWriter, &crawling::LaserTrajectoryWriter::saveFailed,
            m_correctionController,
            &crawling::LaserCorrectionController::trajectorySaveFailed,
            Qt::QueuedConnection);
    connect(m_trajectoryWriter, &crawling::LaserTrajectoryWriter::rawFrameSaved,
            m_correctionController,
            &crawling::LaserCorrectionController::rawFrameSaved,
            Qt::QueuedConnection);
    m_correctionThread->start(QThread::HighPriority);
    m_trajectoryThread->start(QThread::LowPriority);

    m_usbCameraController = new crawling::RobotUsbCameraController;
    m_usbCameraThread = new QThread(this);
    m_usbCameraController->moveToThread(m_usbCameraThread);
    connect(m_usbCameraThread, &QThread::finished, m_usbCameraController,
            &QObject::deleteLater);
    connect(m_usbCameraController, &crawling::RobotUsbCameraController::devicesChanged,
            this, &RobotControlPanel::updateUsbDevices, Qt::QueuedConnection);
    connect(m_usbCameraController, &crawling::RobotUsbCameraController::connectionChanged,
            this, &RobotControlPanel::updateUsbConnection, Qt::QueuedConnection);
    connect(m_usbCameraController, &crawling::RobotUsbCameraController::frameChanged,
            this, &RobotControlPanel::setUsbCameraImage, Qt::QueuedConnection);
    connect(m_usbCameraController, &crawling::RobotUsbCameraController::logMessage,
            this, &RobotControlPanel::appendLog, Qt::QueuedConnection);
    m_usbCameraThread->start(QThread::LowPriority);

    m_commandTimer = new QTimer(this);
    m_commandTimer->setInterval(80);
    connect(m_commandTimer, &QTimer::timeout, this, &RobotControlPanel::sendMotionCommand);
    m_commandTimer->start();

    QTimer::singleShot(0, this, [this] {
        if (m_autoConnectCheck && m_autoConnectCheck->isChecked()) {
            connectConfiguredDevices();
            return;
        }
        // Clamp controls are used independently of the chassis connection.
        // Keep their configured adapters available after startup even when the
        // optional "connect all" preference is disabled.
        bool anyAutoConnect = false;
        for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
            if (m_usbCameraUi[slot].autoConnect && m_usbCameraUi[slot].autoConnect->isChecked()) {
                anyAutoConnect = true;
                break;
            }
        }
        if (anyAutoConnect) {
            appendLog(QStringLiteral("启动自动连接 USB 摄像头：已配置的网络地址或设备"));
            connectConfiguredUsbCamera();
        }
        appendLog(QStringLiteral("启动自动连接夹子电机：已配置的接口"));
        connectConfiguredClampMotor();
    });
}

RobotControlPanel::~RobotControlPanel()
{
    // The clamp controller is owned by the workspace rather than this panel.
    // Disconnect it while the controller and its adapters are still alive so
    // serial, PCAN, and candle handles are released during application exit.
    if (m_clampController)
        m_clampController->disconnectDevice();
    if (m_commandTimer)
        m_commandTimer->stop();
    // Drain the producer before deleting the receiver of the direct mailbox call.
    if (m_sensorController && m_correctionController)
        disconnect(m_sensorController,
                   &crawling::RobotSensorController::correctionProfileFrameReady,
                   m_correctionController,
                   &crawling::LaserCorrectionController::submitProfileFrame);
    if (m_correctionController) {
        QMetaObject::invokeMethod(m_correctionController, "setEnabled",
                                  Qt::BlockingQueuedConnection, Q_ARG(bool, false));
        QMetaObject::invokeMethod(m_correctionController, "shutdown",
                                  Qt::BlockingQueuedConnection);
    }
    if (m_sensorController)
        QMetaObject::invokeMethod(m_sensorController, "shutdown",
                                  Qt::BlockingQueuedConnection);
    finishProfileDetectionLogSession(QStringLiteral("application_shutdown"));
    if (m_trajectoryWriter)
        QMetaObject::invokeMethod(m_trajectoryWriter, "shutdown",
                                  Qt::BlockingQueuedConnection);
    if (m_trajectoryThread) {
        m_trajectoryThread->quit();
        m_trajectoryThread->wait();
    }
    if (m_correctionThread) {
        m_correctionThread->quit();
        m_correctionThread->wait();
    }
    if (m_profileDetectionLogThread) {
        m_profileDetectionLogThread->quit();
        m_profileDetectionLogThread->wait();
    }
    if (m_usbCameraController)
        QMetaObject::invokeMethod(m_usbCameraController, "shutdown",
                                  Qt::BlockingQueuedConnection);
    if (m_sensorThread) {
        m_sensorThread->quit();
        m_sensorThread->wait();
    }
    if (m_usbCameraThread) {
        m_usbCameraThread->quit();
        m_usbCameraThread->wait();
    }
    if (!m_driveThread || !m_driveThread->isRunning() || !m_controller)
        return;
    QMetaObject::invokeMethod(m_controller, "emergencyStop",
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_controller, "shutdown",
                              Qt::BlockingQueuedConnection);
    m_driveThread->quit();
    m_driveThread->wait();
    m_controller = nullptr;
}

void RobotControlPanel::buildUi()
{
    setObjectName(QStringLiteral("robotControlPanel"));
    setStyleSheet(QStringLiteral(R"(
        #robotControlPanel { background: #f3f6fa; }
        QScrollArea, #robotBody, #robotConfigPage { background: #f3f6fa; }
        #robotHeader { background: #13243a; border-radius: 8px; }
        #robotTitle { color: white; font-size: 21px; font-weight: 700; }
        #robotSubtitle { color: #b9c7d8; }
        QLabel[role="chip"] { color: white; background: #526174; border-radius: 10px;
                              padding: 5px 12px; font-weight: 600; }
        QGroupBox { color: #203247; background: white; border: 1px solid #d8e0ea;
                    border-radius: 7px; margin-top: 18px; padding-top: 12px;
                    font-weight: 600; }
        QGroupBox::title { color: #203247; background: white; subcontrol-origin: margin;
                           left: 12px; padding: 2px 6px; }
        QGroupBox QLabel { color: #203247; }
        QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
            color: #203247; background: white; border: 1px solid #b9c5d3;
            border-radius: 4px; padding-left: 7px; min-height: 28px;
            selection-color: white; selection-background-color: #1769aa;
        }
        QComboBox QAbstractItemView { color: #203247; background: white;
                                      selection-color: white; selection-background-color: #1769aa; }
        QPushButton { color: #164568; border: 1px solid #a8c4dc; border-radius: 5px;
                      padding: 6px 12px; background: #eaf2fb; }
        QPushButton:hover { background: #dcecfb; border-color: #4f89c7; }
        QPushButton[kind="primary"] { color: white; background: #1769aa; border-color: #1769aa; }
        QPushButton[kind="primary"]:hover { background: #12578f; }
        QPushButton[kind="danger"] { color: white; background: #c62828; border-color: #c62828;
                                     font-weight: 700; font-size: 16px; }
        QPushButton[kind="danger"]:hover { background: #a91e1e; }
        QPushButton[kind="jog"] { font-size: 18px; font-weight: 700; min-width: 92px; min-height: 52px; }
        QPushButton[kind="jog"][latched="true"] { color: white; background: #1769aa;
                                                  border-color: #0e4d7c; }
        #robotBody QGroupBox { margin-top: 12px; padding-top: 8px; }
        #robotBody QGroupBox::title { left: 9px; padding: 1px 5px; }
        #robotBody QPushButton[kind="jog"] { font-size: 16px; min-width: 76px; min-height: 38px;
                                               padding: 4px 8px; }
        QSlider#clampMotorSpeed::groove:horizontal { height: 6px; background: #d7e2ee;
                                                      border-radius: 3px; }
        QSlider#clampMotorSpeed::handle:horizontal { width: 14px; margin: -5px 0;
                                                      background: #1769aa; border-radius: 7px;
                                                      border: 1px solid #0e4d7c; }
        QSlider#clampMotorSpeed::handle:horizontal:hover { background: #2586c8; }
        QPushButton:disabled { color: #607286; background: #e7edf3; border-color: #cbd5df; }
        QLabel#robotMetric { color: #0b4f6c; font-family: Consolas, monospace; }
        QPlainTextEdit { background: #101a26; color: #c7d4e3; border: 1px solid #26384d;
                         font-family: Consolas, "Microsoft YaHei UI"; }
        QTabWidget::pane { border: 1px solid #d8e0ea; background: #f8fafb; }
        QTabBar::tab { color: #203247; padding: 8px 14px; background: #e4e9ed;
                       border: 1px solid #cbd5df; }
        QTabBar::tab:selected { color: #1769aa; background: white; font-weight: 600; }
        #robotHeader QTabBar { background: transparent; }
        #robotHeader QTabBar::tab { color: #d7e3ef; background: #263b52;
                                    border: 1px solid #4b637b; padding: 7px 14px;
                                    min-height: 28px; }
        #robotHeader QTabBar::tab:selected { color: white; background: #1769aa;
                                             border-color: #1769aa; font-weight: 700; }
        #robotConfigPage { font-size: 12px; }
        #robotConfigPage QLabel { color: #203247; font-size: 12px; }
        #robotConfigPage QGroupBox { margin-top: 12px; padding-top: 8px;
                                    border-radius: 6px; }
        #robotConfigPage QGroupBox::title { left: 9px; padding: 1px 5px; }
        #robotConfigPage QComboBox,
        #robotConfigPage QSpinBox,
        #robotConfigPage QDoubleSpinBox,
        #robotConfigPage QLineEdit { min-height: 24px; max-height: 26px;
                                     padding: 1px 5px; font-size: 12px; }
        #robotConfigPage QPushButton { min-height: 26px; max-height: 30px;
                                       padding: 3px 8px; font-size: 12px; }
        #robotConfigPage QCheckBox { color: #203247; spacing: 6px;
                                     min-height: 21px; font-size: 12px; }
        #robotConfigPage QPlainTextEdit { font-size: 11px; background: white;
                                           color: #203247; border-color: #b9c5d3; }
    )"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(10);

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("robotHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("爬行机器人控制台"));
    title->setObjectName(QStringLiteral("robotTitle"));
    auto *subtitle = new QLabel(QStringLiteral("轮廓定位 · USB 观察 · 底盘手动控制"));
    subtitle->setObjectName(QStringLiteral("robotSubtitle"));
    titles->addWidget(title);
    titles->addWidget(subtitle);
    headerLayout->addLayout(titles);
    auto *headerTabs = new QTabBar(header);
    headerTabs->setObjectName(QStringLiteral("robotHeaderTabs"));
    headerTabs->setExpanding(false);
    headerTabs->setDrawBase(false);
    headerTabs->addTab(QStringLiteral("底盘控制"));
    headerTabs->addTab(QStringLiteral("小车配置"));
    headerTabs->setCurrentIndex(0);
    headerLayout->addWidget(headerTabs);
    headerLayout->addStretch();
    m_connectionLabel = new QLabel(QStringLiteral("适配器未连接"));
    m_connectionLabel->setProperty("role", "chip");
    m_stateLabel = new QLabel(QStringLiteral("未连接"));
    m_stateLabel->setProperty("role", "chip");
    m_autoConnectCheck = new QCheckBox(
        QStringLiteral("启动时自动连接（夹子电机始终连接）"));
    m_autoConnectCheck->setStyleSheet(QStringLiteral("color:white;"));
    m_connectAllButton = actionButton(QStringLiteral("连接全部接口"), "primary");
    auto *disconnectAll = actionButton(QStringLiteral("断开全部接口"));
    headerLayout->addWidget(m_connectionLabel);
    headerLayout->addWidget(m_autoConnectCheck);
    headerLayout->addWidget(m_connectAllButton);
    headerLayout->addWidget(disconnectAll);
    headerLayout->addWidget(m_stateLabel);
    root->addWidget(header);

    m_tabs = new QTabWidget;
    auto *controlScroll = new QScrollArea;
    controlScroll->setWidgetResizable(true);
    controlScroll->setFrameShape(QFrame::NoFrame);
    controlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget;
    body->setObjectName(QStringLiteral("robotBody"));
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *columns = new QHBoxLayout(body);
    columns->setContentsMargins(4, 4, 4, 8);
    columns->setSpacing(12);

    auto *manual = new QGroupBox(QStringLiteral("手动运动"));
    auto *manualLayout = new QVBoxLayout(manual);
    auto *directionGrid = new QGridLayout;
    for (int column = 0; column < 3; ++column)
        directionGrid->setColumnStretch(column, 1);
    auto *forward = actionButton(QStringLiteral("▲ 前进"), "jog");
    auto *reverse = actionButton(QStringLiteral("▼ 后退"), "jog");
    auto *left = actionButton(QStringLiteral("◀ 左转"), "jog");
    auto *right = actionButton(QStringLiteral("右转 ▶"), "jog");
    directionGrid->addWidget(forward, 0, 1);
    directionGrid->addWidget(left, 1, 0);
    directionGrid->addWidget(right, 1, 2);
    directionGrid->addWidget(reverse, 2, 1);
    manualLayout->addLayout(directionGrid);
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(QStringLiteral("手动输出")));
    m_speedSlider = new QSlider(Qt::Horizontal);
    m_speedSlider->setRange(5, 100);
    m_speedLabel = new QLabel(QStringLiteral("30%"));
    m_speedLabel->setMinimumWidth(44);
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedLabel);
    manualLayout->addLayout(speedRow);
    auto *driveGrid = new QGridLayout;
    m_enableButton = actionButton(QStringLiteral("底盘使能"), "primary");
    m_stopMotionButton = actionButton(QStringLiteral("暂停"));
    m_stopMotionButton->setEnabled(false);
    m_stopMotionButton->setToolTip(QStringLiteral("暂停/继续自动纠偏"));
    auto *emergency = actionButton(QStringLiteral("紧急停止"), "danger");
    driveGrid->addWidget(m_enableButton, 0, 0);
    driveGrid->addWidget(m_stopMotionButton, 0, 1);
    driveGrid->addWidget(emergency, 1, 0, 1, 2);
    manualLayout->addLayout(driveGrid);

    auto *clampGroup = new QGroupBox(QStringLiteral("夹子电机手动控制"));
    auto *clampLayout = new QVBoxLayout(clampGroup);
    m_clampControls = new crawling::ClampMotorControlWidget(m_clampController);
    clampLayout->addWidget(m_clampControls);
    connect(m_clampController, &crawling::ClampMotorController::statusChanged,
            this, &RobotControlPanel::appendLog);

    auto *correctionControls = new QGroupBox(QStringLiteral("激光自动纠偏"));
    auto *correctionControlsLayout = new QVBoxLayout(correctionControls);
    auto *correctionHint = new QLabel(QStringLiteral("纠偏参数请在“小车配置”中设置。"));
    correctionHint->setWordWrap(true);
    correctionControlsLayout->addWidget(correctionHint);
    auto *correctionButtons = new QHBoxLayout;
    m_autoCorrectionStart = actionButton(QStringLiteral("启动自动纠偏"), "primary");
    m_autoCorrectionStop = actionButton(QStringLiteral("停止自动纠偏"));
    m_autoCorrectionStop->setEnabled(false);
    correctionButtons->addWidget(m_autoCorrectionStart);
    correctionButtons->addWidget(m_autoCorrectionStop);
    correctionControlsLayout->addLayout(correctionButtons);
    m_correctionStatus = new QLabel(QStringLiteral("未启动"), correctionControls);
    m_correctionStatus->setWordWrap(true);
    correctionControlsLayout->addWidget(m_correctionStatus);
    manualLayout->addWidget(correctionControls);
    manualLayout->addWidget(clampGroup);
    manualLayout->addStretch();

    auto *profileGroup = new QGroupBox(QStringLiteral("SDK 实时轮廓与焊道定位"));
    auto *profileLayout = new QVBoxLayout(profileGroup);
    auto *profileModeRow = new QHBoxLayout;
    profileModeRow->addWidget(new QLabel(QStringLiteral("轮廓显示方式")));
    auto *profileMode = new QComboBox(profileGroup);
    profileMode->addItem(QStringLiteral("XY"), static_cast<int>(RobotProfileView::ViewMode::XY));
    profileMode->addItem(QStringLiteral("XZ"), static_cast<int>(RobotProfileView::ViewMode::XZ));
    profileMode->addItem(QStringLiteral("YZ"), static_cast<int>(RobotProfileView::ViewMode::YZ));
    profileMode->setCurrentIndex(profileMode->findData(
        static_cast<int>(RobotProfileView::ViewMode::XZ)));
    profileModeRow->addWidget(profileMode);
    auto *templateButton = actionButton(QStringLiteral("建立/管理模板"));
    templateButton->setMinimumHeight(30);
    profileModeRow->addWidget(templateButton);
    m_profileDetectionLogCheck = new QCheckBox(
        QStringLiteral("记录检测计算日志"), profileGroup);
    m_profileDetectionLogCheck->setToolTip(
        QStringLiteral("勾选后，每次点击开始都会覆盖旧日志，只记录本次自动纠偏"
                       "从开始到结束的轮廓点、基线、阈值、候选区域和淘汰原因。"));
    profileModeRow->addWidget(m_profileDetectionLogCheck);
    profileModeRow->addWidget(new QLabel(QStringLiteral("定位算法"), profileGroup));
    m_profileDetectionMode = new QComboBox(profileGroup);
    m_profileDetectionMode->addItem(QStringLiteral("基线+肩部（默认）"),
                                    static_cast<int>(crawling::ProfileDetectionMode::BaselineShoulder));
    m_profileDetectionMode->addItem(QStringLiteral("纯模板匹配"),
                                    static_cast<int>(crawling::ProfileDetectionMode::TemplateOnly));
    m_profileDetectionMode->addItem(QStringLiteral("通用检测+模板"),
                                    static_cast<int>(crawling::ProfileDetectionMode::Hybrid));
    m_profileDetectionMode->setToolTip(
        QStringLiteral("选择 SDK X/Z 焊道定位方式；纯模板和混合模式需要至少一个已启用模板。"));
    profileModeRow->addWidget(m_profileDetectionMode);
    connect(m_profileDetectionMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!m_profileDetectionMode) return;
                const auto mode = static_cast<crawling::ProfileDetectionMode>(
                    m_profileDetectionMode->currentData().toInt());
                QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                                     QSettings::IniFormat);
                persistent.setValue(QStringLiteral("laserProfileTuning/profileDetectionMode"),
                                    static_cast<int>(mode));
                persistent.sync();
                if (m_correctionController) {
                    QMetaObject::invokeMethod(
                        m_correctionController,
                        [controller = m_correctionController, mode] {
                            controller->setProfileDetectionMode(mode);
                        }, Qt::QueuedConnection);
                }
                appendLog(QStringLiteral("实时焊道定位算法已切换：%1")
                              .arg(m_profileDetectionMode->currentText()));
            });
    connect(m_profileDetectionLogCheck, &QCheckBox::toggled,
            this, [this](bool enabled) {
                QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                                     QSettings::IniFormat);
                persistent.setValue(QStringLiteral(
                    "laserProfileTuning/profileDetectionTraceEnabled"), enabled);
                persistent.sync();
                appendLog(enabled
                              ? QStringLiteral("已启用焊道检测计算日志；下次点击开始时覆盖旧日志")
                              : QStringLiteral("已关闭焊道检测计算日志；自动纠偏时不记录"));
            });
    profileModeRow->addStretch();
    profileLayout->addLayout(profileModeRow);
    m_laserView = new RobotProfileView;
    m_laserView->setMinimumSize(220, 100);
    connect(profileMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            m_laserView, &RobotProfileView::setViewMode);
    connect(templateButton, &QPushButton::clicked,
            this, &RobotControlPanel::showProfileTemplateDialog);
    profileLayout->addWidget(m_laserView, 1);
    columns->addWidget(manual, 1);

    auto *rightColumn = new QVBoxLayout;
    rightColumn->addWidget(profileGroup, 1);
    auto *status = new QGroupBox(QStringLiteral("车体状态"));
    auto *statusGrid = new QGridLayout(status);
    statusGrid->setColumnStretch(1, 1);
    statusGrid->setColumnStretch(3, 1);
    auto addMetric = [statusGrid, status](int row, int column, const QString &name) {
        statusGrid->addWidget(new QLabel(name), row, column);
        auto *value = metricLabel(status);
        statusGrid->addWidget(value, row, column + 1);
        return value;
    };
    m_infoState = addMetric(0, 0, QStringLiteral("运行状态"));
    m_infoReason = addMetric(1, 0, QStringLiteral("状态说明"));
    m_infoTarget = addMetric(2, 0, QStringLiteral("目标运动"));
    m_infoApplied = addMetric(3, 0, QStringLiteral("实际运动"));
    m_infoLeftWheel = addMetric(4, 0, QStringLiteral("左轮"));
    m_infoRightWheel = addMetric(5, 0, QStringLiteral("右轮"));
    m_infoFeedback = addMetric(6, 0, QStringLiteral("反馈看门狗"));
    m_infoSync = addMetric(7, 0, QStringLiteral("双轮同步"));
    m_imuStatus = addMetric(0, 2, QStringLiteral("RIM302 IMU"));
    m_laserStatus = addMetric(1, 2, QStringLiteral("MV3DLP 激光"));
    m_usbStatus = addMetric(2, 2, QStringLiteral("USB 摄像头"));
    statusGrid->addWidget(new QLabel(QStringLiteral("辅助设备")), 3, 2);
    auto *aux = metricLabel(status);
    aux->setText(QStringLiteral("外部编码器未接入；夹子 CANopen 仅显示检测状态"));
    statusGrid->addWidget(aux, 3, 3, 2, 1);

    auto *usbGroup = new QGroupBox(QStringLiteral("USB 摄像头画面"));
    auto *usbLayout = new QVBoxLayout(usbGroup);
    auto *usbViewRow = new QWidget(usbGroup);
    auto *usbViewRowLayout = new QHBoxLayout(usbViewRow);
    usbViewRowLayout->setContentsMargins(0, 0, 0, 0);
    usbViewRowLayout->setSpacing(8);
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        auto *cell = new QGroupBox(QStringLiteral("摄像头 %1").arg(slot + 1), usbGroup);
        auto *cellLayout = new QVBoxLayout(cell);
        auto *label = new QLabel(QStringLiteral("USB 摄像头 %1 未连接").arg(slot + 1));
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumSize(180, 110);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cellLayout->addWidget(label);
        usbViewRowLayout->addWidget(cell, 1);
        m_usbViews[slot] = label;
    }
    usbLayout->addWidget(usbViewRow);
    rightColumn->addWidget(usbGroup, 1);
    rightColumn->addWidget(status);
    columns->addLayout(rightColumn, 2);
    controlScroll->setWidget(body);
    m_tabs->addTab(controlScroll, QStringLiteral("底盘控制"));
    m_tabs->addTab(buildConfigurationPage(), QStringLiteral("小车配置"));
    if (auto *contentTabs = m_tabs->findChild<QTabBar *>())
        contentTabs->hide();
    connect(headerTabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_tabs && m_tabs->currentIndex() != index)
            m_tabs->setCurrentIndex(index);
    });
    connect(m_tabs, &QTabWidget::currentChanged, headerTabs, &QTabBar::setCurrentIndex);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        QTimer::singleShot(0, this, [this] { refreshVisibleUi(); });
    });
    root->addWidget(m_tabs, 1);

    connect(m_connectAllButton, &QPushButton::clicked,
            this, &RobotControlPanel::connectAllConfiguredDevices);
    connect(disconnectAll, &QPushButton::clicked,
            this, &RobotControlPanel::disconnectAllDevices);
    connect(m_autoConnectCheck, &QCheckBox::toggled,
            this, &RobotControlPanel::persistAutoConnectSetting);
    connect(m_enableButton, &QPushButton::clicked, this, [this] { requestEnable(true); });
    connect(m_stopMotionButton, &QPushButton::clicked,
            this, &RobotControlPanel::stopMotion);
    connect(emergency, &QPushButton::clicked, this, &RobotControlPanel::emergencyStop);
    connect(m_autoCorrectionStart, &QPushButton::clicked,
            this, &RobotControlPanel::startAutoCorrection);
    connect(m_autoCorrectionStop, &QPushButton::clicked, this, [this] {
        crawling::AppLogger::write(
            QStringLiteral("CORRECTION.STOP"),
            QStringLiteral("event=correction_stop_requested source=ui_stop_button active=%1 pending=%2")
                .arg(m_autoCorrectionActive ? 1 : 0)
                .arg(m_autoCorrectionStartPending ? 1 : 0));
        stopAutoCorrection();
    });
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
        m_speedLabel->setText(QStringLiteral("%1%").arg(value));
    });
    m_jogHoldTimer = new QTimer(this);
    m_jogHoldTimer->setSingleShot(true);
    m_jogHoldTimer->setInterval(3000);
    connect(m_jogHoldTimer, &QTimer::timeout, this, [this, forward, reverse] {
        if (!m_pressedJogButton || !m_pressedJogFlag || !*m_pressedJogFlag)
            return;
        if (m_pressedJogButton != forward && m_pressedJogButton != reverse)
            return;
        m_jogLatched = true;
        m_latchedJogButton = m_pressedJogButton;
        m_latchedJogFlag = m_pressedJogFlag;
        m_latchedJogButton->setProperty("latched", true);
        m_latchedJogButton->style()->unpolish(m_latchedJogButton);
        m_latchedJogButton->style()->polish(m_latchedJogButton);
        m_latchedJogButton->setToolTip(QStringLiteral("已锁定运动，点击其他方向键取消"));
        appendLog(QStringLiteral("方向键长按锁定：松开后继续保持，点击其他方向键取消"));
    });
    const auto bindJog = [this](QPushButton *button, bool *flag) {
        button->setAutoRepeat(false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(QStringLiteral("按住3秒可锁定该方向"));
        connect(button, &QPushButton::pressed, this, [this, button, flag] {
            clearJogLatch();
            m_pressedJogButton = button;
            m_pressedJogFlag = flag;
            m_jogHoldTimer->start();
            setMotion(*flag, true);
        });
        connect(button, &QPushButton::released, this, [this, button, flag] {
            if (m_pressedJogButton == button) {
                m_jogHoldTimer->stop();
                m_pressedJogButton = nullptr;
                m_pressedJogFlag = nullptr;
                if (!m_jogLatched)
                    setMotion(*flag, false);
                return;
            }
            if (!m_jogLatched)
                setMotion(*flag, false);
        });
    };
    bindJog(forward, &m_forward);
    bindJog(reverse, &m_reverse);
    bindJog(left, &m_left);
    bindJog(right, &m_right);
}

QWidget *RobotControlPanel::buildConfigurationPage()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("robotConfigPage"));
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    auto *leftColumn = new QVBoxLayout;
    auto *rightColumn = new QVBoxLayout;
    leftColumn->setSpacing(8);
    rightColumn->setSpacing(8);
    leftColumn->setAlignment(Qt::AlignTop);
    rightColumn->setAlignment(Qt::AlignTop);
    // 左列承载 4 个配置组，右列承载 6 个配置组；按 2:3 分配宽度，
    // 避免左列表单占满过多横向空间。
    layout->addLayout(leftColumn, 2);
    layout->addLayout(rightColumn, 3);
    const auto compactGroup = [](QGroupBox *group) {
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        if (group->layout()) {
            group->layout()->setContentsMargins(8, 5, 8, 6);
            group->layout()->setSpacing(4);
        }
    };
    const auto buttonRow = [](std::initializer_list<QPushButton *> buttons) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);
        for (QPushButton *button : buttons)
            rowLayout->addWidget(button, 1);
        return row;
    };
    QList<QCheckBox *> configurationLocks;
    const auto addConfigurationLock = [&configurationLocks](QGroupBox *group) {
        auto *row = new QWidget(group);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addStretch();
        auto *lock = new QCheckBox(QStringLiteral("锁定"), row);
        lock->setToolTip(QStringLiteral("勾选后锁定该子项，取消勾选后才能修改参数"));
        rowLayout->addWidget(lock);
        row->setProperty("configLockExempt", true);

        if (auto *form = qobject_cast<QFormLayout *>(group->layout()))
            form->insertRow(0, row);
        else if (auto *box = qobject_cast<QBoxLayout *>(group->layout()))
            box->insertWidget(0, row);

        configurationLocks.append(lock);
        QObject::connect(lock, &QCheckBox::toggled, group, [group, row](bool locked) {
            const auto children = group->findChildren<QWidget *>(
                QString(), Qt::FindDirectChildrenOnly);
            for (QWidget *child : children) {
                if (child == row || child->property("configLockExempt").toBool())
                    continue;
                if (locked) {
                    if (!child->property("configLockWasEnabled").isValid())
                        child->setProperty("configLockWasEnabled", child->isEnabled());
                    child->setEnabled(false);
                } else if (child->property("configLockWasEnabled").isValid()) {
                    child->setEnabled(child->property("configLockWasEnabled").toBool());
                    child->setProperty("configLockWasEnabled", QVariant());
                }
            }
        });
        return lock;
    };

    auto *correctionGroup = new QGroupBox(QStringLiteral("激光自动纠偏"), page);
    auto *correctionLayout = new QVBoxLayout(correctionGroup);
    auto *correctionForm = new QFormLayout;
    m_correctionSpeed = makeDouble(1.0, 150.0, 1.0, 1, correctionGroup);
    m_correctionSpeed->setValue(5.0);
    m_correctionSegment = makeDouble(1.0, 30.0, 1.0, 1, correctionGroup);
    m_correctionSegment->setValue(30.0);
    m_correctionKp = makeDouble(0.0, 20.0, 0.1, 2, correctionGroup);
    m_correctionKp->setValue(3.0);
    m_correctionKd = makeDouble(0.0, 5.0, 0.01, 2, correctionGroup);
    m_correctionKd->setValue(0.12);
    correctionForm->addRow(QStringLiteral("纠偏速度 (mm/s)"),
                           wrapWithHint(m_correctionSpeed,
                                        QStringLiteral("调大行走纠偏更快；调小更慢更稳")));
    correctionForm->addRow(QStringLiteral("分段长度 (mm)"),
                           wrapWithHint(m_correctionSegment,
                                        QStringLiteral("调大拟合窗口更长更平滑；调小局部跟随更细")));
    correctionForm->addRow(QStringLiteral("Kp"),
                           wrapWithHint(m_correctionKp,
                                        QStringLiteral("调大偏差修正更强但易振荡；调小更柔和")));
    correctionForm->addRow(QStringLiteral("Kd"),
                           wrapWithHint(m_correctionKd,
                                        QStringLiteral("调大抑制摆动更稳；调小响应更灵但易振荡")));
    correctionLayout->addLayout(correctionForm);
    addConfigurationLock(correctionGroup);
    compactGroup(correctionGroup);
    rightColumn->addWidget(correctionGroup);

    auto *adapter = new QGroupBox(QStringLiteral("MWD RS485 适配器"), page);
    auto *adapterForm = new QFormLayout(adapter);
    m_wheelCommunicationMode = new QComboBox(adapter);
    m_wheelCommunicationMode->addItem(QStringLiteral("CAN（默认）"),
                                       QStringLiteral("can"));
    m_wheelCommunicationMode->addItem(QStringLiteral("RS485"),
                                       QStringLiteral("rs485"));
    m_wheelCanPort = new QComboBox(adapter);
    m_wheelCanBaud = new QComboBox(adapter);
    for (int baud : {115200, 230400, 460800, 921600})
        m_wheelCanBaud->addItem(QString::number(baud), baud);
    m_wheelCanBitrate = new QComboBox(adapter);
    for (int bitrate : {125000, 250000, 500000, 800000, 1000000})
        m_wheelCanBitrate->addItem(QString::number(bitrate), bitrate);
    m_leftPort = new QComboBox(adapter);
    m_rightPort = new QComboBox(adapter);
    m_leftBaud = new QComboBox(adapter);
    m_rightBaud = new QComboBox(adapter);
    populateBaudRates(m_leftBaud);
    populateBaudRates(m_rightBaud);
    m_leftId = new QSpinBox(adapter);
    m_rightId = new QSpinBox(adapter);
    m_leftId->setRange(1, 32);
    m_rightId->setRange(1, 32);
    m_leftSign = new QComboBox(adapter);
    m_rightSign = new QComboBox(adapter);
    for (QComboBox *box : {m_leftSign, m_rightSign}) {
        box->addItem(QStringLiteral("+1 正向"), 1);
        box->addItem(QStringLiteral("-1 反向"), -1);
    }
    auto *refresh = actionButton(QStringLiteral("刷新串口"));
    auto *connectButton = actionButton(QStringLiteral("连接底盘"), "primary");
    auto *disconnectButton = actionButton(QStringLiteral("断开底盘"));
    adapterForm->addRow(QStringLiteral("驱动轮通信方式"), m_wheelCommunicationMode);
    auto addTransportRow = [adapterForm](const QString &label, QWidget *widget,
                                          QList<QWidget *> *rows) {
        adapterForm->addRow(label, widget);
        if (rows != nullptr) {
            rows->append(adapterForm->labelForField(widget));
            rows->append(widget);
        }
    };
    addTransportRow(QStringLiteral("CAN/SLCAN 串口"), m_wheelCanPort,
                    &m_wheelCanRows);
    addTransportRow(QStringLiteral("CAN 适配器波特率"), m_wheelCanBaud,
                    &m_wheelCanRows);
    addTransportRow(QStringLiteral("CAN 总线波特率"), m_wheelCanBitrate,
                    &m_wheelCanRows);
    addTransportRow(QStringLiteral("左轮串口"), m_leftPort, &m_wheelRs485Rows);
    addTransportRow(QStringLiteral("左轮波特率"), m_leftBaud, &m_wheelRs485Rows);
    adapterForm->addRow(QStringLiteral("左轮 ID"), m_leftId);
    adapterForm->addRow(QStringLiteral("左轮方向"), m_leftSign);
    addTransportRow(QStringLiteral("右轮串口"), m_rightPort, &m_wheelRs485Rows);
    addTransportRow(QStringLiteral("右轮波特率"), m_rightBaud, &m_wheelRs485Rows);
    adapterForm->addRow(QStringLiteral("右轮 ID"), m_rightId);
    adapterForm->addRow(QStringLiteral("右轮方向"), m_rightSign);
    adapterForm->addRow(QString(), buttonRow({refresh, connectButton, disconnectButton}));
    connect(refresh, &QPushButton::clicked, this, &RobotControlPanel::refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, &RobotControlPanel::connectDrive);
    connect(disconnectButton, &QPushButton::clicked, this, &RobotControlPanel::disconnectDrive);
    const auto updateWheelTransportUi = [this] {
        const bool can = m_wheelCommunicationMode->currentData().toString() == QStringLiteral("can");
        for (QWidget *widget : m_wheelCanRows) {
            widget->setVisible(can);
        }
        for (QWidget *widget : m_wheelRs485Rows) {
            widget->setVisible(!can);
        }
    };
    connect(m_wheelCommunicationMode, &QComboBox::currentIndexChanged,
            this, [updateWheelTransportUi](int) { updateWheelTransportUi(); });
    updateWheelTransportUi();
    addConfigurationLock(adapter);
    compactGroup(adapter);
    leftColumn->addWidget(adapter);

    auto *mapping = new QGroupBox(QStringLiteral("轮子与底盘几何"), page);
    auto *mappingForm = new QFormLayout(mapping);
    m_wheelRadius = makeDouble(1.0, 500.0, 1.0, 2, mapping);
    m_trackWidth = makeDouble(10.0, 2000.0, 1.0, 1, mapping);
    m_ratio = makeDouble(0.001, 1000.0, 0.01, 4, mapping);
    m_ratio->setToolTip(QStringLiteral("只填写输出轴到车轮的外部传动比；直接带轮填 1，不包含电机内部减速比"));
    mappingForm->addRow(QStringLiteral("轮半径 (mm)"), m_wheelRadius);
    mappingForm->addRow(QStringLiteral("轮距 (mm)"), m_trackWidth);
    mappingForm->addRow(QStringLiteral("输出轴到车轮传动比"), m_ratio);
    addConfigurationLock(mapping);
    compactGroup(mapping);
    rightColumn->addWidget(mapping);

    auto *imu = new QGroupBox(QStringLiteral("RIM302 IMU"), page);
    auto *imuForm = new QFormLayout(imu);
    m_imuPort = new QComboBox(imu);
    m_imuBaud = new QComboBox(imu);
    for (int baud : {9600, 19200, 38400, 57600, 115200, 256000})
        m_imuBaud->addItem(QString::number(baud), baud);
    m_imuDivider = new QComboBox(imu);
    for (int divider : {1, 2, 4, 8, 10, 20, 40, 200})
        m_imuDivider->addItem(QStringLiteral("分频 %1").arg(divider), divider);
    auto *imuScan = actionButton(QStringLiteral("连接配置中的 IMU"), "primary");
    auto *imuDisconnect = actionButton(QStringLiteral("断开 IMU"));
    m_imuConfigState = new QLabel(QStringLiteral("未连接"), imu);
    imuForm->addRow(QStringLiteral("串口"), m_imuPort);
    imuForm->addRow(QStringLiteral("波特率"), m_imuBaud);
    imuForm->addRow(QStringLiteral("输出分频"), m_imuDivider);
    imuForm->addRow(QString(), buttonRow({imuScan, imuDisconnect}));
    imuForm->addRow(QStringLiteral("连接状态"), m_imuConfigState);
    addConfigurationLock(imu);
    connect(imuScan, &QPushButton::clicked, this, &RobotControlPanel::connectConfiguredImu);
    connect(imuDisconnect, &QPushButton::clicked, this, [this] {
        if (m_sensorController)
            QMetaObject::invokeMethod(m_sensorController, "disconnectImu",
                                      Qt::QueuedConnection);
    });
    compactGroup(imu);
    leftColumn->addWidget(imu);

    auto *laser = new QGroupBox(QStringLiteral("MV3DLP 激光相机"), page);
    auto *laserForm = new QFormLayout(laser);
    m_laserSerial = new QLineEdit(laser);
    m_laserSerial->setReadOnly(true);
    m_laserSerial->setPlaceholderText(QStringLiteral("扫描后自动选择"));
    m_laserDevice = new QComboBox(laser);
    auto *laserScan = actionButton(QStringLiteral("扫描激光相机"));
    auto *laserConnect = actionButton(QStringLiteral("连接配置中的激光相机"), "primary");
    auto *laserDisconnect = actionButton(QStringLiteral("断开激光相机"));
    m_laserConfigState = new QLabel(QStringLiteral("未连接"), laser);
    m_laserFrameState = new QLabel(QStringLiteral("暂无帧数据"), laser);
    laserForm->addRow(QStringLiteral("当前设备序列号"), m_laserSerial);
    laserForm->addRow(QStringLiteral("已发现设备"), m_laserDevice);
    laserForm->addRow(QString(), buttonRow({laserScan, laserConnect, laserDisconnect}));
    laserForm->addRow(QStringLiteral("连接状态"), m_laserConfigState);
    laserForm->addRow(QStringLiteral("最新帧"), m_laserFrameState);
    connect(laserScan, &QPushButton::clicked, this, [this] {
        if (m_sensorController)
            QMetaObject::invokeMethod(m_sensorController, "scanCamera",
                                      Qt::QueuedConnection);
    });
    connect(laserConnect, &QPushButton::clicked, this,
            &RobotControlPanel::connectConfiguredLaser);
    connect(laserDisconnect, &QPushButton::clicked, this, [this] {
        if (m_sensorController)
            QMetaObject::invokeMethod(m_sensorController, "disconnectCamera",
                                      Qt::QueuedConnection);
    });
    connect(m_laserDevice, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0)
            m_laserSerial->setText(m_laserDevice->itemText(index).section(QStringLiteral(" | "), 0, 0));
    });
    addConfigurationLock(laser);
    compactGroup(laser);
    rightColumn->addWidget(laser);

    auto *usb = new QGroupBox(QStringLiteral("摄像头配置"), page);
    auto *usbOuterLayout = new QVBoxLayout(usb);
    usbOuterLayout->setContentsMargins(8, 12, 8, 8);
    usbOuterLayout->setSpacing(8);
    auto *usb1 = new QGroupBox(QStringLiteral("摄像头 %1").arg(1), usb);
    auto *usbForm1 = new QFormLayout(usb1);
    m_usbCameraUi[0].source = new QComboBox(usb1);
    m_usbCameraUi[0].source->addItem(QStringLiteral("USB 摄像头"),
                                     static_cast<int>(crawling::CameraSource::Usb));
    m_usbCameraUi[0].source->addItem(QStringLiteral("网络摄像头 (RTSP/HTTP)"),
                                     static_cast<int>(crawling::CameraSource::Network));
    m_usbCameraUi[0].device = new QComboBox(usb1);
    m_usbCameraUi[0].device->setCurrentIndex(-1);  // populated by updateUsbDevices().
    m_usbCameraUi[0].networkUrl = new QLineEdit(usb1);
    m_usbCameraUi[0].networkUrl->setPlaceholderText(QStringLiteral("例如 rtsp://192.168.1.50:554/stream 或 http://192.168.1.50/video"));
    m_usbCameraUi[0].fps = new QSpinBox(usb1);
    m_usbCameraUi[0].fps->setRange(1, 120);
    m_usbCameraUi[0].autoConnect = new QCheckBox(QStringLiteral("参与连接全部接口和启动时自动连接"), usb1);
    m_usbCameraUi[0].flipHorizontal = new QCheckBox(QStringLiteral("画面水平翻转"), usb1);
    m_usbCameraUi[0].flipVertical = new QCheckBox(QStringLiteral("画面竖直翻转"), usb1);
    for (QCheckBox *checkBox : {m_usbCameraUi[0].autoConnect, m_usbCameraUi[0].flipHorizontal, m_usbCameraUi[0].flipVertical}) {
        checkBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        checkBox->setMinimumHeight(21);
    }
    m_usbCameraUi[0].scan = actionButton(QStringLiteral("扫描 USB 摄像头"));
    m_usbCameraUi[0].connect = actionButton(QStringLiteral("连接配置中的摄像头"), "primary");
    m_usbCameraUi[0].disconnect = actionButton(QStringLiteral("断开 USB 摄像头"));
    m_usbCameraUi[0].configState = new QLabel(QStringLiteral("未连接"), usb1);
    usbForm1->addRow(QStringLiteral("连接方式"), m_usbCameraUi[0].source);
    usbForm1->addRow(QStringLiteral("设备"), m_usbCameraUi[0].device);
    usbForm1->addRow(QStringLiteral("网络摄像头地址"), m_usbCameraUi[0].networkUrl);
    usbForm1->addRow(QStringLiteral("帧率 (fps)"), m_usbCameraUi[0].fps);
    usbForm1->addRow(QString(), buttonRow({m_usbCameraUi[0].scan, m_usbCameraUi[0].connect, m_usbCameraUi[0].disconnect}));
    auto *opts1 = new QWidget;
    auto *opts1Layout = new QHBoxLayout(opts1);
    opts1Layout->setContentsMargins(0, 0, 0, 0);
    opts1Layout->setSpacing(14);
    opts1Layout->addWidget(m_usbCameraUi[0].autoConnect);
    opts1Layout->addWidget(m_usbCameraUi[0].flipHorizontal);
    opts1Layout->addWidget(m_usbCameraUi[0].flipVertical);
    opts1Layout->addStretch();
    usbForm1->addRow(QString(), opts1);
    usbForm1->addRow(QStringLiteral("连接状态"), m_usbCameraUi[0].configState);
    const int slotIndex0 = 0;
    connect(m_usbCameraUi[0].source, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, slotIndex0](int) { updateUsbCameraSourceUi(slotIndex0); });
    connect(m_usbCameraUi[0].scan, &QPushButton::clicked, this, [this] {
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "scanDevices",
                                      Qt::QueuedConnection);
    });
    connect(m_usbCameraUi[0].connect, &QPushButton::clicked, this, [this, slotIndex0] {
        connectConfiguredUsbCameraSlot(slotIndex0);
    });
    connect(m_usbCameraUi[0].disconnect, &QPushButton::clicked, this, [this, slotIndex0] {
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "disconnectCamera",
                                      Qt::QueuedConnection, Q_ARG(int, slotIndex0));
    });
    updateUsbCameraSourceUi(slotIndex0);
    usbOuterLayout->addWidget(usb1);
    compactGroup(usb1);
    auto *usb2 = new QGroupBox(QStringLiteral("摄像头 %1").arg(2), usb);
    auto *usbForm2 = new QFormLayout(usb2);
    m_usbCameraUi[1].source = new QComboBox(usb2);
    m_usbCameraUi[1].source->addItem(QStringLiteral("USB 摄像头"),
                                     static_cast<int>(crawling::CameraSource::Usb));
    m_usbCameraUi[1].source->addItem(QStringLiteral("网络摄像头 (RTSP/HTTP)"),
                                     static_cast<int>(crawling::CameraSource::Network));
    m_usbCameraUi[1].device = new QComboBox(usb2);
    m_usbCameraUi[1].device->setCurrentIndex(-1);  // populated by updateUsbDevices().
    m_usbCameraUi[1].networkUrl = new QLineEdit(usb2);
    m_usbCameraUi[1].networkUrl->setPlaceholderText(QStringLiteral("例如 rtsp://192.168.1.50:554/stream 或 http://192.168.1.50/video"));
    m_usbCameraUi[1].fps = new QSpinBox(usb2);
    m_usbCameraUi[1].fps->setRange(1, 120);
    m_usbCameraUi[1].autoConnect = new QCheckBox(QStringLiteral("参与连接全部接口和启动时自动连接"), usb2);
    m_usbCameraUi[1].flipHorizontal = new QCheckBox(QStringLiteral("画面水平翻转"), usb2);
    m_usbCameraUi[1].flipVertical = new QCheckBox(QStringLiteral("画面竖直翻转"), usb2);
    for (QCheckBox *checkBox : {m_usbCameraUi[1].autoConnect, m_usbCameraUi[1].flipHorizontal, m_usbCameraUi[1].flipVertical}) {
        checkBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        checkBox->setMinimumHeight(21);
    }
    m_usbCameraUi[1].scan = actionButton(QStringLiteral("扫描 USB 摄像头"));
    m_usbCameraUi[1].connect = actionButton(QStringLiteral("连接配置中的摄像头"), "primary");
    m_usbCameraUi[1].disconnect = actionButton(QStringLiteral("断开 USB 摄像头"));
    m_usbCameraUi[1].configState = new QLabel(QStringLiteral("未连接"), usb2);
    usbForm2->addRow(QStringLiteral("连接方式"), m_usbCameraUi[1].source);
    usbForm2->addRow(QStringLiteral("设备"), m_usbCameraUi[1].device);
    usbForm2->addRow(QStringLiteral("网络摄像头地址"), m_usbCameraUi[1].networkUrl);
    usbForm2->addRow(QStringLiteral("帧率 (fps)"), m_usbCameraUi[1].fps);
    usbForm2->addRow(QString(), buttonRow({m_usbCameraUi[1].scan, m_usbCameraUi[1].connect, m_usbCameraUi[1].disconnect}));
    auto *opts2 = new QWidget;
    auto *opts2Layout = new QHBoxLayout(opts2);
    opts2Layout->setContentsMargins(0, 0, 0, 0);
    opts2Layout->setSpacing(14);
    opts2Layout->addWidget(m_usbCameraUi[1].autoConnect);
    opts2Layout->addWidget(m_usbCameraUi[1].flipHorizontal);
    opts2Layout->addWidget(m_usbCameraUi[1].flipVertical);
    opts2Layout->addStretch();
    usbForm2->addRow(QString(), opts2);
    usbForm2->addRow(QStringLiteral("连接状态"), m_usbCameraUi[1].configState);
    const int slotIndex1 = 1;
    connect(m_usbCameraUi[1].source, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, slotIndex1](int) { updateUsbCameraSourceUi(slotIndex1); });
    connect(m_usbCameraUi[1].scan, &QPushButton::clicked, this, [this] {
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "scanDevices",
                                      Qt::QueuedConnection);
    });
    connect(m_usbCameraUi[1].connect, &QPushButton::clicked, this, [this, slotIndex1] {
        connectConfiguredUsbCameraSlot(slotIndex1);
    });
    connect(m_usbCameraUi[1].disconnect, &QPushButton::clicked, this, [this, slotIndex1] {
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "disconnectCamera",
                                      Qt::QueuedConnection, Q_ARG(int, slotIndex1));
    });
    updateUsbCameraSourceUi(slotIndex1);
    usbOuterLayout->addWidget(usb2);
    compactGroup(usb2);
    compactGroup(usb);
    // The outer box only wraps the two channel groups, so let it grow with them
    // instead of collapsing to a single row like the leaf configuration groups.
    usb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addConfigurationLock(usb);
    leftColumn->addWidget(usb);

    auto *clamp = new QGroupBox(QStringLiteral("夹子电机通信"), page);
    auto *clampForm = new QFormLayout(clamp);
    m_clampSettings = new crawling::ClampMotorSettingsWidget(clamp);
    m_clampSettings->setController(m_clampController);
    clampForm->addRow(m_clampSettings);
    auto *clampSave = actionButton(QStringLiteral("保存夹子配置"));
    auto *clampRefresh = actionButton(QStringLiteral("刷新串口"));
    clampForm->addRow(buttonRow({clampSave, clampRefresh}));
    connect(clampSave, &QPushButton::clicked, this, [this] { saveClampSettings(); });
    connect(clampRefresh, &QPushButton::clicked, m_clampSettings,
            &crawling::ClampMotorSettingsWidget::refreshPorts);
    connect(m_clampSettings, &crawling::ClampMotorSettingsWidget::edited,
            this, [this] {
        m_clampController->stop();
        m_clampControls->setEnabled(false);
        m_clampConfigState->setText(QStringLiteral("配置已修改，请保存后控制"));
    });
    connect(m_clampController, &crawling::ClampMotorController::settingsChanged,
            this, [this] {
        const crawling::DriveSettings snapshot = m_clampController->settings();
        m_settings.clampMotors = snapshot.clampMotors;
        m_settings.clampMotorSyncEnabled = snapshot.clampMotorSyncEnabled;
        m_clampSettings->setSettings(m_settings.clampMotors);
        for (int axis = 0; axis < 3; ++axis) m_clampSettings->setSyncEnabled(axis, m_settings.clampMotorSyncEnabled[axis]);
        m_clampControls->setEnabled(true);
    });
    auto *clampConnect = actionButton(QStringLiteral("连接夹子电机"), "primary");
    auto *clampDisconnect = actionButton(QStringLiteral("断开夹子电机"));
    m_clampConfigState = new QLabel(QStringLiteral("未连接"), clamp);
    clampForm->addRow(QString(), buttonRow({clampConnect, clampDisconnect}));
    clampForm->addRow(QStringLiteral("连接状态"), m_clampConfigState);
    clampForm->addRow(QStringLiteral("检测状态"), metricLabel(clamp));
    connect(clampConnect, &QPushButton::clicked,
            this, &RobotControlPanel::connectConfiguredClampMotor);
    connect(clampDisconnect, &QPushButton::clicked, this, [this] {
        if (m_clampController)
            QMetaObject::invokeMethod(m_clampController, "disconnectDevice",
                                      Qt::QueuedConnection);
    });
    addConfigurationLock(clamp);
    compactGroup(clamp);
    rightColumn->addWidget(clamp);

    auto *limits = new QGroupBox(QStringLiteral("速度与同步参数"), page);
    auto *limitsForm = new QFormLayout(limits);
    m_maxWheelSpeed = makeDouble(5.0, 2000.0, 5.0, 1, limits);
    m_maxLinearSpeed = makeDouble(5.0, 2000.0, 5.0, 1, limits);
    m_maxAngularSpeed = makeDouble(0.5, 573.0, 0.5, 1, limits);
    m_maxLinearAcceleration = makeDouble(10.0, 10000.0, 10.0, 1, limits);
    m_maxAngularAcceleration = makeDouble(0.5, 1146.0, 0.5, 1, limits);
    m_minimumInnerRatio = makeDouble(0.0, 1.0, 0.01, 3, limits);
    m_syncP = makeDouble(0.0, 5.0, 0.01, 3, limits);
    m_syncI = makeDouble(0.0, 5.0, 0.01, 3, limits);
    m_syncMaxCorrection = makeDouble(1.0, 300.0, 1.0, 1, limits);
    m_syncMinSpeed = makeDouble(1.0, 300.0, 1.0, 1, limits);
    limitsForm->addRow(QStringLiteral("最大轮速 (mm/s)"),
                       wrapWithHint(m_maxWheelSpeed,
                                    QStringLiteral("调大单轮允许更快；调小整体更保守")));
    limitsForm->addRow(QStringLiteral("最大线速度 (mm/s)"),
                       wrapWithHint(m_maxLinearSpeed,
                                    QStringLiteral("调大直行上限更高；调小行驶更保守")));
    limitsForm->addRow(QStringLiteral("最大角速度 (deg/s)"),
                       wrapWithHint(m_maxAngularSpeed,
                                    QStringLiteral("调大转向上限更高；调小转弯更缓")));
    limitsForm->addRow(QStringLiteral("线加速度 (mm/s²)"),
                       wrapWithHint(m_maxLinearAcceleration,
                                    QStringLiteral("调大起停更跟手；调小加减速更平顺")));
    limitsForm->addRow(QStringLiteral("角加速度 (deg/s²)"),
                       wrapWithHint(m_maxAngularAcceleration,
                                    QStringLiteral("调大转向响应更快；调小转向更平顺")));
    limitsForm->addRow(QStringLiteral("内侧轮最小比例"),
                       wrapWithHint(m_minimumInnerRatio,
                                    QStringLiteral("调大转弯更稳；调小转弯更急")));
    limitsForm->addRow(QStringLiteral("同步比例增益 P"),
                       wrapWithHint(m_syncP,
                                    QStringLiteral("调大响应快易抖；调小平滑但滞后")));
    limitsForm->addRow(QStringLiteral("同步积分增益 I"),
                       wrapWithHint(m_syncI,
                                    QStringLiteral("调大消偏差快易过冲；调小更稳收敛慢")));
    limitsForm->addRow(QStringLiteral("同步最大修正量 (mm/s)"),
                       wrapWithHint(m_syncMaxCorrection,
                                    QStringLiteral("调大同步强掉速多；调小修正温和")));
    limitsForm->addRow(QStringLiteral("同步最小受控速度 (mm/s)"),
                       wrapWithHint(m_syncMinSpeed,
                                    QStringLiteral("调大低速不同步；调小低速仍同步")));
    addConfigurationLock(limits);
    compactGroup(limits);
    rightColumn->insertWidget(1, limits);

    auto *safety = new QGroupBox(QStringLiteral("安全看门狗"), page);
    auto *safetyForm = new QFormLayout(safety);
    m_commandTimeout = new QSpinBox(safety);
    m_feedbackTimeout = new QSpinBox(safety);
    m_armingTimeout = new QSpinBox(safety);
    m_laserInputTimeout = new QSpinBox(safety);
    for (QSpinBox *box : {m_commandTimeout, m_feedbackTimeout, m_armingTimeout,
                          m_laserInputTimeout}) {
        box->setRange(100, 10000);
        box->setSuffix(QStringLiteral(" ms"));
    }
    m_feedbackTimeout->setMinimum(crawling::DriveSettings::kMinimumFeedbackTimeoutMs);
    m_armingTimeout->setMinimum(crawling::DriveSettings::kMinimumFeedbackTimeoutMs);
    m_laserInputTimeout->setRange(50, 5000);
    safetyForm->addRow(QStringLiteral("命令超时"), m_commandTimeout);
    safetyForm->addRow(QStringLiteral("反馈超时"), m_feedbackTimeout);
    safetyForm->addRow(QStringLiteral("使能等待超时"), m_armingTimeout);
    safetyForm->addRow(QStringLiteral("激光轮廓输入超时"), m_laserInputTimeout);
    auto *save = actionButton(QStringLiteral("保存并应用全部参数"), "primary");
    save->setProperty("configLockExempt", true);
    safetyForm->addRow(QString(), save);
    addConfigurationLock(safety);
    m_laserConfigState->setWordWrap(true);
    m_laserFrameState->setWordWrap(true);
    m_imuConfigState->setWordWrap(true);
    compactGroup(safety);
    leftColumn->addWidget(safety);

    auto *logGroup = new QGroupBox(QStringLiteral("接口日志"), page);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_infoLog = new QPlainTextEdit(logGroup);
    m_infoLog->setReadOnly(true);
    m_infoLog->setMaximumBlockCount(400);
    m_infoLog->setMinimumHeight(170);
    m_infoLog->setMaximumHeight(230);
    logLayout->addWidget(m_infoLog);
    m_logFlushTimer = new QTimer(this);
    m_logFlushTimer->setSingleShot(true);
    m_logFlushTimer->setInterval(150);
    connect(m_logFlushTimer, &QTimer::timeout, this, &RobotControlPanel::flushLogUi);
    compactGroup(logGroup);
    rightColumn->addWidget(logGroup);

    connect(save, &QPushButton::clicked, this, [this, configurationLocks] {
        const QString error = settingsFromUi().validationError();
        if (!error.isEmpty()) {
            appendLog(error);
            QMessageBox::warning(this, QStringLiteral("配置无效"), error);
            return;
        }
        saveSettings();
        if (m_connected)
            connectConfiguredDevices();
        for (QCheckBox *lock : configurationLocks)
            lock->setChecked(true);
    });
    const auto markDirty = [this] {
        if (m_infoLog)
            m_infoLog->setToolTip(QStringLiteral("参数已修改，点击“保存并应用全部参数”"));
    };
    for (QDoubleSpinBox *box : {m_wheelRadius, m_trackWidth, m_ratio, m_maxWheelSpeed,
                                m_maxLinearSpeed, m_maxAngularSpeed,
                                m_maxLinearAcceleration, m_maxAngularAcceleration,
                                m_minimumInnerRatio, m_syncP, m_syncI,
                                m_syncMaxCorrection, m_syncMinSpeed})
        connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, markDirty);
    for (QSpinBox *box : {m_leftId, m_rightId, m_commandTimeout, m_feedbackTimeout,
                          m_armingTimeout, m_laserInputTimeout})
        connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    for (QComboBox *box : {m_wheelCommunicationMode, m_wheelCanPort,
                           m_wheelCanBaud, m_wheelCanBitrate, m_leftPort,
                           m_rightPort, m_leftBaud, m_rightBaud, m_leftSign,
                           m_rightSign})
        connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [markDirty](int) { markDirty(); });
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, markDirty);
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        for (QCheckBox *box : {m_usbCameraUi[slot].autoConnect,
                               m_usbCameraUi[slot].flipHorizontal,
                               m_usbCameraUi[slot].flipVertical}) {
            if (box)
                connect(box, &QCheckBox::toggled, this, markDirty);
        }
        if (m_usbCameraUi[slot].fps)
            connect(m_usbCameraUi[slot].fps,
                    QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    }
    for (QPushButton *button : page->findChildren<QPushButton *>()) {
        button->setMinimumHeight(26);
        button->setMaximumHeight(30);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    scroll->setWidget(page);
    return scroll;
}

void RobotControlPanel::loadSettings()
{
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    m_settings = crawling::DriveSettings::load(persistent);
    settingsToUi(m_settings);
    m_clampController->setSettings(m_settings);
    m_clampControls->setEnabled(true);
    loadProfileWeldTuning();
    loadProfileTemplates();
}

void RobotControlPanel::loadProfileWeldTuning()
{
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    const auto loadDetectionMode = [this, &persistent] {
        if (!m_profileDetectionMode) return;
        QSignalBlocker blocker(m_profileDetectionMode);
        const int mode = persistent.value(
            QStringLiteral("laserProfileTuning/profileDetectionMode"),
            static_cast<int>(crawling::ProfileDetectionMode::BaselineShoulder)).toInt();
        const int row = m_profileDetectionMode->findData(mode);
        m_profileDetectionMode->setCurrentIndex(row >= 0 ? row : 0);
    };
    const int tuningVersion = persistent.value(
        QStringLiteral("laserProfileTuning/version"), 0).toInt();
    if (tuningVersion < 3) {
        m_profileWeldTuning = crawling::ProfileWeldTuning{};
        persistent.setValue(QStringLiteral("laserProfileTuning/version"), 4);
        persistent.sync();
        if (m_profileDetectionLogCheck) {
            QSignalBlocker blocker(m_profileDetectionLogCheck);
            m_profileDetectionLogCheck->setChecked(persistent.value(
                QStringLiteral("laserProfileTuning/profileDetectionTraceEnabled"),
                false).toBool());
        }
        loadDetectionMode();
        return;
    }
    m_profileWeldTuning.smoothingRadius = persistent.value(
        QStringLiteral("laserProfileTuning/smoothingRadius"), 2).toInt();
    m_profileWeldTuning.baselineEdgeRatio = persistent.value(
        QStringLiteral("laserProfileTuning/baselineEdgeRatio"), 0.10).toDouble();
    m_profileWeldTuning.growNoiseSigma = persistent.value(
        QStringLiteral("laserProfileTuning/growNoiseSigma"), 2.0).toDouble();
    m_profileWeldTuning.seedNoiseSigma = persistent.value(
        QStringLiteral("laserProfileTuning/seedNoiseSigma"), 3.0).toDouble();
    m_profileWeldTuning.minimumWidthRatio = persistent.value(
        QStringLiteral("laserProfileTuning/minimumWidthRatio"), 0.005).toDouble();
    m_profileWeldTuning.minimumSeedCount = persistent.value(
        QStringLiteral("laserProfileTuning/minimumSeedCount"), 2).toInt();
    m_profileWeldTuning.minimumSupportRatio = persistent.value(
        QStringLiteral("laserProfileTuning/minimumSupportRatio"), 0.20).toDouble();
    m_profileWeldTuning.maximumCandidateHoleRatio = persistent.value(
        QStringLiteral("laserProfileTuning/maximumCandidateHoleRatio"), 0.12).toDouble();
    m_profileWeldTuning.maximumInternalHoleRatio = persistent.value(
        QStringLiteral("laserProfileTuning/maximumInternalHoleRatio"), 0.70).toDouble();
    m_profileWeldTuning.shoulderRatio = persistent.value(
        QStringLiteral("laserProfileTuning/shoulderRatio"), 0.002).toDouble();
    m_profileWeldTuning.maximumWidthRatio = persistent.value(
        QStringLiteral("laserProfileTuning/maximumWidthRatio"), 0.75).toDouble();
    if (m_profileDetectionLogCheck) {
        QSignalBlocker blocker(m_profileDetectionLogCheck);
        m_profileDetectionLogCheck->setChecked(persistent.value(
            QStringLiteral("laserProfileTuning/profileDetectionTraceEnabled"),
            false).toBool());
    }
    loadDetectionMode();
}

void RobotControlPanel::persistProfileWeldTuning()
{
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    persistent.setValue(QStringLiteral("laserProfileTuning/version"), 4);
    persistent.setValue(QStringLiteral("laserProfileTuning/smoothingRadius"),
                        m_profileWeldTuning.smoothingRadius);
    persistent.setValue(QStringLiteral("laserProfileTuning/baselineEdgeRatio"),
                        m_profileWeldTuning.baselineEdgeRatio);
    persistent.setValue(QStringLiteral("laserProfileTuning/growNoiseSigma"),
                        m_profileWeldTuning.growNoiseSigma);
    persistent.setValue(QStringLiteral("laserProfileTuning/seedNoiseSigma"),
                        m_profileWeldTuning.seedNoiseSigma);
    persistent.setValue(QStringLiteral("laserProfileTuning/minimumWidthRatio"),
                        m_profileWeldTuning.minimumWidthRatio);
    persistent.setValue(QStringLiteral("laserProfileTuning/minimumSeedCount"),
                        m_profileWeldTuning.minimumSeedCount);
    persistent.setValue(QStringLiteral("laserProfileTuning/minimumSupportRatio"),
                        m_profileWeldTuning.minimumSupportRatio);
    persistent.setValue(QStringLiteral("laserProfileTuning/maximumCandidateHoleRatio"),
                        m_profileWeldTuning.maximumCandidateHoleRatio);
    persistent.setValue(QStringLiteral("laserProfileTuning/maximumInternalHoleRatio"),
                        m_profileWeldTuning.maximumInternalHoleRatio);
    persistent.setValue(QStringLiteral("laserProfileTuning/shoulderRatio"),
                        m_profileWeldTuning.shoulderRatio);
    persistent.setValue(QStringLiteral("laserProfileTuning/maximumWidthRatio"),
                        m_profileWeldTuning.maximumWidthRatio);
    if (m_profileDetectionMode)
        persistent.setValue(QStringLiteral("laserProfileTuning/profileDetectionMode"),
                            m_profileDetectionMode->currentData().toInt());
    persistent.sync();
}

void RobotControlPanel::applyProfileWeldTuning()
{
    if (!m_correctionController)
        return;
    const crawling::ProfileWeldTuning tuning = m_profileWeldTuning;
    QMetaObject::invokeMethod(m_correctionController,
        [controller = m_correctionController, tuning] {
            controller->setProfileTuning(tuning);
        }, Qt::QueuedConnection);
}

void RobotControlPanel::loadProfileTemplates()
{
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    m_profileTemplates.clear();
    const int count = persistent.value(QStringLiteral("laserProfileTemplates/count"), 0)
                          .toInt();
    for (int index = 0; index < count; ++index) {
        const QString prefix = QStringLiteral("laserProfileTemplates/%1/").arg(index);
        crawling::ProfileWeldTemplate profileTemplate;
        profileTemplate.name = persistent.value(prefix + QStringLiteral("name")).toString();
        const QStringList encoded = persistent.value(prefix + QStringLiteral("shape")).toStringList();
        for (const QString &value : encoded)
            profileTemplate.normalizedShape.append(value.toFloat());
        profileTemplate.widthRatio = persistent.value(prefix + QStringLiteral("widthRatio"), 0.0)
                                         .toDouble();
        profileTemplate.peakRatio = persistent.value(prefix + QStringLiteral("peakRatio"), -1.0)
                                        .toDouble();
        if (!std::isfinite(profileTemplate.peakRatio) ||
            profileTemplate.peakRatio < 0.0 || profileTemplate.peakRatio > 1.0) {
            int peak = 0;
            for (int sample = 1; sample < profileTemplate.normalizedShape.size(); ++sample) {
                if (profileTemplate.normalizedShape[sample] >
                    profileTemplate.normalizedShape[peak])
                    peak = sample;
            }
            profileTemplate.peakRatio = profileTemplate.normalizedShape.isEmpty()
                ? 0.5 : double(peak) /
                    std::max(1, static_cast<int>(profileTemplate.normalizedShape.size()) - 1);
        }
        profileTemplate.minimumWidthScale = persistent.value(
            prefix + QStringLiteral("minimumWidthScale"), 0.20).toDouble();
        profileTemplate.maximumWidthScale = persistent.value(
            prefix + QStringLiteral("maximumWidthScale"), 5.00).toDouble();
        profileTemplate.minimumSimilarity = persistent.value(
            prefix + QStringLiteral("minimumSimilarity"), 0.85).toDouble();
        profileTemplate.enabled = persistent.value(prefix + QStringLiteral("enabled"), true)
                                      .toBool();
        if (profileTemplate.normalizedShape.size() >= 41 &&
            profileTemplate.widthRatio > 0.0 &&
            profileTemplate.maximumWidthScale >= profileTemplate.minimumWidthScale) {
            if (profileTemplate.name.trimmed().isEmpty())
                profileTemplate.name = QStringLiteral("焊道模板 %1").arg(index + 1);
            m_profileTemplates.append(profileTemplate);
        }
    }
}

void RobotControlPanel::persistProfileTemplates()
{
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    persistent.remove(QStringLiteral("laserProfileTemplates"));
    persistent.setValue(QStringLiteral("laserProfileTemplates/count"),
                        m_profileTemplates.size());
    for (int index = 0; index < m_profileTemplates.size(); ++index) {
        const crawling::ProfileWeldTemplate &profileTemplate = m_profileTemplates[index];
        const QString prefix = QStringLiteral("laserProfileTemplates/%1/").arg(index);
        QStringList encoded;
        encoded.reserve(profileTemplate.normalizedShape.size());
        for (float value : profileTemplate.normalizedShape)
            encoded.append(QString::number(value, 'g', 9));
        persistent.setValue(prefix + QStringLiteral("name"), profileTemplate.name);
        persistent.setValue(prefix + QStringLiteral("shape"), encoded);
        persistent.setValue(prefix + QStringLiteral("widthRatio"), profileTemplate.widthRatio);
        persistent.setValue(prefix + QStringLiteral("peakRatio"), profileTemplate.peakRatio);
        persistent.setValue(prefix + QStringLiteral("minimumWidthScale"),
                            profileTemplate.minimumWidthScale);
        persistent.setValue(prefix + QStringLiteral("maximumWidthScale"),
                            profileTemplate.maximumWidthScale);
        persistent.setValue(prefix + QStringLiteral("minimumSimilarity"),
                            profileTemplate.minimumSimilarity);
        persistent.setValue(prefix + QStringLiteral("enabled"), profileTemplate.enabled);
    }
    persistent.sync();
}

void RobotControlPanel::applyProfileTemplates()
{
    if (!m_correctionController)
        return;
    const QVector<crawling::ProfileWeldTemplate> templates = m_profileTemplates;
    QMetaObject::invokeMethod(m_correctionController,
        [controller = m_correctionController, templates] {
            controller->setProfileTemplates(templates);
        }, Qt::QueuedConnection);
}

void RobotControlPanel::showProfileTemplateDialog()
{
    if (m_profileTemplateDialog) {
        m_profileTemplateDialog->raise();
        m_profileTemplateDialog->activateWindow();
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    m_profileTemplateDialog = dialog;
    dialog->setWindowTitle(QStringLiteral("焊道轮廓模板"));
    dialog->setMinimumSize(700, 620);
    dialog->resize(760, 700);
    auto *layout = new QVBoxLayout(dialog);
    auto *description = new QLabel(
        QStringLiteral("先在 X/Z 图中按住鼠标拖出红色框选焊道区域，再采集模板。"
                       "框选采集不要求当前已经识别成功。所有数值参数均可用滑块调整。"), dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *selector = new QComboBox(dialog);
    layout->addWidget(selector);
    auto *form = new QFormLayout;
    const auto slider = [dialog](int minimum, int maximum, int initial,
                                 int decimals, double scale) {
        auto *container = new QWidget(dialog);
        auto *row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        auto *control = new QSlider(Qt::Horizontal, container);
        auto *value = new QLabel(container);
        value->setMinimumWidth(62);
        control->setRange(minimum, maximum);
        control->setValue(std::clamp(initial, minimum, maximum));
        const auto updateValue = [control, value, decimals, scale] {
            value->setText(QString::number(control->value() * scale, 'f', decimals));
        };
        QObject::connect(control, &QSlider::valueChanged, container,
                         [updateValue](int) { updateValue(); });
        updateValue();
        row->addWidget(control, 1);
        row->addWidget(value);
        return control;
    };
    auto *name = new QLineEdit(dialog);
    auto *minimumSimilarity = slider(30, 95, 85, 0, 1.0);
    auto *widthTolerance = slider(10, 90, 50, 0, 1.0);
    auto *smoothingRadius = slider(1, 5, m_profileWeldTuning.smoothingRadius, 0, 1.0);
    auto *sensitivity = slider(5, 60,
        int((6.5 - m_profileWeldTuning.growNoiseSigma) * 10), 1, 0.1);
    auto *minimumWidthRatio = slider(1, 100, int(m_profileWeldTuning.minimumWidthRatio * 10000), 2, 0.01);
    auto *candidateHoleRatio = slider(0, 25, int(m_profileWeldTuning.maximumCandidateHoleRatio * 100), 0, 1.0);
    auto *maximumWidthRatio = slider(10, 90,
        int(m_profileWeldTuning.maximumWidthRatio * 100), 0, 1.0);
    auto *enabled = new QCheckBox(QStringLiteral("匹配时启用此模板"), dialog);
    form->addRow(QStringLiteral("模板名称"), name);
    form->addRow(QStringLiteral("形状匹配严格度 (%)"), minimumSimilarity);
    form->addRow(QStringLiteral("焊道宽度可变化（±%）"), widthTolerance);
    form->addRow(QStringLiteral("去噪强度（越大越平滑）"), smoothingRadius);
    form->addRow(QStringLiteral("低矮拱起灵敏度（越大越敏感）"), sensitivity);
    form->addRow(QStringLiteral("最小拱起宽度 (%)"), minimumWidthRatio);
    form->addRow(QStringLiteral("允许连续缺测跨度 (%)"), candidateHoleRatio);
    form->addRow(QStringLiteral("最大拱起宽度 (%)"), maximumWidthRatio);
    auto *hint = new QLabel(
        QStringLiteral("识别原理：先估计焊道两侧母材高度，再找高于基线的拱起段；"
                       "窄尖峰、宽斜坡、台阶或不连续拱起都会在某个尺度上显著，取最大的异常凸起区域。"
                       "缺测跨度只用于跨过没有数据的区域；轮廓回到母材后分开识别。"
                       "模板匹配模式会用归一化相关校验形状；混合模式由通用检测提供候选、模板做精定位。"), dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    form->addRow(QString(), enabled);
    layout->addLayout(form);
    auto *selectionStatus = new QLabel(dialog);
    selectionStatus->setWordWrap(true);
    selectionStatus->setText(QStringLiteral("请在 X/Z 图上拖拽红框选择焊道区域。"));
    layout->addWidget(selectionStatus);
    auto *status = new QLabel(dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QHBoxLayout;
    auto *capture = actionButton(QStringLiteral("采集红框区域为模板"), "primary");
    auto *update = actionButton(QStringLiteral("更新匹配参数"));
    auto *reset = actionButton(QStringLiteral("恢复拱起默认值"));
    auto *remove = actionButton(QStringLiteral("删除选中模板"));
    buttons->addWidget(capture);
    buttons->addWidget(update);
    buttons->addWidget(reset);
    buttons->addWidget(remove);
    layout->addLayout(buttons);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    close->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    layout->addWidget(close);

    const auto applyTuning = [this, smoothingRadius, sensitivity,
                              minimumWidthRatio, candidateHoleRatio,
                              maximumWidthRatio] {
        m_profileWeldTuning.smoothingRadius = smoothingRadius->value();
        m_profileWeldTuning.growNoiseSigma = 6.5 - sensitivity->value() / 10.0;
        m_profileWeldTuning.seedNoiseSigma = m_profileWeldTuning.growNoiseSigma * 1.5;
        m_profileWeldTuning.minimumWidthRatio = minimumWidthRatio->value() / 10000.0;
        m_profileWeldTuning.maximumCandidateHoleRatio = candidateHoleRatio->value() / 100.0;
        m_profileWeldTuning.maximumWidthRatio = maximumWidthRatio->value() / 100.0;
        m_profileWeldTuning.baselineEdgeRatio = 0.10;
        m_profileWeldTuning.minimumSeedCount = 2;
        m_profileWeldTuning.minimumSupportRatio = 0.20;
        m_profileWeldTuning.maximumInternalHoleRatio = 0.70;
        m_profileWeldTuning.shoulderRatio = 0.002;
        persistProfileWeldTuning();
        applyProfileWeldTuning();
    };
    const auto fillTemplate = [this, selector, name, minimumSimilarity,
                               widthTolerance, smoothingRadius, sensitivity,
                               minimumWidthRatio, candidateHoleRatio,
                               maximumWidthRatio, enabled, status] {
        const int index = selector->currentData().toInt();
        smoothingRadius->setValue(m_profileWeldTuning.smoothingRadius);
        sensitivity->setValue(int((6.5 - m_profileWeldTuning.growNoiseSigma) * 10));
        minimumWidthRatio->setValue(int(m_profileWeldTuning.minimumWidthRatio * 10000));
        candidateHoleRatio->setValue(int(m_profileWeldTuning.maximumCandidateHoleRatio * 100));
        maximumWidthRatio->setValue(int(m_profileWeldTuning.maximumWidthRatio * 100));
        if (index < 0 || index >= m_profileTemplates.size()) {
            name->setText(QStringLiteral("焊道模板 %1").arg(m_profileTemplates.size() + 1));
            minimumSimilarity->setValue(85);
            widthTolerance->setValue(50);
            enabled->setChecked(true);
            status->setText(QStringLiteral("尚未建立模板。先在主界面的 X/Z 轮廓上拖拽红框，再采集。"));
            return;
        }
        const crawling::ProfileWeldTemplate &profileTemplate = m_profileTemplates[index];
        name->setText(profileTemplate.name);
        minimumSimilarity->setValue(int(profileTemplate.minimumSimilarity * 100));
        widthTolerance->setValue(std::clamp(
            int(std::max(1.0 - profileTemplate.minimumWidthScale,
                         profileTemplate.maximumWidthScale - 1.0) * 100), 10, 90));
        enabled->setChecked(profileTemplate.enabled);
        status->setText(QStringLiteral("特征点 %1；基准宽度占扫描 %2；启用=%3。"
                                       "纯模板模式要求达到相似度阈值，混合模式用于候选确认。")
                            .arg(profileTemplate.normalizedShape.size())
                            .arg(profileTemplate.widthRatio, 0, 'f', 3)
                            .arg(profileTemplate.enabled ? QStringLiteral("是")
                                                         : QStringLiteral("否")));
    };
    for (QSlider *control : {smoothingRadius, sensitivity, minimumWidthRatio,
                             candidateHoleRatio, maximumWidthRatio}) {
        connect(control, &QSlider::valueChanged, dialog,
                [applyTuning](int) { applyTuning(); });
    }
    connect(reset, &QPushButton::clicked, dialog,
            [minimumSimilarity, widthTolerance, smoothingRadius, sensitivity,
             minimumWidthRatio, candidateHoleRatio, maximumWidthRatio,
             enabled, applyTuning] {
                minimumSimilarity->setValue(85);
                widthTolerance->setValue(50);
                smoothingRadius->setValue(2);
                sensitivity->setValue(45);
                minimumWidthRatio->setValue(50);
                candidateHoleRatio->setValue(12);
                maximumWidthRatio->setValue(75);
                enabled->setChecked(true);
                applyTuning();
            });
    const auto refresh = [this, selector, fillTemplate] {
        const int selected = selector->currentData().toInt();
        QSignalBlocker blocker(selector);
        selector->clear();
        for (int index = 0; index < m_profileTemplates.size(); ++index) {
            const crawling::ProfileWeldTemplate &profileTemplate = m_profileTemplates[index];
            selector->addItem(QStringLiteral("%1%2")
                                  .arg(profileTemplate.enabled ? QStringLiteral("● ")
                                                               : QStringLiteral("○ "))
                                  .arg(profileTemplate.name), index);
        }
        const int row = selector->findData(selected);
        selector->setCurrentIndex(row >= 0 ? row : (selector->count() ? 0 : -1));
        fillTemplate();
    };
    connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            dialog, [fillTemplate](int) { fillTemplate(); });
    connect(capture, &QPushButton::clicked, dialog,
            [this, name, minimumSimilarity, widthTolerance,
             enabled, applyTuning, refresh, status] {
                if (!m_laserView || !m_laserView->hasTemplateSelection()) {
                    QMessageBox::warning(this, QStringLiteral("请先框选焊道"),
                        QStringLiteral("请保持模板窗口打开，并在主界面 X/Z 轮廓图上拖动鼠标框选焊道区域。"));
                    return;
                }
                crawling::ProfileWeldTemplate profileTemplate =
                    crawling::ProfileWeldDetector::captureTemplate(
                        m_laserView->rawPoints(), m_laserView->templateSelectionMinimumX(),
                        m_laserView->templateSelectionMaximumX(),
                        name->text().trimmed());
                if (profileTemplate.normalizedShape.size() < 41 ||
                    profileTemplate.widthRatio <= 0.0) {
                    QMessageBox::warning(this, QStringLiteral("无法建立模板"),
                        QStringLiteral("红框内没有足够的有效轮廓点。请扩大红框，"
                                       "并保证焊道两侧保留可见的母材轮廓。"));
                    return;
                }
                profileTemplate.name = profileTemplate.name.isEmpty()
                    ? QStringLiteral("焊道模板 %1").arg(m_profileTemplates.size() + 1)
                    : profileTemplate.name;
                profileTemplate.minimumSimilarity = minimumSimilarity->value() / 100.0;
                profileTemplate.minimumWidthScale =
                    1.0 - widthTolerance->value() / 100.0;
                profileTemplate.maximumWidthScale =
                    1.0 + widthTolerance->value() / 100.0;
                profileTemplate.enabled = enabled->isChecked();
                m_profileTemplates.append(profileTemplate);
                applyTuning();
                persistProfileTemplates();
                applyProfileTemplates();
                appendLog(QStringLiteral("已建立焊道模板：%1（相似度 ≥ %2）")
                              .arg(profileTemplate.name)
                              .arg(profileTemplate.minimumSimilarity, 0, 'f', 2));
                refresh();
                m_laserView->clearTemplateSelection();
                status->setText(QStringLiteral("已完成：模板“%1”已采集并启用。可继续调整参数，或关闭窗口。")
                                    .arg(profileTemplate.name));
            });
    connect(update, &QPushButton::clicked, dialog,
            [this, selector, name, minimumSimilarity, widthTolerance,
             enabled, applyTuning, refresh, status] {
                const int index = selector->currentData().toInt();
                if (index < 0 || index >= m_profileTemplates.size()) {
                    status->setText(QStringLiteral("没有可更新的模板，请先采集一个模板。"));
                    return;
                }
                crawling::ProfileWeldTemplate &profileTemplate = m_profileTemplates[index];
                profileTemplate.name = name->text().trimmed().isEmpty()
                    ? QStringLiteral("焊道模板 %1").arg(index + 1) : name->text().trimmed();
                profileTemplate.minimumSimilarity = minimumSimilarity->value() / 100.0;
                profileTemplate.minimumWidthScale =
                    1.0 - widthTolerance->value() / 100.0;
                profileTemplate.maximumWidthScale =
                    1.0 + widthTolerance->value() / 100.0;
                profileTemplate.enabled = enabled->isChecked();
                applyTuning();
                persistProfileTemplates();
                applyProfileTemplates();
                appendLog(QStringLiteral("已更新焊道模板：%1").arg(profileTemplate.name));
                refresh();
                status->setText(QStringLiteral("已完成：模板“%1”的匹配参数已更新并立即生效。")
                                    .arg(profileTemplate.name));
            });
    connect(remove, &QPushButton::clicked, dialog, [this, selector, refresh] {
        const int index = selector->currentData().toInt();
        if (index < 0 || index >= m_profileTemplates.size()) return;
        const QString name = m_profileTemplates[index].name;
        m_profileTemplates.removeAt(index);
        persistProfileTemplates();
        applyProfileTemplates();
        appendLog(QStringLiteral("已删除焊道模板：%1").arg(name));
        refresh();
    });
    if (m_laserView) {
        m_laserView->setTemplateSelectionEnabled(true);
        connect(m_laserView, &RobotProfileView::templateSelectionChanged, dialog,
                [selectionStatus](bool available, double minimumX, double maximumX) {
                    selectionStatus->setText(available
                        ? QStringLiteral("已框选 X=%1 至 %2；可点击“采集红框区域为模板”。")
                              .arg(minimumX, 0, 'f', 2).arg(maximumX, 0, 'f', 2)
                        : QStringLiteral("红框范围过窄，请重新拖拽。"));
                });
    }
    connect(close, &QDialogButtonBox::rejected, dialog,
            [dialog] { dialog->close(); });
    connect(dialog, &QObject::destroyed, this, [this] {
        m_profileTemplateDialog = nullptr;
        if (m_laserView) m_laserView->setTemplateSelectionEnabled(false);
    });
    refresh();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void RobotControlPanel::saveSettings()
{
    const crawling::DriveSettings current = settingsFromUi();
    const QString error = current.validationError();
    if (!error.isEmpty()) {
        appendLog(error);
        return;
    }
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    current.save(persistent);
    persistent.setValue(QStringLiteral("laserCorrection/speed"),
                        m_correctionSpeed->value() / kMillimetersPerMeter);
    persistent.setValue(QStringLiteral("laserCorrection/segmentLength"),
                        m_correctionSegment->value() / kMillimetersPerMeter);
    persistent.setValue(QStringLiteral("laserCorrection/kp"),
                        m_correctionKp->value());
    persistent.setValue(QStringLiteral("laserCorrection/kd"),
                        m_correctionKd->value());
    persistent.setValue(QStringLiteral("laserCorrection/imageTimeoutMs"),
                        m_laserInputTimeout->value());
    persistent.sync();
    if (persistent.status() != QSettings::NoError) {
        appendLog(QStringLiteral("小车配置保存失败"));
        return;
    }
    m_settings = current;
    m_clampController->setSettings(m_settings);
    m_clampControls->setEnabled(true);
    appendLog(QStringLiteral("小车配置已保存"));
}

void RobotControlPanel::restoreSettings()
{
    loadSettings();
    if (m_correctionController && m_profileDetectionMode) {
        const auto mode = static_cast<crawling::ProfileDetectionMode>(
            m_profileDetectionMode->currentData().toInt());
        QMetaObject::invokeMethod(
            m_correctionController,
            [controller = m_correctionController, mode] {
                controller->setProfileDetectionMode(mode);
            }, Qt::QueuedConnection);
    }
    applyProfileWeldTuning();
    applyProfileTemplates();
    appendLog(QStringLiteral("已恢复保存的小车配置"));
}

crawling::DriveSettings RobotControlPanel::settingsFromUi() const
{
    crawling::DriveSettings value = m_settings;
    const auto comboText = [](const QComboBox *box, const QString &fallback) {
        const QString result = box ? box->currentData().toString().trimmed() : QString();
        return result.isEmpty() ? fallback : result;
    };
    const auto comboInt = [](const QComboBox *box, int fallback) {
        return box && box->currentIndex() >= 0 ? box->currentData().toInt() : fallback;
    };
    value.leftMotorSerialPort = comboText(m_leftPort, value.leftMotorSerialPort);
    value.rightMotorSerialPort = comboText(m_rightPort, value.rightMotorSerialPort);
    value.leftMotorBaudRate = comboInt(m_leftBaud, value.leftMotorBaudRate);
    value.rightMotorBaudRate = comboInt(m_rightBaud, value.rightMotorBaudRate);
    value.leftMotorId = m_leftId->value();
    value.rightMotorId = m_rightId->value();
    value.leftMotorSign = comboInt(m_leftSign, value.leftMotorSign);
    value.rightMotorSign = comboInt(m_rightSign, value.rightMotorSign);
    value.wheelCommunicationMode =
        m_wheelCommunicationMode->currentData().toString() == QStringLiteral("rs485")
            ? crawling::WheelCommunicationMode::Rs485
            : crawling::WheelCommunicationMode::Can;
    value.wheelCanSerialPort = comboText(m_wheelCanPort, value.wheelCanSerialPort);
    value.wheelCanSerialBaudRate = comboInt(m_wheelCanBaud, value.wheelCanSerialBaudRate);
    value.wheelCanBitrate = comboInt(m_wheelCanBitrate, value.wheelCanBitrate);
    value.imuSerialPort = comboText(m_imuPort, value.imuSerialPort);
    value.imuBaudRate = comboInt(m_imuBaud, value.imuBaudRate);
    value.imuOutputDivider = comboInt(m_imuDivider, value.imuOutputDivider);
    // The serial number is selected from the discovery list, never typed by hand.
    value.laserSerialNumber = m_laserSerial->text().trimmed();
        for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        const auto& ui = m_usbCameraUi[slot];
        auto& cam = value.usbCameras[slot];
        if (ui.source && ui.source->currentIndex() >= 0)
            cam.cameraSource = static_cast<crawling::CameraSource>(ui.source->currentData().toInt());
        cam.usbCameraDeviceIndex = (ui.device && ui.device->currentIndex() >= 0)
                                       ? ui.device->currentData().toInt() : -1;
        cam.networkCameraUrl = ui.networkUrl ? ui.networkUrl->text().trimmed() : QString();
        cam.usbCameraFps = ui.fps ? ui.fps->value() : 30;
        cam.usbCameraAutoConnect = ui.autoConnect && ui.autoConnect->isChecked();
        cam.usbCameraFlipHorizontal = ui.flipHorizontal && ui.flipHorizontal->isChecked();
        cam.usbCameraFlipVertical = ui.flipVertical && ui.flipVertical->isChecked();
    }
    value.autoConnectOnStartup = m_autoConnectCheck->isChecked();
    value.manualJogPercent = m_speedSlider->value();
    value.clampMotors = m_clampSettings->settings();
    for (int axis = 0; axis < 3; ++axis) value.clampMotorSyncEnabled[axis] = m_clampSettings->syncEnabled(axis);
    value.wheelRadiusM = m_wheelRadius->value() / kMillimetersPerMeter;
    value.trackWidthM = m_trackWidth->value() / kMillimetersPerMeter;
    value.motorOutputToWheelRatio = m_ratio->value();
    value.maximumWheelSpeedMps = m_maxWheelSpeed->value() / kMillimetersPerMeter;
    value.maximumLinearSpeedMps = m_maxLinearSpeed->value() / kMillimetersPerMeter;
    value.maximumAngularSpeedRadps = m_maxAngularSpeed->value() * kRadiansPerDegree;
    value.maximumLinearAccelerationMps2 = m_maxLinearAcceleration->value() / kMillimetersPerMeter;
    value.maximumAngularAccelerationRadps2 = m_maxAngularAcceleration->value() * kRadiansPerDegree;
    value.minimumInnerWheelRatio = m_minimumInnerRatio->value();
    value.commandTimeoutMs = m_commandTimeout->value();
    value.feedbackTimeoutMs = m_feedbackTimeout->value();
    value.armingTimeoutMs = m_armingTimeout->value();
    value.synchronizer.proportionalGain = m_syncP->value();
    value.synchronizer.integralGain = m_syncI->value();
    value.synchronizer.maximumCorrectionMps = m_syncMaxCorrection->value() / kMillimetersPerMeter;
    value.synchronizer.minimumControlledSpeedMps = m_syncMinSpeed->value() / kMillimetersPerMeter;
    return value;
}

void RobotControlPanel::settingsToUi(const crawling::DriveSettings &settings)
{
    setComboText(m_leftPort, settings.leftMotorSerialPort);
    setComboValue(m_leftBaud, settings.leftMotorBaudRate);
    setComboText(m_rightPort, settings.rightMotorSerialPort);
    setComboValue(m_rightBaud, settings.rightMotorBaudRate);
    setComboValue(m_leftSign, settings.leftMotorSign);
    setComboValue(m_rightSign, settings.rightMotorSign);
    m_leftId->setValue(settings.leftMotorId);
    m_rightId->setValue(settings.rightMotorId);
    setComboValue(m_wheelCommunicationMode,
                  settings.wheelCommunicationMode == crawling::WheelCommunicationMode::Can
                      ? QStringLiteral("can")
                      : QStringLiteral("rs485"));
    setComboText(m_wheelCanPort, settings.wheelCanSerialPort);
    setComboValue(m_wheelCanBaud, settings.wheelCanSerialBaudRate);
    setComboValue(m_wheelCanBitrate, settings.wheelCanBitrate);
    if (m_wheelCommunicationMode != nullptr) {
        const bool can = settings.wheelCommunicationMode ==
                         crawling::WheelCommunicationMode::Can;
        for (QWidget *widget : m_wheelCanRows)
            widget->setVisible(can);
        for (QWidget *widget : m_wheelRs485Rows)
            widget->setVisible(!can);
    }
    setComboText(m_imuPort, settings.imuSerialPort);
    setComboValue(m_imuBaud, settings.imuBaudRate);
    setComboValue(m_imuDivider, settings.imuOutputDivider);
    m_laserSerial->setText(settings.laserSerialNumber);
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        const auto& ui = m_usbCameraUi[slot];
        const auto& cam = settings.usbCameras[slot];
        if (ui.source) {
            const QSignalBlocker blocker(ui.source);
            const int index = ui.source->findData(static_cast<int>(cam.cameraSource));
            ui.source->setCurrentIndex(index >= 0 ? index : 0);
        }
        if (ui.device) {
            const QSignalBlocker blocker(ui.device);
            const int index = ui.device->findData(cam.usbCameraDeviceIndex);
            ui.device->setCurrentIndex(index >= 0 ? index : -1);
        }
        if (ui.networkUrl) ui.networkUrl->setText(cam.networkCameraUrl);
        if (ui.fps) ui.fps->setValue(cam.usbCameraFps);
        if (ui.autoConnect) ui.autoConnect->setChecked(cam.usbCameraAutoConnect);
        if (ui.flipHorizontal) ui.flipHorizontal->setChecked(cam.usbCameraFlipHorizontal);
        if (ui.flipVertical) ui.flipVertical->setChecked(cam.usbCameraFlipVertical);
        updateUsbCameraSourceUi(slot);
    }
    m_autoConnectCheck->setChecked(settings.autoConnectOnStartup);
    m_speedSlider->setValue(settings.manualJogPercent);
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    m_correctionSpeed->setValue(
        persistent.value(QStringLiteral("laserCorrection/speed"), 0.005)
            .toDouble() * kMillimetersPerMeter);
    m_correctionSegment->setValue(
        persistent.value(QStringLiteral("laserCorrection/segmentLength"), 0.03)
            .toDouble() * kMillimetersPerMeter);
    m_correctionKp->setValue(
        persistent.value(QStringLiteral("laserCorrection/kp"), 3.0).toDouble());
    m_correctionKd->setValue(
        persistent.value(QStringLiteral("laserCorrection/kd"), 0.12).toDouble());
    m_laserInputTimeout->setValue(
        persistent.value(QStringLiteral("laserCorrection/imageTimeoutMs"), 500)
            .toInt());
    m_clampSettings->setSettings(settings.clampMotors);
    for (int axis = 0; axis < 3; ++axis) m_clampSettings->setSyncEnabled(axis, settings.clampMotorSyncEnabled[axis]);
    m_wheelRadius->setValue(settings.wheelRadiusM * kMillimetersPerMeter);
    m_trackWidth->setValue(settings.trackWidthM * kMillimetersPerMeter);
    m_ratio->setValue(settings.motorOutputToWheelRatio);
    m_maxWheelSpeed->setValue(settings.maximumWheelSpeedMps * kMillimetersPerMeter);
    m_maxLinearSpeed->setValue(settings.maximumLinearSpeedMps * kMillimetersPerMeter);
    m_maxAngularSpeed->setValue(settings.maximumAngularSpeedRadps * kDegreesPerRadian);
    m_maxLinearAcceleration->setValue(settings.maximumLinearAccelerationMps2 * kMillimetersPerMeter);
    m_maxAngularAcceleration->setValue(settings.maximumAngularAccelerationRadps2 * kDegreesPerRadian);
    m_minimumInnerRatio->setValue(settings.minimumInnerWheelRatio);
    m_commandTimeout->setValue(settings.commandTimeoutMs);
    m_feedbackTimeout->setValue(settings.feedbackTimeoutMs);
    m_armingTimeout->setValue(settings.armingTimeoutMs);
    m_syncP->setValue(settings.synchronizer.proportionalGain);
    m_syncI->setValue(settings.synchronizer.integralGain);
    m_syncMaxCorrection->setValue(settings.synchronizer.maximumCorrectionMps * kMillimetersPerMeter);
    m_syncMinSpeed->setValue(settings.synchronizer.minimumControlledSpeedMps * kMillimetersPerMeter);
    m_speedLabel->setText(QStringLiteral("%1%").arg(settings.manualJogPercent));
}

void RobotControlPanel::refreshPorts()
{
    const auto ports = QSerialPortInfo::availablePorts();
    const auto repopulate = [&ports](QComboBox *box, const QString &selected,
                                     const QString &configured) {
        QSignalBlocker blocker(box);
        box->clear();
        for (const QSerialPortInfo &port : ports) {
            const QString description = port.description().isEmpty()
                                            ? QString()
                                            : QStringLiteral(" (%1)").arg(port.description());
            box->addItem(port.portName() + description, port.portName());
        }
        const QString desired = selected.isEmpty() ? configured : selected;
        if (!desired.isEmpty())
            setComboText(box, desired);
        else
            box->setCurrentIndex(-1);
    };
    const QString selectedLeft = m_leftPort->currentData().toString();
    const QString selectedRight = m_rightPort->currentData().toString();
    const QString selectedWheelCan = m_wheelCanPort->currentData().toString();
    const QString selectedImu = m_imuPort->currentData().toString();
    repopulate(m_leftPort, selectedLeft, m_settings.leftMotorSerialPort);
    repopulate(m_rightPort, selectedRight, m_settings.rightMotorSerialPort);
    repopulate(m_wheelCanPort, selectedWheelCan, m_settings.wheelCanSerialPort);
    repopulate(m_imuPort, selectedImu, m_settings.imuSerialPort);
    m_clampSettings->refreshPorts();
}

void RobotControlPanel::connectDrive()
{
    const auto settings = settingsFromUi();
    const QString error = settings.validationError();
    if (!error.isEmpty()) {
        appendLog(error);
        QMessageBox::warning(this, QStringLiteral("无法连接底盘"), error);
        return;
    }
    m_settings = settings;
    saveSettings();
    requestEnable(false);
    if (m_controller)
        QMetaObject::invokeMethod(m_controller, "connectAdapter",
                                  Qt::QueuedConnection,
                                  Q_ARG(crawling::DriveSettings, m_settings));
}

void RobotControlPanel::disconnectDrive()
{
    requestEnable(false);
    if (m_controller)
        QMetaObject::invokeMethod(m_controller, "disconnectAdapter",
                                  Qt::QueuedConnection);
}

void RobotControlPanel::connectAllConfiguredDevices()
{
    connectConfiguredDevices();
}

void RobotControlPanel::connectConfiguredDevices()
{
    m_settings = settingsFromUi();
    saveSettings();
    const bool canMode =
        m_settings.wheelCommunicationMode == crawling::WheelCommunicationMode::Can;
    if ((canMode && m_settings.wheelCanSerialPort.trimmed().isEmpty()) ||
        (!canMode &&
         (m_settings.leftMotorSerialPort.trimmed().isEmpty() ||
          m_settings.rightMotorSerialPort.trimmed().isEmpty()))) {
        appendLog(QStringLiteral("底盘连接已跳过：驱动轮通信端口配置不完整"));
    } else {
        connectDrive();
    }
    connectConfiguredImu();
    connectConfiguredLaser();
    connectConfiguredUsbCamera();
    connectConfiguredClampMotor();
}

void RobotControlPanel::connectConfiguredImu()
{
    m_settings = settingsFromUi();
    saveSettings();
    if (m_settings.imuSerialPort.trimmed().isEmpty()) {
        appendLog(QStringLiteral("IMU 连接已跳过：未配置串口"));
        return;
    }
    if (m_sensorController)
        QMetaObject::invokeMethod(m_sensorController, "connectImu",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, m_settings.imuSerialPort),
                                  Q_ARG(int, m_settings.imuBaudRate),
                                  Q_ARG(int, m_settings.imuOutputDivider));
}

void RobotControlPanel::connectConfiguredLaser()
{
    m_settings = settingsFromUi();
    QString serial = m_settings.laserSerialNumber.trimmed();
    if (serial.isEmpty() && m_laserDevice->currentIndex() >= 0) {
        serial = m_laserDevice->currentText()
                     .section(QStringLiteral(" | "), 0, 0)
                     .trimmed();
        if (!serial.isEmpty()) {
            m_settings.laserSerialNumber = serial;
            m_laserSerial->setText(serial);
        }
    }
    saveSettings();
    if (serial.isEmpty()) {
        appendLog(QStringLiteral("未填写激光序列号，将自动连接扫描到的第一台设备"));
    }
    if (m_sensorController)
        QMetaObject::invokeMethod(m_sensorController, "connectCamera",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, serial));
}

void RobotControlPanel::connectConfiguredUsbCamera()
{
    m_settings = settingsFromUi();
    saveSettings();
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        connectConfiguredUsbCameraSlot(slot);
    }
}

void RobotControlPanel::connectConfiguredUsbCameraSlot(int slot)
{
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount)
        return;
    m_settings = settingsFromUi();
    saveSettings();
    const auto& cam = m_settings.usbCameras[slot];
    if (!cam.usbCameraAutoConnect) {
        appendLog(QStringLiteral("USB 摄像头 %1 连接已跳过：未勾选自动连接").arg(slot + 1));
        return;
    }
    if (cam.cameraSource == crawling::CameraSource::Network) {
        if (cam.networkCameraUrl.trimmed().isEmpty()) {
            appendLog(QStringLiteral("摄像头 %1 连接已跳过：已选择网络，但地址为空").arg(slot + 1));
            return;
        }
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "connectNetworkCamera",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, slot),
                                      Q_ARG(QString, cam.networkCameraUrl),
                                      Q_ARG(bool, cam.usbCameraFlipHorizontal),
                                      Q_ARG(bool, cam.usbCameraFlipVertical));
        pushUsbDisplaySize(slot);
        return;
    }
    if (cam.usbCameraDeviceIndex < 0) {
        appendLog(QStringLiteral("摄像头 %1 连接已跳过：已选择 USB，但未配置设备，请先扫描并选择").arg(slot + 1));
        return;
    }
    if (m_usbCameraController)
        QMetaObject::invokeMethod(m_usbCameraController, "connectCamera",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, slot),
                                  Q_ARG(int, cam.usbCameraDeviceIndex),
                                  Q_ARG(int, cam.usbCameraFps),
                                  Q_ARG(bool, cam.usbCameraFlipHorizontal),
                                  Q_ARG(bool, cam.usbCameraFlipVertical));
    pushUsbDisplaySize(slot);
}

bool RobotControlPanel::saveClampSettings()
{
    crawling::DriveSettings current = m_settings;
    current.clampMotors = m_clampSettings->settings();
    for (int axis = 0; axis < 3; ++axis) current.clampMotorSyncEnabled[axis] = m_clampSettings->syncEnabled(axis);
    const QString error = current.clampValidationError();
    if (!error.isEmpty()) {
        m_clampConfigState->setText(error);
        appendLog(error);
        return false;
    }
    QSettings persistent(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    auto latest = crawling::DriveSettings::load(persistent);
    latest.clampMotors = current.clampMotors;
    latest.clampMotorSyncEnabled = current.clampMotorSyncEnabled;
    latest.save(persistent);
    persistent.sync();
    if (persistent.status() != QSettings::NoError) {
        appendLog(QStringLiteral("夹子配置保存失败"));
        return false;
    }
    m_settings.clampMotors = current.clampMotors;
    m_settings.clampMotorSyncEnabled = current.clampMotorSyncEnabled;
    m_clampController->setSettings(current);
    m_clampControls->setEnabled(true);
    m_clampConfigState->setText(QStringLiteral("夹子配置已保存"));
    appendLog(QStringLiteral("夹子配置已保存"));
    return true;
}

void RobotControlPanel::connectConfiguredClampMotor()
{
    if (!saveClampSettings())
        return;
    QMetaObject::invokeMethod(m_clampController, "connectDevice", Qt::QueuedConnection);
}

void RobotControlPanel::disconnectAllDevices()
{
    disconnectDrive();
    if (m_clampController)
        QMetaObject::invokeMethod(m_clampController, "disconnectDevice",
                                  Qt::QueuedConnection);
    if (m_sensorController) {
        QMetaObject::invokeMethod(m_sensorController, "disconnectImu",
                                  Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_sensorController, "disconnectCamera",
                                  Qt::QueuedConnection);
    }
    if (m_usbCameraController) {
        for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot)
            QMetaObject::invokeMethod(m_usbCameraController, "disconnectCamera",
                                      Qt::QueuedConnection, Q_ARG(int, slot));
    }
    appendLog(QStringLiteral("已请求断开底盘、夹子电机、IMU、激光相机和 USB 摄像头"));
}

void RobotControlPanel::persistAutoConnectSetting(bool enabled)
{
    m_settings.autoConnectOnStartup = enabled;
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    m_settings.save(persistent);
    persistent.sync();
}

void RobotControlPanel::setMotion(bool &flag, bool active)
{
    if (m_autoCorrectionActive)
        return;
    flag = active;
    sendMotionCommand();
}

void RobotControlPanel::clearJogLatch()
{
    if (m_jogHoldTimer)
        m_jogHoldTimer->stop();
    m_jogLatched = false;
    if (m_latchedJogButton) {
        m_latchedJogButton->setProperty("latched", false);
        m_latchedJogButton->style()->unpolish(m_latchedJogButton);
        m_latchedJogButton->style()->polish(m_latchedJogButton);
        m_latchedJogButton->setToolTip(QStringLiteral("按住3秒可锁定该方向"));
    }
    bool *latchedFlag = m_latchedJogFlag;
    m_latchedJogButton = nullptr;
    m_latchedJogFlag = nullptr;
    if (latchedFlag && *latchedFlag) {
        *latchedFlag = false;
        sendMotionCommand();
    }
}

void RobotControlPanel::requestEnable(bool enabled)
{
    if (m_controller)
        QMetaObject::invokeMethod(m_controller, "requestEnable",
                                  Qt::QueuedConnection, Q_ARG(bool, enabled));
}

void RobotControlPanel::emergencyStop()
{
    crawling::AppLogger::write(
        QStringLiteral("CORRECTION.STOP"),
        QStringLiteral("event=correction_stop_requested source=emergency_stop active=%1 pending=%2")
            .arg(m_autoCorrectionActive ? 1 : 0)
            .arg(m_autoCorrectionStartPending ? 1 : 0));
    stopAutoCorrection();
    clearJogLatch();
    m_forward = m_reverse = m_left = m_right = false;
    for (QSlider *slider : findChildren<QSlider *>(QStringLiteral("clampMotorSpeed"))) {
        const QSignalBlocker blocker(slider);
        slider->setValue(0);
    }
    if (m_clampController)
        m_clampController->stop();
    if (m_controller)
        QMetaObject::invokeMethod(m_controller, "emergencyStop",
                                  Qt::QueuedConnection);
    appendLog(QStringLiteral("已紧急停止，底盘进入非使能状态；重新点击“底盘使能”后可继续控制"));
}

void RobotControlPanel::stopMotion()
{
    crawling::AppLogger::write(
        QStringLiteral("CORRECTION.STOP"),
        QStringLiteral("event=correction_pause_toggle source=pause_button active=%1 pending=%2 paused=%3")
            .arg(m_autoCorrectionActive ? 1 : 0)
            .arg(m_autoCorrectionStartPending ? 1 : 0)
            .arg(m_autoCorrectionPaused ? 1 : 0));
    setAutoCorrectionPaused(!m_autoCorrectionPaused);
}

void RobotControlPanel::setAutoCorrectionPaused(bool paused)
{
    const bool changed = m_autoCorrectionPaused != paused;
    m_autoCorrectionPaused = paused;
    if (m_stopMotionButton) {
        m_stopMotionButton->setText(paused ? QStringLiteral("继续")
                                           : QStringLiteral("暂停"));
        m_stopMotionButton->setEnabled(m_autoCorrectionActive ||
                                       m_autoCorrectionStartPending);
    }
    if (!changed)
        return;
    if (m_autoCorrectionActive && m_correctionController) {
        QMetaObject::invokeMethod(
            m_correctionController, "setPaused", Qt::QueuedConnection,
            Q_ARG(bool, paused));
    }
    emit autoCorrectionPausedChanged(paused);
}

void RobotControlPanel::sendMotionCommand()
{
    if (!m_controller || m_autoCorrectionActive)
        return;
    const auto settings = settingsFromUi();
    const double scale = m_speedSlider->value() / 100.0;
    const int forward = m_forward ? 1 : 0;
    const int reverse = m_reverse ? 1 : 0;
    const int left = m_left ? 1 : 0;
    const int right = m_right ? 1 : 0;
    const double synchronizedLimit =
        crawling::WheelMotorConfig::kMaximumSynchronizedMotorSpeedDps *
        settings.wheelRadiusM * kRadiansPerDegree /
        settings.motorOutputToWheelRatio;
    const double linearLimit = std::min(settings.maximumLinearSpeedMps, synchronizedLimit);
    const double angularLimit = std::min(settings.maximumAngularSpeedRadps,
                                         2.0 * synchronizedLimit / settings.trackWidthM);
    const double linear = (forward - reverse) * linearLimit * scale;
    const double angular = (right - left) * angularLimit * scale;
    QMetaObject::invokeMethod(m_controller, "setInputCommand",
                              Qt::QueuedConnection, Q_ARG(double, linear),
                              Q_ARG(double, angular));
}

void RobotControlPanel::updateTelemetry(const crawling::DriveTelemetry &telemetry)
{
    if (m_telemetryUiClock.isValid() && m_telemetryUiClock.elapsed() < 100)
        return;
    m_telemetryUiClock.restart();
    m_targetText = QStringLiteral("线速度 %1 mm/s，角速度 %2 deg/s")
                       .arg(telemetry.targetLinearMps * kMillimetersPerMeter, 0, 'f', 1)
                       .arg(telemetry.targetAngularRadps * kDegreesPerRadian, 0, 'f', 1);
    m_appliedText = QStringLiteral("线速度 %1 mm/s，角速度 %2 deg/s")
                        .arg(telemetry.appliedLinearMps * kMillimetersPerMeter, 0, 'f', 1)
                        .arg(telemetry.appliedAngularRadps * kDegreesPerRadian, 0, 'f', 1);
    m_leftWheelText = QStringLiteral("目标 %1，实际 %2 mm/s")
                          .arg(telemetry.leftTargetMps * kMillimetersPerMeter, 0, 'f', 1)
                          .arg(telemetry.left.wheelSpeedMps * kMillimetersPerMeter, 0, 'f', 1);
    m_rightWheelText = QStringLiteral("目标 %1，实际 %2 mm/s")
                           .arg(telemetry.rightTargetMps * kMillimetersPerMeter, 0, 'f', 1)
                           .arg(telemetry.right.wheelSpeedMps * kMillimetersPerMeter, 0, 'f', 1);
    m_feedbackText = QStringLiteral("命令 %1，反馈 %2")
                         .arg(telemetry.commandFresh ? QStringLiteral("正常") : QStringLiteral("超时"))
                         .arg(telemetry.feedbackFresh ? QStringLiteral("正常") : QStringLiteral("超时"));
    m_syncText = QStringLiteral("误差 %1，修正 %2 mm/s")
                     .arg(telemetry.synchronizationError, 0, 'f', 3)
                     .arg(telemetry.synchronizationCorrectionMps * kMillimetersPerMeter, 0, 'f', 1);
    updateInformationDialog();
}

void RobotControlPanel::updateState(crawling::DriveState state, const QString &reason)
{
    m_state = state;
    m_reasonText = reason;
    if (m_stateLabel)
        m_stateLabel->setText(crawling::driveStateText(state));
    if (m_infoState)
        m_infoState->setText(crawling::driveStateText(state));
    if (m_infoReason)
        m_infoReason->setText(reason);
    setStateStyle(state);
    if ((state == crawling::DriveState::Fault
         || state == crawling::DriveState::EmergencyStop)
        && m_autoCorrectionStartPending && !m_autoCorrectionActive) {
        // A safety transition invalidates the one-shot enable-then-correct request.
        // Do not restart correction automatically if the drive later recovers.
        m_autoCorrectionStartPending = false;
        setAutoCorrectionPaused(false);
        if (m_autoCorrectionStart)
            m_autoCorrectionStart->setEnabled(true);
        if (m_autoCorrectionStop)
            m_autoCorrectionStop->setEnabled(false);
        if (m_correctionStatus)
            m_correctionStatus->setText(QStringLiteral("未启动"));
    }
    if (state != crawling::DriveState::Enabled && m_jogLatched)
        clearJogLatch();
    const bool canEnable = m_connected && state != crawling::DriveState::Enabled &&
                           state != crawling::DriveState::Arming;
    m_enableButton->setEnabled(canEnable);
    appendLog(reason);
    emitVehicleStatus();
    if (state == crawling::DriveState::Enabled
        && m_autoCorrectionStartPending
        && !m_autoCorrectionActive) {
        QTimer::singleShot(0, this, &RobotControlPanel::startAutoCorrection);
    }
}

void RobotControlPanel::updateConnection(bool connected, const QString &message)
{
    m_connected = connected;
    if (!connected && m_jogLatched)
        clearJogLatch();
    if (!connected && m_autoCorrectionStartPending && !m_autoCorrectionActive) {
        m_autoCorrectionStartPending = false;
        setAutoCorrectionPaused(false);
        if (m_autoCorrectionStart)
            m_autoCorrectionStart->setEnabled(true);
        if (m_autoCorrectionStop)
            m_autoCorrectionStop->setEnabled(false);
        if (m_correctionStatus)
            m_correctionStatus->setText(QStringLiteral("未启动"));
    }
    m_connectionLabel->setText(connected ? QStringLiteral("适配器已连接")
                                          : QStringLiteral("适配器未连接"));
    if (m_infoConnection)
        m_infoConnection->setText(m_connectionLabel->text());
    updateInformationDialog();
    if (message.isEmpty())
        return;
    appendLog(message);
}

void RobotControlPanel::updateImu(const crawling::ImuSample &sample)
{
    if (m_imuUiClock.isValid() && m_imuUiClock.elapsed() < 100)
        return;
    m_imuUiClock.restart();
    m_imuText = QStringLiteral("姿态 R/P/Y %1 / %2 / %3°；角速度 %4 / %5 / %6°/s")
                    .arg(sample.rollRad * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.pitchRad * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.yawRad * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.gyroXRadps * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.gyroYRadps * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.gyroZRadps * kDegreesPerRadian, 0, 'f', 2);
    if (m_imuStatus && m_imuStatus->isVisible())
        m_imuStatus->setText(m_imuText);
    updateInformationDialog();
}

void RobotControlPanel::updateImuConnection(bool connected, const QString &message)
{
    m_imuConnected = connected;
    m_imuText = message.isEmpty() ? (connected ? QStringLiteral("已连接")
                                               : QStringLiteral("未连接"))
                                  : message;
    m_imuStatus->setText(m_imuText);
    m_imuConfigState->setText(m_imuText);
    updateInformationDialog();
}

void RobotControlPanel::updateSensorDetection(bool running, const QString &message)
{
    if (!message.isEmpty())
        appendLog(running ? QStringLiteral("设备检测：%1").arg(message) : message);
}

void RobotControlPanel::updateLaserDevices(const QStringList &devices)
{
    const QString configured = m_laserSerial->text().trimmed();
    int selectedIndex = -1;
    {
        QSignalBlocker blocker(m_laserDevice);
        m_laserDevice->clear();
        m_laserDevice->addItems(devices);
        for (int index = 0; index < m_laserDevice->count(); ++index) {
            if (m_laserDevice->itemText(index).section(QStringLiteral(" | "), 0, 0) ==
                configured) {
                selectedIndex = index;
                break;
            }
        }
        if (selectedIndex < 0 && configured.isEmpty() &&
            m_laserDevice->count() > 0) {
            selectedIndex = 0;
        }
        m_laserDevice->setCurrentIndex(selectedIndex);
    }
    if (selectedIndex >= 0) {
        m_laserSerial->setText(
            m_laserDevice->itemText(selectedIndex)
                .section(QStringLiteral(" | "), 0, 0)
                .trimmed());
    }
}

void RobotControlPanel::updateLaserConnection(bool connected, const QString &message)
{
    m_laserConnected = connected;
    m_laserText = message.isEmpty() ? (connected ? QStringLiteral("已连接")
                                                : QStringLiteral("未连接"))
                                    : message;
    m_laserStatus->setText(m_laserText);
    m_laserConfigState->setText(m_laserText);
    updateInformationDialog();
}

void RobotControlPanel::updateLaserFrame(quint32 frame, quint32 width,
                                         quint32 height, quint64 points)
{
    m_laserFrameState->setText(QStringLiteral("帧 #%1，%2 x %3，数据点 %4")
                                   .arg(frame).arg(width).arg(height).arg(points));
}

bool RobotControlPanel::beginProfileDetectionLogSession()
{
    if (m_profileDetectionLogCheck)
        m_profileDetectionLogCheck->setEnabled(false);

    if (m_correctionController) {
        QMetaObject::invokeMethod(
            m_correctionController, "setProfileDetectionTraceEnabled",
            Qt::BlockingQueuedConnection, Q_ARG(bool, false));
    }

    m_profileDetectionLogSessionActive = false;
    if (!m_profileDetectionLogCheck ||
        !m_profileDetectionLogCheck->isChecked()) {
        return false;
    }

    bool writerStarted = false;
    const bool invoked = m_profileDetectionLogWriter &&
        QMetaObject::invokeMethod(
            m_profileDetectionLogWriter,
            [this, &writerStarted] {
                writerStarted = m_profileDetectionLogWriter->beginSession();
            },
            Qt::BlockingQueuedConnection);
    if (!invoked || !writerStarted) {
        appendLog(QStringLiteral(
            "焊道检测计算日志创建失败，本次自动纠偏继续运行但不记录日志"));
        return false;
    }

    const bool traceEnabled = QMetaObject::invokeMethod(
        m_correctionController, "setProfileDetectionTraceEnabled",
        Qt::BlockingQueuedConnection, Q_ARG(bool, true));
    if (!traceEnabled) {
        QMetaObject::invokeMethod(
            m_profileDetectionLogWriter,
            [this] {
                m_profileDetectionLogWriter->endSession(
                    QStringLiteral("trace_enable_failed"));
            },
            Qt::BlockingQueuedConnection);
        appendLog(QStringLiteral(
            "焊道检测计算日志启动失败，本次自动纠偏继续运行但不记录日志"));
        return false;
    }

    m_profileDetectionLogSessionActive = true;
    appendLog(QStringLiteral(
        "焊道检测计算日志已覆盖并开始记录：logs/profile_weld_detection.log"));
    return true;
}

void RobotControlPanel::finishProfileDetectionLogSession(const QString &reason)
{
    if (m_correctionController && m_correctionThread &&
        m_correctionThread->isRunning()) {
        QMetaObject::invokeMethod(
            m_correctionController, "setProfileDetectionTraceEnabled",
            Qt::BlockingQueuedConnection, Q_ARG(bool, false));
    }
    if (m_profileDetectionLogSessionActive && m_profileDetectionLogWriter &&
        m_profileDetectionLogThread && m_profileDetectionLogThread->isRunning()) {
        QMetaObject::invokeMethod(
            m_profileDetectionLogWriter,
            [this, reason] {
                m_profileDetectionLogWriter->endSession(reason);
            },
            Qt::BlockingQueuedConnection);
    }
    m_profileDetectionLogSessionActive = false;
    if (m_profileDetectionLogCheck)
        m_profileDetectionLogCheck->setEnabled(true);
}

void RobotControlPanel::startAutoCorrection()
{
    if (!m_correctionController || !m_sensorController)
        return;
    if (!m_connected) {
        m_autoCorrectionStartPending = false;
        setAutoCorrectionPaused(false);
        appendLog(QStringLiteral("自动纠偏启动失败：底盘未连接"));
        return;
    }
    if (m_state == crawling::DriveState::EmergencyStop
        || m_state == crawling::DriveState::Fault) {
        m_autoCorrectionStartPending = false;
        setAutoCorrectionPaused(false);
        m_autoCorrectionStart->setEnabled(true);
        m_autoCorrectionStop->setEnabled(false);
        appendLog(QStringLiteral("自动纠偏启动失败：底盘处于紧急停止或安全故障状态，请先重新使能底盘"));
        return;
    }
    if (m_state != crawling::DriveState::Enabled) {
        if (!m_autoCorrectionStartPending) {
            m_autoCorrectionStartPending = true;
            m_autoCorrectionStart->setEnabled(false);
            m_autoCorrectionStop->setEnabled(true);
            setAutoCorrectionPaused(false);
            m_correctionStatus->setText(QStringLiteral("等待底盘使能后启动纠偏"));
            appendLog(QStringLiteral("已请求底盘使能，底盘就绪后自动启动纠偏"));
            QMetaObject::invokeMethod(m_controller, "requestEnable",
                                      Qt::QueuedConnection, Q_ARG(bool, true));
        }
        return;
    }
    if (!m_laserConnected) {
        m_autoCorrectionStartPending = false;
        setAutoCorrectionPaused(false);
        m_autoCorrectionStart->setEnabled(true);
        m_autoCorrectionStop->setEnabled(false);
        appendLog(QStringLiteral("自动纠偏启动失败：激光设备未连接"));
        return;
    }

    crawling::LaserCorrectionSettings settings;
    settings.targetSpeedMps = m_correctionSpeed->value() / kMillimetersPerMeter;
    settings.segmentLengthM = m_correctionSegment->value() / kMillimetersPerMeter;
    settings.proportionalGain = m_correctionKp->value();
    settings.derivativeGain = m_correctionKd->value();
    settings.imageTimeoutMs = m_laserInputTimeout->value();
    settings.wheelRadiusM = m_settings.wheelRadiusM;
    settings.trackWidthM = m_settings.trackWidthM;
    settings.detector.profileTuning = m_profileWeldTuning;
    settings.detector.profileTemplates = m_profileTemplates;
    settings.detector.profileDetectionMode = m_profileDetectionMode
        ? static_cast<crawling::ProfileDetectionMode>(
              m_profileDetectionMode->currentData().toInt())
        : crawling::ProfileDetectionMode::BaselineShoulder;
    settings.minimumInnerWheelRatio = m_settings.minimumInnerWheelRatio;

    const double synchronizedLimit =
        crawling::WheelMotorConfig::kMaximumSynchronizedMotorSpeedDps *
        settings.wheelRadiusM * kRadiansPerDegree /
        m_settings.motorOutputToWheelRatio;
    const double speedLimit =
        std::min({m_settings.maximumLinearSpeedMps,
                  m_settings.maximumWheelSpeedMps, synchronizedLimit});
    if (!std::isfinite(speedLimit) || settings.targetSpeedMps <= 0.0 ||
        settings.targetSpeedMps > speedLimit) {
        appendLog(QStringLiteral("自动纠偏速度超过当前底盘上限，未启动"));
        return;
    }

    beginProfileDetectionLogSession();
    crawling::AppLogger::write(
        QStringLiteral("CORRECTION.START"),
        QStringLiteral(
            "event=start_request state=%1 laser_connected=%2 ui_speed_mmps=%3 "
            "target_speed_mps=%4 segment_mm=%5 kp=%6 kd=%7 speed_limit_mps=%8 "
             "drive_linear_accel_mps2=%9 image_timeout_ms=%10 profile_detection_mode=%11 executable=%12")
            .arg(crawling::driveStateText(m_state))
            .arg(m_laserConnected ? 1 : 0)
            .arg(m_correctionSpeed->value(), 0, 'f', 1)
            .arg(settings.targetSpeedMps, 0, 'f', 6)
            .arg(m_correctionSegment->value(), 0, 'f', 1)
            .arg(settings.proportionalGain, 0, 'f', 3)
            .arg(settings.derivativeGain, 0, 'f', 3)
            .arg(speedLimit, 0, 'f', 6)
            .arg(m_settings.maximumLinearAccelerationMps2, 0, 'f', 4)
             .arg(settings.imageTimeoutMs)
             .arg(static_cast<int>(settings.detector.profileDetectionMode))
            .arg(QCoreApplication::applicationFilePath()));

    m_forward = m_reverse = m_left = m_right = false;
    const bool prestartStopQueued = QMetaObject::invokeMethod(
        m_controller, "setInputCommand", Qt::QueuedConnection,
        Q_ARG(double, 0.0), Q_ARG(double, 0.0));
    crawling::AppLogger::write(
        QStringLiteral("CORRECTION.START"),
        QStringLiteral("event=correction_prestart_stop result=%1 "
                       "reason=clear_previous_manual_motion target_thread=%2")
            .arg(prestartStopQueued ? QStringLiteral("QUEUED")
                                     : QStringLiteral("REJECTED"))
            .arg(reinterpret_cast<quintptr>(m_controller->thread()), 0, 16));
    m_autoCorrectionActive = true;
    m_autoCorrectionStartPending = true;
    m_autoCorrectionStart->setEnabled(false);
    m_autoCorrectionStop->setEnabled(true);
    setAutoCorrectionPaused(false);
    m_correctionStatus->setText(QStringLiteral("启动准备：等待激光轮廓流"));
    QMetaObject::invokeMethod(m_correctionController, "setSettings",
                              Qt::QueuedConnection,
                              Q_ARG(crawling::LaserCorrectionSettings, settings));
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    persistent.setValue(QStringLiteral("laserCorrection/speed"),
                        settings.targetSpeedMps);
    persistent.setValue(QStringLiteral("laserCorrection/segmentLength"),
                        settings.segmentLengthM);
    persistent.setValue(QStringLiteral("laserCorrection/kp"),
                        settings.proportionalGain);
    persistent.setValue(QStringLiteral("laserCorrection/kd"),
                        settings.derivativeGain);
    persistent.setValue(QStringLiteral("laserCorrection/imageTimeoutMs"),
                        settings.imageTimeoutMs);
    persistent.sync();
    QMetaObject::invokeMethod(m_sensorController, "prepareCorrectionProfile",
                              Qt::QueuedConnection);
    appendLog(QStringLiteral("已请求启动自动纠偏（速度 %1 mm/s，A2 速度闭环），等待激光轮廓流")
                  .arg(m_correctionSpeed->value(), 0, 'f', 1));
}

void RobotControlPanel::stopAutoCorrection()
{
    if (!m_correctionController)
        return;
    m_autoCorrectionStartPending = false;
    const bool wasActive = m_autoCorrectionActive;
    m_autoCorrectionActive = false;
    const bool wasPaused = m_autoCorrectionPaused;
    m_autoCorrectionPaused = false;
    if (wasPaused)
        emit autoCorrectionPausedChanged(false);
    setAutoCorrectionPaused(false);
    finishProfileDetectionLogSession(QStringLiteral("operator_stop"));
    m_autoCorrectionStart->setEnabled(true);
    m_autoCorrectionStop->setEnabled(false);
    QMetaObject::invokeMethod(m_correctionController, "setEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, false));
    if (m_sensorController)
        QMetaObject::invokeMethod(m_sensorController, "restoreOriginalPreview",
                                  Qt::QueuedConnection);
    if (wasActive)
        appendLog(QStringLiteral("自动纠偏已停止"));
    if (m_correctionStatus)
        m_correctionStatus->setText(QStringLiteral("未启动"));
}

void RobotControlPanel::updateCorrectionStatus(
    const crawling::LaserCorrectionStatus &status)
{
    if (!m_correctionStatus)
        return;
    if (!status.active) {
        const bool wasRunning = m_autoCorrectionActive || m_autoCorrectionStartPending;
        if (wasRunning || m_profileDetectionLogSessionActive)
            finishProfileDetectionLogSession(
                status.reason.isEmpty() ? QStringLiteral("controller_stopped")
                                        : status.reason);
        m_autoCorrectionActive = false;
        m_autoCorrectionStartPending = false;
        const bool wasPaused = m_autoCorrectionPaused;
        m_autoCorrectionPaused = false;
        if (wasPaused)
            emit autoCorrectionPausedChanged(false);
        setAutoCorrectionPaused(false);
        m_autoCorrectionStart->setEnabled(true);
        m_autoCorrectionStop->setEnabled(false);
        if (m_sensorController)
            QMetaObject::invokeMethod(m_sensorController, "restoreOriginalPreview",
                                      Qt::QueuedConnection);
        if (wasRunning) {
            crawling::AppLogger::write(
                QStringLiteral("CORRECTION.START"),
                QStringLiteral("event=controller_stopped reason=%1")
                    .arg(status.reason));
            appendLog(QStringLiteral("自动纠偏结束：%1").arg(
                status.reason.isEmpty() ? QStringLiteral("控制器未给出原因")
                                        : status.reason));
        }
        m_correctionStatus->setText(status.reason.isEmpty()
                                         ? QStringLiteral("未启动")
                                         : status.reason);
        return;
    }
    if (!m_correctionStatus->isVisible())
        return;
    // 使用状态机的详细描述，而不是简单的"已定位/等待定位"
    QString seamStatusText;
    if (!status.seamStateDescription.isEmpty()) {
        seamStatusText = status.seamStateDescription;
        if (status.laserCenterCalibrated && status.gapValid) {
            seamStatusText += QStringLiteral("（偏移 %1 mm）")
                .arg(status.laserCenterOffset, 0, 'f', 2);
        }
    } else {
        // 向后兼容：如果status没有提供状态描述，使用旧逻辑
        seamStatusText = status.gapValid ? QStringLiteral("已定位") : QStringLiteral("等待定位");
    }

    m_correctionStatus->setText(
        QStringLiteral("%1%2；焊道%3；置信度 %4；线速度 %5 mm/s；角速度 %6 deg/s")
            .arg(status.phase.isEmpty() ? QStringLiteral("运行中") : status.phase)
            .arg(status.paused ? QStringLiteral("（已暂停）") : QString())
            .arg(seamStatusText)
            .arg(status.confidence, 0, 'f', 2)
            .arg(status.linearCommandMps * kMillimetersPerMeter, 0, 'f', 1)
            .arg(status.angularCommandRadps * kDegreesPerRadian, 0, 'f', 2));
}

void RobotControlPanel::updateUsbCameraSourceUi(int slot)
{
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount)
        return;
    const auto& ui = m_usbCameraUi[slot];
    const bool network =
        ui.source && ui.source->currentData().toInt() ==
                         static_cast<int>(crawling::CameraSource::Network);
    if (ui.device)
        ui.device->setEnabled(!network);
    if (ui.networkUrl)
        ui.networkUrl->setEnabled(network);
    if (ui.fps)
        ui.fps->setEnabled(!network);
    if (ui.scan)
        ui.scan->setEnabled(!network);
    if (ui.connect) {
        ui.connect->setText(network ? QStringLiteral("连接配置中的网络摄像头")
                                    : QStringLiteral("连接配置中的 USB 摄像头"));
    }
}

void RobotControlPanel::updateUsbDevices(const QStringList &devices)
{
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        QComboBox *combo = m_usbCameraUi[slot].device;
        if (!combo)
            continue;
        const int configured = combo->currentIndex() >= 0
                                 ? combo->currentData().toInt()
                                 : m_settings.usbCameras[slot].usbCameraDeviceIndex;
        QSignalBlocker blocker(combo);
        combo->clear();
        for (const QString &device : devices) {
            bool okay = false;
            const int index = device.section(QStringLiteral(" | "), 0, 0).toInt(&okay);
            if (okay)
                combo->addItem(device, index);
        }
        const int selected = combo->findData(configured);
        combo->setCurrentIndex(selected >= 0 ? selected : -1);
    }
}

void RobotControlPanel::updateUsbConnection(int slot, bool connected, const QString &message)
{
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount)
        return;
    m_usbConnectedSlot[slot] = connected;
    const QString statusText = message.isEmpty()
                                    ? (connected ? QStringLiteral("已连接")
                                                    : QStringLiteral("未连接"))
                                    : message;
    // The combined "USB status" label shows whichever channel is connected; if both are
    // connected, prefer the lower-numbered slot for stability in the snapshot.
    int snapshotSlot = -1;
    for (int i = 0; i < crawling::kUsbCameraChannelCount; ++i) {
        if (m_usbConnectedSlot[i]) { snapshotSlot = i; break; }
    }
    if (snapshotSlot < 0) {
        m_usbStatus->setText(QStringLiteral("未连接"));
        m_usbText = m_usbStatus->text();
    } else {
        m_usbStatus->setText(QStringLiteral("%1 已连接").arg(snapshotSlot + 1));
        m_usbText = m_usbStatus->text();
    }
    if (m_usbCameraUi[slot].configState)
        m_usbCameraUi[slot].configState->setText(statusText);
    if (!connected) {
        m_usbImages[slot] = QImage();
        if (m_usbViews[slot]) {
            m_usbViews[slot]->setPixmap(QPixmap());
            m_usbViews[slot]->setText(QStringLiteral("USB 摄像头 %1 未连接").arg(slot + 1));
        }
    }
    updateInformationDialog();
}

void RobotControlPanel::updateClampConnection(bool connected, const QString &message)
{
    if (m_clampConfigState)
        m_clampConfigState->setText(message.isEmpty()
                                        ? (connected ? QStringLiteral("已连接")
                                                     : QStringLiteral("未连接"))
                                        : message);
}

void RobotControlPanel::appendLog(const QString &message)
{
    if (message.isEmpty())
        return;
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz  ")) + message;
    m_logLines.append(line);
    while (m_logLines.size() > 400)
        m_logLines.removeFirst();
    if (m_infoLog && m_infoLog->isVisible() && m_logFlushTimer) {
        m_pendingLogLines.append(line);
        while (m_pendingLogLines.size() > 400)
            m_pendingLogLines.removeFirst();
        if (!m_logFlushTimer->isActive())
            m_logFlushTimer->start();
    } else {
        m_logViewDirty = true;
    }
}

void RobotControlPanel::flushLogUi()
{
    if (!m_infoLog || !m_infoLog->isVisible()) {
        m_logViewDirty = true;
        m_pendingLogLines.clear();
        return;
    }
    if (m_logViewDirty) {
        m_infoLog->setPlainText(m_logLines.join(QStringLiteral("\n")));
        m_logViewDirty = false;
        m_pendingLogLines.clear();
        return;
    }
    if (m_pendingLogLines.isEmpty())
        return;
    m_infoLog->appendPlainText(m_pendingLogLines.join(QStringLiteral("\n")));
    m_pendingLogLines.clear();
}

void RobotControlPanel::refreshVisibleUi()
{
    if (m_imuStatus && m_imuStatus->isVisible())
        m_imuStatus->setText(m_imuText);
    updateInformationDialog();
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        if (!m_usbImages[slot].isNull())
            displayUsbCameraImage(slot, m_usbImages[slot]);
    }
    flushLogUi();
    if (m_laserView && m_laserView->isVisible())
        m_laserView->update();
}

void RobotControlPanel::setLaserProfilePoints(const QVector<QVector3D> &points)
{
    m_laserPoints = points;
    if (m_laserView)
        m_laserView->setPoints(points);
}

void RobotControlPanel::setLaserProfileImage(const QImage &image)
{
    m_laserImage = image;
}

void RobotControlPanel::displayUsbCameraImage(int slot, const QImage &image)
{
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount || image.isNull() || !m_usbViews[slot])
        return;
    m_usbImages[slot] = image;
    if (!m_usbViews[slot]->isVisible())
        return;
    m_usbViews[slot]->setText(QString());
    const qreal dpr = m_usbViews[slot]->devicePixelRatioF();
    // Image is already scaled in worker thread, just convert to pixmap.
    QPixmap pix = QPixmap::fromImage(image);
    pix.setDevicePixelRatio(dpr);
    m_usbViews[slot]->setPixmap(pix);
}

void RobotControlPanel::setUsbCameraImage(int slot, const QImage &image)
{
    if (slot < 0 || slot >= crawling::kUsbCameraChannelCount)
        return;
    displayUsbCameraImage(slot, image);
    // Acknowledge exactly one delivered camera frame. Resize repainting must
    // not release the capture backpressure as if a new frame had arrived.
    if (m_usbCameraController)
        QMetaObject::invokeMethod(m_usbCameraController, "frameConsumed",
                                  Qt::QueuedConnection, Q_ARG(int, slot));
}

void RobotControlPanel::pushUsbDisplaySize(int slot)
{
    if (!m_usbCameraController || !m_usbViews[slot])
        return;
    const QWidget* view = m_usbViews[slot];
    const QSize logicalSize = view->size().expandedTo(QSize(260, 160));
    const qreal dpr = view->devicePixelRatioF();
    const int physicalWidth = qRound(logicalSize.width() * dpr);
    const int physicalHeight = qRound(logicalSize.height() * dpr);
    QMetaObject::invokeMethod(m_usbCameraController, "setDisplaySize",
                              Qt::QueuedConnection,
                              Q_ARG(int, slot),
                              Q_ARG(int, physicalWidth),
                              Q_ARG(int, physicalHeight));
}
void RobotControlPanel::setStateStyle(crawling::DriveState state)
{
    QString color = QStringLiteral("#526174");
    if (state == crawling::DriveState::Enabled)
        color = QStringLiteral("#16804b");
    else if (state == crawling::DriveState::Fault ||
             state == crawling::DriveState::EmergencyStop)
        color = QStringLiteral("#c62828");
    else if (state == crawling::DriveState::Arming)
        color = QStringLiteral("#b26a00");
    m_stateLabel->setStyleSheet(QStringLiteral("QLabel { color:white; background:%1; "
                                                "border-radius:10px; padding:5px 12px; "
                                                "font-weight:600; }").arg(color));
}

void RobotControlPanel::updateInformationDialog()
{
    emitVehicleStatus();
    if (!m_infoDialog || !m_infoDialog->isVisible())
        return;
    const auto setLabel = [](QLabel *label, const QString &text) {
        if (label)
            label->setText(text);
    };
    setLabel(m_infoState, crawling::driveStateText(m_state));
    setLabel(m_infoReason, m_reasonText);
    setLabel(m_infoTarget, m_targetText);
    setLabel(m_infoApplied, m_appliedText);
    setLabel(m_infoLeftWheel, m_leftWheelText);
    setLabel(m_infoRightWheel, m_rightWheelText);
    setLabel(m_infoFeedback, m_feedbackText);
    setLabel(m_infoSync, m_syncText);
    setLabel(m_dialogState, crawling::driveStateText(m_state));
    setLabel(m_dialogReason, m_reasonText);
    setLabel(m_dialogConnection, m_connected ? QStringLiteral("适配器已连接")
                                              : QStringLiteral("适配器未连接"));
    setLabel(m_dialogTarget, m_targetText);
    setLabel(m_dialogApplied, m_appliedText);
    setLabel(m_dialogLeftWheel, m_leftWheelText);
    setLabel(m_dialogRightWheel, m_rightWheelText);
    setLabel(m_dialogFeedback, m_feedbackText);
    setLabel(m_dialogSync, m_syncText);
}

void RobotControlPanel::emitVehicleStatus()
{
    // 顺序固定（12 项）：运行状态/状态说明/适配器/目标运动/实际运动/
    // 左轮/右轮/反馈看门狗/双轮同步/IMU/激光/USB。
    const QStringList metrics{
        crawling::driveStateText(m_state),
        m_reasonText,
        m_connected ? QStringLiteral("适配器已连接") : QStringLiteral("适配器未连接"),
        m_targetText,
        m_appliedText,
        m_leftWheelText,
        m_rightWheelText,
        m_feedbackText,
        m_syncText,
        m_imuText,
        m_laserText,
        m_usbStatus ? m_usbStatus->text() : m_usbText,
    };
    emit vehicleStatusChanged(metrics);
}

void RobotControlPanel::showInformationDialog(QWidget *parent)
{
    if (!m_infoDialog) {
        auto *dialog = new QDialog(parent ? parent : this);
        dialog->setWindowTitle(QStringLiteral("小车信息"));
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        auto *layout = new QFormLayout(dialog);
        m_dialogState = new QLabel(dialog);
        m_dialogReason = new QLabel(dialog);
        m_dialogConnection = new QLabel(dialog);
        m_dialogTarget = new QLabel(dialog);
        m_dialogApplied = new QLabel(dialog);
        m_dialogLeftWheel = new QLabel(dialog);
        m_dialogRightWheel = new QLabel(dialog);
        m_dialogFeedback = new QLabel(dialog);
        m_dialogSync = new QLabel(dialog);
        const QList<QLabel *> dialogLabels = {
            m_dialogState.data(), m_dialogReason.data(), m_dialogConnection.data(),
            m_dialogTarget.data(), m_dialogApplied.data(), m_dialogLeftWheel.data(),
            m_dialogRightWheel.data(), m_dialogFeedback.data(), m_dialogSync.data()};
        for (QLabel *label : dialogLabels) {
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        }
        layout->addRow(QStringLiteral("运行状态"), m_dialogState);
        layout->addRow(QStringLiteral("状态说明"), m_dialogReason);
        layout->addRow(QStringLiteral("适配器"), m_dialogConnection);
        layout->addRow(QStringLiteral("目标运动"), m_dialogTarget);
        layout->addRow(QStringLiteral("实际运动"), m_dialogApplied);
        layout->addRow(QStringLiteral("左轮"), m_dialogLeftWheel);
        layout->addRow(QStringLiteral("右轮"), m_dialogRightWheel);
        layout->addRow(QStringLiteral("反馈看门狗"), m_dialogFeedback);
        layout->addRow(QStringLiteral("双轮同步"), m_dialogSync);
        m_infoDialog = dialog;
    }
    m_infoDialog->show();
    updateInformationDialog();
    m_infoDialog->adjustSize();
    m_infoDialog->raise();
    m_infoDialog->activateWindow();
}

void RobotControlPanel::showConfigurationPage()
{
    if (m_tabs) {
        m_tabs->setCurrentIndex(1);
        show();
        raise();
        activateWindow();
    }
}

void RobotControlPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    for (int slot = 0; slot < crawling::kUsbCameraChannelCount; ++slot) {
        if (!m_usbImages[slot].isNull())
            displayUsbCameraImage(slot, m_usbImages[slot]);
        pushUsbDisplaySize(slot);
    }
}

void RobotControlPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] { refreshVisibleUi(); });
}

void RobotControlPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event == nullptr ||
        (event->type() != QEvent::WindowDeactivate &&
         event->type() != QEvent::ApplicationDeactivate)) {
        return;
    }
    if (m_autoCorrectionActive) {
        const QString source = event->type() == QEvent::WindowDeactivate
                                   ? QStringLiteral("window_deactivate")
                                   : QStringLiteral("application_deactivate");
        crawling::AppLogger::write(
            QStringLiteral("CORRECTION.STOP"),
            QStringLiteral("event=correction_stop_requested source=%1 active=1 pending=%2")
                .arg(source)
                .arg(m_autoCorrectionStartPending ? 1 : 0));
        stopAutoCorrection();
    }
    clearJogLatch();
    const bool wasJogging = m_forward || m_reverse || m_left || m_right;
    m_forward = m_reverse = m_left = m_right = false;
    if (wasJogging)
        sendMotionCommand();
    for (QSlider *slider : findChildren<QSlider *>(QStringLiteral("clampMotorSpeed"))) {
        const QSignalBlocker blocker(slider);
        slider->setValue(0);
    }
    if (m_clampController)
        m_clampController->stop();
}


