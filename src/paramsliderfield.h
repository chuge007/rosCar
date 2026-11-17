#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QSpinBox;

namespace pa1664::ui {

// 把现有数值输入框包装成「滑轨拖动 + 数值框」的一行控件。
//
// 设计约束：
// - 原 QSpinBox / QDoubleSpinBox 对象仍然存在并保留全部方法、信号与
//   parameterName 属性，调用方持有的指针、qobject_cast、valueChanged 连接
//   和 QSignalBlocker 语义都不变，因此参数下发/回读链路不需要改动。
// - 整数型输入框按 1 步进拖动，浮点型按小数位精度拖动；范围过大时只压缩
//   滑轨分段数，不改变数值范围本身。
// - 数值框关闭了上下微调按钮（滑轨已承担粗调，框内仍可键盘精确输入）。
QWidget *paramSliderField(QSpinBox *box, QWidget *parent = nullptr);
QWidget *paramSliderField(QDoubleSpinBox *box, QWidget *parent = nullptr);

// 带说明文案的版本：说明显示在滑轨下方（原 wrapWithHint 的覆盖式提示改为
// 独立一行，避免与滑轨重叠）。
QWidget *paramSliderField(QSpinBox *box, const QString &hint, QWidget *parent = nullptr);
QWidget *paramSliderField(QDoubleSpinBox *box, const QString &hint, QWidget *parent = nullptr);

// 运行期调用过 setRange / setMaximum 之后，让滑轨范围跟上（数值不变）。
// 一般情况下包装控件会通过 LayoutRequest / valueChanged 自动跟上，只有
// 改了范围又没改数值时才需要显式调用。
void refreshParamSlider(QSpinBox *box);
void refreshParamSlider(QDoubleSpinBox *box);

} // namespace pa1664::ui
