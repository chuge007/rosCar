#include "clamp_motor_widgets.h"
#include "clamp_motor_controller.h"
#include "candle_can_adapter.h"
#include "pcan_can_adapter.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QTimer>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QKeyEvent>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QtAlgorithms>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <memory>
#include <utility>

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

QString selectedPort(const QComboBox* box) {
  const QString text = box->currentText().trimmed();
  // Editable combos can retain the previous index while the user types a name.
  const int index = box->findText(text, Qt::MatchFixedString);
  if (index >= 0) {
    const QString id = box->itemData(index).toString();
    if (CandleCanAdapter::isCandlePort(id) || PcanCanAdapter::isPcanPort(id)) return id;
  }
  const QString canonical = PcanCanAdapter::canonicalPort(text);
  return canonical.isEmpty() ? text : canonical;
}

void selectPort(QComboBox* box, const QString& id) {
  const QString pcanId = PcanCanAdapter::canonicalPort(id);
  int index = box->findData(pcanId.isEmpty() ? id : pcanId, Qt::UserRole, Qt::MatchFixedString);
  if (index < 0) index = box->findText(id, Qt::MatchFixedString);
  if (index >= 0) box->setCurrentIndex(index);
  else if (id.startsWith(QStringLiteral("candle:"), Qt::CaseInsensitive)) {
    box->addItem(QStringLiteral("candle（未连接）"), id);
    box->setCurrentIndex(box->count() - 1);
  } else if (!pcanId.isEmpty()) {
    box->addItem(PcanCanAdapter::portName(pcanId) + QStringLiteral("（未连接）"), pcanId);
    box->setCurrentIndex(box->count() - 1);
  } else box->setEditText(id);
}

