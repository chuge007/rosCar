#pragma once

class QAbstractSpinBox;
class QDoubleSpinBox;
class QSpinBox;
class QWidget;

// 把已有的数字输入框包成「横向滑轨 + 数值框」，返回可直接放进布局的容器。
// 数值框本身仍可键盘输入与滚轮调整，value()/setValue()/setRange()/suffix 等调用点
// 完全不需要改动；整数框用整数刻度，浮点框按 singleStep 映射浮点刻度。
namespace SliderInput {

QWidget *wrap(QSpinBox *spin, QWidget *parent = nullptr,
              int minimumSliderWidth = 88);
QWidget *wrap(QDoubleSpinBox *spin, QWidget *parent = nullptr,
              int minimumSliderWidth = 88);

// 运行时改过 range / singleStep / decimals 之后调用，让滑块刻度重新对齐。
// 绑定器本身也会周期性自检，通常在构造期设好范围就不必手动调用。
void sync(QAbstractSpinBox *spin);

}  // namespace SliderInput
