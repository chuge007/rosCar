#include "jog_button_touch_filter.h"

#include <QTimer>
#include <QDebug>

namespace {
constexpr int kMinimumTouchJogMs = 150;
constexpr int kMaxRapidPressMs = 300;
constexpr int kLongPressDurationMs = 3000;
}

JogButtonTouchFilter::JogButtonTouchFilter(QPushButton *button, QObject *parent,
                                           int minimumTouchMs, int maxRapidPressMs)
    : QObject(parent)
    , m_button(button)
    , m_gestureActive(false)
    , m_rapidPressGuard(false)
    , m_releaseTimer(new QTimer(this))
    , m_rapidPressTimer(new QTimer(this))
    , m_lastReleaseTime(0)
    , m_minimumTouchMs(minimumTouchMs)
    , m_maxRapidPressMs(maxRapidPressMs)
{
    if (m_button) {
        m_button->setAttribute(Qt::WA_AcceptTouchEvents, true);
        m_button->setAutoRepeat(false);  // 禁用按钮自动重复
        connect(m_button, &QPushButton::pressed,
                this, &JogButtonTouchFilter::onButtonPressed);
        connect(m_button, &QPushButton::released,
                this, &JogButtonTouchFilter::onButtonReleased);
    }

    m_releaseTimer->setSingleShot(true);
    connect(m_releaseTimer, &QTimer::timeout,
            this, &JogButtonTouchFilter::finishGesture);
    
    m_rapidPressTimer->setSingleShot(true);
    connect(m_rapidPressTimer, &QTimer::timeout, this, [this]() {
        m_rapidPressGuard = false;
    });
}

void JogButtonTouchFilter::setMinimumTouchDuration(int ms)
{
    if (ms > 0 && ms < 5000) {
        m_minimumTouchMs = ms;
    }
}

void JogButtonTouchFilter::setMaxRapidPressDuration(int ms)
{
    if (ms > 0 && ms < 2000) {
        m_maxRapidPressMs = ms;
    }
}

qint64 JogButtonTouchFilter::currentPressDuration() const
{
    if (!m_gestureActive) {
        return 0;
    }
    return m_pressTimer.elapsed();
}

bool JogButtonTouchFilter::eventFilter(QObject *watched, QEvent *event)
{
    // 可以在这里添加额外的触摸事件处理
    if (watched == m_button && event->type() == QEvent::TouchBegin) {
        // 触摸开始事件的特殊处理
        qDebug() << "Touch begin detected";
    }
    return QObject::eventFilter(watched, event);
}

void JogButtonTouchFilter::onButtonPressed()
{
    // 停止所有待处理的定时器
    m_releaseTimer->stop();

    // 防抖：检查是否在快速重复点击期间
    if (m_rapidPressGuard) {
        qDebug() << "Rapid press detected, ignoring";
        return;
    }
    
    // 防止误触：检查与上次释放的时间间隔
    if (m_lastReleaseTime > 0) {
        const qint64 timeSinceLastRelease = m_pressTimer.elapsed() - m_lastReleaseTime;
        if (timeSinceLastRelease < 50) {  // 50ms内的重复触发视为误触
            qDebug() << "Too fast after last release:" << timeSinceLastRelease << "ms, ignoring";
            return;
        }
    }

    // 去重：如果已经在激活状态，忽略重复的pressed信号
    if (m_gestureActive) {
        qDebug() << "Gesture already active, ignoring duplicate pressed";
        return;
    }

    // 激活手势
    m_gestureActive = true;
    m_pressTimer.start();
    
    qDebug() << "Jog pressed - gesture started";
    emit jogPressed();
}

void JogButtonTouchFilter::onButtonReleased()
{
    // 如果没有激活的手势，忽略
    if (!m_gestureActive) {
        qDebug() << "No active gesture, ignoring released";
        return;
    }

    // 计算按压持续时间
    const qint64 elapsed = m_pressTimer.elapsed();
    
    qDebug() << "Jog released - duration:" << elapsed << "ms";
    
    // 检测长按
    if (elapsed >= kLongPressDurationMs) {
        emit longPressDetected(elapsed);
    }
    
    // 如果按压时间不足最小持续时间，延迟发送释放信号
    if (elapsed < m_minimumTouchMs) {
        const int delay = m_minimumTouchMs - static_cast<int>(elapsed);
        qDebug() << "Duration too short, delaying release by" << delay << "ms";
        m_releaseTimer->start(delay);
    } else {
        // 立即完成手势
        finishGesture();
    }
    
    // 记录释放时间
    m_lastReleaseTime = m_pressTimer.elapsed();
    
    // 启动防抖保护
    m_rapidPressGuard = true;
    m_rapidPressTimer->start(m_maxRapidPressMs);
}

void JogButtonTouchFilter::finishGesture()
{
    // 防止重复调用
    if (!m_gestureActive) {
        qDebug() << "Gesture already finished";
        return;
    }

    const qint64 totalDuration = m_pressTimer.elapsed();
    qDebug() << "Jog gesture finished - total duration:" << totalDuration << "ms";
    
    m_gestureActive = false;
    emit jogReleased();
}
