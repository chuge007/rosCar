#include "clamp_motor_widgets.h"
#include "clamp_motor_controller.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QtAlgorithms>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace crawling {
namespace {
class JogSlider final : public QSlider {
 public:
  JogSlider() : QSlider(Qt::Horizontal) {}
 protected:
  // A groove click can change value without setting isSliderDown().
  void mouseReleaseEvent(QMouseEvent* event) override {
    QSlider::mouseReleaseEvent(event);
    setValue(0);
  }
  void keyReleaseEvent(QKeyEvent* event) override {
    QSlider::keyReleaseEvent(event);
    setValue(0);
  }
  void wheelEvent(QWheelEvent* event) override {
    event->ignore(); // Scrolling the motor list must not start a motor.
  }
};

QString axisName(int axis) {
  return QString::fromLatin1(axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
}

void selectNumber(QComboBox* box, int value) {
  int index = box->findData(value);
  if (index < 0) {
    box->addItem(QString::number(value), value);
    index = box->count() - 1;
  }
  box->setCurrentIndex(index);
}

void populatePorts(QComboBox* box) {
  const QSignalBlocker blocker(box);
  const QString selected = box->currentText();
  box->clear();
  box->addItem(QString());
  for (const auto& port : QSerialPortInfo::availablePorts())
    box->addItem(port.portName());
  box->setEditText(selected);
}
}

ClampMotorSettingsWidget::ClampMotorSettingsWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("clampMotorSettings"));
  setAutoFillBackground(true);
  QPalette settingsPalette = palette();
  settingsPalette.setColor(QPalette::Window, Qt::white);
  settingsPalette.setColor(QPalette::Base, Qt::white);
  setPalette(settingsPalette);
  setStyleSheet(QStringLiteral(R"(
    #clampMotorSettings { background: white; color: #203247; }
    #clampMotorSettings QTabWidget::pane { background: white; border: 1px solid #d8e0ea; }
    #clampMotorSettings QTabBar { qproperty-expanding: 0; }
    #clampMotorSettings QTabBar::tab {
      min-width: 64px; min-height: 28px; padding: 4px 12px;
      color: #203247; background: #e4e9ed; border: 1px solid #cbd5df;
    }
    #clampMotorSettings QTabBar::tab:selected {
      color: #1769aa; background: white; font-weight: 600;
    }
    #clampMotorSettings QWidget { background: white; }
    #clampMotorSettings QLabel { color: #203247; background: transparent; }
  )"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* hint = new QLabel(QStringLiteral(
      "按 X/Y/Z 轴设置数量，再逐台选择通信方式、端口和 ID。同一轴可混用不同协议；"
      "共用总线的电机填写相同端口及波特率。端口留空表示未配置。修改后请保存再控制。"));
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto* axes = new QTabWidget;
  axes->setObjectName(QStringLiteral("clampAxesTabs"));
  layout->addWidget(axes);
  for (int axis = 0; axis < 3; ++axis) {
    auto* page = new QWidget;
    page->setAutoFillBackground(true);
    page->setPalette(settingsPalette);
    auto* column = new QVBoxLayout(page);
    auto* countForm = new QFormLayout;
    counts_[axis] = new QSpinBox;
    counts_[axis]->setRange(0, DriveSettings::kMaximumClampMotorsPerAxis);
    countForm->addRow(QStringLiteral("电机数量"), counts_[axis]);
    column->addLayout(countForm);
    tabs_[axis] = new QTabWidget;
    tabs_[axis]->setObjectName(QStringLiteral("clampMotorTabs"));
    column->addWidget(tabs_[axis]);
    axes->addTab(page, axisName(axis) + QStringLiteral(" 轴"));
    connect(counts_[axis], QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this, axis](int count) {
      if (loading_) return;
      resizeAxis(axis, count);
      notifyEdited();
    });
  }
  setSettings(DriveSettings().clampMotors);
}

ClampMotorSettingsWidget::~ClampMotorSettingsWidget() {
  for (auto& axis : forms_)
    qDeleteAll(axis);
}

void ClampMotorSettingsWidget::notifyEdited() {
  if (!loading_) emit edited();
}

