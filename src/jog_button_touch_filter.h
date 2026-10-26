#pragma once

#include <QObject>
#include <QEvent>
#include <QPushButton>
#include <QTouchEvent>

/**
 * @brief 触摸事件过滤器 - 专门用于方向控制按钮
 *
 * 解决Windows触摸屏上QPushButton的pressed/released信号几乎同时触发的问题。
 * 该过滤器直接捕获TouchBegin和TouchEnd事件,确保"按住"操作能正常工作。
 *
 * 兼容性:
 * - 鼠标: 正常工作(不拦截鼠标事件,保持原有pressed/released信号)
 * - 触摸屏: 捕获Touch事件,手动触发pressed/released信号
 */
class JogButtonTouchFilter : public QObject
{
    Q_OBJECT
public:
    explicit JogButtonTouchFilter(QPushButton *button, QObject *parent = nullptr)
        : QObject(parent)
        , m_button(button)
        , m_touchActive(false)
    {
        if (m_button) {
            m_button->setAttribute(Qt::WA_AcceptTouchEvents, true);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_button)
            return QObject::eventFilter(watched, event);

        switch (event->type()) {
        case QEvent::TouchBegin: {
            // 触摸开始 - 模拟按下
            m_touchActive = true;
            // 手动触发pressed信号
            QMetaObject::invokeMethod(m_button, "pressed", Qt::DirectConnection);
            event->accept();
            return true;  // 阻止事件继续传播,避免合成鼠标事件干扰
        }
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            // 触摸结束/取消 - 模拟释放
            if (m_touchActive) {
                m_touchActive = false;
                // 手动触发released信号
                QMetaObject::invokeMethod(m_button, "released", Qt::DirectConnection);
                event->accept();
                return true;  // 阻止事件继续传播
            }
            break;
        }
        case QEvent::TouchUpdate: {
            // 触摸移动 - 如果移出按钮范围,视为取消
            auto *touchEvent = static_cast<QTouchEvent*>(event);
            if (m_touchActive && !touchEvent->touchPoints().isEmpty()) {
                const QTouchEvent::TouchPoint &point = touchEvent->touchPoints().first();
                const QRect buttonRect = m_button->rect();
                const QPoint localPos = m_button->mapFromGlobal(point.globalPosition().toPoint());

                if (!buttonRect.contains(localPos)) {
                    // 触摸点移出按钮 - 触发释放
                    m_touchActive = false;
                    QMetaObject::invokeMethod(m_button, "released", Qt::DirectConnection);
                }
            }
            event->accept();
            return true;
        }
        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QPushButton *m_button;
    bool m_touchActive;
};
