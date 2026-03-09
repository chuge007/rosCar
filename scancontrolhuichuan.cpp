#include "ScanControlHuiChuan.h"

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
#include <QElapsedTimer>


// 辅助函数：计算字节序列的校验和（简单求和）
quint8 ScanControlHuiChuan::calculateChecksum(const QByteArray &data) {
    quint8 sum = 0;
    for (int i = 0; i < data.size();  ++i) {
        sum += static_cast<quint8>(data.at(i));
    }
    return sum;
}


//void ScanControlHuiChuan::processFrame(const QByteArray& frame)
//{
//    // 最小长度校验
//    // CMD(5) + DATA(8) = 13 byte
//    if (frame.size() < 13) {
//        qDebug() << "[FRAME] length error:" << frame.size();
//        return;
//    }

//    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

//    // ========== 帧头校验 ==========
//    if (buf[0] != 0x3E || buf[1] != 0x90) {
//        qDebug() << "[FRAME] header error";
//        return;
//    }

//    uint8_t id   = buf[2];
//    uint8_t len  = buf[3];
//    uint8_t cmd_sum = buf[4];

//    // CMD 校验
//    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
//    if (cmd_sum != calc_cmd_sum) {
//        qDebug() << "[FRAME] CMD checksum error";
//        return;
//    }

//    // DATA 起始索引
//    int dataIndex = 5;

//    // DATA 校验
//    uint8_t data_sum = buf[dataIndex + 7];  // DATA_SUM
//    uint8_t calc_data_sum = 0;
//    for (int i = 0; i < 7; ++i) {
//        calc_data_sum += buf[dataIndex + i];
//    }

//    if (data_sum != calc_data_sum) {
//        qDebug() << "[FRAME] DATA checksum error";
//        return;
//    }

//    // ========== 数据解析 ==========
//    int8_t temperature = static_cast<int8_t>(buf[dataIndex + 0]);

//    int16_t iq_or_power =
//        (int16_t)(buf[dataIndex + 2] << 8 | buf[dataIndex + 1]);

//    int16_t speed =
//        (int16_t)(buf[dataIndex + 4] << 8 | buf[dataIndex + 3]);

//    uint16_t encoder =
//        (uint16_t)(buf[dataIndex + 6] << 8 | buf[dataIndex + 5]);

//    // ========== 输出 ==========
//    qDebug() << "ID:" << id
//             << "Temp:" << temperature
//             << "IQ/Power:" << iq_or_power
//             << "Speed:" << speed
//             << "Encoder:" << encoder;

//    // 👉 这里就是你要的编码器值
//    // encoder = 0 ~ 65535 (取决于 14/15/16bit 编码器)

//    emit positionChange(encoder);
//}





void ScanControlHuiChuan::processFrame(const QByteArray& frame)
{
    // ===== 长度校验 =====
    // CMD(5) + DATA(7) = 12 byte
    if (frame.size() < 12) {
        qDebug() << "[0x90] length error:" << frame.size();
        return;
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ===== 帧头校验 =====
    if (buf[0] != 0x3E || buf[1] != 0x90) {
        qDebug() << "[0x90] header error";
        return;
    }

    uint8_t id      = buf[2];
    uint8_t len     = buf[3];   // 应为 0x06
    uint8_t cmd_sum = buf[4];

    // ===== CMD 校验 =====
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qDebug() << "[0x90] CMD checksum error";
        return;
    }

    int dataIndex = 5;

    // ===== DATA 校验 =====
    uint8_t data_sum = buf[dataIndex + 6];  // DATA_SUM
    uint8_t calc_data_sum = 0;
    for (int i = 0; i < 6; ++i) {
        calc_data_sum += buf[dataIndex + i];
    }

    if (data_sum != calc_data_sum) {
        qDebug() << "[0x90] DATA checksum error";
        return;
    }

    // ===== 数据解析 =====
    uint16_t encoder =
            (uint16_t)(buf[dataIndex + 1] << 8 | buf[dataIndex + 0]);

    uint16_t encoderRaw =
            (uint16_t)(buf[dataIndex + 3] << 8 | buf[dataIndex + 2]);

    uint16_t encoderOffset =
            (uint16_t)(buf[dataIndex + 5] << 8 | buf[dataIndex + 4]);

    // ===== 输出 =====
    qDebug() << "[0x90] ID:" << id
             << "Encoder:" << encoder
             << "Raw:" << encoderRaw
             << "Offset:" << encoderOffset;

    // ===== 信号输出 =====
    //emit positionChange(encoder);          // 实际位置
    //emit encoderRawChange(encoderRaw);     // 原始编码器
    //emit encoderOffsetChange(encoderOffset); // 零偏
}