void ClampMotorSettingsWidget::updateMode(MotorForm* f) {
  const auto mode = clampModeFromKey(f->mode->currentData().toString());
  const bool rs485 = mode == ClampCommunicationMode::MwdRs485;
  const bool zdt = mode == ClampCommunicationMode::ZdtCan;
  f->id->setRange(1, zdt ? 255 : mode == ClampCommunicationMode::TtCanopen ? 127 : 32);
  f->bitrate->setVisible(!rs485);
  f->form->labelForField(f->bitrate)->setVisible(!rs485);
  f->zdt->setVisible(zdt);
  const int baud = f->baud->currentData().toInt();
  const QSignalBlocker baudBlocker(f->baud);
  f->baud->clear();
  const QList<int> rates = rs485
      ? QList<int>{9600, 19200, 38400, 57600, 115200, 230400, 460800, 1000000, 2000000}
      : QList<int>{9600, 19200, 38400, 57600, 115200, 230400, 460800, 500000,
                   921600, 1000000, 1500000, 2000000, 2500000, 4000000};
  for (int rate : rates) f->baud->addItem(QString::number(rate), rate);
  selectNumber(f->baud, rates.contains(baud) ? baud : 115200);
  const int bitrate = f->bitrate->currentData().toInt();
  const QSignalBlocker bitrateBlocker(f->bitrate);
  f->bitrate->clear();
  QList<int> canRates{125000, 250000, 500000, 800000, 1000000};
  if (mode != ClampCommunicationMode::TtCanopen) canRates.prepend(100000);
  if (zdt) canRates = {10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000};
  for (int rate : canRates) f->bitrate->addItem(QString::number(rate), rate);
  selectNumber(f->bitrate, canRates.contains(bitrate) ? bitrate : 500000);
}

void ClampMotorSettingsWidget::addMotor(int axis, const ClampMotorSettings& motor) {
  auto* f = new MotorForm;
  f->page = new QWidget;
  f->form = new QFormLayout(f->page);
  f->mode = new QComboBox;
  for (auto mode : {ClampCommunicationMode::TtCanopen, ClampCommunicationMode::MwdRs485,
                    ClampCommunicationMode::MwdCan, ClampCommunicationMode::ZdtCan})
    f->mode->addItem(clampModeName(mode), clampModeKey(mode));
  f->mode->setCurrentIndex(f->mode->findData(clampModeKey(motor.communicationMode)));
  f->form->addRow(QStringLiteral("通信方式"), f->mode);
  f->id = new QSpinBox;
  f->form->addRow(QStringLiteral("电机 ID"), f->id);
  f->sign = new QComboBox;
  f->sign->addItem(QStringLiteral("正向 +1"), 1);
  f->sign->addItem(QStringLiteral("反向 -1"), -1);
  f->form->addRow(QStringLiteral("方向"), f->sign);
  f->port = new QComboBox;
  f->port->setEditable(true);
  populatePorts(f->port);
  f->port->setEditText(motor.serialPort);
  f->form->addRow(QStringLiteral("串口 / SLCAN 适配器"), f->port);
  f->baud = new QComboBox;
  f->form->addRow(QStringLiteral("串口 / 适配器波特率"), f->baud);
  f->bitrate = new QComboBox;
  f->form->addRow(QStringLiteral("CAN 总线波特率"), f->bitrate);
  f->zdt = new QGroupBox(QStringLiteral("ZDT CAN 参数"));
  auto* zdtForm = new QFormLayout(f->zdt);
  f->firmware = new QComboBox;
  f->firmware->addItem(QStringLiteral("X 固件（速度 0.1 RPM）"), int(ZdtFirmware::X));
  f->firmware->addItem(QStringLiteral("Emm 固件（速度 1 RPM）"), int(ZdtFirmware::Emm));
  zdtForm->addRow(QStringLiteral("驱动器固件"), f->firmware);
  f->maximumRpm = new QSpinBox;
  f->maximumRpm->setRange(1, 3000);
  f->maximumRpm->setSuffix(QStringLiteral(" RPM"));
  zdtForm->addRow(QStringLiteral("100% 对应转速"), f->maximumRpm);
  f->acceleration = new QSpinBox;
  zdtForm->addRow(QStringLiteral("加速度"), f->acceleration);
  auto* note = new QLabel(QStringLiteral(
      "驱动器需设置 CAN、自由协议及固定校验 6B，使用支持扩展帧的 SLCAN 适配器。"
      "X 加速度单位为 RPM/s；Emm 为 0–255 档，且需关闭“速度缩小 10 倍”选项。"
      "加速度 0 为直接启动。CAN 83.333 kbit/s 暂不支持。"));
  note->setWordWrap(true);
  zdtForm->addRow(note);
  f->form->addRow(f->zdt);
  updateMode(f);
  f->id->setValue(motor.motorId);
  selectNumber(f->sign, motor.sign);
  selectNumber(f->baud, motor.serialBaudRate);
  selectNumber(f->bitrate, motor.canBitrate);
  selectNumber(f->firmware, int(motor.zdtFirmware));
  f->maximumRpm->setValue(motor.zdtMaximumSpeedRpm);
  f->acceleration->setRange(0, motor.zdtFirmware == ZdtFirmware::X ? 65535 : 255);
  f->acceleration->setValue(motor.zdtAcceleration);
  forms_[axis].append(f);
  tabs_[axis]->addTab(f->page, axisName(axis) + QString::number(forms_[axis].size()));
  connect(f->mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this, f] { updateMode(f); notifyEdited(); });
  connect(f->firmware, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this, f] {
    const bool x = f->firmware->currentData().toInt() == int(ZdtFirmware::X);
    const int previous = f->acceleration->value();
    f->acceleration->setRange(0, x ? 65535 : 255);
    if (!x && previous > 255) f->acceleration->setValue(10);
    notifyEdited();
  });
  for (QComboBox* box : {f->baud, f->bitrate, f->sign})
    connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { notifyEdited(); });
  connect(f->port, &QComboBox::currentTextChanged, this, [this] { notifyEdited(); });
  for (QSpinBox* box : {f->id, f->maximumRpm, f->acceleration})
    connect(box, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this] { notifyEdited(); });
}

