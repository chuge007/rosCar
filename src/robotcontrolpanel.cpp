#include "robotcontrolpanel.h"

#include "robot_profile_view.h"
#include "robot_sensor_controller.h"
#include "robot_usb_camera_controller.h"
#include "synchronized_drive_controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QDateTime>
#include <QDialog>
#include <QDoubleSpinBox>
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
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
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

RobotControlPanel::RobotControlPanel(QWidget *parent) : QWidget(parent)
{
    qRegisterMetaType<crawling::DriveSettings>("crawling::DriveSettings");
    qRegisterMetaType<crawling::DriveState>("crawling::DriveState");
    qRegisterMetaType<crawling::DriveTelemetry>("crawling::DriveTelemetry");
    qRegisterMetaType<crawling::ImuSample>("crawling::ImuSample");
    qRegisterMetaType<QVector<QVector3D>>("QVector<QVector3D>");

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
    m_driveThread->start();

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
    connect(m_sensorController, &crawling::RobotSensorController::pointCloudProfileChanged,
            this, &RobotControlPanel::setLaserProfilePoints, Qt::QueuedConnection);
    connect(m_sensorController, &crawling::RobotSensorController::logMessage,
            this, &RobotControlPanel::appendLog, Qt::QueuedConnection);
    m_sensorThread->start();

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
    m_usbCameraThread->start();

    m_commandTimer = new QTimer(this);
    m_commandTimer->setInterval(80);
    connect(m_commandTimer, &QTimer::timeout, this, &RobotControlPanel::sendMotionCommand);
    m_commandTimer->start();

    QTimer::singleShot(0, this, [this] {
        if (m_autoConnectCheck && m_autoConnectCheck->isChecked())
            connectConfiguredDevices();
    });
}

