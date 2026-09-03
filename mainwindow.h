#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDoubleValidator>


#include "scancontroltaida.h"
#include "scancontrolhuichuan.h"
struct Point2D {
    float x;
    float y;
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    int currentPathIndex = 0;
    std::vector<Point2D> path;
    QTimer* arriveTimer = nullptr;

     bool stopScan;


    void sendNextPoint();
    void MainWindow::startArriveCheck(const Point2D& target);

    std::vector<Point2D> generateBowScanPathDense(
            float xLength,
            float yLength,
            float yStep
        );


    QSettings *settings;
    int origin;
    int start;
    int end;


    int xo;
    int yo;

    int xs;
    int ys;

    int xe;
    int ye;

    int sweep;


    float xlenght;
    float ylenght;
    float step;

    Point2D position;


private:
    void initWidget();
    void connectFun();
    void updateWight();
    void saveseting();
private slots:

    void scanEnd();

    void on_connectBtn_clicked();

    void updatePosition(float pos);
    void updatePosition2(float pos);
    void setMOrigin();

    void setOrigin();
    void setStart();
    void setEnd();
    void on_backZero();

    void backOrigin_velocity();
    void jog_velocity();

private:
    Ui::MainWindow *ui;

    ScanControlAbstract *scanCtrl;
    ScanControlAbstract *scanCtrl2;

    ScanControlHuiChuan *scanCtrlHunChuan;
    ScanControlHuiChuan *scanCtrlTaiDa;

};

#endif // MAINWINDOW_H
