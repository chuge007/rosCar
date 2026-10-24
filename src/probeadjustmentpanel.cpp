#include "probeadjustmentpanel.h"

#include "clamp_motor_controller.h"
#include "clamp_motor_widgets.h"
#include "robot_app_logger.h"
#include "services/remote_workbench_session.h"
#include "services/robot_json_codec.h"

using crawling::AppLogger;

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

ProbeAdjustmentPanel::ProbeAdjustmentPanel(QWidget *parent)
    : ProbeAdjustmentPanel(nullptr, nullptr, parent)
{
}

ProbeAdjustmentPanel::ProbeAdjustmentPanel(crawling::ClampMotorController *controller,
                                           QWidget *parent)
    : ProbeAdjustmentPanel(controller, nullptr, parent)
{
}

ProbeAdjustmentPanel::ProbeAdjustmentPanel(
    crawling::ClampMotorController *controller,
    pa1664::remote::RemoteWorkbenchSession *remoteSession, QWidget *parent)
    : QWidget(parent), m_controller(controller), m_remoteSession(remoteSession)
{
    if (!m_controller && !m_remoteSession)
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

    m_motorSettings->setRemoteMode(m_remoteSession != nullptr);
    m_motorControls->setRemoteMode(m_remoteSession != nullptr);
    connect(refresh, &QPushButton::clicked, this, &ProbeAdjustmentPanel::refreshPorts);
    connect(save, &QPushButton::clicked, this, &ProbeAdjustmentPanel::saveSettings);
    connect(connectButton, &QPushButton::clicked, this, [this] {
        if (!applySettings()) return;
        if (m_remoteSession) {
            const QJsonObject settings{
                {QStringLiteral("settings"),
                 pa1664::remote::driveSettingsToJson(m_settings)}};
            m_remoteSession->request(QStringLiteral("robot.clamp.settings.update"),
                                     settings);
            m_remoteSession->request(QStringLiteral("robot.clamp.connect"),
                                     settings);
            return;
        }
        if (m_controller) m_controller->connectDevice();
    });
    connect(disconnect, &QPushButton::clicked, this, [this] {
        if (m_remoteSession) {
            m_remoteSession->request(QStringLiteral("robot.clamp.disconnect"));
        } else if (m_controller) {
            m_controller->disconnectDevice();
        }
    });
    connect(m_motorSettings, &crawling::ClampMotorSettingsWidget::edited, this, [this] {
        if (m_remoteSession) {
            if (m_motorControls) m_motorControls->setEnabled(false);
            m_status->setText(QStringLiteral("配置已修改，请保存或连接后控制"));
            return;
        }
        if (m_controller) m_controller->stop();
        m_motorControls->setEnabled(false);
        m_status->setText(QStringLiteral("配置已修改，请保存或连接后控制"));
    });
    if (m_remoteSession) {
        connect(m_motorSettings,
                &crawling::ClampMotorSettingsWidget::positionRequested, this,
                [this](int axis, int index) {
                    m_remoteSession->request(
                        QStringLiteral("robot.clamp.requestPosition"),
                        QJsonObject{{QStringLiteral("axis"), axis},
                                    {QStringLiteral("index"), index}});
                });
        connect(m_motorSettings,
                &crawling::ClampMotorSettingsWidget::limitsChanged, this,
                &ProbeAdjustmentPanel::saveSettings);
        connect(m_motorControls,
                &crawling::ClampMotorControlWidget::motorSpeedRequested, this,
                [this](int axis, int index, int percent) {
                    m_remoteSession->request(
                        QStringLiteral("robot.clamp.setMotorSpeed"),
                        QJsonObject{{QStringLiteral("axis"), axis},
                                    {QStringLiteral("index"), index},
                                    {QStringLiteral("percent"), percent}});
                });
        connect(m_motorControls,
                &crawling::ClampMotorControlWidget::stopRequested, this, [this] {
                    m_remoteSession->request(QStringLiteral("robot.clamp.stop"));
                });
        connect(m_remoteSession,
                &pa1664::remote::RemoteWorkbenchSession::responseReceived, this,
                [this](const QString &operation, quint64, bool ok,
                       const QJsonObject &data, const QString &errorCode,
                       const QString &errorText) {
                    if (operation != QStringLiteral("robot.peripherals.serialPorts"))
                        return;
                    if (!ok) {
                        m_status->setText(
                            QStringLiteral("读取小车主机串口失败 [%1]：%2")
                                .arg(errorCode, errorText));
                        return;
                    }
                    QStringList ids;
                    QStringList labels;
                    QStringList tooltips;
                    const QJsonArray ports =
                        data.value(QStringLiteral("ports")).toArray();
                    for (const QJsonValue &value : ports) {
                        const QJsonObject port = value.toObject();
                        const QString id =
                            port.value(QStringLiteral("id")).toString().trimmed();
                        const QString kind =
                            port.value(QStringLiteral("kind")).toString().trimmed();
                        if (id.isEmpty() ||
                            (kind != QStringLiteral("serial") &&
                             kind != QStringLiteral("can")))
                            continue;
                        const QString label =
                            port.value(QStringLiteral("label")).toString(id).trimmed();
                        ids.append(id);
                        labels.append(label.isEmpty() ? id : label);
                        tooltips.append(
                            port.value(QStringLiteral("description")).toString());
                    }
                    m_motorSettings->setAvailablePorts(ids, labels, tooltips);
                    const QString pcanError =
                        data.value(QStringLiteral("pcanError")).toString().trimmed();
                    m_status->setText(
                        pcanError.isEmpty()
                            ? QStringLiteral("已刷新小车主机串口和 CAN 适配器")
                            : QStringLiteral("已刷新小车主机端口；PCAN：%1")
                                  .arg(pcanError));
                });
        connect(m_remoteSession,
                &pa1664::remote::RemoteWorkbenchSession::eventReceived, this,
                [this](const QString &name, const QJsonObject &data) {
                    const auto applyRemoteSnapshot = [this](
                        const QJsonObject &snapshot) {
                        const QJsonObject settingsJson =
                            snapshot.value(QStringLiteral("settings")).toObject();
                        if (!settingsJson.isEmpty()) {
                            crawling::DriveSettings remote = m_settings;
                            QString error;
                            if (pa1664::remote::driveSettingsFromJson(
                                    settingsJson, &remote, &error)) {
                                m_settings = remote;
                                m_motorSettings->setSettings(
                                    m_settings.clampMotors);
                                m_motorControls->setRemoteSettings(
                                    m_settings.clampMotors,
                                    m_settings.clampMotorSyncEnabled);
                                refreshSpacingMotors();
                            }
                        }
                        if (snapshot.contains(QStringLiteral("clampConnected"))) {
                            m_remoteClampConnected =
                                snapshot.value(QStringLiteral("clampConnected"))
                                    .toBool(false);
                            m_motorControls->setRemoteConnected(
                                m_remoteClampConnected);
                            m_status->setText(
                                snapshot.value(QStringLiteral("clampStatus"))
                                    .toString(m_remoteClampConnected
                                                  ? QStringLiteral("远程夹子电机已连接")
                                                  : QStringLiteral("远程夹子电机未连接")));
                        }
                    };
                    if (name == QStringLiteral("host.ready")) {
                        applyRemoteSnapshot(
                            data.value(QStringLiteral("peripherals")).toObject());
                        refreshPorts();
                        return;
                    }
                    if (name == QStringLiteral("host.peripheralsClaimed")) {
                        applyRemoteSnapshot(data);
                        return;
                    }
                    if (name == QStringLiteral("host.peripheralsReleased")) {
                        m_remoteClampConnected = false;
                        m_motorControls->setRemoteConnected(false);
                        m_status->setText(QStringLiteral("远程主机已释放夹子电机"));
                        return;
                    }
                    if (name == QStringLiteral("robot.clamp.position")) {
                        const int axis = data.value(QStringLiteral("axis")).toInt(-1);
                        const int index = data.value(QStringLiteral("index")).toInt(-1);
                        onPositionReceived(
                            axis, index,
                            data.value(QStringLiteral("degrees")).toDouble());
                        return;
                    }
                    if (name == QStringLiteral("robot.clamp.connection")) {
                        m_remoteClampConnected =
                            data.value(QStringLiteral("connected")).toBool(false);
                        m_motorControls->setRemoteConnected(
                            m_remoteClampConnected);
                        m_status->setText(
                            data.value(QStringLiteral("message")).toString(
                                m_remoteClampConnected
                                    ? QStringLiteral("远程夹子电机已连接")
                                    : QStringLiteral("远程夹子电机未连接")));
                        return;
                    }
                    if (name == QStringLiteral("robot.clamp.status")) {
                        m_status->setText(data.value(QStringLiteral("message"))
                                              .toString());
                        return;
                    }
                    if (name == QStringLiteral("robot.clamp.settingsChanged")) {
                        QJsonObject settingsJson =
                            data.value(QStringLiteral("settings")).toObject();
                        if (settingsJson.isEmpty())
                            settingsJson = data.value(QStringLiteral("clampMotors"))
                                               .toObject();
                        crawling::DriveSettings remote = m_settings;
                        QString error;
                        if (pa1664::remote::driveSettingsFromJson(
                                settingsJson, &remote, &error)) {
                            m_settings = remote;
                            m_motorSettings->setSettings(m_settings.clampMotors);
                            m_motorControls->setRemoteSettings(
                                m_settings.clampMotors,
                                m_settings.clampMotorSyncEnabled);
                            refreshSpacingMotors();
                        }
                    }
                });
    } else {
        connect(m_controller.data(),
                &crawling::ClampMotorController::settingsChanged, this, [this] {
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
    }
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
    if (!m_remoteSession && m_controller) m_controller->stop();
}

void ProbeAdjustmentPanel::refreshPorts()
{
    if (m_remoteSession) {
        if (!m_remoteSession->isReady()) {
            m_status->setText(QStringLiteral("远程小车主机链路未就绪"));
            return;
        }
        if (!m_remoteSession->hasCapability(
                QString::fromLatin1(pa1664::remote::kCapabilityRobotSensorsV1))) {
            m_status->setText(QStringLiteral("远程主机不支持硬件端口枚举"));
            return;
        }
        m_status->setText(QStringLiteral("正在读取小车主机端口..."));
        m_remoteSession->request(
            QStringLiteral("robot.peripherals.serialPorts"));
        return;
    }
    m_motorSettings->refreshPorts();
}

void ProbeAdjustmentPanel::loadSettings()
{
    QSettings store(crawling::DriveSettings::persistentFilePath(), QSettings::IniFormat);
    m_settings = crawling::DriveSettings::load(store);
    m_motorSettings->setSettings(m_settings.clampMotors);
    m_motorControls->setRemoteSettings(m_settings.clampMotors,
                                       m_settings.clampMotorSyncEnabled);
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
    if (m_remoteSession) {
        m_motorControls->setEnabled(true);
        m_status->setText(QStringLiteral("远程夹子控制待命"));
        return;
    }
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
    if (m_remoteSession) {
        m_motorControls->setRemoteSettings(m_settings.clampMotors,
                                           m_settings.clampMotorSyncEnabled);
        m_motorControls->setEnabled(true);
        return true;
    }
    if (!m_controller) return false;
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
    if (m_remoteSession) {
        m_settings.clampMotors = current.clampMotors;
        m_remoteSession->request(
            QStringLiteral("robot.clamp.settings.update"),
            QJsonObject{{QStringLiteral("settings"),
                         pa1664::remote::driveSettingsToJson(m_settings)}});
        m_motorSettings->setSettings(m_settings.clampMotors);
        m_motorControls->setRemoteSettings(m_settings.clampMotors,
                                           m_settings.clampMotorSyncEnabled);
        m_motorControls->setEnabled(true);
        m_status->setText(QStringLiteral("正在保存远程夹子配置"));
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
    if (supported && m_spacingZeroValid) {
        if (m_remoteSession) {
            m_remoteSession->request(
                QStringLiteral("robot.clamp.requestPosition"),
                QJsonObject{{QStringLiteral("axis"), m_spacingMotorAxis},
                            {QStringLiteral("index"), m_spacingMotorIndex}});
        } else if (m_controller) {
            m_controller->requestPosition(m_spacingMotorAxis,
                                          m_spacingMotorIndex);
        }
    }
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
    if (!motor || (!m_controller && !m_remoteSession)) {
        m_status->setText(QStringLiteral("请先选择控制间距的电机"));
        return;
    }
    if (!crawling::ClampMotorController::supportsPosition(*motor)) {
        m_status->setText(QStringLiteral("该电机协议不支持读取位置"));
        return;
    }
    m_resetZeroPending = true;
    m_status->setText(QStringLiteral("正在读取电机当前位置作为最小间距零点…"));
    if (m_remoteSession) {
        m_remoteSession->request(
            QStringLiteral("robot.clamp.requestPosition"),
            QJsonObject{{QStringLiteral("axis"), m_spacingMotorAxis},
                        {QStringLiteral("index"), m_spacingMotorIndex}});
    } else if (m_controller) {
        m_controller->requestPosition(m_spacingMotorAxis, m_spacingMotorIndex);
    }
    QTimer::singleShot(1500, this, [this] {
        if (!m_resetZeroPending) return;
        m_resetZeroPending = false;
        m_status->setText(QStringLiteral("重置失败：未收到电机位置反馈，请检查连接"));
    });
}

void ProbeAdjustmentPanel::onMoveToSpacing()
{
    const auto *motor = spacingMotor();
    if (!motor || (!m_controller && !m_remoteSession)) {
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
    const int positionKey = m_spacingMotorAxis * 1000 + m_spacingMotorIndex;
    const bool known = m_remoteSession
        ? m_remotePositions.contains(positionKey)
        : (m_controller && m_controller->hasPosition(m_spacingMotorAxis,
                                                     m_spacingMotorIndex));
    const double current = known
        ? (m_remoteSession ? m_remotePositions.value(positionKey)
                           : m_controller->lastPosition(m_spacingMotorAxis,
                                                        m_spacingMotorIndex))
        : 0.0;
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
    if (m_remoteSession) {
        m_remoteSession->request(
            QStringLiteral("robot.clamp.moveToPosition"),
            QJsonObject{{QStringLiteral("axis"), m_spacingMotorAxis},
                        {QStringLiteral("index"), m_spacingMotorIndex},
                        {QStringLiteral("degrees"), degrees}});
    } else if (m_controller) {
        m_controller->moveToPosition(m_spacingMotorAxis, m_spacingMotorIndex,
                                     degrees);
    }
}

void ProbeAdjustmentPanel::onPositionReceived(int axis, int index, double degrees)
{
    if (axis < 0 || axis >= 3 || index < 0) return;
    m_remotePositions.insert(axis * 1000 + index, degrees);
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
