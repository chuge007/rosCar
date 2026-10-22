#pragma once

#include "drive_settings.h"

#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QSpinBox;

namespace crawling {
class ClampMotorController;
class ClampMotorSettingsWidget;
class ClampMotorControlWidget;
}

class ProbeAdjustmentPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ProbeAdjustmentPanel(QWidget *parent = nullptr);
    ProbeAdjustmentPanel(crawling::ClampMotorController *controller,
                         QWidget *parent);
    ~ProbeAdjustmentPanel() override;

private:
    void refreshPorts();
    void loadSettings();
    void saveSettings();
    bool applySettings();

    QPointer<crawling::ClampMotorController> m_controller;
    crawling::DriveSettings m_settings;
    crawling::ClampMotorSettingsWidget *m_motorSettings = nullptr;
    crawling::ClampMotorControlWidget *m_motorControls = nullptr;
    QLabel *m_status = nullptr;
};