void populatePorts(QComboBox* box) {
  const QSignalBlocker blocker(box);
  const QString selectedId = selectedPort(box);
  box->clear();
  box->addItem(QString());
  for (const auto& port : QSerialPortInfo::availablePorts())
    box->addItem(port.portName());
  for (const auto& port : CandleCanAdapter::availablePorts()) {
    box->addItem(port.name, port.id);
    box->setItemData(box->count() - 1, QStringLiteral("原生 candleLight USB CAN\n") +
        port.id.mid(7), Qt::ToolTipRole);
  }
  QString pcanError;
  for (const auto& port : PcanCanAdapter::availablePorts(&pcanError)) {
    box->addItem(port.name, port.id);
    box->setItemData(box->count() - 1, port.description, Qt::ToolTipRole);
  }
  box->setProperty("pcanEnumerationError", pcanError);
  selectPort(box, selectedId);
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
    syncCheckboxes_[axis] = new QCheckBox(QStringLiteral("同步运动"));
    syncCheckboxes_[axis]->setToolTip(QStringLiteral("勾选后，在主控页面拖动该轴组内任意电机滑条，组内其他电机同步运动"));
    column->addWidget(syncCheckboxes_[axis]);
    connect(syncCheckboxes_[axis], &QCheckBox::toggled, this, [this] { notifyEdited(); });
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

void ClampMotorSettingsWidget::setController(ClampMotorController* controller) {
  if (controller_ == controller) return;
  if (controller_) disconnect(controller_, nullptr, this, nullptr);
  controller_ = remote_ ? nullptr : controller;
  if (!controller_) return;
  connect(controller_, &ClampMotorController::positionReceived, this,
          &ClampMotorSettingsWidget::setPositionFeedback);
}

void ClampMotorSettingsWidget::setRemoteMode(bool remote) {
  if (remote_ == remote) return;
  remote_ = remote;
  if (remote_ && controller_) {
    disconnect(controller_, nullptr, this, nullptr);
    controller_ = nullptr;
  }
}

void ClampMotorSettingsWidget::setPositionFeedback(int axis, int index,
                                                    double degrees) {
  const int key = axis * 1000 + index;
  if (!pendingLimitRecords_.contains(key)) return;
  const bool upper = pendingLimitRecords_.take(key);
  if (axis < 0 || axis >= 3 || index < 0 || index >= forms_[axis].size())
    return;
  auto* f = forms_[axis][index];
  if (upper) {
    f->upperLimit = degrees;
    f->upperLimitEnabled = true;
  } else {
    f->lowerLimit = degrees;
    f->lowerLimitEnabled = true;
  }
  if (f->upperLimitEnabled && f->lowerLimitEnabled &&
      f->upperLimit < f->lowerLimit) {
    std::swap(f->upperLimit, f->lowerLimit);
  }
  updateLimitStatus(f);
  // Apply limit immediately without marking edited, to avoid stop+reconnect.
  applyLimitsNow(axis, index, f);
}

void ClampMotorSettingsWidget::beginRecordLimit(MotorForm* f, bool upper) {
  int axis = -1, index = -1;
  for (int a = 0; a < 3 && axis < 0; ++a) {
    const int i = forms_[a].indexOf(f);
    if (i >= 0) { axis = a; index = i; }
  }
  if (axis < 0) return;
  if (!controller_ && !remote_) {
    f->limitStatus->setText(QStringLiteral("控制器不可用，无法读取位置"));
    return;
  }
  const auto configuredAxes = settings();
  if (index < 0 || index >= configuredAxes[axis].size()) {
    f->limitStatus->setText(QStringLiteral("请先保存夹子配置再记录限位"));
    return;
  }
  if (!ClampMotorController::supportsPosition(configuredAxes[axis][index])) {
    f->limitStatus->setText(QStringLiteral("该电机协议不支持读取位置"));
    return;
  }
  const int key = axis * 1000 + index;
  pendingLimitRecords_.insert(key, upper);
  f->limitStatus->setText(QStringLiteral("正在读取电机当前位置…"));
  if (remote_) {
    emit positionRequested(axis, index);
  } else {
    controller_->requestPosition(axis, index);
  }
  QTimer::singleShot(1200, this, [this, key, axis, index] {
    if (!pendingLimitRecords_.remove(key)) return;
    if (index < forms_[axis].size())
      forms_[axis][index]->limitStatus->setText(
          QStringLiteral("读取位置超时：请确认已保存配置、已连接电机，且协议支持位置读取（MWD CAN / ZDT CAN）"));
  });
}

void ClampMotorSettingsWidget::updateLimitStatus(MotorForm* f) {
  const auto text = [](bool enabled, double degrees) {
    return enabled ? QStringLiteral("%1°").arg(degrees, 0, 'f', 2) : QStringLiteral("未设置");
  };
  f->limitStatus->setText(QStringLiteral("上限 %1 / 下限 %2（电机角度）")
      .arg(text(f->upperLimitEnabled, f->upperLimit), text(f->lowerLimitEnabled, f->lowerLimit)));
}

void ClampMotorSettingsWidget::applyLimitsNow(int axis, int index, MotorForm* f) {
  // Limits only change travel checks, so apply and persist them immediately
  // without emitting edited(): that would stop motors and disable controls.
  if (remote_) {
    emit limitsChanged();
    return;
  }
  if (!controller_) return;
  auto current = controller_->settings();
  if (axis < 0 || axis >= 3 || index < 0 || index >= current.clampMotors[axis].size()) return;
  auto& motor = current.clampMotors[axis][index];
  motor.upperLimit = f->upperLimit;
  motor.lowerLimit = f->lowerLimit;
  motor.upperLimitEnabled = f->upperLimitEnabled;
  motor.lowerLimitEnabled = f->lowerLimitEnabled;
  QSettings store(DriveSettings::persistentFilePath(), QSettings::IniFormat);
  auto latest = DriveSettings::load(store);
  if (index < latest.clampMotors[axis].size()) {
    auto& saved = latest.clampMotors[axis][index];
    saved.upperLimit = motor.upperLimit;
    saved.lowerLimit = motor.lowerLimit;
    saved.upperLimitEnabled = motor.upperLimitEnabled;
    saved.lowerLimitEnabled = motor.lowerLimitEnabled;
    latest.save(store);
    store.sync();
  }
  controller_->setSettings(current);
}

void ClampMotorSettingsWidget::notifyEdited() {
  if (!loading_) emit edited();
}

void ClampMotorSettingsWidget::updateMode(MotorForm* f) {
  const auto mode = clampModeFromKey(f->mode->currentData().toString());
  const bool rs485 = mode == ClampCommunicationMode::MwdRs485;
  const bool zdt = mode == ClampCommunicationMode::ZdtCan;
  const bool nativeCan = CandleCanAdapter::isCandlePort(selectedPort(f->port)) ||
      PcanCanAdapter::isPcanPort(selectedPort(f->port));
  f->baud->setVisible(!nativeCan);
  f->form->labelForField(f->baud)->setVisible(!nativeCan);
  QString portHint = nativeCan
      ? QStringLiteral("PCAN / candleLight 原生 CAN：仅使用 CAN 总线波特率，不使用串口波特率；不支持 RS485。")
      : QStringLiteral("MWD CAN 可选择 PCAN_USBBUS1 等原生 PCAN 通道；串口用于 RS485 或 SLCAN，candle0 等为原生 USB CAN。");
  if (mode == ClampCommunicationMode::MwdCan)
    portHint += QStringLiteral("\nCAN 波特率必须与电机一致，MCF302CB 例程为 1000000 bit/s；使用经典 CAN。");
  const QString pcanError = f->port->property("pcanEnumerationError").toString();
  if (!pcanError.isEmpty()) portHint += QLatin1Char('\n') + pcanError;
  f->port->setToolTip(portHint);
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
  f->alias = new QLineEdit;
  f->alias->setPlaceholderText(QStringLiteral("可选，主控页面显示此名称"));
  f->alias->setText(motor.alias);
  f->form->addRow(QStringLiteral("别名"), f->alias);
  f->gearRatio = new QDoubleSpinBox;
  f->gearRatio->setRange(0.001, 10000.0);
  f->gearRatio->setDecimals(3);
  f->gearRatio->setSuffix(QStringLiteral(" mm/圈"));
  f->gearRatio->setValue(motor.gearRatio);
  f->gearRatio->setToolTip(QStringLiteral("电机转一圈，探头实际移动的距离"));
  f->form->addRow(QStringLiteral("传动比"), f->gearRatio);
  auto* limitRow = new QHBoxLayout;
  f->recordUpper = new QPushButton(QStringLiteral("记录上限位"));
  f->recordLower = new QPushButton(QStringLiteral("记录下限位"));
  auto* clearLimits = new QPushButton(QStringLiteral("清除限位"));
  f->limitStatus = new QLabel(QStringLiteral("未设置限位"));
  f->limitStatus->setStyleSheet(QStringLiteral("color:#52606d;"));
  f->limitStatus->setWordWrap(true);
  f->upperLimit = motor.upperLimit;
  f->lowerLimit = motor.lowerLimit;
  f->upperLimitEnabled = motor.upperLimitEnabled;
  f->lowerLimitEnabled = motor.lowerLimitEnabled;
  f->recordUpper->setToolTip(QStringLiteral("先在手动控制里把电机移动到上限位置，再点击记录"));
  f->recordLower->setToolTip(QStringLiteral("先在手动控制里把电机移动到下限位置，再点击记录"));
  limitRow->addWidget(f->recordUpper);
  limitRow->addWidget(f->recordLower);
  limitRow->addWidget(clearLimits);
  connect(f->recordUpper, &QPushButton::clicked, this, [this, f] { beginRecordLimit(f, true); });
  connect(f->recordLower, &QPushButton::clicked, this, [this, f] { beginRecordLimit(f, false); });
  connect(clearLimits, &QPushButton::clicked, this, [this, f] {
    f->upperLimitEnabled = false;
    f->lowerLimitEnabled = false;
    updateLimitStatus(f);
    for (int a = 0; a < 3; ++a) {
      const int i = forms_[a].indexOf(f);
      if (i >= 0) { applyLimitsNow(a, i, f); break; }
    }
  });
  limitRow->addWidget(f->limitStatus);
  limitRow->addStretch();
  f->form->addRow(QStringLiteral("限位"), limitRow);
  f->sign = new QComboBox;
  f->sign->addItem(QStringLiteral("正向 +1"), 1);
  f->sign->addItem(QStringLiteral("反向 -1"), -1);
  f->form->addRow(QStringLiteral("方向"), f->sign);
  f->port = new QComboBox;
  f->port->setEditable(true);
  populatePorts(f->port);
  selectPort(f->port, motor.serialPort);
  f->form->addRow(QStringLiteral("串口 / CAN 适配器"), f->port);
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
      "驱动器需设置 CAN、自由协议及固定校验 6B，使用 PCAN、candleLight（candle0 等）"
      "或支持扩展帧的 SLCAN 适配器。请先关闭占用通道的其他控制程序。"
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
  updateLimitStatus(f);
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
  connect(f->port, &QComboBox::currentTextChanged, this,
          [this, f] { updateMode(f); notifyEdited(); });
  for (QSpinBox* box : {f->id, f->maximumRpm, f->acceleration})
    connect(box, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this] { notifyEdited(); });
  connect(f->alias, &QLineEdit::textChanged, this, [this] { notifyEdited(); });
  connect(f->gearRatio, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
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
      motor.serialPort = selectedPort(f->port);
      motor.serialBaudRate = f->baud->currentData().toInt();
      motor.canBitrate = f->bitrate->currentData().toInt();
      motor.motorId = f->id->value();
      motor.sign = f->sign->currentData().toInt();
      motor.alias = f->alias->text().trimmed();
      motor.gearRatio = f->gearRatio->value();
      motor.upperLimit = f->upperLimit;
      motor.lowerLimit = f->lowerLimit;
      motor.upperLimitEnabled = f->upperLimitEnabled;
      motor.lowerLimitEnabled = f->lowerLimitEnabled;
      motor.zdtFirmware = ZdtFirmware(f->firmware->currentData().toInt());
      motor.zdtMaximumSpeedRpm = f->maximumRpm->value();
      motor.zdtAcceleration = f->acceleration->value();
      axes[axis].append(motor);
    }
  }
  return axes;
}