void ScanControlHuiChuan::processFrame92(const QByteArray& frame)
{
    // ===== 长度校验 =====
    // CMD(5) + DATA(9) = 14 byte
    if (frame.size() < 14) {
        qDebug() << "[0x92] length error:" << frame.size();
        return;
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(frame.constData());

    // ===== 帧头 + 命令校验 =====
    if (buf[0] != 0x3E || buf[1] != 0x92) {
        qDebug() << "[0x92] header error";
        return;
    }

    uint8_t id      = buf[2];
    uint8_t len     = buf[3];   // 必须是 0x08
    uint8_t cmd_sum = buf[4];

    if (len != 0x08) {
        qDebug() << "[0x92] LEN error:" << len;
        return;
    }

    // ===== CMD 校验 =====
    uint8_t calc_cmd_sum = buf[0] + buf[1] + buf[2] + buf[3];
    if (cmd_sum != calc_cmd_sum) {
        qDebug() << "[0x92] CMD checksum error";
        return;
    }

    int dataIndex = 5;

    // ===== DATA 校验 =====
    uint8_t data_sum = buf[dataIndex + 8];  // DATA_SUM
    uint8_t calc_data_sum = 0;
    for (int i = 0; i < 8; ++i) {
        calc_data_sum += buf[dataIndex + i];
    }

    if (data_sum != calc_data_sum) {
        qDebug() << "[0x92] DATA checksum error";
        return;
    }

    // ===== 解析 motorAngle (int64_t, little-endian) =====
    int64_t motorAngle = 0;
    for (int i = 0; i < 8; ++i) {
        motorAngle |= (int64_t(buf[dataIndex + i]) << (8 * i));
    }

    // 单位换算：0.01° / LSB
    double angleDeg = motorAngle * 0.01;

    // ===== 输出 =====
    //    qDebug() << "[0x92] ID:" << id
    //             << "motorAngle(raw):" << motorAngle
    //             << "angle(deg):" << angleDeg
    //             <<"mm"<<angleDeg/60.0;

    // ===== 信号 =====
    if(id==2){
        emit positionChangex(angleDeg/60.0);     // 实际工程角度（°）
    }else {
        emit positionChangey(angleDeg/60.0);
    }


}


ScanControlHuiChuan::ScanControlHuiChuan(QObject *parent) :
    ScanControlAbstract(parent)
{
    init();
}

ScanControlHuiChuan::~ScanControlHuiChuan()
{

    delete tcpSocket;
    if (settings) {

        delete settings;
        settings = nullptr;
    }

    if (timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
}




void ScanControlHuiChuan::writeAxisStopStatus(int address)
{

}



void ScanControlHuiChuan::readAxisEndState()
{

}



void ScanControlHuiChuan::on_connectBtn_clicked()
{





    // 连接成功
    connect(tcpSocket, &QTcpSocket::connected, this, [this]{
        qDebug() << "TCP Client conecet";
        emit tcpStateChange(true);
    });

    // 断开连接
    connect(tcpSocket, &QTcpSocket::disconnected, this, [this]{
        qDebug() << "TCP Client disconect";
        emit tcpStateChange(false);
    });

    // ❗连接失败 / 网络错误（关键）
    connect(tcpSocket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this,
            [this](QAbstractSocket::SocketError err){
        qDebug() << "TCP Client faild:" << err
                 << tcpSocket->errorString();
        emit tcpStateChange(false);
    });


    qDebug() << "Try connect:" << PlcIP << PlcPort;

    // 状态切换
    if(tcpSocket->state() == QAbstractSocket::ConnectedState){
        tcpSocket->disconnectFromHost();
    }else {
        tcpSocket->setProxy(QNetworkProxy::NoProxy);
        tcpSocket->connectToHost(PlcIP, PlcPort);

        if (tcpSocket->waitForConnected(3000)) { // 等待最多3秒
            qDebug() << QString::fromLocal8Bit("TCP 连接成功：");
            emit tcpStateChange(true);
        } else {
            qDebug() << QString::fromLocal8Bit("TCP 连接失败：") << tcpSocket->errorString();
            emit tcpStateChange(false);
        }

        connect(tcpSocket, &QTcpSocket::readyRead, this, [this]() {

            QByteArray data = tcpSocket->readAll();
            rxBuffer.append(data);





//            QElapsedTimer timer;
//            timer.start();

//            const int MAX_PROCESS_MS = 5;

//            while (rxBuffer.size() >= 5) {

//                if (timer.elapsed() > MAX_PROCESS_MS) {
//                    break;   // 退出本轮解析，让事件循环继续
//                }


//                // 找 0x3E 帧头
//                int headIndex = rxBuffer.indexOf(char(0x3E));
//                if (headIndex < 0) {
//                    rxBuffer.clear();
//                    return;
//                }

//                // 丢掉帧头前的垃圾
//                if (headIndex > 0)
//                    rxBuffer.remove(0, headIndex);

//                if (rxBuffer.size() < 5)
//                    return; // CMD 不完整，等待数据

//                uint8_t cmd = (uint8_t)rxBuffer[1];
//                int frameLen = (cmd == 0x90) ? 12 : (cmd == 0x92) ? 14 : 0;

//                if (frameLen == 0) {
//                    // 未知命令，丢掉 1 字节，继续找
//                    rxBuffer.remove(0, 1);
//                    return;
//                }

//                if (rxBuffer.size() < frameLen)
//                    return; // 半帧，等待更多数据

//                //qDebug()<<"rxBuffer"<<rxBuffer;

//                QByteArray frame = rxBuffer.left(frameLen);
//                rxBuffer.remove(0, frameLen);

//                // 校验 CMD
//                uint8_t cmd_sum = (uint8_t)frame[4];
//                uint8_t calc_sum = 0;
//                for (int i = 0; i < 4; ++i) calc_sum += (uint8_t)frame[i];

//                if (cmd_sum != calc_sum) {
//                    qDebug() << "[CMD checksum error] cmd:" << QString::number(cmd,16)
//                             << "frame:" << frame.toHex();
//                    return; // 丢掉当前帧
//                }

//                if (cmd == 0x92){
//                 processFrame92(frame);
//                }
//                // 分发
////                if (cmd == 0x90)
////                    processFrame(frame);
////                else if (cmd == 0x92)
////                    processFrame92(frame);
//            }


            QElapsedTimer timer;
            timer.start();

            const int MAX_PROCESS_MS = 5;   // 单次最多处理 5ms（推荐 2~10ms）

            while (rxBuffer.size() >= 5) {

                // ⛔ 时间片限制
                if (timer.elapsed() > MAX_PROCESS_MS) {
                    break;   // 退出本轮解析，让事件循环继续
                }

                // ====== 原逻辑 ======

                int headIndex = rxBuffer.indexOf(char(0x3E));
                if (headIndex < 0) {
                    rxBuffer.clear();
                    break;
                }

                if (headIndex > 0)
                    rxBuffer.remove(0, headIndex);

                if (rxBuffer.size() < 5)
                    break;

                uint8_t cmd = (uint8_t)rxBuffer[1];
                int frameLen = (cmd == 0x90) ? 12 :
                               (cmd == 0x92) ? 14 : 0;

                if (frameLen == 0) {
                    rxBuffer.remove(0, 1);
                    continue;
                }

                if (rxBuffer.size() < frameLen)
                    break;

                QByteArray frame = rxBuffer.left(frameLen);
                rxBuffer.remove(0, frameLen);

                // CMD 校验
                uint8_t cmd_sum = (uint8_t)frame[4];
                uint8_t calc_sum = 0;
                for (int i = 0; i < 4; ++i) calc_sum += (uint8_t)frame[i];

                if (cmd_sum != calc_sum) {
                    qDebug() << "[CMD checksum error] cmd:" << QString::number(cmd,16)
                             << "frame:" << frame.toHex();
                    continue;   // 丢帧但继续解析
                }

                // 分发处理
                if (cmd == 0x92){
                    processFrame92(frame);
                }
            }
        });


    }

}

void ScanControlHuiChuan::on_setOriginBtn_clicked()
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
    frame.append(static_cast<char>(0x00));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    tcpSocket->write(frame);
    tcpSocket->flush();

    QTimer::singleShot(10, [this]() {
        QByteArray frame;
        frame.append(static_cast<char>(0x3E));      // 帧头
        frame.append(static_cast<char>(0x19));      // 命令
        frame.append(static_cast<char>(0x02));   // 电机ID
        frame.append(static_cast<char>(0x00));      // 数据长度
        quint8 cmdSum = calculateChecksum(frame);   // 校验和
        frame.append(static_cast<char>(cmdSum));
        tcpSocket->write(frame);
        tcpSocket->flush();
    });


    qDebug() << QString::fromLocal8Bit("TCP 发送速度控制命令: ID =") << 0x19
             << QString::fromLocal8Bit(" 帧数据: ") << frame.toHex(' ').toUpper();
}



void ScanControlHuiChuan::on_xAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddPressed;

}

