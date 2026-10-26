#include "probeadjustmentpanel.h"

#include "clamp_motor_controller.h"
#include "clamp_motor_widgets.h"
#include "robot_app_logger.h"

using crawling::AppLogger;

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

ProbeAdjustmentPanel::ProbeAdjustmentPanel(QWidget *parent)
    : ProbeAdjustmentPanel(nullptr, parent)
{
}

ProbeAdjustmentPanel::ProbeAdjustmentPanel(crawling::ClampMotorController *controller,
                                           QWidget *parent)
    : QWidget(parent), m_controller(controller)
{
    if (!m_controller)
        m_controller = new crawling::ClampMotorController(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    auto *hint = new QLabel(QStringLiteral(
        "按轴配置每台夹子电机，保存后分别点动。与小车页共用配置；松开按钮或滑块即停止该电机。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *configuration = new QGroupBox(QStringLiteral("夹子电机配置"));
    auto *configurationLayout = new QVBoxLayout(configuration);
    m_motorSettings = new crawling::ClampMotorSettingsWidget;
    configurationLayout->addWidget(m_motorSettings);
    auto *buttons = new QHBoxLayout;
    auto *refresh = new QPushButton(QStringLiteral("刷新串口"));
    auto *save = new QPushButton(QStringLiteral("保存夹子配置"));
    auto *connectButton = new QPushButton(QStringLiteral("连接夹子电机"));
    auto *disconnect = new QPushButton(QStringLiteral("断开"));
    for (auto *button : {refresh, save})
        buttons->addWidget(button);
    configurationLayout->addLayout(buttons);
    configuration->hide();

    auto *manual = new QGroupBox(QStringLiteral("探头夹子电机手动控制"));
    auto *manualLayout = new QVBoxLayout(manual);
    m_motorControls = new crawling::ClampMotorControlWidget(m_controller.data());
    auto *connectionRow = new QHBoxLayout;
    connectionRow->addWidget(connectButton);
    connectionRow->addWidget(disconnect);
    connectionRow->addStretch();
    manualLayout->addLayout(connectionRow);
    manualLayout->addWidget(m_motorControls);
    layout->addWidget(manual);

    auto *spacing = new QGroupBox(QStringLiteral("探头间距控制"));
    auto *spacingLayout = new QVBoxLayout(spacing);
    auto *motorRow = new QHBoxLayout;
    motorRow->addWidget(new QLabel(QStringLiteral("控制电机：")));
    m_spacingMotorCombo = new QComboBox;
    motorRow->addWidget(m_spacingMotorCombo, 1);
    spacingLayout->addLayout(motorRow);
    auto *infoRow = new QHBoxLayout;
    m_spacingGearLabel = new QLabel(QStringLiteral("传动比: --"));
    m_spacingProtocolLabel = new QLabel(QStringLiteral("协议: --"));
    infoRow->addWidget(m_spacingGearLabel);
    infoRow->addWidget(m_spacingProtocolLabel);
    infoRow->addStretch();
    spacingLayout->addLayout(infoRow);
    auto *minRow = new QHBoxLayout;
    minRow->addWidget(new QLabel(QStringLiteral("最小间距(mm)：")));
    m_minSpacingSpin = new QDoubleSpinBox;
    m_minSpacingSpin->setRange(0, 1000);
    m_minSpacingSpin->setDecimals(2);
    m_minSpacingSpin->setValue(10.0);
    minRow->addWidget(m_minSpacingSpin);
    m_resetZeroButton = new QPushButton(QStringLiteral("重置零点"));
    minRow->addWidget(m_resetZeroButton);
    minRow->addStretch();
    spacingLayout->addLayout(minRow);
    auto *targetRow = new QHBoxLayout;
    targetRow->addWidget(new QLabel(QStringLiteral("目标间距(mm)：")));
    m_targetSpacingSpin = new QDoubleSpinBox;
    m_targetSpacingSpin->setRange(0, 1000);
    m_targetSpacingSpin->setDecimals(2);
    m_targetSpacingSpin->setValue(50.0);
    targetRow->addWidget(m_targetSpacingSpin);
    m_moveToSpacingButton = new QPushButton(QStringLiteral("移动到间距"));
    targetRow->addWidget(m_moveToSpacingButton);
    targetRow->addStretch();
    spacingLayout->addLayout(targetRow);
    auto *currentRow = new QHBoxLayout;
    currentRow->addWidget(new QLabel(QStringLiteral("当前间距：")));
    m_currentSpacingLabel = new QLabel(QStringLiteral("--"));
    currentRow->addWidget(m_currentSpacingLabel, 1);
    spacingLayout->addLayout(currentRow);
    layout->addWidget(spacing);

    m_status = new QLabel(QStringLiteral("探头调节待命"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch();
    scroll->setWidget(body);

    connect(refresh, &QPushButton::clicked, this, &ProbeAdjustmentPanel::refreshPorts);
    connect(save, &QPushButton::clicked, this, &ProbeAdjustmentPanel::saveSettings);
    connect(connectButton, &QPushButton::clicked, this, [this] {
        if (applySettings()) m_controller->connectDevice();
    });
    connect(disconnect, &QPushButton::clicked,
            m_controller.data(), &crawling::ClampMotorController::disconnectDevice);
    connect(m_motorSettings, &crawling::ClampMotorSettingsWidget::edited, this, [this] {
        m_controller->stop();
        m_motorControls->setEnabled(false);
        m_status->setText(QStringLiteral("配置已修改，请保存或连接后控制"));
    });
    connect(m_controller.data(), &crawling::ClampMotorController::settingsChanged, this, [this] {
        const crawling::DriveSettings snapshot = m_controller->settings();
        m_settings.clampMotors = snapshot.clampMotors;
        m_motorSettings->setSettings(m_settings.clampMotors);
        m_motorControls->setEnabled(true);
        refreshSpacingMotors();
    });
    connect(m_controller.data(), &crawling::ClampMotorController::statusChanged,
            m_status, &QLabel::setText);
    connect(m_controller.data(), &crawling::ClampMotorController::connectionChanged,
            this, [this](bool, const QString &message) { m_status->setText(message); });
    connect(m_controller.data(), &QObject::destroyed, this, [this] { setEnabled(false); });
    connect(m_controller.data(), &crawling::ClampMotorController::positionReceived,
            this, &ProbeAdjustmentPanel::onPositionReceived);
    connect(m_spacingMotorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProbeAdjustmentPanel::onSpacingMotorChanged);
    connect(m_resetZeroButton, &QPushButton::clicked, this, &ProbeAdjustmentPanel::onResetZero);
    connect(m_moveToSpacingButton, &QPushButton::clicked, this, &ProbeAdjustmentPanel::onMoveToSpacing);
    connect(m_minSpacingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this] { saveSpacingSettings(); });
    connect(m_targetSpacingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this] { saveSpacingSettings(); });
    loadSettings();
}

ProbeAdjustmentPanel::~ProbeAdjustmentPanel()
{
    if (m_controller) m_controller->stop();
}

void ProbeAdjustmentPanel::refreshPorts()
{
    m_motorSettings->refreshPorts();
}

void ProbeAdjustmentPanel::loadSettings()
{
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    m_settings = crawling::DriveSettings::load(store);
    m_motorSettings->setSettings(m_settings.clampMotors);
    {
        QSettings spacing(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
        spacing.beginGroup(QStringLiteral("probeSpacing"));
        const QSignalBlocker minBlocker(m_minSpacingSpin);
        const QSignalBlocker targetBlocker(m_targetSpacingSpin);
        m_minSpacingSpin->setValue(spacing.value(QStringLiteral("minSpacingMm"), 10.0).toDouble());
        m_targetSpacingSpin->setValue(spacing.value(QStringLiteral("targetMm"), 50.0).toDouble());
        spacing.endGroup();
    }
    refreshSpacingMotors();
    applySettings();
}

bool ProbeAdjustmentPanel::applySettings()
{
    auto current = m_settings;
    current.clampMotors = m_motorSettings->settings();
    const QString error = current.clampValidationError();
    if (!error.isEmpty()) {
        m_status->setText(error);
        return false;
    }
    m_settings.clampMotors = current.clampMotors;
    m_controller->setSettings(current);
    m_motorControls->setEnabled(true);
    return true;
}

void ProbeAdjustmentPanel::saveSettings()
{
    auto current = m_settings;
    current.clampMotors = m_motorSettings->settings();
    const QString error = current.clampValidationError();
    if (!error.isEmpty()) {
        m_status->setText(error);
        return;
    }
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    // Reload before merging so this page never overwrites chassis/sensor settings.
    auto latest = crawling::DriveSettings::load(store);
    latest.clampMotors = current.clampMotors;
    latest.save(store);
    store.sync();
    if (store.status() != QSettings::NoError) {
        m_status->setText(QStringLiteral("夹子配置保存失败"));
        return;
    }
    m_settings = latest;
    applySettings();
    m_status->setText(QStringLiteral("夹子配置已保存，两个页面已同步"));
}

const crawling::ClampMotorSettings *ProbeAdjustmentPanel::spacingMotor() const
{
    if (m_spacingMotorAxis < 0 || m_spacingMotorAxis >= 3) return nullptr;
    const auto &motors = m_settings.clampMotors[m_spacingMotorAxis];
    if (m_spacingMotorIndex < 0 || m_spacingMotorIndex >= motors.size()) return nullptr;
    return &motors[m_spacingMotorIndex];
}

void ProbeAdjustmentPanel::refreshSpacingMotors()
{
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    const int savedKey = store.value(QStringLiteral("probeSpacing/motorKey"), -1).toInt();
    const int wantedKey = m_spacingMotorAxis >= 0
        ? m_spacingMotorAxis * 1000 + m_spacingMotorIndex : savedKey;
    m_refreshingSpacingMotors = true;
    {
        const QSignalBlocker blocker(m_spacingMotorCombo);
        m_spacingMotorCombo->clear();
        static const char *axisNames[] = {"X", "Y", "Z"};
        for (int axis = 0; axis < 3; ++axis) {
            const auto &motors = m_settings.clampMotors[axis];
            for (int index = 0; index < motors.size(); ++index) {
                const QString alias = motors[index].alias.trimmed();
                m_spacingMotorCombo->addItem(
                    alias.isEmpty() ? QStringLiteral("%1%2").arg(QLatin1String(axisNames[axis])).arg(index + 1)
                                    : alias,
                    axis * 1000 + index);
            }
        }
        const int found = m_spacingMotorCombo->findData(wantedKey);
        m_spacingMotorCombo->setCurrentIndex(found >= 0 ? found : (m_spacingMotorCombo->count() ? 0 : -1));
    }
    m_refreshingSpacingMotors = false;
    onSpacingMotorChanged(m_spacingMotorCombo->currentIndex());
}

void ProbeAdjustmentPanel::onSpacingMotorChanged(int index)
{
    m_resetZeroPending = false;
    const int key = index >= 0 ? m_spacingMotorCombo->itemData(index).toInt() : -1;
    m_spacingMotorAxis = key >= 0 ? key / 1000 : -1;
    m_spacingMotorIndex = key >= 0 ? key % 1000 : -1;
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    const QString prefix = QStringLiteral("probeSpacing/motor_%1/").arg(key);
    m_spacingZeroDegrees = store.value(prefix + QStringLiteral("zeroDegrees"), 0.0).toDouble();
    m_spacingZeroValid = key >= 0 && store.value(prefix + QStringLiteral("zeroValid"), false).toBool();

    const auto *motor = spacingMotor();
    const bool supported = motor && crawling::ClampMotorController::supportsPosition(*motor);
    m_spacingGearLabel->setText(motor
        ? QStringLiteral("传动比: %1 mm/圈").arg(motor->gearRatio, 0, 'f', 3)
        : QStringLiteral("传动比: --"));
    m_spacingProtocolLabel->setText(motor
        ? QStringLiteral("协议: %1%2").arg(crawling::clampModeName(motor->communicationMode),
              supported ? QString() : QStringLiteral("（不支持位置控制）"))
        : QStringLiteral("协议: --"));
    m_resetZeroButton->setEnabled(supported);
    m_moveToSpacingButton->setEnabled(supported);
    m_currentSpacingLabel->setText(m_spacingZeroValid ? QStringLiteral("--（等待位置反馈）")
                                                      : QStringLiteral("未设置零点，请先重置"));
    if (!m_refreshingSpacingMotors) saveSpacingSettings();
    if (supported && m_spacingZeroValid && m_controller)
        m_controller->requestPosition(m_spacingMotorAxis, m_spacingMotorIndex);
}

void ProbeAdjustmentPanel::saveSpacingSettings()
{
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    store.beginGroup(QStringLiteral("probeSpacing"));
    if (m_spacingMotorAxis >= 0) {
        const int key = m_spacingMotorAxis * 1000 + m_spacingMotorIndex;
        store.setValue(QStringLiteral("motorKey"), key);
        const QString prefix = QStringLiteral("motor_%1/").arg(key);
        store.setValue(prefix + QStringLiteral("zeroDegrees"), m_spacingZeroDegrees);
        store.setValue(prefix + QStringLiteral("zeroValid"), m_spacingZeroValid);
    }
    store.setValue(QStringLiteral("minSpacingMm"), m_minSpacingSpin->value());
    store.setValue(QStringLiteral("targetMm"), m_targetSpacingSpin->value());
    store.endGroup();
    store.sync();
}

void ProbeAdjustmentPanel::onResetZero()
{
    const auto *motor = spacingMotor();
    if (!motor || !m_controller) {
        m_status->setText(QStringLiteral("请先选择控制间距的电机"));
        return;
    }
    if (!crawling::ClampMotorController::supportsPosition(*motor)) {
        m_status->setText(QStringLiteral("该电机协议不支持读取位置"));
        return;
    }
    m_resetZeroPending = true;
    m_status->setText(QStringLiteral("正在读取电机当前位置作为最小间距零点…"));
    m_controller->requestPosition(m_spacingMotorAxis, m_spacingMotorIndex);
    QTimer::singleShot(1500, this, [this] {
        if (!m_resetZeroPending) return;
        m_resetZeroPending = false;
        m_status->setText(QStringLiteral("重置失败：未收到电机位置反馈，请检查连接"));
    });
}

void ProbeAdjustmentPanel::onMoveToSpacing()
{
    const auto *motor = spacingMotor();
    if (!motor || !m_controller) {
        m_status->setText(QStringLiteral("请先选择控制间距的电机"));
        return;
    }
    if (!crawling::ClampMotorController::supportsPosition(*motor)) {
        m_status->setText(QStringLiteral("该电机协议不支持位置控制"));
        return;
    }
    if (!m_spacingZeroValid) {
        m_status->setText(QStringLiteral("尚未设置零点：请将探头移到最小间距后点击“重置零点”"));
        return;
    }
    if (motor->gearRatio <= 0.0) {
        m_status->setText(QStringLiteral("传动比必须大于 0，请在夹子电机配置中设置"));
        return;
    }
    const double minimum = m_minSpacingSpin->value();
    const double target = m_targetSpacingSpin->value();
    if (target < minimum) {
        m_status->setText(QStringLiteral("目标间距 %1 mm 小于最小间距 %2 mm")
                              .arg(target, 0, 'f', 2).arg(minimum, 0, 'f', 2));
        return;
    }
    const double sign = motor->sign < 0 ? -1.0 : 1.0;
    const double degrees = m_spacingZeroDegrees + sign * (target - minimum) / motor->gearRatio * 360.0;
    saveSpacingSettings();
    const bool known = m_controller->hasPosition(m_spacingMotorAxis, m_spacingMotorIndex);
    const double current = known ? m_controller->lastPosition(m_spacingMotorAxis, m_spacingMotorIndex) : 0.0;
    AppLogger::write(QStringLiteral("PROBE.SPACING"),
        QStringLiteral("event=move_request axis=%1 index=%2 min_mm=%3 target_mm=%4 gear_mm_per_rev=%5 "
                       "sign=%6 zero_deg=%7 target_deg=%8 current_deg=%9")
            .arg(m_spacingMotorAxis).arg(m_spacingMotorIndex + 1)
            .arg(minimum, 0, 'f', 3).arg(target, 0, 'f', 3).arg(motor->gearRatio, 0, 'f', 3)
            .arg(motor->sign).arg(m_spacingZeroDegrees, 0, 'f', 2).arg(degrees, 0, 'f', 2)
            .arg(known ? QString::number(current, 'f', 2) : QStringLiteral("unknown")));
    if (known && qAbs(degrees - current) < 1.0) {
        m_status->setText(QStringLiteral("已在目标间距 %1 mm（电机 %2°），无需移动")
                              .arg(target, 0, 'f', 2).arg(current, 0, 'f', 2));
        return;
    }
    m_status->setText(QStringLiteral("正在移动到间距 %1 mm：电机需转动 %2°（目标 %3°）")
                          .arg(target, 0, 'f', 2)
                          .arg(known ? degrees - current : degrees - m_spacingZeroDegrees, 0, 'f', 1)
                          .arg(degrees, 0, 'f', 2));
    m_controller->moveToPosition(m_spacingMotorAxis, m_spacingMotorIndex, degrees);
}

void ProbeAdjustmentPanel::onPositionReceived(int axis, int index, double degrees)
{
    if (axis != m_spacingMotorAxis || index != m_spacingMotorIndex) return;
    const auto *motor = spacingMotor();
    if (!motor) return;
    if (m_resetZeroPending) {
        m_resetZeroPending = false;
        m_spacingZeroDegrees = degrees;
        m_spacingZeroValid = true;
        saveSpacingSettings();
        m_status->setText(QStringLiteral("已将当前位置 %1° 记录为最小间距 %2 mm")
                              .arg(degrees, 0, 'f', 2).arg(m_minSpacingSpin->value(), 0, 'f', 2));
    }
    if (!m_spacingZeroValid || motor->gearRatio <= 0.0) return;
    const double sign = motor->sign < 0 ? -1.0 : 1.0;
    const double spacing = m_minSpacingSpin->value()
        + sign * (degrees - m_spacingZeroDegrees) / 360.0 * motor->gearRatio;
    m_currentSpacingLabel->setText(QStringLiteral("%1 mm（电机 %2°）")
                                       .arg(spacing, 0, 'f', 2).arg(degrees, 0, 'f', 2));
}
