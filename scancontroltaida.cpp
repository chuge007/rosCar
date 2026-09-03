#include "ScanControlTaiDa.h"

#include <QDebug>
#include <qwidget.h>
#include <QDebug>
#include <qwidget.h>
#include <QMessageBox>
#include <QTimer>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QNetworkProxy>


// 辅助函数：计算字节序列的校验和（简单求和）
quint8 ScanControlTaiDa::calculateChecksum(const QByteArray &data) {
    quint8 sum = 0;
    for (int i = 0; i < data.size();  ++i) {
        sum += static_cast<quint8>(data.at(i));
    }
    return sum;
}

void ScanControlTaiDa::runPosintion(int64_t angleControl, uint32_t maxSpeed){


}


void ScanControlTaiDa::processFrame(const QByteArray& frame)
{
    // 最小长度校验
    // CMD(5) + DATA(8) = 13 byte
    if (frame.size() < 13) {
        qDebug() << "[FRAME] length error:" << frame.size();
        return;
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ========== 帧头校验 ==========
    if (buf[0] != 0x3E || buf[1] != 0x9C) {
        qDebug() << "[FRAME] header error";
        return;
    }

    uint8_t id   = buf[2];
    uint8_t len  = buf[3];
    uint8_t cmd_sum = buf[4];

    // CMD 校验
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qDebug() << "[FRAME] CMD checksum error";
        return;
    }

    // DATA 起始索引
    int dataIndex = 5;

    // DATA 校验
    uint8_t data_sum = buf[dataIndex + 7];  // DATA_SUM
    uint8_t calc_data_sum = 0;
    for (int i = 0; i < 7; ++i) {
        calc_data_sum += buf[dataIndex + i];
    }

    if (data_sum != calc_data_sum) {
        qDebug() << "[FRAME] DATA checksum error";
        return;
    }

    // ========== 数据解析 ==========
    int8_t temperature = static_cast<int8_t>(buf[dataIndex + 0]);

    int16_t iq_or_power =
        (int16_t)(buf[dataIndex + 2] << 8 | buf[dataIndex + 1]);

    int16_t speed =
        (int16_t)(buf[dataIndex + 4] << 8 | buf[dataIndex + 3]);

    uint16_t encoder =
        (uint16_t)(buf[dataIndex + 6] << 8 | buf[dataIndex + 5]);

    // ========== 输出 ==========
    qDebug() << "ID:" << id
             << "Temp:" << temperature
             << "IQ/Power:" << iq_or_power
             << "Speed:" << speed
             << "Encoder:" << encoder;

    // 👉 这里就是你要的编码器值
    // encoder = 0 ~ 65535 (取决于 14/15/16bit 编码器)

    //emit positionChange(QPointF);
}

ScanControlTaiDa::ScanControlTaiDa(QObject *parent) :
    ScanControlAbstract(parent)
{

}

ScanControlTaiDa::~ScanControlTaiDa()
{

}




void ScanControlTaiDa::connectFun()
{
    settings = new QSettings("./scan_setting.ini", QSettings::IniFormat);

    timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, &ScanControlTaiDa::performTasks);


}



void ScanControlTaiDa::writeAxisStopStatus(int address)
{

}



void ScanControlTaiDa::readAxisEndState()
{

}




void ScanControlTaiDa::on_connectBtn_clicked()
{
    tcpSocket = new QTcpSocket(this);
    tcpSocket->setProxy(QNetworkProxy::NoProxy);  // ✅ 禁用代理
    tcpSocket->connectToHost(PlcIP, PlcPort);

    connect(tcpSocket, &QTcpSocket::readyRead, this, [this]() {
        rxBuffer.append(tcpSocket->readAll());

        while (true) {
            int idx = rxBuffer.indexOf("\r\n");
            if (idx < 0) break;

            QByteArray frame = rxBuffer.left(idx);
            rxBuffer.remove(0, idx + 2);

            qDebug() << "[TCP FRAME]" << frame;
            processFrame(frame);
        }
    });
}


void ScanControlTaiDa::on_setOriginBtn_clicked()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << QString::fromLocal8Bit("TCP 未连接");
        return;
    }

    // 构造帧命令部分（同串口逻辑）
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x19));      // 命令
    frame.append(static_cast<char>(0x01));   // 电机ID
    frame.append(static_cast<char>(0x04));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    tcpSocket->write(frame);
    tcpSocket->flush();

    qDebug() << QString::fromLocal8Bit("TCP 发送速度控制命令: ID =") << 0x01
             << QString::fromLocal8Bit(" 帧数据: ") << frame.toHex(' ').toUpper();
}