RobotControlPanel::~RobotControlPanel()
{
    if (m_commandTimer)
        m_commandTimer->stop();
    if (m_sensorController)
        QMetaObject::invokeMethod(m_sensorController, "shutdown",
                                  Qt::BlockingQueuedConnection);
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
        #robotConfigPage QPlainTextEdit { font-size: 11px; }
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
    m_autoConnectCheck = new QCheckBox(QStringLiteral("启动时自动连接"));
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
    auto *body = new QWidget;
    body->setObjectName(QStringLiteral("robotBody"));
    body->setMinimumHeight(760);
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
    auto *stop = actionButton(QStringLiteral("停止并保持"));
    auto *emergency = actionButton(QStringLiteral("紧急停止"), "danger");
    driveGrid->addWidget(m_enableButton, 0, 0);
    driveGrid->addWidget(stop, 0, 1);
    driveGrid->addWidget(emergency, 1, 0, 1, 2);
    manualLayout->addLayout(driveGrid);
    manualLayout->addStretch();

    auto *profileGroup = new QGroupBox(QStringLiteral("SDK 实时轮廓与焊道定位"));
    auto *profileLayout = new QVBoxLayout(profileGroup);
    m_laserView = new RobotProfileView;
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
    m_usbView = new QLabel(QStringLiteral("USB 摄像头未连接"));
    m_usbView->setAlignment(Qt::AlignCenter);
    m_usbView->setMinimumSize(300, 180);
    usbLayout->addWidget(m_usbView);
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
    root->addWidget(m_tabs, 1);

    connect(m_connectAllButton, &QPushButton::clicked,
            this, &RobotControlPanel::connectAllConfiguredDevices);
    connect(disconnectAll, &QPushButton::clicked,
            this, &RobotControlPanel::disconnectAllDevices);
    connect(m_autoConnectCheck, &QCheckBox::toggled,
            this, &RobotControlPanel::persistAutoConnectSetting);
    connect(m_enableButton, &QPushButton::clicked, this, [this] { requestEnable(true); });
    connect(stop, &QPushButton::clicked, this, &RobotControlPanel::stopMotion);
    connect(emergency, &QPushButton::clicked, this, &RobotControlPanel::emergencyStop);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
        m_speedLabel->setText(QStringLiteral("%1%").arg(value));
    });
    const auto bindJog = [this](QPushButton *button, bool *flag) {
        connect(button, &QPushButton::pressed, this, [this, flag] { setMotion(*flag, true); });
        connect(button, &QPushButton::released, this, [this, flag] { setMotion(*flag, false); });
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
    layout->addLayout(leftColumn, 1);
    layout->addLayout(rightColumn, 1);
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

    auto *adapter = new QGroupBox(QStringLiteral("MWD RS485 适配器"), page);
    auto *adapterForm = new QFormLayout(adapter);
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
    adapterForm->addRow(QStringLiteral("左轮串口"), m_leftPort);
    adapterForm->addRow(QStringLiteral("左轮波特率"), m_leftBaud);
    adapterForm->addRow(QStringLiteral("左轮 ID"), m_leftId);
    adapterForm->addRow(QStringLiteral("左轮方向"), m_leftSign);
    adapterForm->addRow(QStringLiteral("右轮串口"), m_rightPort);
    adapterForm->addRow(QStringLiteral("右轮波特率"), m_rightBaud);
    adapterForm->addRow(QStringLiteral("右轮 ID"), m_rightId);
    adapterForm->addRow(QStringLiteral("右轮方向"), m_rightSign);
    adapterForm->addRow(QString(), buttonRow({refresh, connectButton, disconnectButton}));
    connect(refresh, &QPushButton::clicked, this, &RobotControlPanel::refreshPorts);
    connect(connectButton, &QPushButton::clicked, this, &RobotControlPanel::connectDrive);
    connect(disconnectButton, &QPushButton::clicked, this, &RobotControlPanel::disconnectDrive);
    compactGroup(adapter);
    leftColumn->addWidget(adapter);

    auto *mapping = new QGroupBox(QStringLiteral("轮子与底盘几何"), page);
    auto *mappingForm = new QFormLayout(mapping);
    m_wheelRadius = makeDouble(1.0, 500.0, 1.0, 2, mapping);
    m_trackWidth = makeDouble(10.0, 2000.0, 1.0, 1, mapping);
    m_ratio = makeDouble(0.001, 1000.0, 0.01, 4, mapping);
    mappingForm->addRow(QStringLiteral("轮半径 (mm)"), m_wheelRadius);
    mappingForm->addRow(QStringLiteral("轮距 (mm)"), m_trackWidth);
    mappingForm->addRow(QStringLiteral("电机输出/轮子传动比"), m_ratio);
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
    m_laserSerial->setPlaceholderText(QStringLiteral("填写序列号，或扫描后选择"));
    m_laserDevice = new QComboBox(laser);
    auto *laserScan = actionButton(QStringLiteral("扫描激光相机"));
    auto *laserConnect = actionButton(QStringLiteral("连接配置中的激光相机"), "primary");
    auto *laserDisconnect = actionButton(QStringLiteral("断开激光相机"));
    m_laserConfigState = new QLabel(QStringLiteral("未连接"), laser);
    m_laserFrameState = new QLabel(QStringLiteral("暂无帧数据"), laser);
    laserForm->addRow(QStringLiteral("配置序列号"), m_laserSerial);
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
    compactGroup(laser);
    rightColumn->addWidget(laser);

    auto *usb = new QGroupBox(QStringLiteral("USB 摄像头配置"), page);
    auto *usbForm = new QFormLayout(usb);
    m_cameraDevice = new QComboBox(usb);
    for (int index = 0; index < 10; ++index)
        m_cameraDevice->addItem(QStringLiteral("设备 %1").arg(index), index);
    m_cameraDevice->setCurrentIndex(-1);
    m_cameraFps = new QSpinBox(usb);
    m_cameraFps->setRange(1, 120);
    m_cameraAutoConnect = new QCheckBox(QStringLiteral("参与连接全部接口和启动时自动连接"), usb);
    m_cameraFlipHorizontal = new QCheckBox(QStringLiteral("画面水平翻转"), usb);
    m_cameraFlipVertical = new QCheckBox(QStringLiteral("画面竖直翻转"), usb);
    for (QCheckBox *check : {m_cameraAutoConnect, m_cameraFlipHorizontal,
                             m_cameraFlipVertical}) {
        check->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        check->setMinimumHeight(21);
    }
    auto *usbScan = actionButton(QStringLiteral("扫描 USB 摄像头"));
    auto *usbConnect = actionButton(QStringLiteral("连接配置中的 USB 摄像头"), "primary");
    auto *usbDisconnect = actionButton(QStringLiteral("断开 USB 摄像头"));
    m_usbConfigState = new QLabel(QStringLiteral("未连接"), usb);
    usbForm->addRow(QStringLiteral("设备"), m_cameraDevice);
    usbForm->addRow(QStringLiteral("帧率 (fps)"), m_cameraFps);
    usbForm->addRow(QString(), buttonRow({usbScan, usbConnect, usbDisconnect}));
    auto *cameraOptions = new QWidget;
    auto *cameraOptionsLayout = new QHBoxLayout(cameraOptions);
    cameraOptionsLayout->setContentsMargins(0, 0, 0, 0);
    cameraOptionsLayout->setSpacing(14);
    cameraOptionsLayout->addWidget(m_cameraAutoConnect);
    cameraOptionsLayout->addWidget(m_cameraFlipHorizontal);
    cameraOptionsLayout->addWidget(m_cameraFlipVertical);
    cameraOptionsLayout->addStretch();
    usbForm->addRow(QString(), cameraOptions);
    usbForm->addRow(QStringLiteral("连接状态"), m_usbConfigState);
    connect(usbScan, &QPushButton::clicked, this, [this] {
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "scanDevices",
                                      Qt::QueuedConnection);
    });
    connect(usbConnect, &QPushButton::clicked, this,
            &RobotControlPanel::connectConfiguredUsbCamera);
    connect(usbDisconnect, &QPushButton::clicked, this, [this] {
        if (m_usbCameraController)
            QMetaObject::invokeMethod(m_usbCameraController, "disconnectCamera",
                                      Qt::QueuedConnection);
    });
    compactGroup(usb);
    leftColumn->addWidget(usb);

    auto *clamp = new QGroupBox(QStringLiteral("夹子电机 CANopen"), page);
    auto *clampForm = new QFormLayout(clamp);
    m_clampPort = new QComboBox(clamp);
    m_clampBaud = new QComboBox(clamp);
    for (int baud : {115200, 230400, 460800, 921600})
        m_clampBaud->addItem(QString::number(baud), baud);
    m_clampCanBitrate = new QComboBox(clamp);
    for (int bitrate : {125000, 250000, 500000, 800000, 1000000})
        m_clampCanBitrate->addItem(QString::number(bitrate), bitrate);
    m_clampNodeId = new QSpinBox(clamp);
    m_clampNodeId->setRange(0, 127);
    m_clampXId = new QSpinBox(clamp);
    m_clampYId = new QSpinBox(clamp);
    m_clampZId = new QSpinBox(clamp);
    for (QSpinBox *box : {m_clampXId, m_clampYId, m_clampZId})
        box->setRange(1, 127);
    m_clampXSign = new QComboBox(clamp);
    m_clampYSign = new QComboBox(clamp);
    m_clampZSign = new QComboBox(clamp);
    for (QComboBox *box : {m_clampXSign, m_clampYSign, m_clampZSign}) {
        box->addItem(QStringLiteral("+1 正向"), 1);
        box->addItem(QStringLiteral("-1 反向"), -1);
    }
    clampForm->addRow(QStringLiteral("SLCAN 串口"), m_clampPort);
    clampForm->addRow(QStringLiteral("适配器波特率"), m_clampBaud);
    clampForm->addRow(QStringLiteral("CAN 波特率"), m_clampCanBitrate);
    clampForm->addRow(QStringLiteral("CANopen 节点 ID"), m_clampNodeId);
    auto addClampAxis = [clamp, clampForm](const QString &name, QSpinBox *id,
                                           QComboBox *sign) {
        auto *row = new QWidget(clamp);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(id);
        rowLayout->addWidget(sign);
        clampForm->addRow(name, row);
    };
    addClampAxis(QStringLiteral("X 电机 ID / 方向"), m_clampXId, m_clampXSign);
    addClampAxis(QStringLiteral("Y 电机 ID / 方向"), m_clampYId, m_clampYSign);
    addClampAxis(QStringLiteral("Z 电机 ID / 方向"), m_clampZId, m_clampZSign);
    clampForm->addRow(QStringLiteral("检测状态"), metricLabel(clamp));
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
    limitsForm->addRow(QStringLiteral("最大轮速 (mm/s)"), m_maxWheelSpeed);
    limitsForm->addRow(QStringLiteral("最大线速度 (mm/s)"), m_maxLinearSpeed);
    limitsForm->addRow(QStringLiteral("最大角速度 (deg/s)"), m_maxAngularSpeed);
    limitsForm->addRow(QStringLiteral("线加速度 (mm/s²)"), m_maxLinearAcceleration);
    limitsForm->addRow(QStringLiteral("角加速度 (deg/s²)"), m_maxAngularAcceleration);
    limitsForm->addRow(QStringLiteral("内侧轮最小比例"), m_minimumInnerRatio);
    limitsForm->addRow(QStringLiteral("同步比例增益 P"), m_syncP);
    limitsForm->addRow(QStringLiteral("同步积分增益 I"), m_syncI);
    limitsForm->addRow(QStringLiteral("同步最大修正量 (mm/s)"), m_syncMaxCorrection);
    limitsForm->addRow(QStringLiteral("同步最小受控速度 (mm/s)"), m_syncMinSpeed);
    compactGroup(limits);
    rightColumn->insertWidget(1, limits);

    auto *safety = new QGroupBox(QStringLiteral("安全看门狗"), page);
    auto *safetyForm = new QFormLayout(safety);
    m_commandTimeout = new QSpinBox(safety);
    m_feedbackTimeout = new QSpinBox(safety);
    m_armingTimeout = new QSpinBox(safety);
    for (QSpinBox *box : {m_commandTimeout, m_feedbackTimeout, m_armingTimeout}) {
        box->setRange(100, 10000);
        box->setSuffix(QStringLiteral(" ms"));
    }
    m_feedbackTimeout->setMinimum(crawling::DriveSettings::kMinimumFeedbackTimeoutMs);
    m_armingTimeout->setMinimum(crawling::DriveSettings::kMinimumFeedbackTimeoutMs);
    safetyForm->addRow(QStringLiteral("命令超时"), m_commandTimeout);
    safetyForm->addRow(QStringLiteral("反馈超时"), m_feedbackTimeout);
    safetyForm->addRow(QStringLiteral("使能等待超时"), m_armingTimeout);
    auto *save = actionButton(QStringLiteral("保存并应用全部参数"), "primary");
    safetyForm->addRow(QString(), save);
    m_laserConfigState->setWordWrap(true);
    m_laserFrameState->setWordWrap(true);
    m_imuConfigState->setWordWrap(true);
    m_usbConfigState->setWordWrap(true);
    compactGroup(safety);
    leftColumn->addWidget(safety);

    auto *logGroup = new QGroupBox(QStringLiteral("接口日志"), page);
    auto *logLayout = new QVBoxLayout(logGroup);
    m_infoLog = new QPlainTextEdit(logGroup);
    m_infoLog->setReadOnly(true);
    m_infoLog->setMaximumBlockCount(400);
    m_infoLog->setMinimumHeight(90);
    m_infoLog->setMaximumHeight(130);
    logLayout->addWidget(m_infoLog);
    compactGroup(logGroup);
    rightColumn->addWidget(logGroup);

    connect(save, &QPushButton::clicked, this, [this] {
        const QString error = settingsFromUi().validationError();
        if (!error.isEmpty()) {
            appendLog(error);
            QMessageBox::warning(this, QStringLiteral("配置无效"), error);
            return;
        }
        saveSettings();
        if (m_connected)
            connectConfiguredDevices();
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
                          m_armingTimeout, m_clampNodeId, m_clampXId, m_clampYId,
                          m_clampZId, m_cameraFps})
        connect(box, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    connect(m_laserSerial, &QLineEdit::textChanged, this, markDirty);
    for (QCheckBox *box : {m_cameraAutoConnect, m_cameraFlipHorizontal,
                           m_cameraFlipVertical, m_autoConnectCheck})
        connect(box, &QCheckBox::toggled, this, markDirty);
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
}

