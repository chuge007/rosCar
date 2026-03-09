#ifndef ScanControlTaiDa_H
#define ScanControlTaiDa_H

#include "scancontrolabstract.h"
#include <QMutex>
#include <QTcpSocket>

class ScanControlTaiDa : public ScanControlAbstract
{
    Q_OBJECT

public:
    explicit ScanControlTaiDa(QObject *parent = nullptr);
    ~ScanControlTaiDa();

    void setModbusTcpIP(QString ip) { PlcIP = ip;}   //设置IP
    void setModbusPort(int port) { PlcPort = port;}   //设置端口
void runPosintion(int64_t angleControl, uint32_t maxSpeed);

public slots:
    void init();

    void on_connectBtn_clicked();

    void on_setOriginBtn_clicked();     //设置起点

    //点动
    void on_xAddBtn_pressed();  //X+
    void on_xAddBtn_released();
    void on_xSubBtn_pressed();  //X-
    void on_xSubBtn_released();
    void on_yAddBtn_pressed();  //Y+
    void on_yAddBtn_released();
    void on_ySubBtn_pressed();  //Y-
    void on_ySubBtn_released();
    void on_zAddBtn_pressed();  //Y+
    void on_zAddBtn_released();
    void on_zSubBtn_pressed();  //Y-
    void on_zSubBtn_released();
    void on_rAddBtn_pressed();  //Z+
    void on_rAddBtn_released();
    void on_rSubBtn_pressed();  //Z-
    void on_rSubBtn_released() ;


    void on_setLimitBtn_clicked();      //设置机械极限位

    void on_start();
    void on_stop();
    void on_end();
    void on_alarmReset();
    void on_backZero();

    void processFrame(const QByteArray& frame);
    quint8 calculateChecksum(const QByteArray &data);
private:
        QMutex mutex;
        AxisJog lastAxisJog = AxisJog::NotAxisJog;

private slots:
    void performTasks();




private:

    void connectFun();

    void writeAxisStopStatus(int address);
    void writeBackZero();
    void readAxisEndState();

    void updataCurrentPos();        //更新当前点位置
    void perfromJogTasks();         //点动



private:
    QTcpSocket *tcpSocket = nullptr;
    QString PlcIP;
    int PlcPort = 502;
    QSettings *settings = nullptr;
    QTimer *timer = nullptr;
    QByteArray rxBuffer;

    float speed;
    void sendCommandTcp(quint8 motorId, int32_t speedValue,int cmd);

    float xScanLenght = 0;
    float yScanLenght = 0;
    float yScanStep = 0;
    float xVelocity = 20;
    float yVelocity = 20;
    float jogVelocity = 20;
    int jopStep = 1;
    double keepTime = 0;

    bool isInit = true;         //链接服务初始化
    bool isStartScan = false;   //开始扫查标志位
    bool isPerform = false;     //任务执行标志位
    bool isStopScan = false;    //暂停扫查标志位
    bool isKeepScan = false;    //继续扫查标志位
    bool isAxisStop = false;    //轴停止状态
    bool updateCurPos = false;  //更新轴位置
    bool isEndScan = false;     //结束扫查标志位
    bool isJogDone = false;     //点动完成标志位

    bool isRunTarget = false;   //执行目标点

    bool isHaveMachine = true;  //判断是否有机械原点
    bool isHaveLimit = true;    //判断是否有机械极限

    bool manState[2] = {false, false};  //手动状态位

    QQueue<QPair<QString, float>> tasks;    //任务列表

    AxisJog axisInch = AxisJog::NotAxisJog;      //寸动
    AxisJog  axisJog = AxisJog::NotAxisJog;      //点动

    QPointF zeroPoint  = QPointF(0,0);       //虚拟0点
    QPointF limitPoint = QPointF(500, 400);  //极限位置
    QPointF currentPos = QPointF(0,0);

};

#endif // ScanControlTaiDa_H
