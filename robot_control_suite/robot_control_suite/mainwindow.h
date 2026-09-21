#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>
#include <QPointF>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVector>

class QCloseEvent;
class QDialog;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QKeyEvent;
class QPaintEvent;
class QProcessEnvironment;
class QWidget;

class ContourPlot : public QWidget
{
    Q_OBJECT
public:
    explicit ContourPlot(QWidget *parent = nullptr);
    void setPoints(const QVector<QPointF> &points);
    void setSeamLateral(double lateral, bool valid);
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPointF> points_;
    double seamLateral_ = 0.0;
    bool seamValid_ = false;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void commandTick();
    void pollDiscovery();
    void pollStatus();
    void pollProfile();
    void pollJointState();
    void discoveryProcessFinished(int exitCode, QProcess::ExitStatus status);
    void statusProcessFinished(int exitCode, QProcess::ExitStatus status);
    void profileProcessFinished(int exitCode, QProcess::ExitStatus status);
    void jointProcessFinished(int exitCode, QProcess::ExitStatus status);
    void discoveryProcessTimeout();
    void statusProcessTimeout();
    void profileProcessTimeout();
    void jointProcessTimeout();
    void applyDomain();
    void applyDriveLimits();
    void applyMotorMapping();
    void toggleDrive();
    void toggleAuto();
    void startAutoWalk();
    void stopAutoWalk();
    void captureLocalization();
    void resetCorrection();
    void emergencyStop();
    void releaseEmergencyStop();
    void openContourDialog();
    void drivePressed();
    void driveReleased();

private:
    void buildUi();
    void loadSettings();
    void saveSettings() const;
    QString resolveRos2Root() const;
    QString resolveRos2Overlay() const;
    void configureRosEnvironment(QProcessEnvironment &environment) const;
    void publishTwist(double linear, double angular);
    void startCmdVelBridge();
    void sendPendingTwist();
    void callService(const QString &service, const QString &type, const QString &request);
    QProcess *startCli(const QStringList &arguments, bool includeOverlay = true);
    void parseStatus(const QByteArray &output);
    void parseImu(const QByteArray &output);
    void parseProfile(const QByteArray &output);
    void parseJointState(const QByteArray &output);
    void updateCommandFromKeys();
    void setPressed(const QString &key, bool pressed);

    QLineEdit *domainEdit_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QLabel *modeLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *errorLabel_ = nullptr;
    QLabel *previewErrorLabel_ = nullptr;
    QLabel *headingLabel_ = nullptr;
    QLabel *curvatureLabel_ = nullptr;
    QLabel *angularAccelLabel_ = nullptr;
    QLabel *linearCommandLabel_ = nullptr;
    QLabel *angularCommandLabel_ = nullptr;
    QLabel *confidenceLabel_ = nullptr;
    QLabel *fitLabel_ = nullptr;
    QLabel *motorLabel_ = nullptr;
    QDoubleSpinBox *linearSpeedSpin_ = nullptr;
    QDoubleSpinBox *angularSpeedSpin_ = nullptr;
    QDoubleSpinBox *linearAccelSpin_ = nullptr;
    QDoubleSpinBox *angularAccelSpin_ = nullptr;
    QDoubleSpinBox *maxWheelSpeedSpin_ = nullptr;
    QDoubleSpinBox *minimumInnerWheelRatioSpin_ = nullptr;
    QSpinBox *leftMotorIdSpin_ = nullptr;
    QSpinBox *rightMotorIdSpin_ = nullptr;
    QComboBox *leftMotorSignCombo_ = nullptr;
    QComboBox *rightMotorSignCombo_ = nullptr;
    ContourPlot *plot_ = nullptr;
    QDialog *contourDialog_ = nullptr;
    ContourPlot *dialogPlot_ = nullptr;

    QTimer commandTimer_;
    QTimer discoveryTimer_;
    QTimer statusTimer_;
    QTimer profileTimer_;
    QTimer jointTimer_;
    QTimer discoveryTimeoutTimer_;
    QTimer statusTimeoutTimer_;
    QTimer profileTimeoutTimer_;
    QTimer jointTimeoutTimer_;
    QProcess *discoveryProcess_ = nullptr;
    QProcess *statusProcess_ = nullptr;
    QProcess *profileProcess_ = nullptr;
    QProcess *jointProcess_ = nullptr;
    QProcess *cmdVelProcess_ = nullptr;
    QString ros2Root_;
    QString ros2Overlay_;
    QString ros2Python_;
    QString ros2Script_;
    QSet<QString> pressedKeys_;
    bool ros2Available_ = false;
    bool rosDiscovered_ = false;
    bool manualEnabled_ = false;
    bool autoEnabled_ = false;
    bool emergencyLatched_ = false;
    bool contourValid_ = false;
    bool geometryValid_ = false;
    double contourLateral_ = 0.0;
    QVector<QPointF> contourPoints_;
    QByteArray lastStatusOutput_;
    QByteArray lastImuOutput_;
    QByteArray lastProfileOutput_;
    QByteArray lastJointOutput_;
    double headingReferenceRad_ = 0.0;
    double pendingLinear_ = 0.0;
    double pendingAngular_ = 0.0;
    bool leftJointValid_ = false;
    bool rightJointValid_ = false;
    double leftJointPosition_ = 0.0;
    double leftJointVelocity_ = 0.0;
    double leftJointEffort_ = 0.0;
    double rightJointPosition_ = 0.0;
    double rightJointVelocity_ = 0.0;
    double rightJointEffort_ = 0.0;
};

#endif // MAINWINDOW_H