void RobotControlPanel::saveSettings()
{
    const crawling::DriveSettings current = settingsFromUi();
    const QString error = current.validationError();
    if (!error.isEmpty()) {
        appendLog(error);
        return;
    }
    m_settings = current;
    QSettings persistent(crawling::DriveSettings::persistentFilePath(),
                         QSettings::IniFormat);
    m_settings.save(persistent);
    persistent.sync();
    appendLog(QStringLiteral("小车配置已保存"));
}

void RobotControlPanel::restoreSettings()
{
    loadSettings();
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
    value.imuSerialPort = comboText(m_imuPort, value.imuSerialPort);
    value.imuBaudRate = comboInt(m_imuBaud, value.imuBaudRate);
    value.imuOutputDivider = comboInt(m_imuDivider, value.imuOutputDivider);
    value.laserSerialNumber = m_laserSerial->text().trimmed();
    value.usbCameraDeviceIndex = m_cameraDevice->currentIndex() >= 0
                                     ? m_cameraDevice->currentData().toInt() : -1;
    value.usbCameraFps = m_cameraFps->value();
    value.usbCameraAutoConnect = m_cameraAutoConnect->isChecked();
    value.usbCameraFlipHorizontal = m_cameraFlipHorizontal->isChecked();
    value.usbCameraFlipVertical = m_cameraFlipVertical->isChecked();
    value.autoConnectOnStartup = m_autoConnectCheck->isChecked();
    value.manualJogPercent = m_speedSlider->value();
    value.clampSerialPort = comboText(m_clampPort, value.clampSerialPort);
    value.clampSerialBaudRate = comboInt(m_clampBaud, value.clampSerialBaudRate);
    value.clampCanBitrate = comboInt(m_clampCanBitrate, value.clampCanBitrate);
    value.clampNodeId = m_clampNodeId->value();
    value.clampXMotorId = m_clampXId->value();
    value.clampYMotorId = m_clampYId->value();
    value.clampZMotorId = m_clampZId->value();
    value.clampXMotorSign = comboInt(m_clampXSign, value.clampXMotorSign);
    value.clampYMotorSign = comboInt(m_clampYSign, value.clampYMotorSign);
    value.clampZMotorSign = comboInt(m_clampZSign, value.clampZMotorSign);
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
    setComboText(m_imuPort, settings.imuSerialPort);
    setComboValue(m_imuBaud, settings.imuBaudRate);
    setComboValue(m_imuDivider, settings.imuOutputDivider);
    m_laserSerial->setText(settings.laserSerialNumber);
    setComboValue(m_cameraDevice, settings.usbCameraDeviceIndex);
    if (settings.usbCameraDeviceIndex < 0)
        m_cameraDevice->setCurrentIndex(-1);
    m_cameraFps->setValue(settings.usbCameraFps);
    m_cameraAutoConnect->setChecked(settings.usbCameraAutoConnect);
    m_cameraFlipHorizontal->setChecked(settings.usbCameraFlipHorizontal);
    m_cameraFlipVertical->setChecked(settings.usbCameraFlipVertical);
    m_autoConnectCheck->setChecked(settings.autoConnectOnStartup);
    m_speedSlider->setValue(settings.manualJogPercent);
    setComboText(m_clampPort, settings.clampSerialPort);
    setComboValue(m_clampBaud, settings.clampSerialBaudRate);
    setComboValue(m_clampCanBitrate, settings.clampCanBitrate);
    m_clampNodeId->setValue(settings.clampNodeId);
    m_clampXId->setValue(settings.clampXMotorId);
    m_clampYId->setValue(settings.clampYMotorId);
    m_clampZId->setValue(settings.clampZMotorId);
    setComboValue(m_clampXSign, settings.clampXMotorSign);
    setComboValue(m_clampYSign, settings.clampYMotorSign);
    setComboValue(m_clampZSign, settings.clampZMotorSign);
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
    const QString selectedImu = m_imuPort->currentData().toString();
    const QString selectedClamp = m_clampPort->currentData().toString();
    repopulate(m_leftPort, selectedLeft, m_settings.leftMotorSerialPort);
    repopulate(m_rightPort, selectedRight, m_settings.rightMotorSerialPort);
    repopulate(m_imuPort, selectedImu, m_settings.imuSerialPort);
    repopulate(m_clampPort, selectedClamp, m_settings.clampSerialPort);
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
    if (m_settings.leftMotorSerialPort.trimmed().isEmpty() ||
        m_settings.rightMotorSerialPort.trimmed().isEmpty()) {
        appendLog(QStringLiteral("底盘连接已跳过：左右轮串口配置不完整"));
    } else {
        connectDrive();
    }
    connectConfiguredImu();
    connectConfiguredLaser();
    connectConfiguredUsbCamera();
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
    saveSettings();
    if (m_settings.laserSerialNumber.trimmed().isEmpty()) {
        appendLog(QStringLiteral("激光相机连接已跳过：未配置序列号"));
        return;
    }
    if (m_sensorController)
        QMetaObject::invokeMethod(m_sensorController, "connectCamera",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, m_settings.laserSerialNumber));
}

