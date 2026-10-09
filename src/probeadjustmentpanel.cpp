#include "probeadjustmentpanel.h"

#include "clamp_motor_controller.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

ProbeAdjustmentPanel::ProbeAdjustmentPanel(QWidget *parent) : QWidget(parent)
{
    m_controller = new crawling::ClampMotorController(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(10);

    auto *hint = new QLabel(QStringLiteral(
        "探头夹具手动微调。先确认串口和电机 ID，再使用 X/Y/Z 点动；紧急情况请点击全部停止。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#52606d"));
    layout->addWidget(hint);

    auto *connection = new QGroupBox(QStringLiteral("夹具控制器"));
    auto *form = new QFormLayout(connection);
    auto *portRow = new QHBoxLayout;
    m_communicationMode = new QComboBox;
    m_communicationMode->addItem(QStringLiteral("TT CANopen CAN"),
                                 QStringLiteral("tt_canopen"));
    m_communicationMode->addItem(QStringLiteral("MWD RS485"),
                                 QStringLiteral("mwd_rs485"));
    m_communicationMode->addItem(QStringLiteral("MWD CAN"),
                                 QStringLiteral("mwd_can"));
    form->addRow(QStringLiteral("通信方式"), m_communicationMode);
    m_port = new QComboBox;
    m_port->setEditable(true);
    auto *refresh = new QPushButton(QStringLiteral("刷新"));
    portRow->addWidget(m_port, 1);
    portRow->addWidget(refresh);
    form->addRow(QStringLiteral("串口"), portRow);
    m_baud = new QComboBox;
    for (int baud : {9600, 19200, 38400, 57600, 115200, 230400, 460800,
                     500000, 921600, 1000000, 1500000, 2000000, 2500000,
                     4000000})
        m_baud->addItem(QString::number(baud), baud);
    form->addRow(QStringLiteral("串口/适配器波特率"), m_baud);
    m_bitrate = new QComboBox;
    for (int bitrate : {100000, 125000, 250000, 500000, 800000, 1000000})
        m_bitrate->addItem(QString::number(bitrate), bitrate);
    m_nodeId = new QSpinBox;
    m_nodeId->setRange(0, 127);
    form->addRow(QStringLiteral("CAN 波特率（CAN 模式）"), m_bitrate);
    form->addRow(QStringLiteral("TT CANopen 节点 ID"), m_nodeId);
    auto *modeOptions = new QStackedWidget(connection);
    auto *ttCanOptions = new QLabel(
        QStringLiteral("TT CANopen：使用上方 CAN 波特率和 CANopen 节点 ID"),
        modeOptions);
    ttCanOptions->setWordWrap(true);
    auto *rs485Options = new QLabel(
        QStringLiteral("MWD RS485 Modbus RTU：速度模式写入 WorkMode=2、T_Velocity=6010"),
        modeOptions);
    rs485Options->setWordWrap(true);
    auto *mwdCanOptions = new QLabel(
        QStringLiteral("MWD CAN：使用上方 CAN 波特率，电机 ID 范围为 1-32"),
        modeOptions);
    mwdCanOptions->setWordWrap(true);
    modeOptions->addWidget(ttCanOptions);
    modeOptions->addWidget(rs485Options);
    modeOptions->addWidget(mwdCanOptions);
    form->addRow(QStringLiteral("模式参数"), modeOptions);
    const auto updateModeOptions = [modeOptions, this](int index) {
        modeOptions->setCurrentIndex(index);
        const int maximumMotorId = index == 0 ? 127 : 32;
        for (QSpinBox *box : {m_xId, m_yId, m_zId}) {
            box->setRange(1, maximumMotorId);
        }
    };
    connect(m_communicationMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            modeOptions, updateModeOptions);
    layout->addWidget(connection);

    auto *axes = new QGroupBox(QStringLiteral("探头手动微调"));
    auto *grid = new QGridLayout(axes);
    grid->addWidget(new QLabel(QStringLiteral("轴")), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("电机 ID")), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("负向")), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("正向")), 0, 3);
    m_xId = new QSpinBox; m_yId = new QSpinBox; m_zId = new QSpinBox;
    for (QSpinBox *id : {m_xId, m_yId, m_zId}) id->setRange(1, 127);
    updateModeOptions(m_communicationMode->currentIndex());
    const QStringList names = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    QSpinBox *ids[] = {m_xId, m_yId, m_zId};
    for (int row = 0; row < 3; ++row) {
        grid->addWidget(new QLabel(names[row]), row + 1, 0);
        grid->addWidget(ids[row], row + 1, 1);
        auto *minus = new QPushButton(QStringLiteral("按住 −"));
        auto *plus = new QPushButton(QStringLiteral("按住 ＋"));
        grid->addWidget(minus, row + 1, 2);
        grid->addWidget(plus, row + 1, 3);
        connect(minus, &QPushButton::pressed, this, [this, row] {
            applySettings();
            if (row == 0) m_controller->moveXNegative();
            else if (row == 1) m_controller->moveYNegative();
            else m_controller->moveZNegative();
        });
        connect(plus, &QPushButton::pressed, this, [this, row] {
            applySettings();
            if (row == 0) m_controller->moveXPositive();
            else if (row == 1) m_controller->moveYPositive();
            else m_controller->moveZPositive();
        });
        connect(minus, &QPushButton::released,
                m_controller, &crawling::ClampMotorController::stop);
        connect(plus, &QPushButton::released,
                m_controller, &crawling::ClampMotorController::stop);
    }
    auto *stop = new QPushButton(QStringLiteral("全部停止"));
    stop->setStyleSheet(QStringLiteral("font-weight:600;color:#b42318"));
    grid->addWidget(stop, 4, 0, 1, 4);
    layout->addWidget(axes);

    auto *save = new QPushButton(QStringLiteral("保存探头调节设置"));
    layout->addWidget(save);
    m_status = new QLabel(QStringLiteral("探头调节待命"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch();
    scroll->setWidget(body);

    connect(refresh, &QPushButton::clicked, this, &ProbeAdjustmentPanel::refreshPorts);
    connect(save, &QPushButton::clicked, this, &ProbeAdjustmentPanel::saveSettings);
    connect(stop, &QPushButton::clicked, m_controller, &crawling::ClampMotorController::stop);
    connect(m_controller, &crawling::ClampMotorController::statusChanged,
            m_status, &QLabel::setText);

    refreshPorts();
    loadSettings();
}

ProbeAdjustmentPanel::~ProbeAdjustmentPanel()
{
    m_controller->stop();
}

void ProbeAdjustmentPanel::refreshPorts()
{
    const QString current = m_port->currentText();
    m_port->clear();
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts())
        m_port->addItem(port.portName());
    if (!current.isEmpty()) {
        const int index = m_port->findText(current);
        if (index < 0) m_port->addItem(current);
        m_port->setCurrentText(current);
    }
}

