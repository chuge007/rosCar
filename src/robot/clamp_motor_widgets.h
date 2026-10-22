#pragma once

#include "drive_settings.h"

#include <QPointer>
#include <QWidget>
#include <array>

class QComboBox;
class QFormLayout;
class QGroupBox;
class QHideEvent;
class QScrollArea;
class QSpinBox;
class QTabWidget;

namespace crawling {
class ClampMotorController;

class ClampMotorSettingsWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit ClampMotorSettingsWidget(QWidget* parent = nullptr);
  ~ClampMotorSettingsWidget() override;
  ClampMotorAxes settings() const;
  void setSettings(const ClampMotorAxes& axes);
  void refreshPorts();
 signals:
  void edited();
 private:
  struct MotorForm {
    QWidget* page = nullptr;
    QFormLayout* form = nullptr;
    QComboBox* mode = nullptr;
    QComboBox* port = nullptr;
    QComboBox* baud = nullptr;
    QComboBox* bitrate = nullptr;
    QComboBox* sign = nullptr;
    QSpinBox* id = nullptr;
    QGroupBox* zdt = nullptr;
    QComboBox* firmware = nullptr;
    QSpinBox* maximumRpm = nullptr;
    QSpinBox* acceleration = nullptr;
  };
  void resizeAxis(int axis, int count);
  void addMotor(int axis, const ClampMotorSettings& motor);
  void updateMode(MotorForm* form);
  void notifyEdited();
  std::array<QSpinBox*, 3> counts_{};
  std::array<QTabWidget*, 3> tabs_{};
  std::array<QVector<MotorForm*>, 3> forms_;
  bool loading_ = false;
};

class ClampMotorControlWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit ClampMotorControlWidget(ClampMotorController* controller,
                                    QWidget* parent = nullptr);
  void resetControls();
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void changeEvent(QEvent* event) override;
 private:
  void rebuild();
  QPointer<ClampMotorController> controller_;
  QScrollArea* scroll_ = nullptr;
};
} // namespace crawling
