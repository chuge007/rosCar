#include "paramsliderfield.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <memory>

namespace pa1664::ui {
namespace {

// 滑轨最多分成的段数。范围再大也只是滑块每像素跨度变大，不会溢出 int，
// 也不会因为步数过多而失步；精确值仍然可以键盘输入。
constexpr int kMaximumSliderSteps = 200000;
constexpr int kMinimumSpinBoxWidth = 78;

struct SpinRange
{
    double minimum = 0.0;
    double maximum = 0.0;
    // 滑轨实际映射到的上限。数值框允许更大范围，但滑轨只覆盖
    // [minimum, sliderMaximum]；超出部分滑块停在最右端。
    double sliderMaximum = 0.0;
    int decimals = 0;
    bool integer = true;
};

SpinRange rangeOf(const QAbstractSpinBox *box)
{
    SpinRange range;
    if (const auto *spin = qobject_cast<const QSpinBox *>(box)) {
        range.minimum = spin->minimum();
        range.maximum = spin->maximum();
        range.integer = true;
    } else if (const auto *spin = qobject_cast<const QDoubleSpinBox *>(box)) {
        range.minimum = spin->minimum();
        range.maximum = spin->maximum();
        range.decimals = spin->decimals();
        range.integer = false;
    }
    range.sliderMaximum = range.maximum;
    const QVariant capped = box->property("paramSliderMaximum");
    if (capped.isValid()) {
        const double value = capped.toDouble();
        if (value > range.minimum && value < range.maximum)
            range.sliderMaximum = value;
    }
    return range;
}

int sliderSteps(const SpinRange &range)
{
    const double span = range.sliderMaximum - range.minimum;
    if (!(span > 0.0))
        return 0;
    if (range.integer)
        return int(std::min(span, double(kMaximumSliderSteps)));
    const double unit = std::pow(10.0, range.decimals);
    double steps = span * unit;
    if (steps < 1.0)
        steps = 1.0;
    if (steps > kMaximumSliderSteps)
        steps = kMaximumSliderSteps;
    return int(std::lround(steps));
}

double valueAt(const SpinRange &range, int steps, int position)
{
    if (steps <= 0)
        return range.minimum;
    const double span = range.sliderMaximum - range.minimum;
    return range.minimum + span * double(position) / double(steps);
}

int positionOf(const SpinRange &range, int steps, double value)
{
    if (steps <= 0)
        return 0;
    const double span = range.sliderMaximum - range.minimum;
    if (!(span > 0.0))
        return 0;
    // 数值超过滑轨上限时 ratio > 1，夹到最右端（滑块拉满），
    // 而不是把整条刻度按数值框的极值压缩。
    const double ratio = (value - range.minimum) / span;
    return int(std::lround(qBound(0.0, ratio, 1.0) * double(steps)));
}

double boxValue(const QAbstractSpinBox *box)
{
    if (const auto *spin = qobject_cast<const QDoubleSpinBox *>(box))
        return spin->value();
    if (const auto *spin = qobject_cast<const QSpinBox *>(box))
        return spin->value();
    return 0.0;
}

void setBoxValue(QAbstractSpinBox *box, double value)
{
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(box)) {
        // 走十进制字符串回写，避免 0.1 之类步进累加出的二进制尾差。
        const double rounded = QString::number(value, 'f', spin->decimals()).toDouble();
        if (!qFuzzyCompare(rounded + 1.0, spin->value() + 1.0))
            spin->setValue(rounded);
    } else if (auto *spin = qobject_cast<QSpinBox *>(box)) {
        spin->setValue(int(std::lround(value)));
    }
}

QSlider *sliderOf(const QAbstractSpinBox *box)
{
    const QVariant stored = box->property("paramSlider");
    return qobject_cast<QSlider *>(stored.value<QObject *>());
}

// 按数值框当前的范围与数值重设滑轨范围和位置（不改变数值框自身数值）。
void updateBounds(QAbstractSpinBox *box, QSlider *slider)
{
    if (!box || !slider)
        return;
    const SpinRange range = rangeOf(box);
    const int steps = sliderSteps(range);
    {
        const QSignalBlocker blocker(slider);
        slider->setRange(0, steps);
        slider->setPageStep(qMax(1, steps / 10));
        slider->setValue(positionOf(range, steps, boxValue(box)));
    }
    slider->setEnabled(box->isEnabled() && steps > 0);
    slider->setToolTip(steps > 0 ? box->toolTip()
                                 : QStringLiteral("该参数当前只有一个可选值"));
}

// 数值框被 setEnabled/setRange/setMaximum 改动后没法直接发信号通知滑轨，
// 用事件过滤器兜住：禁用状态同步、范围变化同步。
class SliderBindingFilter final : public QObject
{
public:
    SliderBindingFilter(QAbstractSpinBox *box, QSlider *slider)
        : QObject(box), m_box(box), m_slider(slider)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_box && m_slider) {
            switch (event->type()) {
            case QEvent::EnabledChange:
            case QEvent::LayoutRequest:
            case QEvent::Show:
                updateBounds(m_box, m_slider);
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QAbstractSpinBox *m_box;
    QPointer<QSlider> m_slider;
};

template <typename SpinBox>
QWidget *buildField(SpinBox *box, const QString &hint, QWidget *parent)
{
    if (!box)
        return nullptr;

    QWidget *host = parent ? parent : box->parentWidget();
    auto *container = new QWidget(host);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *row = new QWidget(container);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);

    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(QStringLiteral("paramSlider"));
    slider->setMinimumHeight(20);
    slider->setMaximumHeight(22);
    slider->setFocusPolicy(Qt::StrongFocus);

    box->setParent(row);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (box->maximumWidth() >= kMinimumSpinBoxWidth)
        box->setMinimumWidth(qMax(box->minimumWidth(), kMinimumSpinBoxWidth));
    box->setProperty("paramSlider", QVariant::fromValue<QObject *>(slider));

    rowLayout->addWidget(slider, 1);
    rowLayout->addWidget(box, 0);
    layout->addWidget(row);

    if (!hint.isEmpty()) {
        auto *label = new QLabel(hint, container);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color:#8a94a6; font-size:11px;"));
        layout->addWidget(label);
    }

    // guard 防止「滑轨 → 数值框 → 滑轨」互相触发形成回路。
    auto guard = std::make_shared<bool>(false);
    const QPointer<QSlider> sliderGuard(slider);
    const QPointer<QAbstractSpinBox> boxGuard(box);

    // 拖动滑轨：按当前范围换算成数值写回数值框，原有 valueChanged 链路照常触发。
    QObject::connect(slider, &QSlider::valueChanged, container,
                     [boxGuard, sliderGuard, guard](int position) {
        QAbstractSpinBox *spin = boxGuard.data();
        QSlider *control = sliderGuard.data();
        if (!spin || !control || *guard)
            return;
        const SpinRange range = rangeOf(spin);
        const int steps = sliderSteps(range);
        *guard = true;
        setBoxValue(spin, valueAt(range, steps, position));
        *guard = false;
    });

    // 程序化赋值（含被 QSignalBlocker 屏蔽的赋值）后把滑轨位置对齐。
    QObject::connect(box, &SpinBox::valueChanged, container,
                     [boxGuard, sliderGuard, guard](auto) {
        QAbstractSpinBox *spin = boxGuard.data();
        QSlider *control = sliderGuard.data();
        if (!spin || !control || *guard)
            return;
        *guard = true;
        updateBounds(spin, control);
        *guard = false;
    });

    // QSignalBlocker 只屏蔽数值框自身信号，内部编辑框的 textChanged 仍会发出：
    // 参数回读（reload/settingsToUi 用 QSignalBlocker 包裹 setValue）靠它同步滑轨。
    if (auto *edit = box->findChild<QLineEdit *>()) {
        QObject::connect(edit, &QLineEdit::textChanged, container,
                         [boxGuard, sliderGuard, guard](const QString &) {
            QAbstractSpinBox *spin = boxGuard.data();
            QSlider *control = sliderGuard.data();
            if (!spin || !control || *guard)
                return;
            const SpinRange range = rangeOf(spin);
            const int steps = sliderSteps(range);
            const QSignalBlocker blocker(control);
            control->setRange(0, steps);
            control->setValue(positionOf(range, steps, boxValue(spin)));
        });
    }

    box->installEventFilter(new SliderBindingFilter(box, slider));
    updateBounds(box, slider);
    return container;
}

} // namespace

QWidget *paramSliderField(QSpinBox *box, QWidget *parent)
{
    return buildField(box, QString(), parent);
}

QWidget *paramSliderField(QDoubleSpinBox *box, QWidget *parent)
{
    return buildField(box, QString(), parent);
}

QWidget *paramSliderField(QSpinBox *box, const QString &hint, QWidget *parent)
{
    return buildField(box, hint, parent);
}

QWidget *paramSliderField(QDoubleSpinBox *box, const QString &hint, QWidget *parent)
{
    return buildField(box, hint, parent);
}

void refreshParamSlider(QSpinBox *box)
{
    if (box)
        updateBounds(box, sliderOf(box));
}

void refreshParamSlider(QDoubleSpinBox *box)
{
    if (box)
        updateBounds(box, sliderOf(box));
}

void setParamSliderMaximum(QSpinBox *box, double maximum)
{
    if (!box)
        return;
    box->setProperty("paramSliderMaximum", maximum);
    updateBounds(box, sliderOf(box));
}

void setParamSliderMaximum(QDoubleSpinBox *box, double maximum)
{
    if (!box)
        return;
    box->setProperty("paramSliderMaximum", maximum);
    updateBounds(box, sliderOf(box));
}

} // namespace pa1664::ui