void ScanControlHuiChuan::on_xAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogAddReleased;

}

void ScanControlHuiChuan::on_xSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubPressed;
}

void ScanControlHuiChuan::on_xSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::XJogSubReleased;
}

void ScanControlHuiChuan::on_yAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddPressed;
}

void ScanControlHuiChuan::on_yAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogAddReleased;
}

void ScanControlHuiChuan::on_ySubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubPressed;
}

void ScanControlHuiChuan::on_ySubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::YJogSubReleased;
}

void ScanControlHuiChuan::on_zAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddPressed;
}

void ScanControlHuiChuan::on_zAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogAddReleased;
}

void ScanControlHuiChuan::on_zSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubPressed;
}

void ScanControlHuiChuan::on_zSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::ZJogSubReleased;
}

void ScanControlHuiChuan::on_rAddBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddPressed;
}

void ScanControlHuiChuan::on_rAddBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogAddReleased;
}

void ScanControlHuiChuan::on_rSubBtn_pressed()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubPressed;
}

void ScanControlHuiChuan::on_rSubBtn_released()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    if((tasks.count() != 0)  || isEndScan || isAxisStop || isJogDone || isRunTarget) return;
    updateCurPos = false;
    axisJog = AxisJog::RJogSubReleased;
}


