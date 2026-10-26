#include "productchrome.h"

#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>

ProductGlobalBar::ProductGlobalBar(QWidget *parent)
    : QToolBar(QStringLiteral("全局功能"), parent)
{
    setObjectName(QStringLiteral("productGlobalBar"));
    setMovable(false);
    setFloatable(false);
    setToolButtonStyle(Qt::ToolButtonTextOnly);

    const auto addNavigation = [this](const QString &text, auto signal) {
        QAction *action = addAction(text);
        connect(action, &QAction::triggered, this, signal);
    };
    addNavigation(QStringLiteral("▣ 项目管理"), &ProductGlobalBar::projectRequested);
    addNavigation(QStringLiteral("▤ 检测任务"), &ProductGlobalBar::taskRequested);
    addNavigation(QStringLiteral("▥ 数据分析"), &ProductGlobalBar::dataRequested);
    addNavigation(QStringLiteral("⇩ 报告导出"), &ProductGlobalBar::reportRequested);
    addNavigation(QStringLiteral("⚙ 系统设置"), &ProductGlobalBar::settingsRequested);

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addWidget(spacer);

    m_server = new QLabel(QStringLiteral("● 服务端：待连接"), this);
    m_ip = new QLabel(QStringLiteral("IP：127.0.0.1"), this);
    m_probe = new QLabel(QStringLiteral("● 探头：未知"), this);
    m_encoder = new QLabel(QStringLiteral("● 编码器：未知"), this);
    for (QLabel *label : {m_server, m_ip, m_probe, m_encoder}) {
        label->setProperty("role", "deviceState");
        addWidget(label);
    }
    addSeparator();
    m_board = new QLabel(QStringLiteral("● 板卡未确认"), this);
    m_board->setObjectName(QStringLiteral("globalBoardState"));
    addWidget(m_board);

    m_alarm = addAction(QStringLiteral("⚠ 报警 0"));
    if (QWidget *widget = widgetForAction(m_alarm))
        widget->setObjectName(QStringLiteral("globalAlarmButton"));
    connect(m_alarm, &QAction::triggered, this, &ProductGlobalBar::alarmRequested);

    auto *clock = new QLabel(this);
    clock->setObjectName(QStringLiteral("globalDateTime"));
    addWidget(clock);
    const auto updateClock = [clock] {
        clock->setText(QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
    };
    updateClock();
    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, updateClock);
    timer->start();
}

void ProductGlobalBar::setDeviceState(const QString &address, bool serviceConnected,
                                      bool hardwareOnline)
{
    m_ip->setText(QStringLiteral("IP：%1").arg(address));
    m_server->setText(serviceConnected ? QStringLiteral("● 服务端：已连接")
                                       : QStringLiteral("● 服务端：待连接"));
    m_server->setStyleSheet(serviceConnected ? QStringLiteral("color:#15803d")
                                             : QStringLiteral("color:#64748b"));
    m_probe->setText(hardwareOnline ? QStringLiteral("● 探头：数据正常")
                                    : QStringLiteral("● 探头：未知"));
    m_probe->setStyleSheet(hardwareOnline ? QStringLiteral("color:#15803d")
                                          : QStringLiteral("color:#64748b"));
    m_board->setText(hardwareOnline ? QStringLiteral("● 板卡在线")
                                    : (serviceConnected ? QStringLiteral("● 板卡未确认")
                                                        : QStringLiteral("● 板卡离线")));
    m_board->setStyleSheet(hardwareOnline ? QStringLiteral("color:#15803d;font-weight:700")
                                          : QStringLiteral("color:#c47b00;font-weight:700"));
}

void ProductGlobalBar::setEncoderKnown(bool known)
{
    m_encoder->setText(known ? QStringLiteral("● 编码器：在线")
                             : QStringLiteral("● 编码器：未知"));
    m_encoder->setStyleSheet(known ? QStringLiteral("color:#15803d")
                                   : QStringLiteral("color:#64748b"));
}

void ProductGlobalBar::setAlarmCount(int count)
{
    m_alarm->setText(QStringLiteral("⚠ 报警 %1").arg(qMax(0, count)));
}

ProductActionRail::ProductActionRail(QWidget *parent)
    : QToolBar(QStringLiteral("功能导航"), parent)
{
    setObjectName(QStringLiteral("productActionRail"));
    setMovable(false);
    setFloatable(false);
    setOrientation(Qt::Vertical);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    setMinimumWidth(86);

    struct Section { const char *label; int index; };
    constexpr Section sections[] = {
        {"连接", 0}, {"状态", 1}, {"超声", 2}, {"工件", 3},
        {"运动", 4}, {"扫描", 5}, {"数据", 6}
    };
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    for (const Section &section : sections) {
        QAction *action = addAction(QString::fromUtf8(section.label));
        action->setCheckable(true);
        action->setData(section.index);
        group->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, index = section.index] { emit sectionRequested(index); });
    }
    QAction *start = addAction(QStringLiteral("启动"));
    QAction *stop = addAction(QStringLiteral("停止"));
    addSeparator();
    QAction *close = addAction(QStringLiteral("关闭"));
    widgetForAction(start)->setObjectName(QStringLiteral("startScanButton"));
    widgetForAction(stop)->setObjectName(QStringLiteral("stopScanButton"));
    widgetForAction(close)->setObjectName(QStringLiteral("closeProgramButton"));
    connect(start, &QAction::triggered, this, &ProductActionRail::startRequested);
    connect(stop, &QAction::triggered, this, &ProductActionRail::stopRequested);
    connect(close, &QAction::triggered, this, &ProductActionRail::closeRequested);
    setCurrentSection(5);
}

void ProductActionRail::setCurrentSection(int index)
{
    for (QAction *action : actions()) {
        if (action->isCheckable() && action->data().isValid())
            action->setChecked(action->data().toInt() == index);
    }
}

ProductScanStatus::ProductScanStatus(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("productScanStatus"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 3, 14, 3);
    layout->setSpacing(12);
    m_state = new QLabel(QStringLiteral("◔ 扫查待机 · 可操作"), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFormat(QString());
    m_progress->setMinimumWidth(90);
    m_progress->setMaximumWidth(220);
    m_progress->setFixedHeight(9);
    m_progressText = new QLabel(QStringLiteral("0%"), this);
    m_position = new QLabel(QStringLiteral("当前位置：-- mm"), this);
    m_health = new QLabel(QStringLiteral("● 系统待机"), this);
    layout->addWidget(m_state);
    layout->addWidget(m_progress);
    layout->addWidget(m_progressText);
    layout->addWidget(m_position);
    layout->addStretch();
    layout->addWidget(m_health);
}

void ProductScanStatus::setStateText(const QString &text, bool error)
{
    m_state->setText(QStringLiteral("◔ %1").arg(text));
    m_state->setStyleSheet(error ? QStringLiteral("color:#c62828") : QString());
}

void ProductScanStatus::setProgress(int value)
{
    value = qBound(0, value, 100);
    m_progress->setValue(value);
    m_progressText->setText(QStringLiteral("%1%").arg(value));
}

void ProductScanStatus::setPosition(double millimeters)
{
    m_position->setText(QStringLiteral("当前位置：%1 mm").arg(millimeters, 0, 'f', 1));
}

void ProductScanStatus::setSystemHealthy(bool healthy)
{
    m_health->setText(healthy ? QStringLiteral("● 系统正常")
                              : QStringLiteral("● 系统待确认"));
    m_health->setStyleSheet(healthy ? QStringLiteral("color:#168a4b;font-weight:700")
                                    : QStringLiteral("color:#c47b00;font-weight:700"));
}
