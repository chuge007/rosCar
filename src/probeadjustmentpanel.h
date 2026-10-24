#pragma once

#include "drive_settings.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;

namespace pa1664::remote { class RemoteWorkbenchSession; }

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
    ProbeAdjustmentPanel(crawling::ClampMotorController *controller,
                         pa1664::remote::RemoteWorkbenchSession *remoteSession,
                         QWidget *parent);
    ~ProbeAdjustmentPanel() override;

private:
    void refreshPorts();
    void loadSettings();
    void saveSettings();
    bool applySettings();

    QPointer<crawling::ClampMotorController> m_controller;
    pa1664::remote::RemoteWorkbenchSession *m_remoteSession = nullptr;
    crawling::DriveSettings m_settings;
    QHash<int, double> m_remotePositions;
    crawling::ClampMotorSettingsWidget *m_motorSettings = nullptr;
    crawling::ClampMotorControlWidget *m_motorControls = nullptr;
    QLabel *m_status = nullptr;
    QComboBox *m_spacingMotorCombo = nullptr;
    QLabel *m_spacingGearLabel = nullptr;
    QLabel *m_spacingProtocolLabel = nullptr;
    QDoubleSpinBox *m_minSpacingSpin = nullptr;
    QPushButton *m_resetZeroButton = nullptr;
    QDoubleSpinBox *m_targetSpacingSpin = nullptr;
    QPushButton *m_moveToSpacingButton = nullptr;
    QLabel *m_currentSpacingLabel = nullptr;
    int m_spacingMotorAxis = -1;
    int m_spacingMotorIndex = -1;
    double m_spacingZeroDegrees = 0.0;
    bool m_spacingZeroValid = false;
    bool m_resetZeroPending = false;
    bool m_refreshingSpacingMotors = false;
    bool m_remoteClampConnected = false;

    void refreshSpacingMotors();
    void saveSpacingSettings();
    const crawling::ClampMotorSettings *spacingMotor() const;
    void onSpacingMotorChanged(int index);
    void onResetZero();
    void onMoveToSpacing();
    void onPositionReceived(int axis, int index, double degrees);
};
