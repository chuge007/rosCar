#ifndef CLIENT_H
#define CLIENT_H

#if defined(_WIN32) || defined(_WIN64)
#ifdef CLIENT_LIBRARY
#define CLIENT_EXPORT __declspec(dllexport)
#else
#define CLIENT_EXPORT __declspec(dllimport)
#endif
#else
#define CLIENT_EXPORT
#endif

#undef CLIENT_LIBRARY

// 检测是否在Qt环境中
#ifdef __cplusplus
#if defined(QT_CORE_LIB) || defined(QT_VERSION) || defined(Q_MOC_RUN)
#define USING_QT 1
#include <QByteArray>
#include <QObject>
#else
#define USING_QT 0
#include <cstdint>
#include <functional>
#include <vector>
#endif
#else
#define USING_QT 0
#endif

#include <functional>
#include <memory>
#include <string>

#include "basic_def.h"

class CLIENT_EXPORT Client
#if USING_QT
    : public QObject
#endif
{
#if USING_QT
    Q_OBJECT
#endif

public:
    static Client &getInstance();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

protected:
    explicit Client
#if USING_QT
        (QObject *parent = nullptr
#else
        ()
#endif
        );
    ~Client();

public:
    // ========== Multi-server management interface ==========
    /// \brief cn:添加服务器 en:Adding servers
    void addServer(const std::string &address, int deviceId);

    /// \brief cn:移除服务器 en:Remove servers
    bool removeServer(const std::string &address, int deviceId);

    /// \brief cn:设置已有设备的ip地址
    /// \brief en:Set the ip address of an existing device
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setServerAddr(const std::string &address, int deviceId);

    /// \brief cn:设置当前活动的设备
    /// \brief en:Sets the currently active device
    bool setCurrentServerId(int deviceId);
    /// \return en:Obtain the current server address of the configuration file
    std::string getCurrentServerAddr();
    int getCurrentServerId();
    /// \brief  cn:配置服务器端口
    /// \brief  en:To configure the server port, the client needs to be restarted
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setServerPort(quint16 port);
    /// \brief  cn:连接服务器 en:Connect to the server
    /// \return cn:是否连接成功 en:Whether connection was successful
    bool Connect();
    /// \brief  cn:断开连接 en:Disconnect to the server
    /// \return cn:是否断开成功 en:Whether disconnection was successful
    bool Disconnect();

    struct ServerInfo
    {
        int serverId;
        std::string address;
        bool connected;
    };
    /// \brief 获取所有服务器信息
    std::vector<ServerInfo> getAllServers() const;

    /***************************** Capture Config ******************************/
    /// \brief cn:开始采集 en:Start data acquisition
    /// \param[in] flag true:开始采集  en:Start data acquisition
    /// \param[in] flag false:停止采集 en:Stop data acquisition
    bool startCapture(bool flag = true);
    /// \brief  cn:重新编码器 en:Re-acquire data
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool resetCapture();
    /// \brief  cn:获取beam的数量 en:Get number of beams
    /// \return cn:声束数量 en:Beam count
    /// \retval -1 cn:获取失败 en:For failure
    /// \retval >0 cn:获取成功 en:For success
    int getBeamCounts(int groupId = -1);

    /// \brief cn:获取声束位置（x坐标） en:Get beam positions (x-coordinates)
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:声束x坐标列表 en:Beam x-coordinates
    std::vector<double> getBeamPositions(int groupId = -1);

    /****************************** Group Config ************************/
    /// \brief cn:设置当前工作组 en:Set current working group
    /// \param [in] groupId cn:工作组编号>=1 en:Working group ID>=1
    /// \return cn:是否设置成功 en:Whether setting was successful
    /// \note  cn:当前活动是指参数设置、获取默认都是该组
    /// \note  en:The active group is used by default for parameter operations
    bool setCurrentGroup(int groupId = -1);
    /// \brief  cn:获取当前活动的工作组 en:Get the current active group
    /// \retval cn:当前活动工作组的编号 en:Current active group ID
    int getCurrentGroup();
    /// \brief cn:增加工作组,从指定的组复制一个工作组
    /// \brief en:Add a group by copying from a specified group
    /// \param cn:groupId指定工作组 -1表示当前工作组
    /// \param en:groupId specifies group; -1 refers to current group
    /// \return cn:是否添加成功 en:Whether addition was successful
    bool copyGroup(int groupId = -1);
    /// \brief cn:删除工作组,至少保留一个组
    /// \brief en:Delete a group, at least one must be retained
    /// \param[in] groupId cn:需要删除的工作组编号;-1 表示当前工作组
    /// \param[in] groupId en:groupId of group to delete; -1 indicates current group
    /// \return cn:是否删除成功 en:Whether deletion was successful
    bool removeGroup(int groupId = -1);
/// \brief cn:获取所有工作组编号 en:Get all group IDs
/// \param[in] cn:flag:true 获取使能的Group信息 false:获取所有Group信息
/// \param[in] en:flag:true gets enabled groups; false:gets all groups
/// \retval cn:存放所有工作组编号的容器 en:Container storing all group IDs
#if USING_QT
    QVector<int> getGroupsNo(bool flag = true);
#else
    std::vector<int> getGroupsNo(bool flag = true);
#endif

    /// \brief  cn:设置扫查模式 (全局参数) en:Set scan mode (global parameter)
    /// \param  cn:扫查模式 en:Scan mode
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setScanMode(int mode);
    /// \brief  cn:获取扫查模式 (全局参数) en:Get scan mode (global parameter)
    int getScanMode();

    /****************************** General Config ************************/
    /// \brief cn:设置工作组增益 en:Set group gain
    /// \param[in] cn:设置增益值 en:Set gain value (0.01dB)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGain(double gain, int groupId = -1);
    /// \brief  cn:获取增益值(db) en:Get gain value
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:工作组的增益 en:gain
    double getGain(int groupId = -1);
    /// \brief  cn:设置工作组范围起点 en:Set group range start point
    /// \param  cn:范围起点(mm) en:range start
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setRangeStart(double start, int groupId = -1);
    /// \brief  cn:获取工作组范围起点 en:Get group range start point
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:工作组范围起点 en:range start point
    double getRangeStart(int groupId = -1);
    /// \brief  cn:设置工作组范围终点 en:Set group range end point
    /// \param  cn:范围终点(mm) en:range end
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setRangeEnd(double end, int groupId = -1);
    /// \brief  cn:获取工作组范围终点 en:Get group range end point
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:工作组范围终点 en:range end point
    double getRangeEnd(int groupId = -1);
    /// \brief  cn:设置工件声速 en:Set velocity of workpiece
    /// \param  cn:工件声速 en:velocity of workpiece (m/s)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWorkpieceVelocity(double velocity, int groupId = -1);
    /// \brief  cn:获取工件声速 en:Get velocity of workpiece
    /// \return cn:工件声速 en:velocity of workpiece (m/s)
    double getWorkpieceVelocity(int groupId = -1);

    /// \brief  cn:设置激发电压 en:Set group excitation voltage
    /// \param  cn:激发电压 en:voltage (0.1V)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setPaVoltage(int voltage);
    bool setUtVoltage(int voltage);
    /// \brief  cn:获取激发电压 en:Get group excitation voltage
    /// \return cn:工作组激发电压 en:excitation voltage
    int getPaVoltage();
    int getUtVoltage();
    /// \brief  cn:设置工作组脉冲宽度 en:Set group pulse width
    /// \param  cn:脉冲宽度 en:pulse width (ns)
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setPulseWidth(int width, int groupId = -1);
    /// \brief  cn:获取工作组脉冲宽度(ns) en:Get group pulse width
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:工作组脉冲宽度 en:pulse width
    int getPulseWidth(int groupId = -1);
    /// \brief  cn:设置工作组电压极性 en:Set group voltage polarity
    /// \param  cn:电压极性模式 en:voltage polarity mode
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setVoltagePolarity(PolarityType mode, int groupId = -1);
    /// \brief  cn:获取工作组电压极性 en:Get group voltage polarity
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:工作组电压极性 en:voltage polarity
    PolarityType getVoltagePolarity(int groupId = -1);
    /// \brief  cn:低通滤波值 en:Low-pass filter value
    bool setFilterLow(double freq, int groupId = -1);
    double getFilterLow(int groupId = -1);
    /// \brief  cn:高通滤波值 en:High-pass filter value
    bool setFilterHigh(double freq, int groupId = -1);
    double getFilterHigh(int groupId = -1);
    /// \brief  cn:设置视频滤波 en:Set video filter
    /// \param[in] cn:视频滤波类型 en:video filter type
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setVideoFilterMHz(VideoFilter type, int groupId = -1);
    /// \brief  cn:获取视频滤波 en:Get video filter
    /// \param  cn:工作组编号 en:groupId
    /// \return cn:视频滤波类型 en:video filter type
    VideoFilter getVideoFilterMHz(int groupId = -1);
    /// \brief  cn:设置检波模式 en:Set detection mode
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setRectifierMode(RectifierType mode, int groupId = -1);
    /// \brief  cn:获取检波模式 en:Get detection mode
    RectifierType getRectifierMode(int groupId = -1);
    /// \brief  cn:设置工作组采样点数量 en:Set number of sampling points in group
    /// \param[in] cn:采样点数 en:quantity
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setPointQuantity(int quantity, int groupId = -1);
    /// \brief cn:获取工作组采样点数量 en:Get number of sampling points in group
    int getPointQuantity(int groupId = -1);
    /// \brief cn:设置TFM接收延迟 en:Set the Receiving Delay for TFM
    /// \param[in] cn:零点位置 en:position (ns)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setTfmRecDelay(double delay, int groupId = -1);
    /// \brief cn:获取TFM接收延迟 en:Get the Receiving Delay for TFM
    /// \return cn:工作组TFM接收延迟 en:TFM Receiving Delay
    double getTfmRecDelay(int groupId = -1);
    /// \brief cn:设置最大幅值 en:Set amplitudeType
    /// \param cn:幅值 en:AmplitudeType
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setMaxAmplitude(MaxAmplitude type, int groupId = -1);
    ///\brief cn:获取最大幅值  en:Get AmplitudeType
    MaxAmplitude getMaxAmplitude(int groupId = -1);
    /// \brief  cn:设置抑制值 en:Set suppression value
    /// \param  cn:抑制值 en:reject value
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setReceiverReject(int reject, int groupId = -1);
    /// \brief  cn:获取抑制值 en:Get suppression value
    int getReceiverReject(int groupId = -1);
    /// \brief  cn:设置采集帧率（hz) en:Set frame rate value
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setFrameRate(int rate);
    /// \brief  cn:获取PRF值 en:Get PRF value
    int getFrameRate();

    /// \brief  cn:扫描类型 en:Scan type
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setScanType(scanType type, int groupId = -1);
    /// \brief  cn:获取扫描类型 en:Get scan type
    scanType getScanType(int groupId = -1);
    /// \brief  cn:设置波束模式 en:Set beam mode
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamMode(BeamMode mode, int groupId = -1);
    /// \brief  cn:获取波束模式 en:Get beam mode
    BeamMode getBeamMode(int groupId = -1);
    /// \brief  cn设置孔径 en:Set aperture
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamAperture(int BeamAperture, int groupId = -1);
    /// \brief  cn:获取孔径 en:Get aperture
    int getBeamAperture(int groupId = -1);
    /// \brief  cn:设置步进 en:Set step
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamElementStep(int step, int groupId = -1);
    /// \brief  cn:获取步进 en:Get step
    int getBeamElementStep(int groupId = -1);
    /// \brief  cn:设置角度步进 en:Set angle step
    /// \param  BeamAngleStep 角度步进 (0.01°)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamAngleStep(double BeamAngleStep, int groupId = -1);
    /// \brief  cn:获取角度步进 en:Get angle step
    double getBeamAngleStep(int groupId = -1);
    /// \brief  cn:设置首晶片 en:Set first element
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamFirstElement(int firstElement, int groupId = -1);
    /// \brief  cn:获取首晶片 en:Get first element
    int getBeamFirstElement(int groupId = -1);
    /// \brief  cn:设置末晶片 en:Set last element
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamLastElement(int lastElement, int groupId = -1);
    /// \brief  cn:获取末晶片 en:Get last element
    int getBeamLastElement(int groupId = -1);
    /// \brief  cn:设置起始角度,激发角度1 en:Set start angle (excitation angle 1)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamAngleMin(double BeamAngleMin, int groupId = -1);
    /// \brief  cn:获取起始角度 en:Get start angle
    double getBeamAngleMin(int groupId = -1);
    /// \brief  cn:设置结束角度，激发角度2 en:Set end angle (excitation angle 2)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setBeamAngleMax(double BeamAngleMax, int groupId = -1);
    /// \brief  cn:获取结束角度 en:Get end angle
    double getBeamAngleMax(int groupId = -1);
    /// \brief  cn:设置线扫角度 en:Set linear angle
    bool setBeamAngleLinear(double angle, int groupId = -1);
    double getBeamAngleLinear(int groupId = -1);
    /// \brief  cn:设置聚焦模式 en:Set focusing mode
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setFocusMode(FocalType mode, int groupId = -1);
    /// \brief  cn:获取聚焦模式 en:Get focusing mode
    FocalType getFocusMode(int groupId = -1);
    /// \brief  cn:设置聚焦位置 en:Set focusing position
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setFocalPosition(double position, int groupId = -1);
    /// \brief  cn:获取聚焦位置 en:Get focusing position
    double getFocalPosition(int groupId = -1);

    /************************* Gate Config *******************************/
    /// \brief  cn:设置闸门A使能 en:Enable Gate A
    /// \param[in] cn:闸门使能 en:enable
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateAEnable(bool enable, int groupId = -1);
    /// \brief cn:获取闸门A使能状态 en:Get Gate A enable status
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门A使能状态 en:Gate A enable status
    bool getGateAEnable(int groupId = -1);
    /// \brief cn:设置闸门A同步模式 en:Set Gate A synchronization mode
    /// \param[in] cn:闸门同步选项 en:Gate synchronization options
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateASynchronMode(GateSynchron synchro, int groupId = -1);
    /// \brief cn:获取闸门A同步模式 en:Get Gate A synchronization mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门A同步选项 en:Gate A synchronization options
    GateSynchron getGateASynchronMode(int groupId = -1);
    /// \brief cn:设置闸门A起始位置 en:Set Gate A start position
    /// \param[in] cn:闸门A起始位置 en:Gate A start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateAStart(double start, int groupId = -1);
    /// \brief cn:获取闸门A起始位置 en:Get Gate A start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门A起始位置 en:Gate A start position
    double getGateAStart(int groupId = -1);
    /// \brief cn:设置闸门A终点位置 en:Set Gate A end position
    /// \param[in] cn:闸门A终点位置  en:Gate A end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateAEnd(double end, int groupId = -1);
    /// \brief cn:获取闸门A终点位置 en:Get Gate A end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门A终点位置 en:Gate A end position
    double getGateAEnd(int groupId = -1);
    /// \brief cn:设置闸门A阈值 en:Set Gate A threshold
    /// \param[in] cn:闸门A阈值 (%) en:Gate A threshold (%)
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateAThreshold(double threshold, int groupId = -1);
    /// \brief  cn:获取闸门A阈值 en:Get Gate A threshold
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门A阈值 en:Gate A threshold
    double getGateAThreshold(int groupId = -1);
    /// \brief cn:设置闸门A测量模式 en:Set Gate A measurement mode
    /// \param[in] cn:闸门A测量模式 en:Gate A measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateAMeasureType(measureType measureType, int groupId = -1);
    /// \brief cn:获取闸门A测量模式 en:Get Gate A measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门A测量模式 en:Gate A measurement mode
    measureType getGateAMeasureType(int groupId = -1);

    /// \brief cn:设置闸门B使能 en:Enable Gate B
    /// \param[in] cn:闸门使能 en:enable
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateBEnable(bool enable, int groupId = -1);
    /// \brief cn:获取闸门B使能状态 en:Get Gate B enable status
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B使能状态 en:Gate B enable status
    bool getGateBEnable(int groupId = -1);
    /// \brief cn:设置闸门B同步模式 en:Set Gate B synchronization mode
    /// \param[in] cn:闸门同步选项 en:Gate synchronization options
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateBSynchronMode(GateSynchron synchro, int groupId = -1);
    /// \brief cn:获取闸门B同步模式 en:Get Gate B synchronization mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B同步选项 en:Gate B synchronization options
    GateSynchron getGateBSynchronMode(int groupId = -1);
    /// \brief cn:设置闸门B起始位置 en:Set Gate B start position
    /// \param[in] cn:闸门B起始位置  en:Gate B start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateBStart(double start, int groupId = -1);
    /// \brief cn:获取闸门B起始位置 en:Get Gate B start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B起始位置 en:Gate B start position
    double getGateBStart(int groupId = -1);
    /// \brief cn:设置闸门B终点位置 en:Set Gate B end position
    /// \param[in] cn:闸门B终点位置 en:Gate B end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateBEnd(double end, int groupId = -1);
    /// \brief cn:获取闸门B终点位置 en:Get Gate B end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B终点位置 en:Gate B end position
    double getGateBEnd(int groupId = -1);
    /// \brief cn:设置闸门B阈值 en:Set Gate B threshold
    /// \param[in] cn:闸门B阈值 (%) en:Gate B threshold (%)
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateBThreshold(double threshold, int groupId = -1);
    /// \brief cn:获取闸门B阈值 en:Get Gate B threshold
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B阈值 en:en:Gate B threshold
    double getGateBThreshold(int groupId = -1);
    /// \brief cn:设置闸门B测量模式 en:Set Gate B measurement mode
    /// \param[in] cn:闸门B测量模式 en:Gate B measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateBMeasureType(measureType measureType, int groupId = -1);
    /// \brief cn:获取闸门B测量模式 en:Get Gate B measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B测量模式 en:Gate B measurement mode
    measureType getGateBMeasureType(int groupId = -1);

    /// \brief cn:设置闸门C使能 en:Enable Gate C
    /// \param[in] cn:闸门使能 en:enable
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateCEnable(bool enable, int groupId = -1);
    /// \brief cn:获取闸门C使能状态 en:Get Gate C enable status
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门C使能状态 en:Gate C enable status
    bool getGateCEnable(int groupId = -1);
    /// \brief cn:设置闸门C同步模式 en:Set Gate C synchronization mode
    /// \param[in] cn:闸门同步选项 en:Gate synchronization options
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateCSynchronMode(GateSynchron synchro, int groupId = -1);
    /// \brief cn:获取闸门C同步模式 en:Get Gate C synchronization mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门C同步选项 en:Gate C synchronization options
    GateSynchron getGateCSynchronMode(int groupId = -1);
    /// \brief cn:设置闸门C起始位置 en:Set Gate C start position
    /// \param[in] cn:闸门C起始位置  en:Gate C start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateCStart(double start, int groupId = -1);
    /// \brief cn:获取闸门C起始位置 en:Get Gate C start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门C起始位置 en:Gate C start position
    double getGateCStart(int groupId = -1);
    /// \brief cn:设置闸门C终点位置 en:Set Gate C end position
    /// \param[in] cn:闸门C终点位置 en:Gate C end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateCEnd(double end, int groupId = -1);
    /// \brief cn:获取闸门C终点位置 en:Get Gate C end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门C终点位置 en:Gate C end position
    double getGateCEnd(int groupId = -1);
    /// \brief cn:设置闸门C阈值 en:Set Gate C threshold
    /// \param[in] cn:闸门C阈值 (%) en:Gate C threshold (%)
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateCThreshold(double threshold, int groupId = -1);
    /// \brief cn:获取闸门C阈值 en:Get Gate C threshold
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门C阈值 en:en:Gate C threshold
    double getGateCThreshold(int groupId = -1);
    /// \brief cn:设置闸门C测量模式 en:Set Gate C measurement mode
    /// \param[in] cn:闸门C测量模式 en:Gate C measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateCMeasureType(measureType measureType, int groupId = -1);
    /// \brief cn:获取闸门C测量模式 en:Get Gate C measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门C测量模式 en:Gate C measurement mode
    measureType getGateCMeasureType(int groupId = -1);

    /// \brief cn:设置闸门I使能 en:Enable Gate I
    /// \param[in] cn:闸门使能 en:enable
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateIEnable(bool enable, int groupId = -1);
    /// \brief cn:获取闸门I使能状态 en:Get Gate I enable status
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门I使能状态 en:Gate I enable status
    bool getGateIEnable(int groupId = -1);
    /// \brief cn:设置闸门I同步模式 en:Set Gate I synchronization mode
    /// \param[in] cn:闸门同步选项 en:Gate synchronization options
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateISynchronMode(GateSynchron synchro, int groupId = -1);
    /// \brief cn:获取闸门B同步模式 en:Get Gate B synchronization mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门B同步选项 en:Gate B synchronization options
    GateSynchron getGateISynchronMode(int groupId = -1);
    /// \brief cn:设置闸门I起始位置 en:Set Gate I start position
    /// \param[in] cn:闸门I起始位置 en:Gate I start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateIStart(double start, int groupId = -1);
    /// \brief cn:获取闸门I起始位置 en:Get Gate I start position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门I起始位置 en:Gate I start position
    double getGateIStart(int groupId = -1);
    /// \brief cn:设置闸门I终点位置 en:Set Gate I end position
    /// \param[in] cn:闸门I终点位置 en:Gate I end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateIEnd(double end, int groupId = -1);
    /// \brief cn:获取闸门I终点位置 en:Get Gate I end position
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门I终点位置 en:Gate I end position
    double getGateIEnd(int groupId = -1);
    /// \brief cn:设置闸门I阈值 en:Set Gate I threshold
    /// \param[in] cn:闸门I阈值 en:threshold (%)
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateIThreshold(double threshold, int groupId = -1);
    /// \brief cn:获取闸门I阈值 en:Get Gate I threshold
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门I阈值 en:Gate I threshold
    double getGateIThreshold(int groupId = -1);
    /// \brief cn:设置闸门I测量模式 en:Set Gate I measurement mode
    /// \param[in] cn:闸门I测量模式 en:Gate I measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateIMeasureType(measureType measureType, int groupId = -1);
    /// \brief cn:获取闸门I测量模式 en:Get Gate I measurement mode
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:闸门I测量模式 en:Gate I measurement mode
    measureType getGateIMeasureType(int groupId = -1);
    /// \brief cn:设置闸门I同步采集 en:Set Gate I sync acquisition
    /// \param[in] cn:开关 en:enable
    /// \param[in] cn:工作组编号 en:groupId
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setGateISyncSample(bool enable, int groupId = -1);
    bool getGateISyncSample(int groupId = -1);

    /************************* Wedge Config *******************************/
    /// \brief cn:设置楔块使能 en:Set enable wedge
    /// \param[in] cn:楔块使能 en:enable
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeEnable(bool enable, int groupId = -1);
    /// \brief  cn:获取楔块使能状态 en:Get wedge enable status
    bool getWedgeEnable(int groupId = -1);
    /// \brief  cn:设置楔块角度 en:Set wedge angle
    /// \param  cn:楔块角度 en:wedge angle
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeAngle(double angle, int groupId = -1);
    /// \brief  cn:获取楔块角度 en:Get wedge angle
    double getWedgeAngle(int groupId = -1);
    /// \brief  cn:设置楔块声速 en:Set wedge velocity
    /// \param  cn:楔块声速 en:wedge velocity (m/s)
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeVelocity(double wedgeVelocity, int groupId = -1);
    /// \brief  cn:获取楔块声速 en:Get wedge velocity
    double getWedgeVelocity(int groupId = -1);
    /// \brief  cn:设置楔块X值 en:Set wedge X value
    /// \param  cn:x值 en:x value
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeX(double x, int groupId = -1);
    /// \brief  cn:获取楔块X值 en:Get wedge X value
    double getWedgeX(int groupId = -1);
    /// \brief  cn:设置楔块Z值 en:Set wedge Z value
    /// \param  cn:z值 en:z value
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeZ(double z, int groupId = -1);
    /// \brief  cn:获取楔块Z值 en:Get wedge Z value
    double getWedgeZ(int groupId = -1);
    /// \brief  cn:设置楔块屋顶角 en:Set wedge roof angle
    /// \param  cn:楔块屋顶角 en:roof angle
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeRoofAngle(double roofangle, int groupId = -1);
    /// \brief  cn:获取楔块屋顶角 en:Get wedge roof angle
    double getWedgeRoofAngle(int groupId = -1);
    /// \brief  cn:设置楔块GCp值 en:Set wedge GCp value
    /// \param  cn:gcp值 en:gcp value
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeGCp(double gcp, int groupId = -1);
    /// \brief  cn:获取楔块GCp值 en:Get wedge GCp value
    double getWedgeGCp(int groupId = -1);
    /// \brief  cn:设置楔块长度 en:Set wedge length
    /// \param  cn:楔块长度  en:wedge length
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeLength(double wedgeLength, int groupId = -1);
    /// \brief  cn:获取楔块长度 en:Get wedge length
    double getWedgeLength(int groupId = -1);
    /// \brief  cn:设置楔块宽度 en:Set wedge width
    /// \param  cn:楔块宽度  en:wedge width
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeWidth(double wedgeWidth, int groupId = -1);
    /// \brief  cn:获取楔块宽度 en:Get wedge width
    double getWedgeWidth(int groupId = -1);
    /// \brief  cn:设置楔块高度 en:Set wedge height
    /// \param  cn:楔块高度 en:wedge height
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setWedgeHeight(double wedgeHeight, int groupId = -1);
    /// \brief  cn:获取楔块高度 en:Get wedge height
    double getWedgeHeight(int groupId = -1);

    /// \brief  cn:设置探头频率 en:Set probe frequency
    /// \param  cn:频率 en:frequency
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setProbeFrequency(double frequency, int groupId = -1);
    /// \brief  cn:获取探头频率 en:Get probe frequency
    double getProbeFrequency(int groupId = -1);
    /// \brief  cn:设置双晶使能 en:Enable dual-element probe
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setDualCrystalEnable(bool enable, int groupId = -1);
    /// \brief  cn:获取双晶使能状态 en:Get dual-element probe status
    bool getDualCrystalEnable(int groupId = -1);
    /// \brief  cn:设置反装使能 en:Enable reverse mounting
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setReverseEnable(bool flag, int groupId = -1);
    /// \brief  cn:获取反装使能状态 en:Get reverse mounting status
    bool getReverseEnable(int groupId = -1);
    /// \brief  cn:设置探头主轴阵元数量 en:Set number of elements in probe main axis
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setProbePrimaryElements(int elements, int groupId = -1);
    /// \brief  cn:获取探头主轴阵元数量 en:Get number of elements in probe main axis
    int getProbePrimaryElements(int groupId = -1);
    /// \brief  cn:设置探头主轴阵元间距 cn:Set pitch of elements in probe main axis
    /// \param  cn:间距 en:pitch
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setProbePrimaryElementsPitch(double pitch, int groupId = -1);
    /// \brief  cn:获取探头主轴阵元间距 en:Get pitch of elements in probe main axis
    double getProbePrimaryElementsPitch(int groupId = -1);
    /// \brief  cn:设置探头次轴阵元数量 en:Set number of elements in probe secondary axis
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setProbeSecondaryElements(int elements, int groupId = -1);
    /// \brief  cn:获取探头次轴阵元数量 en:Get number of elements in probe secondary axis
    int getProbeSecondaryElements(int groupId = -1);
    /// \brief  cn:设置探头次轴阵元间距 en:Set pitch of elements in probe secondary axis
    /// \param  cn:间距 en:pitch
    /// \return cn:是否设置成功 en:Whether setting was successful
    bool setProbeSecondaryElementsPitch(double pitch, int groupId = -1);
    /// \brief  cn:获取探头次轴阵元间距 en:Get pitch of elements in probe secondary axis
    double getProbeSecondaryElementsPitch(int groupId = -1);
    /// \brief  cn:设置发射通道  en:Set transmit channel
    /// \return cn:是否设置成功  en:Whether setting was successful
    bool setTransmissionChannel(int transmission, int groupId = -1);
    /// \brief  cn:获取发射通道  en:Get transmit channel
    int getTransmissionChannel(int groupId = -1);
    /// \brief  cn:设置接收通道  en:Set receive channel
    /// \return cn:是否设置成功  en:Whether setting was successful
    bool setReceptionChannel(int reception, int groupId = -1);
    /// \brief cn:获取接收通道  en:Get receive channel
    int getReceptionChannel(int groupId = -1);

    /******************* TCG *************************/
    /// \brief  cn:设置TCG使能  en:Enable TCG (Time-Corrected Gain)
    /// \return cn:是否设置成功  en:Whether setting was successful
    bool setTcgEnable(bool enable, int groupId = -1);
    /// \brief  cn:获取TCG使能状态 en:Get TCG enable status
    /// \return cn:TCG是否使能 en:Whether TCG is enabled
    bool getTcgEnable(int groupId = -1);

    /// \brief  cn:设置第index个tcg点深度 en:Set the depth of the TCG point at the specified index
    /// \param  cn:index从1开始,index最大值为10
    /// \param  en:Index starts from 1, and the maximum index value is 10
    /// \param  cn:tcg点深度  en:depth
    /// \return cn:是否设置成功  en:Whether setting was successful
    bool setTcgPointDepthIndex(int index, double depth, int groupId = -1);
    /// \brief  cn:获取第index个tcg点深度 en:Get depth of TCG point at index
    /// \param  cn:index从1开始,index最大值为10
    /// \param  en:Index starts from 1, and the maximum index value is 10
    double getTcgPointDepthIndex(int index, int groupId = -1);
    /// \brief  cn:设置指定声束第index个tcg点深度 en:Set depth of TCG point at index for specific
    /// beam \param  cn:index从1开始 \param  en:Index starts from 1, and the maximum
    /// index value is 10 \param  cn:tcg点深度  en:depth \param  cn:声束索引，从0开始 en:Beam index,
    /// starting from 0 \return cn:是否设置成功  en:Whether setting was successful
    bool setTcgPointDepth(int index, double depth, int beamIndex, int groupId = -1);
    /// \brief  cn:获取指定声束第index个tcg点深度 en:Get depth of TCG point at index for specific
    /// beam \param  cn:index从1开始 \param  en:Index starts from 1, and the maximum
    /// index value is 10 \param  cn:声束索引，从0开始 en:Beam index, starting from 0
    double getTcgPointDepth(int index, int beamIndex, int groupId = -1);
    /// \brief  cn:设置第index个tcg点增益 en:Set gain of TCG point at index
    /// \param  cn:index从1开始,index最大值为10
    /// \param  en:Index starts from 1, and the maximum index value is 10
    /// \param  cn:tcg点增益 en:gain
    /// \return cn:是否设置成功  en:Whether setting was successful
    bool setTcgPointGainIndex(int index, double gain, int groupId = -1);
    /// \brief  cn:获取第index个tcg点增益 en:Get gain of TCG point at index
    /// \param  cn:index从1开始,index最大值为10
    /// \param  en:Index starts from 1, and the maximum index value is 10
    double getTcgPointGainIndex(int index, int groupId = -1);
    /// \brief  cn:设置指定声束第index个tcg点增益 en:Set gain of TCG point at index for specific
    /// beam \param  cn:index从1开始 \param  en:Index starts from 1, and the maximum
    /// index value is 10 \param  cn:tcg点增益 en:gain \param  cn:声束索引，从0开始 en:Beam index,
    /// starting from 0 \return cn:是否设置成功  en:Whether setting was successful
    bool setTcgPointGain(int index, double gain, int beamIndex, int groupId = -1);
    /// \brief  cn:获取指定声束第index个tcg点增益 en:Get gain of TCG point at index for specific
    /// beam \param  cn:index从1开始 \param  en:Index starts from 1, and the maximum
    /// index value is 10 \param  cn:声束索引，从0开始 en:Beam index, starting from 0
    double getTcgPointGain(int index, int beamIndex, int groupId = -1);
    /// \brief  cn:设置编码器方向 en:Set encoder direction
    bool setScannerDirection(ScannerDir mode);
    ScannerDir getScannerDirection();

    /// \brief  cn:设置厚度
    bool setThickness(double thick, int groupId);
    double getThickness(int groupId);

    /******************* Config File Save/Load *************************/
    /// \brief cn:保存配置到文件 en:Save configuration to file
    /// \param filePath cn:文件路径，为空则保存到默认路径 en:File path, empty for default path
    /// \return cn:是否成功 en:true on success
    bool saveConfig(const std::string &filePath = "");
    /// \brief cn:从文件加载配置 en:Load configuration from file
    /// \param filePath cn:文件路径，为空则从默认路径加载 en:File path, empty for default path
    /// \return cn:是否成功 en:true on success
    bool loadConfig(const std::string &filePath = "");
    /// \brief cn:将当前配置批量推送到设备 en:Push current config to device in batch
    void pushConfigToDevice();
    /// \brief cn:获取默认配置文件路径 en:Get default config file path
    static std::string getDefaultConfigPath();

    using DataPacketCallback = void (*)(const char *data, int length, int deviceId);
    // Setting the callback
    void setDataPacketCallback(DataPacketCallback callback);

    // 移除回调
    void removeDataPacketCallback();

#if USING_QT
signals:
    void DataPacket(const QByteArray &datagram, int deviceId);
#endif

private:
    class ClientPrivate;
    std::shared_ptr<ClientPrivate> config;
};

#endif // CLIENT_H
