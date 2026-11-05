#pragma once

#include <QObject>
#include <QEvent>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPushButton>
#include <QTouchEvent>

class QTimer;

/**
 * @brief 触摸事件过滤器 - 专门用于方向控制按钮
 *
 * 解决Windows触摸屏上QPushButton的pressed/released信号几乎同时触发的问题。
 * 该过滤器监听按钮自己的pressed/released信号，做去重和最小持续时间保证。
 *
 * 工作原理:
 * - 不拦截任何事件，让QPushButton的UI状态正常工作
 * - 连接按钮的pressed/released信号
 * - 在pressed时发出jogPressed信号
 * - 在released时检查持续时间，如果不足150ms则延迟发出jogReleased信号
 * - 去重：连续多次pressed只发出一次jogPressed
 */
class JogButtonTouchFilter : public QObject
{
    Q_OBJECT
public:
    explicit JogButtonTouchFilter(QPushButton *button, QObject *parent = nullptr,
                                  int minimumTouchMs = 150, int maxRapidPressMs = 300);
    
    // 可配置的参数
    void setMinimumTouchDuration(int ms);
    void setMaxRapidPressDuration(int ms);
    int minimumTouchDuration() const { return m_minimumTouchMs; }
    
    // 状态查询
    bool isActive() const { return m_gestureActive; }
    qint64 currentPressDuration() const;

signals:
    void jogPressed();
    void jogReleased();
    void longPressDetected(qint64 duration);  // 长按检测（可选）

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onButtonPressed();
    void onButtonReleased();
    void finishGesture();

private:
    QPushButton *m_button;
    bool m_gestureActive;
    bool m_rapidPressGuard;  // 防止快速连续点击
    QElapsedTimer m_pressTimer;
    QTimer *m_releaseTimer;
    QTimer *m_rapidPressTimer;  // 防抖定时器
    qint64 m_lastReleaseTime;  // 上次释放时间戳
    
    int m_minimumTouchMs;
    int m_maxRapidPressMs;
};
