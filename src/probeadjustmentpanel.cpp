#include "probeadjustmentpanel.h"

#include "clamp_motor_controller.h"
#include "clamp_motor_widgets.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
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
    for (auto *button : {refresh, save, connectButton, disconnect})
        buttons->addWidget(button);
    configurationLayout->addLayout(buttons);
    layout->addWidget(configuration);

    auto *manual = new QGroupBox(QStringLiteral("探头夹子电机手动控制"));
    auto *manualLayout = new QVBoxLayout(manual);
    m_motorControls = new crawling::ClampMotorControlWidget(m_controller.data());
    manualLayout->addWidget(m_motorControls);
    layout->addWidget(manual);
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
        m_settings.clampMotors = m_controller->settings().clampMotors;
        m_motorSettings->setSettings(m_settings.clampMotors);
        m_motorControls->setEnabled(true);
    });
    connect(m_controller.data(), &crawling::ClampMotorController::statusChanged,
            m_status, &QLabel::setText);
    connect(m_controller.data(), &crawling::ClampMotorController::connectionChanged,
            this, [this](bool, const QString &message) { m_status->setText(message); });
    connect(m_controller.data(), &QObject::destroyed, this, [this] { setEnabled(false); });
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