void ProbeAdjustmentPanel::loadSettings()
{
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    m_settings = crawling::DriveSettings::load(store);
    m_communicationMode->setCurrentIndex(qMax(
        0, m_communicationMode->findData(
               m_settings.clampCommunicationMode ==
                       crawling::ClampCommunicationMode::TtCanopen
                   ? QStringLiteral("tt_canopen")
                   : m_settings.clampCommunicationMode ==
                         crawling::ClampCommunicationMode::MwdRs485
                         ? QStringLiteral("mwd_rs485")
                         : QStringLiteral("mwd_can"))));
    if (!m_settings.clampSerialPort.isEmpty()) {
        if (m_port->findText(m_settings.clampSerialPort) < 0)
            m_port->addItem(m_settings.clampSerialPort);
        m_port->setCurrentText(m_settings.clampSerialPort);
    }
    m_baud->setCurrentIndex(qMax(0, m_baud->findData(m_settings.clampSerialBaudRate)));
    m_bitrate->setCurrentIndex(qMax(0, m_bitrate->findData(m_settings.clampCanBitrate)));
    m_nodeId->setValue(m_settings.clampNodeId);
    m_xId->setValue(m_settings.clampXMotorId);
    m_yId->setValue(m_settings.clampYMotorId);
    m_zId->setValue(m_settings.clampZMotorId);
    applySettings();
}

void ProbeAdjustmentPanel::applySettings()
{
    const QString mode = m_communicationMode->currentData().toString();
    m_settings.clampCommunicationMode =
        mode == QStringLiteral("mwd_rs485")
            ? crawling::ClampCommunicationMode::MwdRs485
            : mode == QStringLiteral("mwd_can")
                  ? crawling::ClampCommunicationMode::MwdCan
                  : crawling::ClampCommunicationMode::TtCanopen;
    m_settings.clampSerialPort = m_port->currentText().trimmed();
    m_settings.clampSerialBaudRate = m_baud->currentData().toInt();
    m_settings.clampCanBitrate = m_bitrate->currentData().toInt();
    m_settings.clampNodeId = m_nodeId->value();
    m_settings.clampXMotorId = m_xId->value();
    m_settings.clampYMotorId = m_yId->value();
    m_settings.clampZMotorId = m_zId->value();
    m_controller->setSettings(m_settings);
}

void ProbeAdjustmentPanel::saveSettings()
{
    applySettings();
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    // 小车页和探头页共享同一份现场配置。保存探头参数时重新加载磁盘
    // 最新值，只覆盖夹具字段，避免把小车页刚保存的底盘参数写回旧值。
    crawling::DriveSettings latest = crawling::DriveSettings::load(store);
    latest.clampCommunicationMode = m_settings.clampCommunicationMode;
    latest.clampSerialPort = m_settings.clampSerialPort;
    latest.clampSerialBaudRate = m_settings.clampSerialBaudRate;
    latest.clampCanBitrate = m_settings.clampCanBitrate;
    latest.clampNodeId = m_settings.clampNodeId;
    latest.clampXMotorId = m_settings.clampXMotorId;
    latest.clampYMotorId = m_settings.clampYMotorId;
    latest.clampZMotorId = m_settings.clampZMotorId;
    latest.clampXMotorSign = m_settings.clampXMotorSign;
    latest.clampYMotorSign = m_settings.clampYMotorSign;
    latest.clampZMotorSign = m_settings.clampZMotorSign;
    latest.save(store);
    m_settings = latest;
    store.sync();
    m_status->setText(QStringLiteral("探头调节设置已保存"));
}