void ScanControlHuiChuan::on_setLimitBtn_clicked()
{

}





void ScanControlHuiChuan::init()
{
    settings = new QSettings("./scan_setting.ini", QSettings::IniFormat);

    timer = new QTimer(this);
    timer->setInterval(400);
    connect(timer, &QTimer::timeout, this, &ScanControlHuiChuan::performTasks);
    timer->start();
    tcpSocket=new QTcpSocket(this);

}



void ScanControlHuiChuan::performTasks()
{

    if(!pushsend){
        perfromJogTasks();
        updataCurrentPos();
    }
}



void ScanControlHuiChuan::updataCurrentPos()
{

    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        //qDebug() << QString::fromLocal8Bit("TCP 未连接");
        return;
    }


    QByteArray frame92;
    frame92.append(0x3E);
    frame92.append(0x92);
    frame92.append(2);  // ID
    frame92.append(static_cast<char>(0x00));  // 数据长度
    frame92.append(calculateChecksum(frame92));

    tcpSocket->write(frame92);
    tcpSocket->flush();


    QTimer::singleShot(200, [this]() {
        QByteArray frame92;
        frame92.append(0x3E);
        frame92.append(0x92);
        frame92.append(1);  // ID
        frame92.append(static_cast<char>(0x00));  // 数据长度
        frame92.append(calculateChecksum(frame92));

        tcpSocket->write(frame92);
        tcpSocket->flush();
    });
}



void ScanControlHuiChuan::perfromJogTasks()
{

    //qDebug()<<"axisJog"<<axisJog;
    if((lastAxisJog==XJogAddReleased||lastAxisJog==XJogSubReleased||lastAxisJog==YJogAddReleased
        ||lastAxisJog==YJogSubReleased||axisJog==0)
            &&axisJog==lastAxisJog ){return;}

    if(axisJog != AxisJog::NotAxisJog && axisInch == AxisJog::NotAxisJog){
        int motorId1=0x01;
        int motorId2=0x02;
        switch (axisJog) {
        case XJogAddPressed:{
            sendCommandTcp(motorId2, -speed,0xA2);
        }break;
        case XJogAddReleased:{
            sendCommandTcp(motorId2, 0,0xA2);
            axisJog==0;
        }break;
        case XJogSubPressed:{
            sendCommandTcp(motorId2, speed,0xA2);
        }break;
        case XJogSubReleased:{
            sendCommandTcp(motorId2, 0,0xA2);
            axisJog==0;
        }break;
        case YJogAddPressed:{
            sendCommandTcp(motorId1, speed,0xA2);
        }break;
        case YJogAddReleased:{
            sendCommandTcp(motorId1, 0,0xA2);
            axisJog==0;
        }break;
        case YJogSubPressed:{
            sendCommandTcp(motorId1, -speed,0xA2);
        }break;
        case YJogSubReleased:{
            sendCommandTcp(motorId1, 0,0xA2);
            axisJog==0;
        }break;
        }

        lastAxisJog=axisJog;

    }
}