void ClampMotorSettingsWidget::setSettings(const ClampMotorAxes& axes) {
  const auto current = settings();
  bool same = current == axes;
  for (int axis = 0; same && axis < 3; ++axis)
    for (int index = 0; index < axes[axis].size(); ++index)
      if (current[axis][index].alias != axes[axis][index].alias) { same = false; break; }
  if (same) return;
  loading_ = true;
  for (int axis = 0; axis < 3; ++axis) {
    resizeAxis(axis, 0);
    for (const auto& motor : axes[axis]) addMotor(axis, motor);
    counts_[axis]->setValue(forms_[axis].size());
  }
  loading_ = false;
}

void ClampMotorSettingsWidget::setSyncEnabled(int axis, bool enabled) {
  if (axis < 0 || axis >= 3) return;
  const QSignalBlocker blocker(syncCheckboxes_[axis]);
  syncCheckboxes_[axis]->setChecked(enabled);
}

bool ClampMotorSettingsWidget::syncEnabled(int axis) const {
  return axis >= 0 && axis < 3 && syncCheckboxes_[axis]->isChecked();
}

void ClampMotorSettingsWidget::refreshPorts() {
  for (const auto& axis : forms_)
    for (auto* form : axis) {
      populatePorts(form->port);
      updateMode(form);
    }
}