void ClampMotorSettingsWidget::resizeAxis(int axis, int count) {
  while (forms_[axis].size() > count) {
    auto* f = forms_[axis].takeLast();
    tabs_[axis]->removeTab(tabs_[axis]->count() - 1);
    delete f->page;
    delete f;
  }
  while (forms_[axis].size() < count) {
    const auto current = settings();
    ClampMotorSettings motor;
    if (!current[axis].isEmpty()) motor = current[axis].last();
    const bool zdt = motor.communicationMode == ClampCommunicationMode::ZdtCan;
    const int maximumId = zdt ? 255 :
        motor.communicationMode == ClampCommunicationMode::TtCanopen ? 127 : 32;
    for (int candidate = 1; candidate <= maximumId; ++candidate) {
      bool used = false;
      for (const auto& motors : current) {
        for (const auto& existing : motors) {
          if (existing.serialPort.trimmed().compare(motor.serialPort.trimmed(),
                                                   Qt::CaseInsensitive) == 0 &&
              (existing.communicationMode == ClampCommunicationMode::ZdtCan) == zdt &&
              existing.motorId == candidate)
            used = true;
        }
      }
      if (!used) { motor.motorId = candidate; break; }
    }
    addMotor(axis, motor);
  }
}

ClampMotorAxes ClampMotorSettingsWidget::settings() const {
  ClampMotorAxes axes;
  for (int axis = 0; axis < 3; ++axis) {
    for (const auto* f : forms_[axis]) {
      ClampMotorSettings motor;
      motor.communicationMode = clampModeFromKey(f->mode->currentData().toString());
      motor.serialPort = f->port->currentText().trimmed();
      motor.serialBaudRate = f->baud->currentData().toInt();
      motor.canBitrate = f->bitrate->currentData().toInt();
      motor.motorId = f->id->value();
      motor.sign = f->sign->currentData().toInt();
      motor.zdtFirmware = ZdtFirmware(f->firmware->currentData().toInt());
      motor.zdtMaximumSpeedRpm = f->maximumRpm->value();
      motor.zdtAcceleration = f->acceleration->value();
      axes[axis].append(motor);
    }
  }
  return axes;
}

