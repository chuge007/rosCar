#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mv3dlp::vendor {

#if defined(_WIN32)
#define MV3DLP_CALL __stdcall
#else
#define MV3DLP_CALL
#endif

using Status = std::int32_t;
using Handle = void*;
using Bool = std::int32_t;

constexpr Status kOk = static_cast<Status>(0x00000000u);
constexpr Status kErrorHandle = static_cast<Status>(0x80060000u);
constexpr Status kErrorSupport = static_cast<Status>(0x80060001u);
constexpr Status kErrorBufferOverrun = static_cast<Status>(0x80060002u);
constexpr Status kErrorCallOrder = static_cast<Status>(0x80060003u);
constexpr Status kErrorParameter = static_cast<Status>(0x80060004u);
constexpr Status kErrorResource = static_cast<Status>(0x80060005u);
constexpr Status kErrorNoData = static_cast<Status>(0x80060006u);
constexpr Status kErrorPrecondition = static_cast<Status>(0x80060007u);
constexpr Status kErrorVersion = static_cast<Status>(0x80060008u);
constexpr Status kErrorNoEnoughBuffer = static_cast<Status>(0x80060009u);
constexpr Status kErrorAbnormalImage = static_cast<Status>(0x8006000Au);
constexpr Status kErrorLoadLibrary = static_cast<Status>(0x8006000Bu);
constexpr Status kErrorAlgorithm = static_cast<Status>(0x8006000Cu);
constexpr Status kErrorDeviceOffline = static_cast<Status>(0x8006000Du);
constexpr Status kErrorAccessDenied = static_cast<Status>(0x8006000Eu);
constexpr Status kErrorOutOfRange = static_cast<Status>(0x8006000Fu);
constexpr Status kErrorUnknown = static_cast<Status>(0x800600FFu);

constexpr Bool kTrue = 1;
constexpr Bool kFalse = 0;

constexpr std::uint32_t kImageTypeMono8 = 0x01080001u;
constexpr std::uint32_t kImageTypeDepth = 0x011000B8u;
constexpr std::uint32_t kImageTypeProfile = 0x023000B9u;
constexpr std::uint32_t kImageTypePointCloud = 0x026000C0u;
constexpr std::uint32_t kImageTypeRgb24Packed = 0x02180014u;
constexpr std::uint32_t kImageTypeJpeg = 0x80180001u;
constexpr std::uint32_t kImageTypeProfileAbc32 = 0x82603001u;

constexpr std::uint32_t kExceptionDisconnect = 1u;
constexpr std::size_t kMaxStringLength = 256u;
constexpr std::size_t kMaxEnumCount = 16u;

enum class ParamType : std::int32_t {
    undefined = -1,
    bool_value = 1,
    int_value = 2,
    float_value = 3,
    enum_value = 4,
    string_value = 5,
};

struct DeviceInfoRaw {
    char chManufacturerName[32];
    char chModelName[32];
    char chDeviceVersion[32];
    char chManufacturerSpecificInfo[48];
    char chSerialNumber[16];
    char chUserDefinedName[16];
    unsigned char chMacAddress[8];
    std::int32_t enIPCfgMode;
    char chCurrentIp[16];
    char chCurrentSubNetMask[16];
    char chDefultGateWay[16];
    char chNetExport[16];
    std::uint32_t nDevTypeInfo;
    std::uint8_t nReserved[12];
};

struct ImageDataRaw {
    std::int32_t enImageType;
    std::uint32_t nWidth;
    std::uint32_t nHeight;
    std::uint8_t* pData;
    std::uint32_t nDataLen;
    std::uint8_t* pIntensityData;
    std::uint32_t nIntensityDataLen;
    std::uint32_t nFrameNum;
    std::int64_t nTimeStamp;
    Bool bValid;
    float fXScale;
    float fYScale;
    float fZScale;
    std::int32_t nXOffset;
    std::int32_t nYOffset;
    std::int32_t nZOffset;
    std::uint8_t nReserved[16];
};

struct IntParamRaw {
    std::int64_t nCurValue;
    std::int64_t nMax;
    std::int64_t nMin;
    std::int64_t nInc;
};

struct EnumParamRaw {
    std::uint32_t nCurValue;
    std::uint32_t nSupportedNum;
    std::uint32_t nSupportValue[kMaxEnumCount];
};

struct FloatParamRaw {
    float fCurValue;
    float fMax;
    float fMin;
};

struct StringParamRaw {
    char chCurValue[kMaxStringLength];
    std::uint32_t nMaxLength;
};

union ParamInfoRaw {
    Bool bBoolParam;
    IntParamRaw stIntParam;
    FloatParamRaw stFloatParam;
    EnumParamRaw stEnumParam;
    StringParamRaw stStringParam;
};

struct ParamRaw {
    ParamType enParamType;
    ParamInfoRaw ParamInfo;
    std::uint8_t nReserved[16];
};

struct ExceptionInfoRaw {
    std::int32_t enExceptionType;
    char chExceptionDesc[kMaxStringLength];
    std::uint8_t nReserved[4];
};