void RobotControlPanel::connectConfiguredUsbCamera()
{
    m_settings = settingsFromUi();
    saveSettings();
    if (!m_settings.usbCameraAutoConnect) {
        appendLog(QStringLiteral("USB 摄像头连接已跳过：未勾选自动连接"));
        return;
    }
    if (m_settings.usbCameraDeviceIndex < 0) {
        appendLog(QStringLiteral("USB 摄像头连接已跳过：未配置设备"));
        return;
    }
    if (m_usbCameraController)
        QMetaObject::invokeMethod(m_usbCameraController, "connectCamera",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, m_settings.usbCameraDeviceIndex),
                                  Q_ARG(int, m_settings.usbCameraFps),
                                  Q_ARG(bool, m_settings.usbCameraFlipHorizontal),
                                  Q_ARG(bool, m_settings.usbCameraFlipVertical));
}

void RobotControlPanel::disconnectAllDevices()
{
    disconnectDrive();
    if (m_sensorController) {
        QMetaObject::invokeMethod(m_sensorController, "disconnectImu",
                                  Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_sensorController, "disconnectCamera",
                                  Qt::QueuedConnection);
    }
    if (m_usbCameraController)
        QMetaObject::invokeMethod(m_usbCameraController, "disconnectCamera",
                                  Qt::QueuedConnection);
    appendLog(QStringLiteral("已请求断开底盘、IMU、激光相机和 USB 摄像头"));
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
    flag = active;
    sendMotionCommand();
}