void ClampMotorSettingsWidget::setSettings(const ClampMotorAxes& axes) {
  if (settings() == axes) return;
  loading_ = true;
  for (int axis = 0; axis < 3; ++axis) {
    resizeAxis(axis, 0);
    for (const auto& motor : axes[axis]) addMotor(axis, motor);
    counts_[axis]->setValue(forms_[axis].size());
  }
  loading_ = false;
}

void ClampMotorSettingsWidget::refreshPorts() {
  for (const auto& axis : forms_)
    for (auto* form : axis) populatePorts(form->port);
}

ClampMotorControlWidget::ClampMotorControlWidget(ClampMotorController* controller,
                                                 QWidget* parent)
    : QWidget(parent), controller_(controller) {
  setObjectName(QStringLiteral("clampMotorControls"));
  setAutoFillBackground(true);
  QPalette controlsPalette = palette();
  controlsPalette.setColor(QPalette::Window, Qt::white);
  controlsPalette.setColor(QPalette::Base, Qt::white);
  setPalette(controlsPalette);
  setStyleSheet(QStringLiteral(R"(
    #clampMotorControls { background: white; color: #203247; }
    #clampMotorControls QScrollArea { background: white; border: 0; }
    #clampMotorControls QScrollArea QWidget { background: white; }
    #clampMotorControls QLabel { color: #203247; background: transparent; }
    #clampMotorControls QSlider#clampMotorSpeed::groove:horizontal {
      height: 8px; background: #d7e2ee; border-radius: 4px;
    }
    #clampMotorControls QSlider#clampMotorSpeed::handle:horizontal {
      width: 24px; margin: -10px 0; background: #1769aa;
      border: 1px solid #0e4d7c; border-radius: 12px;
    }
    #clampMotorControls QSlider#clampMotorSpeed::handle:horizontal:hover {
      background: #2586c8;
    }
    #clampMotorControls QSlider#clampMotorSpeed::handle:horizontal:disabled {
      background: #bac8d5; border-color: #aab8c6;
    }
    #clampMotorControls QPushButton {
      min-width: 64px; min-height: 36px; padding: 4px 10px;
      color: #164568; background: #eaf2fb; border: 1px solid #a8c4dc;
      border-radius: 5px; font-size: 16px;
    }
    #clampMotorControls QPushButton:hover { background: #dcecfb; border-color: #4f89c7; }
    #clampMotorControls QPushButton:disabled {
      color: #607286; background: #e7edf3; border-color: #cbd5df;
    }
  )"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  scroll_ = new QScrollArea;
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setAutoFillBackground(true);
  scroll_->viewport()->setAutoFillBackground(true);
  scroll_->viewport()->setPalette(controlsPalette);
  scroll_->setMinimumHeight(180);
  scroll_->setMaximumHeight(400);
  layout->addWidget(scroll_);
  connect(controller_.data(), &ClampMotorController::settingsChanged,
          this, &ClampMotorControlWidget::rebuild);
  connect(controller_.data(), &ClampMotorController::controlsResetRequested,
          this, &ClampMotorControlWidget::resetControls);
  connect(controller_.data(), &ClampMotorController::connectionChanged,
          this, [this](bool connected, const QString&) { if (!connected) resetControls(); });
  connect(controller_.data(), &QObject::destroyed, this, [this] { setEnabled(false); });
  qApp->installEventFilter(this);
  rebuild();
}