using ImageDataCallback = void(MV3DLP_CALL*)(ImageDataRaw* image_data, void* user);
using ExceptionCallback = void(MV3DLP_CALL*)(ExceptionInfoRaw* exception_info, void* user);

using FnGetVersion = const char*(MV3DLP_CALL*)();
using FnInitialize = Status(MV3DLP_CALL*)();
using FnFinalize = Status(MV3DLP_CALL*)();
using FnGetDeviceNumber = Status(MV3DLP_CALL*)(std::uint32_t* device_number);
using FnGetDeviceList = Status(MV3DLP_CALL*)(DeviceInfoRaw* devices, std::uint32_t max_count, std::uint32_t* device_count);
using FnOpenDeviceByIP = Status(MV3DLP_CALL*)(Handle* handle, const char* ip_address);
using FnOpenDeviceBySN = Status(MV3DLP_CALL*)(Handle* handle, const char* serial_number);
using FnCloseDevice = Status(MV3DLP_CALL*)(Handle* handle);
using FnRegisterExceptionCallBack = Status(MV3DLP_CALL*)(Handle handle, ExceptionCallback callback, void* user);
using FnStartMeasure = Status(MV3DLP_CALL*)(Handle handle);
using FnStopMeasure = Status(MV3DLP_CALL*)(Handle handle);
using FnSoftTrigger = Status(MV3DLP_CALL*)(Handle handle);
using FnGetImage = Status(MV3DLP_CALL*)(Handle handle, ImageDataRaw* image_data, std::uint32_t timeout_ms);
using FnRegisterImageDataCallBack = Status(MV3DLP_CALL*)(Handle handle, ImageDataCallback callback, void* user);
using FnClearDataBuffer = Status(MV3DLP_CALL*)(Handle handle);
using FnGetParam = Status(MV3DLP_CALL*)(Handle handle, const char* key, ParamRaw* param);
using FnSetParam = Status(MV3DLP_CALL*)(Handle handle, const char* key, ParamRaw* param);
using FnExecute = Status(MV3DLP_CALL*)(Handle handle, const char* key);
using FnMapDepthToPointCloud = Status(MV3DLP_CALL*)(ImageDataRaw* depth_image, ImageDataRaw* point_cloud_image);

class VendorSdk {
public:
    VendorSdk(const std::string& library_path, const std::vector<std::string>& search_paths);
    ~VendorSdk();

    VendorSdk(VendorSdk&& other) noexcept;
    VendorSdk& operator=(VendorSdk&& other) noexcept;

    VendorSdk(const VendorSdk&) = delete;
    VendorSdk& operator=(const VendorSdk&) = delete;

    const std::string& loadedLibraryPath() const noexcept;
    std::string version() const;

    Status initialize() const;
    Status finalize() const;
    Status getDeviceNumber(std::uint32_t* device_number) const;
    Status getDeviceList(DeviceInfoRaw* devices, std::uint32_t max_count, std::uint32_t* device_count) const;
    Status openDeviceByIP(Handle* handle, const char* ip_address) const;
    Status openDeviceBySN(Handle* handle, const char* serial_number) const;
    Status closeDevice(Handle* handle) const;
    Status registerExceptionCallBack(Handle handle, ExceptionCallback callback, void* user) const;
    Status startMeasure(Handle handle) const;
    Status stopMeasure(Handle handle) const;
    Status softTrigger(Handle handle) const;
    Status getImage(Handle handle, ImageDataRaw* image_data, std::uint32_t timeout_ms) const;
    Status registerImageDataCallBack(Handle handle, ImageDataCallback callback, void* user) const;
    Status clearDataBuffer(Handle handle) const;
    Status getParam(Handle handle, const char* key, ParamRaw* param) const;
    Status setParam(Handle handle, const char* key, ParamRaw* param) const;
    Status execute(Handle handle, const char* key) const;
    Status mapDepthToPointCloud(ImageDataRaw* depth_image, ImageDataRaw* point_cloud_image) const;

private:
    void* module_ = nullptr;
    std::string loaded_library_path_;

    FnGetVersion get_version_ = nullptr;
    FnInitialize initialize_ = nullptr;
    FnFinalize finalize_ = nullptr;
    FnGetDeviceNumber get_device_number_ = nullptr;
    FnGetDeviceList get_device_list_ = nullptr;
    FnOpenDeviceByIP open_device_by_ip_ = nullptr;
    FnOpenDeviceBySN open_device_by_sn_ = nullptr;
    FnCloseDevice close_device_ = nullptr;
    FnRegisterExceptionCallBack register_exception_callback_ = nullptr;
    FnStartMeasure start_measure_ = nullptr;
    FnStopMeasure stop_measure_ = nullptr;
    FnSoftTrigger soft_trigger_ = nullptr;
    FnGetImage get_image_ = nullptr;
    FnRegisterImageDataCallBack register_image_callback_ = nullptr;
    FnClearDataBuffer clear_data_buffer_ = nullptr;
    FnGetParam get_param_ = nullptr;
    FnSetParam set_param_ = nullptr;
    FnExecute execute_ = nullptr;
    FnMapDepthToPointCloud map_depth_to_point_cloud_ = nullptr;
};

}  // namespace mv3dlp::vendor

