#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointF>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVector>

class QCloseEvent;
class QDialog;
class QLabel;
class QLineEdit;
class QKeyEvent;
class QPaintEvent;
class QProcessEnvironment;
class QSlider;
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
    void discoveryProcessFinished(int exitCode, QProcess::ExitStatus status);
    void statusProcessFinished(int exitCode, QProcess::ExitStatus status);
    void profileProcessFinished(int exitCode, QProcess::ExitStatus status);
    void discoveryProcessTimeout();
    void statusProcessTimeout();
    void profileProcessTimeout();
    void applyDomain();
    void toggleDrive();
    void toggleAuto();
    void resetCorrection();
    void emergencyStop();
    void releaseEmergencyStop();
    void openContourDialog();
    void drivePressed();
    void driveReleased();

private:
    void buildUi();
    QString resolveRos2Root() const;
    QString resolveRos2Overlay() const;
    void configureRosEnvironment(QProcessEnvironment &environment) const;
    void publishTwist(double linear, double angular);
    void callService(const QString &service, const QString &type, const QString &request);
    QProcess *startCli(const QStringList &arguments, bool includeOverlay = true);
    void parseStatus(const QByteArray &output);
    void parseProfile(const QByteArray &output);
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
    QSlider *speedSlider_ = nullptr;
    QSlider *turnSlider_ = nullptr;
    ContourPlot *plot_ = nullptr;
    QDialog *contourDialog_ = nullptr;
    ContourPlot *dialogPlot_ = nullptr;

    QTimer commandTimer_;
    QTimer discoveryTimer_;
    QTimer statusTimer_;
    QTimer profileTimer_;
    QTimer discoveryTimeoutTimer_;
    QTimer statusTimeoutTimer_;
    QTimer profileTimeoutTimer_;
    QProcess *discoveryProcess_ = nullptr;
    QProcess *statusProcess_ = nullptr;
    QProcess *profileProcess_ = nullptr;
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
};

#endif // MAINWINDOW_H
