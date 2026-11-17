#include "sliderinput.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QHash>
#include <QHBoxLayout>
#include <QPointer>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#include <cmath>

namespace {

// 滑轨刻度上限。范围除以步距超过它时按上限均分，拖动分辨率变粗，
// 但仍然落在步距的整数倍上，精确值可直接在数值框里输入。
constexpr int kMaximumSliderSteps = 3000;
constexpr int kMinimumSliderSteps = 24;
constexpr int kRangeWatchIntervalMs = 700;

struct Mapping {
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 1.0;
    bool logarithmic = false;
    int steps = 100;
};

Mapping buildMapping(double minimum, double maximum, double step)
{
    Mapping mapping;
    mapping.minimum = minimum;
    mapping.maximum = maximum > minimum ? maximum : minimum + 1.0;
    mapping.step = (step > 0.0 && std::isfinite(step)) ? step : 1.0;
    // 跨数量级的量（如传动比 0.001~1000）用线性刻度会把常用区间压成两三格。
    mapping.logarithmic = minimum > 0.0 && mapping.maximum / minimum >= 100.0;
    if (mapping.logarithmic) {
        mapping.steps = 1000;
    } else {
        const double rawSteps = (mapping.maximum - mapping.minimum) / mapping.step;
        mapping.steps = qBound(kMinimumSliderSteps,
                               qRound(rawSteps),
                               kMaximumSliderSteps);
    }
    return mapping;
}

int positionForValue(const Mapping &mapping, double value)
{
    const double bounded = qBound(mapping.minimum, value, mapping.maximum);
    if (mapping.logarithmic) {
        const double logMinimum = std::log(mapping.minimum);
        const double logMaximum = std::log(mapping.maximum);
        const double t = (std::log(bounded) - logMinimum) / (logMaximum - logMinimum);
        return qBound(0, qRound(t * mapping.steps), mapping.steps);
    }
    const double t = (bounded - mapping.minimum) / (mapping.maximum - mapping.minimum);
    return qBound(0, qRound(t * mapping.steps), mapping.steps);
}

double valueForPosition(const Mapping &mapping, int position)
{
    const double t = qBound(0.0, double(position) / qMax(1, mapping.steps), 1.0);
    double value = 0.0;
    if (mapping.logarithmic) {
        const double logMinimum = std::log(mapping.minimum);
        const double logMaximum = std::log(mapping.maximum);
        value = std::exp(logMinimum + t * (logMaximum - logMinimum));
    } else {
        value = mapping.minimum + t * (mapping.maximum - mapping.minimum);
    }
    // 对齐到步距，避免拖出 0.30000000000000004 这类值。
    if (mapping.step > 0.0)
        value = std::round(value / mapping.step) * mapping.step;
    return qBound(mapping.minimum, value, mapping.maximum);
}

class SpinSliderBinding final : public QObject
{
public:
    SpinSliderBinding(QAbstractSpinBox *spin, QSlider *slider)
        : QObject(slider), spin_(spin), slider_(slider) {}

    void start()
    {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_)) {
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    [this](double) { syncSliderFromSpin(); });
        } else if (auto *spin = qobject_cast<QSpinBox *>(spin_)) {
            connect(spin, &QSpinBox::valueChanged, this,
                    [this](int) { syncSliderFromSpin(); });
        }
        connect(slider_, &QSlider::valueChanged, this, [this](int position) {
            if (updating_)
                return;
            const double value = valueForPosition(mapping_, position);
            updating_ = true;
            if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_)) {
                spin->setValue(value);
            } else if (auto *spin = qobject_cast<QSpinBox *>(spin_)) {
                spin->setValue(qRound(value));
            }
            updating_ = false;
        });
        // setRange / setSingleStep 不是虚函数，无法挂钩子；低频自检即可覆盖。
        watchTimer_.setInterval(kRangeWatchIntervalMs);
        connect(&watchTimer_, &QTimer::timeout, this, [this] {
            if (!mappingDiffers())
                return;
            refreshMapping();
            syncSliderFromSpin();
        });
        watchTimer_.start();
        resync();
    }

    void resync()
    {
        refreshMapping();
        syncSliderFromSpin();
    }

    QWidget *rowWidget() const
    {
        return slider_ != nullptr ? slider_->parentWidget() : nullptr;
    }