void ClampMotorSettingsWidget::setAvailablePorts(const QStringList& ids,
                                                  const QStringList& labels,
                                                  const QStringList& tooltips) {
  for (const auto& axis : forms_) {
    for (auto* form : axis) {
      const QSignalBlocker blocker(form->port);
      const QString selectedId = selectedPort(form->port);
      form->port->clear();
      form->port->addItem(QString());
      for (int i = 0; i < ids.size(); ++i) {
        const QString id = ids.at(i).trimmed();
        if (id.isEmpty()) continue;
        const QString label = i < labels.size() && !labels.at(i).trimmed().isEmpty()
                                  ? labels.at(i)
                                  : id;
        const int index = form->port->count();
        form->port->addItem(label, id);
        if (i < tooltips.size() && !tooltips.at(i).isEmpty())
          form->port->setItemData(index, tooltips.at(i), Qt::ToolTipRole);
      }
      form->port->setProperty("pcanEnumerationError", QString());
      selectPort(form->port, selectedId);
      updateMode(form);
    }
  }
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
  speedFlushTimer_ = new QTimer(this);
  speedFlushTimer_->setSingleShot(true);
  speedFlushTimer_->setInterval(40);
  connect(speedFlushTimer_, &QTimer::timeout,
          this, &ClampMotorControlWidget::flushMotorSpeeds);
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
  if (controller_) {
    connect(controller_.data(), &ClampMotorController::settingsChanged,
            this, &ClampMotorControlWidget::rebuild);
    connect(controller_.data(), &ClampMotorController::controlsResetRequested,
            this, &ClampMotorControlWidget::resetControls);
    connect(controller_.data(), &ClampMotorController::connectionChanged,
            this, [this](bool connected, const QString&) {
              if (!connected) resetControls();
            });
    connect(controller_.data(), &QObject::destroyed, this,
            [this] { setEnabled(false); });
    connect(controller_.data(), &ClampMotorController::limitReached,
            this, &ClampMotorControlWidget::onLimitReached);
  }
  qApp->installEventFilter(this);
  rebuild();
}

