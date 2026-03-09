#ifndef SCANCONTROLABSTRACT_H
#define SCANCONTROLABSTRACT_H

#include <QObject>
#include <QModbusTcpClient>
#include <QModbusReply>
#include <QDoubleValidator>
#include <QPointF>
#include <QTimer>
#include <QQueue>
#include <QPair>
#include <QSettings>
#include <QTimer>
#include <QTime>

#include "modbusconfig.h"
#include <QTcpSocket>


enum AxisJog{       //寸动
    NotAxisJog = 0,
    XJogAdd,
    XJogSub,
    YJogAdd,
    YJogSub,
    ZJogAdd,
    ZJogSub,
    RJogAdd,
    RJogSub,

    XJogAddPressed ,
    XJogAddReleased,
    XJogSubPressed ,
    XJogSubReleased,
    YJogAddPressed ,
    YJogAddReleased,
    YJogSubPressed ,
    YJogSubReleased,
    ZJogAddPressed ,
    ZJogAddReleased,
    ZJogSubPressed ,
    ZJogSubReleased,
    RJogAddPressed ,
    RJogAddReleased,
    RJogSubPressed ,
    RJogSubReleased,
};


class ScanControlAbstract : public QObject
{
    Q_OBJECT

public:
    explicit ScanControlAbstract(QObject *parent = nullptr) : QObject(parent){}
    virtual ~ScanControlAbstract() {}

    virtual void setModbusTcpIP(QString ip)  = 0;   //设置IP
    virtual void setModbusPort(int port)  = 0;      //设置端口

    //virtual QPointF currentPosition() const  = 0;   //当前位置

    virtual void runPosintion(int64_t angleControl, uint32_t maxSpeed)=0;

    float speed=0;
    uint8_t motorId = 0x01;   // 电机ID = 1号电机

    bool pushsend=false;
public slots:
    virtual void init() = 0;   //初始化设置

    virtual void on_connectBtn_clicked() = 0;       //连接服务

    virtual void  updataCurrentPos()=0;
    //点动
    virtual void on_xAddBtn_pressed() = 0;  //X+
    virtual void on_xAddBtn_released() = 0;
    virtual void on_xSubBtn_pressed() = 0;  //X-
    virtual void on_xSubBtn_released() = 0;
    virtual void on_yAddBtn_pressed() = 0;  //Y+
    virtual void on_yAddBtn_released() = 0;
    virtual void on_ySubBtn_pressed() = 0;  //Y-
    virtual void on_ySubBtn_released() = 0;
    virtual void on_zAddBtn_pressed() = 0;  //Z+
    virtual void on_zAddBtn_released() = 0;
    virtual void on_zSubBtn_pressed() = 0;  //Z-
    virtual void on_zSubBtn_released() = 0;
    virtual void on_rAddBtn_pressed() = 0;  //Z+
    virtual void on_rAddBtn_released() = 0;
    virtual void on_rSubBtn_pressed() = 0;  //Z-
    virtual void on_rSubBtn_released() = 0;


    virtual void on_setLimitBtn_clicked() = 0;      //设置机械极限位

    virtual void on_start()=0;
    virtual void on_stop()=0;
    virtual void on_end()=0;
    virtual void on_alarmReset()=0;
    virtual void on_backZero()=0;
    virtual void on_setOriginBtn_clicked()=0;

signals:
    void tcpStateChange(bool connected);
    void positionChangex(float posx);                   //轴位置变化
    void positionChangey(float posy);


};

#endif // SCANCONTROLABSTRACT_H