void RobotControlPanel::requestEnable(bool enabled)
{
    if (m_controller)
        QMetaObject::invokeMethod(m_controller, "requestEnable",
                                  Qt::QueuedConnection, Q_ARG(bool, enabled));
}

void RobotControlPanel::emergencyStop()
{
    m_forward = m_reverse = m_left = m_right = false;
    if (m_controller)
        QMetaObject::invokeMethod(m_controller, "emergencyStop",
                                  Qt::QueuedConnection);
}

void RobotControlPanel::stopMotion()
{
    m_forward = m_reverse = m_left = m_right = false;
    if (m_controller) {
        QMetaObject::invokeMethod(m_controller, "setInputCommand",
                                  Qt::QueuedConnection, Q_ARG(double, 0.0),
                                  Q_ARG(double, 0.0));
        QMetaObject::invokeMethod(m_controller, "requestEnable",
                                  Qt::QueuedConnection, Q_ARG(bool, false));
    }
}

void RobotControlPanel::sendMotionCommand()
{
    if (!m_controller)
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
    const bool canEnable = m_connected && state != crawling::DriveState::Enabled &&
                           state != crawling::DriveState::Arming &&
                           state != crawling::DriveState::EmergencyStop;
    m_enableButton->setEnabled(canEnable);
    appendLog(reason);
}