void ClampMotorControlWidget::rebuild() {
  if (!controller_) return;
  if (auto* old = scroll_->takeWidget()) old->deleteLater();
  auto* body = new QWidget;
  body->setObjectName(QStringLiteral("clampMotorControlList"));
  body->setAutoFillBackground(true);
  QPalette bodyPalette = palette();
  bodyPalette.setColor(QPalette::Window, Qt::white);
  bodyPalette.setColor(QPalette::Base, Qt::white);
  body->setPalette(bodyPalette);
  auto* grid = new QGridLayout(body);
  grid->setContentsMargins(8, 8, 8, 8);
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(8);
  grid->setColumnStretch(2, 1);
  int row = 0;
  const auto& axes = controller_->settings().clampMotors;
  for (int axis = 0; axis < 3; ++axis) {
    for (int index = 0; index < axes[axis].size(); ++index, ++row) {
      const auto& motor = axes[axis][index];
      auto* label = new QLabel(QStringLiteral("%1%2 · ID %3\n%4")
          .arg(axisName(axis)).arg(index + 1).arg(motor.motorId)
          .arg(clampModeName(motor.communicationMode)));
      label->setToolTip(motor.serialPort);
      grid->addWidget(label, row, 0);
      auto* minus = new QLabel(QStringLiteral("−"));
      auto* plus = new QLabel(QStringLiteral("+"));
      minus->setAlignment(Qt::AlignCenter);
      plus->setAlignment(Qt::AlignCenter);
      auto* slider = new JogSlider;
      slider->setObjectName(QStringLiteral("clampMotorSpeed"));
      slider->setRange(-100, 100);
      slider->setSingleStep(5);
      slider->setPageStep(10);
      slider->setMinimumWidth(240);
      slider->setFixedHeight(40);
      slider->setAccessibleName(QStringLiteral("%1%2 电机双向点动")
          .arg(axisName(axis)).arg(index + 1));
      slider->setToolTip(QStringLiteral("负向 ← 速度 → 正向；松开即停止"));
      auto* value = new QLabel(QStringLiteral("0%"));
      value->setMinimumWidth(56);
      value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      auto* stop = new QPushButton(QStringLiteral("停止"));
      stop->setMinimumSize(76, 40);
      grid->addWidget(minus, row, 1);
      grid->addWidget(slider, row, 2);
      grid->addWidget(plus, row, 3);
      grid->addWidget(value, row, 4);
      grid->addWidget(stop, row, 5);
      connect(slider, &QSlider::valueChanged, this, [this, axis, index, value](int percent) {
        value->setText(percent > 0 ? QStringLiteral("+%1%").arg(percent)
                                   : QStringLiteral("%1%").arg(percent));
        controller_->setMotorSpeed(axis, index, percent);
      });
      connect(slider, &QSlider::sliderReleased, slider, [slider] { slider->setValue(0); });
      connect(stop, &QPushButton::clicked, this, [this, axis, index, slider] {
        slider->setValue(0);
        controller_->stopMotor(axis, index);
      });
      const bool assigned = !motor.serialPort.trimmed().isEmpty();
      slider->setEnabled(assigned);
      stop->setEnabled(assigned);
    }
  }
  if (row == 0) grid->addWidget(new QLabel(QStringLiteral("尚未配置夹子电机")), 0, 0);
  grid->setRowStretch(row + 1, 1);
  scroll_->setWidget(body);
}

void ClampMotorControlWidget::resetControls() {
  for (auto* slider : findChildren<QSlider*>(QStringLiteral("clampMotorSpeed"))) {
    const QSignalBlocker blocker(slider);
    slider->setValue(0);
  }
  // Value labels follow valueChanged; reset them without generating commands.
  for (auto* label : scroll_->findChildren<QLabel*>())
    if (label->text().endsWith(QLatin1Char('%'))) label->setText(QStringLiteral("0%"));
}

bool ClampMotorControlWidget::eventFilter(QObject* watched, QEvent* event) {
  const auto type = event->type();
  if ((type == QEvent::WindowDeactivate && watched == window()) ||
      type == QEvent::ApplicationDeactivate) {
    if (controller_) controller_->stop();
    resetControls();
  } else if ((type == QEvent::KeyRelease || type == QEvent::FocusOut) &&
             qobject_cast<QSlider*>(watched) && isAncestorOf(qobject_cast<QWidget*>(watched))) {
    static_cast<QSlider*>(watched)->setValue(0);
  }
  return QWidget::eventFilter(watched, event);
}

void ClampMotorControlWidget::hideEvent(QHideEvent* event) {
  if (controller_) controller_->stop();
  resetControls();
  QWidget::hideEvent(event);
}

void ClampMotorControlWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::EnabledChange && !isEnabled()) {
    if (controller_) controller_->stop();
    resetControls();
  }
  QWidget::changeEvent(event);
}
} // namespace crawling