void ScanControlHuiChuan::sendCommandTcp(quint8 motorId, int speedValue,int cmd) {



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
    //tcpSocket->waitForBytesWritten(100);

    //    qDebug() << QString::fromLocal8Bit("TCP 发送速度控制命令: ID =") << motorId
    //             << QString::fromLocal8Bit(" 速度值 =") << speedValue
    //             << QString::fromLocal8Bit(" 帧数据: ") << frame.toHex(' ').toUpper();

}

void ScanControlHuiChuan::runPosintion(int64_t angleControl, uint32_t maxSpeed)
{
    QByteArray frame;

    /* ---------- CMD FRAME ---------- */
    frame.append(static_cast<char>(0x3E));   // 帧头
    frame.append(static_cast<char>(0xA4));   // 命令
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x0C));   // 数据长度(12 byte data)

    quint8 cmdSum = calculateChecksum(frame); // CMD[0..3]校验
    frame.append(static_cast<char>(cmdSum));  // CMD_SUM


    /* ---------- DATA FRAME ---------- */
    QByteArray dataFrame;

    // angleControl (8 byte)
    quint8* angleBytes = reinterpret_cast<quint8*>(&angleControl);
    for(int i = 0; i < 8; ++i){
        dataFrame.append(static_cast<char>(angleBytes[i]));
    }

    // maxSpeed (uint32_t, 4 byte)
    quint8* speedBytes = reinterpret_cast<quint8*>(&maxSpeed);
    for(int i = 0; i < 4; ++i){
        dataFrame.append(static_cast<char>(speedBytes[i]));
    }

    // DATA_SUM
    quint8 dataSum = calculateChecksum(dataFrame); // DATA[0..11]
    dataFrame.append(static_cast<char>(dataSum));

    /* ---------- 拼接总帧 ---------- */
    frame.append(dataFrame);

    /* ---------- 发送 ---------- */
    tcpSocket->write(frame);
    tcpSocket->flush();

    //tcpSocket->waitForBytesWritten(150);
    qDebug() << "A4 CMD HEX =" << frame.toHex(' ');
}



void ScanControlHuiChuan::on_start(){


}
void ScanControlHuiChuan::on_stop(){


}
void ScanControlHuiChuan::on_end(){

    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x81));      // 命令
    frame.append(static_cast<char>(0x01));   // 电机ID
    frame.append(static_cast<char>(0x00));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    tcpSocket->write(frame);
    tcpSocket->flush();

    pushsend=false;


    QTimer::singleShot(10, [this]() {

        QByteArray frame;
        frame.append(static_cast<char>(0x3E));      // 帧头
        frame.append(static_cast<char>(0x81));      // 命令
        frame.append(static_cast<char>(0x02));   // 电机ID
        frame.append(static_cast<char>(0x00));      // 数据长度

        quint8 cmdSum = calculateChecksum(frame);
        frame.append(static_cast<char>(cmdSum));
        tcpSocket->write(frame);
        tcpSocket->flush();

        pushsend=false;
    });

}
void ScanControlHuiChuan::on_backZero(){


}


void ScanControlHuiChuan::on_alarmReset(){

    QByteArray frame;
    frame.append(static_cast<char>(0x3E));      // 帧头
    frame.append(static_cast<char>(0x9B));      // 命令
    frame.append(static_cast<char>(motorId));   // 电机ID
    frame.append(static_cast<char>(0x00));      // 数据长度
    quint8 cmdSum = calculateChecksum(frame);   // 校验和
    frame.append(static_cast<char>(cmdSum));

    tcpSocket->write(frame);
    tcpSocket->flush();
}
