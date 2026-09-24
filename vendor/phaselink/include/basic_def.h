#ifndef BASIC_DEF_H
#define BASIC_DEF_H
// 电压极性
enum class PolarityType {
    Negative, // 负脉冲
    Positive, // 正脉冲
    Duplex    // 全脉冲
};

// 检波模式
enum class RectifierType {
    Rectifier_RF, // 射频
    Rectifier_FW, // 全波
    Rectifier_HW, // 正半波
    Rectifier_NW, // 负半波
};

// 视频滤波
enum class VideoFilter : int {
    k1MHz = 1000,
    k2MHz = 2000,
    k3MHz = 3000,
    k4MHz = 4000,
    k5MHz = 5000,
    k6MHz = 6000,
    k7MHz = 7000,
    kBypass = 8000
};

// 扫描类型
enum class scanType {
    LinearScan = 1,
    SectorialScan,
    UltrasonicMicroscopy,
    UTMode,
    FocalLawTypeUnknown
};
// 波束模式
enum class BeamMode {
    OneBeam,     // 单波束
    DoubleBeams, // 双波束
    FourBeams    // 4 波束
};

// 聚焦模式
enum class FocalType {
    FocalType_TrueDepth,     // 深度聚焦
    FocalType_HalfPath,      // 等声程聚焦
    NdtFocalType_Projection, // 水平聚焦
    No_Focal                 // 不聚焦
};

// 同步模式
enum class GateSynchron { Pulser = 0, GateI, GateA, GateB, SyncTypeUnknown };

// 闸门测试方式
enum class measureType {
    MaxPeak = 0,
    WaveFront,
}; // 0:波峰 1:边沿

// 编码器方向
enum class ScannerDir { Normal, Reverse };

enum class MaxAmplitude : int {
    kPercentage1600 = 2048,
    kPercentage800 = 4096,
    kPercentage400 = 8192,
    kPercentage200 = 16384
};

#endif // BASIC_DEF_H