void ClampMotorControlWidget::setRemoteMode(bool remote) {
  if (remote_ == remote) return;
  remote_ = remote;
  if (remote_ && controller_) {
    disconnect(controller_, nullptr, this, nullptr);
    controller_ = nullptr;
  }
  rebuild();
}

void ClampMotorControlWidget::setRemoteSettings(
    const ClampMotorAxes& axes, const std::array<bool, 3>& syncEnabled) {
  remoteAxes_ = axes;
  remoteSyncEnabled_ = syncEnabled;
  if (remote_) rebuild();
}

void ClampMotorControlWidget::setRemoteConnected(bool connected) {
  if (remoteConnected_ == connected) return;
  remoteConnected_ = connected;
  if (remote_) rebuild();
}

void ClampMotorControlWidget::applyLimitReached(int axis, int index,
                                                 bool upper) {
  onLimitReached(axis, index, upper);
}

void ClampMotorControlWidget::queueMotorSpeed(int axis, int index, int percent) {
  const int key = axis * 1000 + index;
  if (percent == 0) {
    pendingMotorSpeeds_.remove(key);
    if (remote_)
      emit motorSpeedRequested(axis, index, 0);
    else if (controller_)
      controller_->setMotorSpeed(axis, index, 0);
    return;
  }
  pendingMotorSpeeds_.insert(key, percent);
  if (!speedFlushTimer_->isActive())
    speedFlushTimer_->start();
}

void ClampMotorControlWidget::flushMotorSpeeds() {
  const auto pending = std::exchange(pendingMotorSpeeds_, {});
  if (remote_) {
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
      emit motorSpeedRequested(it.key() / 1000, it.key() % 1000, it.value());
    }
    return;
  }
  if (!controller_) return;
  for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
    controller_->setMotorSpeed(it.key() / 1000, it.key() % 1000, it.value());
}

void ClampMotorControlWidget::onLimitReached(int axis, int index, bool upper) {
  const int key = axis * 1000 + index;
  pendingMotorSpeeds_.remove(key);
  if (auto hint = limitHints_.value(key)) {
    hint->setText(upper ? QStringLiteral("已到上限位") : QStringLiteral("已到下限位"));
    hint->setProperty("limitDirection", upper ? 1 : -1);
  }
  if (auto slider = sliders_.value(key)) {
    const QSignalBlocker blocker(slider);
    slider->setValue(0);
  }
}

