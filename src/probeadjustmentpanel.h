#pragma once

#include "drive_settings.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QSpinBox;

namespace crawling {
class ClampMotorController;
}

class ProbeAdjustmentPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ProbeAdjustmentPanel(QWidget *parent = nullptr);
    ~ProbeAdjustmentPanel() override;

private:
    void refreshPorts();
    void loadSettings();
    void saveSettings();
    void applySettings();

    crawling::ClampMotorController *m_controller = nullptr;
    crawling::DriveSettings m_settings;
    QComboBox *m_port = nullptr;
    QComboBox *m_baud = nullptr;
    QComboBox *m_bitrate = nullptr;
    QSpinBox *m_nodeId = nullptr;
    QSpinBox *m_xId = nullptr;
    QSpinBox *m_yId = nullptr;
    QSpinBox *m_zId = nullptr;
    QLabel *m_status = nullptr;
};