void RobotControlPanel::updateConnection(bool connected, const QString &message)
{
    m_connected = connected;
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
    m_imuText = QStringLiteral("姿态 R/P/Y %1 / %2 / %3°；角速度 %4 / %5 / %6°/s")
                    .arg(sample.rollRad * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.pitchRad * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.yawRad * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.gyroXRadps * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.gyroYRadps * kDegreesPerRadian, 0, 'f', 2)
                    .arg(sample.gyroZRadps * kDegreesPerRadian, 0, 'f', 2);
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
    QSignalBlocker blocker(m_laserDevice);
    m_laserDevice->clear();
    m_laserDevice->addItems(devices);
    for (int index = 0; index < m_laserDevice->count(); ++index) {
        if (m_laserDevice->itemText(index).section(QStringLiteral(" | "), 0, 0) == configured) {
            m_laserDevice->setCurrentIndex(index);
            break;
        }
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

void RobotControlPanel::updateUsbDevices(const QStringList &devices)
{
    const int configured = m_cameraDevice->currentIndex() >= 0
                               ? m_cameraDevice->currentData().toInt()
                               : m_settings.usbCameraDeviceIndex;
    QSignalBlocker blocker(m_cameraDevice);
    m_cameraDevice->clear();
    for (const QString &device : devices) {
        bool okay = false;
        const int index = device.section(QStringLiteral(" | "), 0, 0).toInt(&okay);
        if (okay)
            m_cameraDevice->addItem(device, index);
    }
    const int selected = m_cameraDevice->findData(configured);
    m_cameraDevice->setCurrentIndex(selected >= 0 ? selected : -1);
}

void RobotControlPanel::updateUsbConnection(bool connected, const QString &message)
{
    m_usbConnected = connected;
    m_usbStatus->setText(message.isEmpty() ? (connected ? QStringLiteral("已连接")
                                                        : QStringLiteral("未连接"))
                                           : message);
    m_usbConfigState->setText(m_usbStatus->text());
    if (!connected) {
        m_usbImage = QImage();
        m_usbView->setPixmap(QPixmap());
        m_usbView->setText(QStringLiteral("USB 摄像头未连接"));
    }
    updateInformationDialog();
}

void RobotControlPanel::appendLog(const QString &message)
{
    if (message.isEmpty())
        return;
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz  ")) + message;
    m_logLines.append(line);
    while (m_logLines.size() > 400)
        m_logLines.removeFirst();
    if (m_infoLog)
        m_infoLog->appendPlainText(line);
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

void RobotControlPanel::setUsbCameraImage(const QImage &image)
{
    if (image.isNull())
        return;
    m_usbImage = image;
    const QSize target = m_usbView->size().expandedTo(QSize(260, 160));
    m_usbView->setText(QString());
    m_usbView->setPixmap(QPixmap::fromImage(image).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    if (!m_infoDialog)
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
    updateInformationDialog();
    m_infoDialog->adjustSize();
    m_infoDialog->show();
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
    if (!m_usbImage.isNull())
        setUsbCameraImage(m_usbImage);
}