private:
    double spinValue() const
    {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_))
            return spin->value();
        if (auto *spin = qobject_cast<QSpinBox *>(spin_))
            return double(spin->value());
        return 0.0;
    }

    bool mappingDiffers() const
    {
        const Mapping current = currentMapping();
        return !qFuzzyCompare(current.minimum + 1.0, mapping_.minimum + 1.0)
            || !qFuzzyCompare(current.maximum + 1.0, mapping_.maximum + 1.0)
            || !qFuzzyCompare(current.step + 1.0, mapping_.step + 1.0);
    }

    Mapping currentMapping() const
    {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(spin_))
            return buildMapping(spin->minimum(), spin->maximum(), spin->singleStep());
        if (auto *spin = qobject_cast<QSpinBox *>(spin_))
            return buildMapping(spin->minimum(), spin->maximum(), spin->singleStep());
        return {};
    }

    void refreshMapping()
    {
        mapping_ = currentMapping();
        const QSignalBlocker blocker(slider_);
        slider_->setRange(0, mapping_.steps);
        slider_->setPageStep(qMax(1, mapping_.steps / 10));
        slider_->setSingleStep(1);
    }

    void syncSliderFromSpin()
    {
        if (updating_)
            return;
        const int position = positionForValue(mapping_, spinValue());
        if (slider_->value() == position)
            return;
        const QSignalBlocker blocker(slider_);
        slider_->setValue(position);
    }

    QAbstractSpinBox *spin_ = nullptr;
    QSlider *slider_ = nullptr;
    Mapping mapping_;
    bool updating_ = false;
    QTimer watchTimer_;
};

QHash<QAbstractSpinBox *, QPointer<SpinSliderBinding>> &bindingRegistry()
{
    static QHash<QAbstractSpinBox *, QPointer<SpinSliderBinding>> registry;
    return registry;
}

QWidget *wrapSpin(QAbstractSpinBox *spin, QWidget *parent, int minimumSliderWidth)
{
    if (spin == nullptr)
        return nullptr;
    if (SpinSliderBinding *existing = bindingRegistry().value(spin).data()) {
        if (QWidget *row = existing->rowWidget())
            return row;
    }

    auto *row = new QWidget(parent != nullptr ? parent : spin->parentWidget());
    row->setObjectName(QStringLiteral("sliderSpinRow"));
    // 深色主题下未绘制背景的 QWidget 会露出窗口底色，整行发黑；这里显式铺白底，
    // 与全局样式表里的输入框保持一致，滑轨按浅色轨道绘制。
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setStyleSheet(QStringLiteral(
        "#sliderSpinRow { background:#ffffff; border:1px solid #b9c5d0;"
        " border-radius:4px; }"
        "#sliderSpinRow QSpinBox, #sliderSpinRow QDoubleSpinBox {"
        " border:none; background:transparent; }"
        "QSlider::groove:horizontal { height:6px; background:#dde5ee;"
        " border-radius:3px; }"
        "QSlider::sub-page:horizontal { background:#2684c7; border-radius:3px; }"
        "QSlider::handle:horizontal { width:16px; margin:-6px 0;"
        " border-radius:8px; background:#ffffff; border:2px solid #2684c7; }"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 1, 4, 1);
    layout->setSpacing(6);

    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(QStringLiteral("parameterSlider"));
    slider->setMinimumWidth(minimumSliderWidth);
    slider->setMinimumHeight(22);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(slider, 1);

    // 保留数值框便于精确输入，只去掉上下按钮，避免和滑轨重复占位。
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setMinimumWidth(84);
    spin->setMaximumWidth(124);
    spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(spin, 0);

    auto *binding = new SpinSliderBinding(spin, slider);
    binding->start();
    bindingRegistry().insert(spin, binding);
    QObject::connect(spin, &QObject::destroyed, slider, [spin] {
        bindingRegistry().remove(spin);
    });
    return row;
}

}  // namespace

QWidget *SliderInput::wrap(QSpinBox *spin, QWidget *parent, int minimumSliderWidth)
{
    return wrapSpin(spin, parent, minimumSliderWidth);
}

QWidget *SliderInput::wrap(QDoubleSpinBox *spin, QWidget *parent,
                           int minimumSliderWidth)
{
    return wrapSpin(spin, parent, minimumSliderWidth);
}

void SliderInput::sync(QAbstractSpinBox *spin)
{
    if (SpinSliderBinding *binding = bindingRegistry().value(spin).data())
        binding->resync();
}
