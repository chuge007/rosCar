#pragma once

#include "drive_settings.h"

#include <QHash>
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
class QLineEdit;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace crawling {
class ClampMotorController;

class ClampMotorSettingsWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit ClampMotorSettingsWidget(QWidget* parent = nullptr);
  void setController(ClampMotorController* controller);
  ~ClampMotorSettingsWidget() override;
  ClampMotorAxes settings() const;
  void setSettings(const ClampMotorAxes& axes);
  void setSyncEnabled(int axis, bool enabled);
  bool syncEnabled(int axis) const;
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
    QLineEdit* alias = nullptr;
    QDoubleSpinBox* gearRatio = nullptr;
    QPushButton* recordUpper = nullptr;
    QPushButton* recordLower = nullptr;
    QLabel* limitStatus = nullptr;
    double upperLimit = 0.0;
    double lowerLimit = 0.0;
    bool upperLimitEnabled = false;
    bool lowerLimitEnabled = false;
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
  std::array<QCheckBox*, 3> syncCheckboxes_{};
  void updateLimitStatus(MotorForm* form);
  void beginRecordLimit(MotorForm* form, bool upper);
  void applyLimitsNow(int axis, int index, MotorForm* form);
  QPointer<ClampMotorController> controller_;
  QHash<int, bool> pendingLimitRecords_;
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
  void onLimitReached(int axis, int index, bool upper);
  void queueMotorSpeed(int axis, int index, int percent);
  void flushMotorSpeeds();
  QPointer<ClampMotorController> controller_;
  QScrollArea* scroll_ = nullptr;
  QTimer* speedFlushTimer_ = nullptr;
  QHash<int, int> pendingMotorSpeeds_;
  bool syncing_ = false;
  QHash<int, QPointer<QSlider>> sliders_;
  QHash<int, QPointer<QLabel>> limitHints_;
};
} // namespace crawling