void ScanControlTaiDa::on_xAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddPressed;

}

void ScanControlTaiDa::on_xAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddReleased;

}

void ScanControlTaiDa::on_xSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubPressed;
}

void ScanControlTaiDa::on_xSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubReleased;
}

void ScanControlTaiDa::on_yAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddPressed;
}

void ScanControlTaiDa::on_yAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddReleased;
}

void ScanControlTaiDa::on_ySubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubPressed;
}

void ScanControlTaiDa::on_ySubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubReleased;
}

void ScanControlTaiDa::on_zAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddPressed;
}

void ScanControlTaiDa::on_zAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddReleased;
}

void ScanControlTaiDa::on_zSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubPressed;
}

void ScanControlTaiDa::on_zSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubReleased;
}

void ScanControlTaiDa::on_rAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddPressed;
}

void ScanControlTaiDa::on_rAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddReleased;
}

void ScanControlTaiDa::on_rSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubPressed;
}

void ScanControlTaiDa::on_rSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubReleased;
}


void ScanControlTaiDa::on_setLimitBtn_clicked()
{

}





void ScanControlTaiDa::init()
{

    connectFun();

}




void ScanControlTaiDa::performTasks()
{

    updataCurrentPos();
    perfromJogTasks();
}



void ScanControlTaiDa::updataCurrentPos()
{

    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << QString::fromLocal8Bit("TCP 未连接");
        return;
    }

    // 构造帧命令部分（同串口逻辑）
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x9C));      // 命令
    frame.append(static_cast<char>(0x01));   // 电机ID
    frame.append(static_cast<char>(0x04));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    tcpSocket->write(frame);
    tcpSocket->flush();

    qDebug() << QString::fromLocal8Bit("TCP 发送速度控制命令: ID =") << 0x01
             << QString::fromLocal8Bit(" 帧数据: ") << frame.toHex(' ').toUpper();

}



void ScanControlTaiDa::perfromJogTasks()
{


    if(axisJog != AxisJog::NotAxisJog && axisInch == AxisJog::NotAxisJog){
        switch (axisJog) {
            case XJogAddPressed:{
                sendCommandTcp(0x01, speed,0xA2);
            }break;
            case XJogAddReleased:{
               sendCommandTcp(0x01, 0,0xA2);
            }break;
            case XJogSubPressed:{
               sendCommandTcp(0x01, -speed,0xA2);
            }break;
            case XJogSubReleased:{
                sendCommandTcp(0x01, 0,0xA2);
            }break;
        }

        lastAxisJog=axisJog;
    }
}




void ScanControlTaiDa::sendCommandTcp(quint8 motorId, int32_t speedValue,int cmd) {

    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << QString::fromLocal8Bit("TCP 未连接");
        return;
    }

    // 构造帧命令部分（同串口逻辑）
    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(cmd));      // 命令
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x04));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    // 数据部分
    QByteArray dataFrame;
    quint8* bytes = reinterpret_cast<quint8*>(&speedValue);
    dataFrame.append(static_cast<char>(bytes[0]));
    dataFrame.append(static_cast<char>(bytes[1]));
    dataFrame.append(static_cast<char>(bytes[2]));
    dataFrame.append(static_cast<char>(bytes[3]));
    quint8 dataSum = calculateChecksum(dataFrame);
    dataFrame.append(static_cast<char>(dataSum));

    // 合并帧并发送
    frame.append(dataFrame);
    tcpSocket->write(frame);
    tcpSocket->flush();

    qDebug() << QString::fromLocal8Bit("TCP 发送速度控制命令: ID =") << motorId
             << QString::fromLocal8Bit(" 速度值 =") << speedValue
             << QString::fromLocal8Bit(" 帧数据: ") << frame.toHex(' ').toUpper();
}


void ScanControlTaiDa::on_start(){


}
void ScanControlTaiDa::on_stop(){


}
void ScanControlTaiDa::on_end(){


}
void ScanControlTaiDa::on_backZero(){


}


void ScanControlTaiDa::on_alarmReset(){


}