void ClampMotorControlWidget::rebuild() {
  if (!controller_ && !remote_) return;
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
  sliders_.clear();
  limitHints_.clear();
  int row = 0;
  DriveSettings snapshot;
  if (remote_) {
    snapshot.clampMotors = remoteAxes_;
    snapshot.clampMotorSyncEnabled = remoteSyncEnabled_;
  } else {
    snapshot = controller_->settings();
  }
  const auto& axes = snapshot.clampMotors;
  const auto& syncEnabled = snapshot.clampMotorSyncEnabled;
  for (int axis = 0; axis < 3; ++axis) {
    auto group = std::make_shared<QVector<QPointer<QSlider>>>();
    for (int index = 0; index < axes[axis].size(); ++index, ++row) {
      const auto& motor = axes[axis][index];
      const QString defaultName = QStringLiteral("%1%2").arg(axisName(axis)).arg(index + 1);
      const QString name = motor.alias.trimmed().isEmpty()
          ? QStringLiteral("%1 · ID %2\n%3").arg(defaultName).arg(motor.motorId)
                .arg(clampModeName(motor.communicationMode))
          : motor.alias.trimmed();
      auto* label = new QLabel(name);
      label->setToolTip(QStringLiteral("%1 · ID %2 · %3 · %4")
          .arg(defaultName).arg(motor.motorId)
          .arg(clampModeName(motor.communicationMode), motor.serialPort));
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
      slider->setAccessibleName(QStringLiteral("%1 电机双向点动").arg(name));
      slider->setToolTip(QStringLiteral(
          "滑块离中心的拖动幅度就是运行速度：向左负向、向右正向；松开即停止"));
      grid->addWidget(minus, row, 1);
      grid->addWidget(slider, row, 2);
      grid->addWidget(plus, row, 3);
      auto* limitHint = new QLabel;
      limitHint->setStyleSheet(QStringLiteral("color: #c62828; font-size: 12px;"));
      limitHint->setMinimumWidth(72);
      grid->addWidget(limitHint, row, 4);
      const int key = axis * 1000 + index;
      sliders_.insert(key, slider);
      limitHints_.insert(key, limitHint);
      const bool sync = syncEnabled[axis];
      connect(slider, &QSlider::valueChanged, this,
              [this, axis, index, slider, group, sync, limitHint](int percent) {
        // Clear first; the controller re-emits limitReached synchronously if still blocked.
        if (percent != 0) {
          limitHint->clear();
          limitHint->setProperty("limitDirection", 0);
        }
        queueMotorSpeed(axis, index, percent);
        if (!sync || syncing_) return;
        syncing_ = true;
        for (const auto& other : *group)
          if (other && other != slider && other->isEnabled()) other->setValue(percent);
        syncing_ = false;
      });
      connect(slider, &QSlider::sliderReleased, slider, [slider] { slider->setValue(0); });
      slider->setEnabled(!motor.serialPort.trimmed().isEmpty() &&
                         (!remote_ || remoteConnected_));
      group->append(slider);
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
}

bool ClampMotorControlWidget::eventFilter(QObject* watched, QEvent* event) {
  const auto type = event->type();
  if ((type == QEvent::WindowDeactivate && watched == window()) ||
      type == QEvent::ApplicationDeactivate) {
    speedFlushTimer_->stop();
    pendingMotorSpeeds_.clear();
    if (remote_)
      emit stopRequested();
    else if (controller_)
      controller_->stop();
    resetControls();
  } else if ((type == QEvent::KeyRelease || type == QEvent::FocusOut) &&
             qobject_cast<QSlider*>(watched) && isAncestorOf(qobject_cast<QWidget*>(watched))) {
    static_cast<QSlider*>(watched)->setValue(0);
  }
  return QWidget::eventFilter(watched, event);
}

void ClampMotorControlWidget::hideEvent(QHideEvent* event) {
  speedFlushTimer_->stop();
  pendingMotorSpeeds_.clear();
  if (remote_)
    emit stopRequested();
  else if (controller_)
    controller_->stop();
  resetControls();
  QWidget::hideEvent(event);
}

void ClampMotorControlWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::EnabledChange && !isEnabled()) {
    speedFlushTimer_->stop();
    pendingMotorSpeeds_.clear();
    if (remote_)
      emit stopRequested();
    else if (controller_)
      controller_->stop();
    resetControls();
  }
  QWidget::changeEvent(event);
}
} // namespace crawling
