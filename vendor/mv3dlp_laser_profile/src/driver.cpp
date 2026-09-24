#include "mv3dlp_laser_profile/driver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "vendor_sdk.hpp"

namespace mv3dlp {
namespace {

std::string fromFixedBuffer(const char* data, std::size_t max_size) {
    std::size_t length = 0;
    while (length < max_size && data[length] != '\0') {
        ++length;
    }
    return std::string{data, length};
}

FrameType toFrameType(std::int32_t image_type) {
    const std::uint32_t value = static_cast<std::uint32_t>(image_type);
    switch (value) {
        case vendor::kImageTypeMono8:
            return FrameType::mono8;
        case vendor::kImageTypeDepth:
            return FrameType::depth;
        case vendor::kImageTypePointCloud:
            return FrameType::point_cloud;
        case vendor::kImageTypeRgb24Packed:
            return FrameType::rgb24;
        case vendor::kImageTypeJpeg:
            return FrameType::jpeg;
        case vendor::kImageTypeProfileAbc32:
        case vendor::kImageTypeProfile:
            return FrameType::profile_abc32;
        default:
            return FrameType::unknown;
    }
}

std::int32_t toVendorImageType(FrameType frame_type) {
    switch (frame_type) {
        case FrameType::mono8:
            return static_cast<std::int32_t>(vendor::kImageTypeMono8);
        case FrameType::depth:
            return static_cast<std::int32_t>(vendor::kImageTypeDepth);
        case FrameType::point_cloud:
            return static_cast<std::int32_t>(vendor::kImageTypePointCloud);
        case FrameType::rgb24:
            return static_cast<std::int32_t>(vendor::kImageTypeRgb24Packed);
        case FrameType::jpeg:
            return static_cast<std::int32_t>(vendor::kImageTypeJpeg);
        case FrameType::profile_abc32:
            return static_cast<std::int32_t>(vendor::kImageTypeProfileAbc32);
        case FrameType::unknown:
        default:
            return 0;
    }
}

Frame copyFrame(const vendor::ImageDataRaw& raw) {
    Frame frame;
    frame.type = toFrameType(raw.enImageType);
    frame.width = raw.nWidth;
    frame.height = raw.nHeight;
    frame.frame_number = raw.nFrameNum;
    frame.timestamp = raw.nTimeStamp;
    frame.valid = raw.bValid == vendor::kTrue;
    frame.x_scale = raw.fXScale;
    frame.y_scale = raw.fYScale;
    frame.z_scale = raw.fZScale;
    frame.x_offset = raw.nXOffset;
    frame.y_offset = raw.nYOffset;
    frame.z_offset = raw.nZOffset;

    if (raw.pData != nullptr && raw.nDataLen > 0) {
        frame.data.assign(raw.pData, raw.pData + raw.nDataLen);
    }

    if (raw.pIntensityData != nullptr && raw.nIntensityDataLen > 0) {
        frame.intensity_data.assign(raw.pIntensityData, raw.pIntensityData + raw.nIntensityDataLen);
    }

    return frame;
}

Frame copyProfileFrame(const vendor::ProfileDataRaw& raw) {
    Frame frame;
    frame.type = FrameType::profile_abc32;
    frame.width = raw.nLinePntNum;
    frame.height = raw.nProfileCnt;
    frame.frame_number = raw.nFrameNum;
    frame.timestamp = raw.nTimeStamp;
    frame.valid = raw.bValid == vendor::kTrue;

    constexpr std::size_t kRawCoordinateSize = sizeof(std::uint16_t) * 3u;
    constexpr float kMicrometersPerMillimeter = 1000.0F;
    const std::size_t declared_point_count =
        static_cast<std::size_t>(raw.nLinePntNum) * static_cast<std::size_t>(raw.nProfileCnt);
    if (raw.pData == nullptr || declared_point_count == 0u || raw.nDataLen == 0u) {
        return frame;
    }

    // Some SDK builds expose profile callback data as already converted
    // float XYZ (12 bytes/point), while the HFR path exposes packed 16-bit
    // coordinates (6 bytes/point).  Preserve the former without interpreting
    // float bytes as uint16_t coordinates.
    constexpr std::size_t kFloatPointSize = sizeof(float) * 3u;
    if (declared_point_count <= std::numeric_limits<std::size_t>::max() / kFloatPointSize) {
        const std::size_t float_data_size = declared_point_count * kFloatPointSize;
        if (raw.nDataLen == float_data_size) {
            frame.data.assign(raw.pData, raw.pData + float_data_size);
            return frame;
        }
    }

    const std::size_t available_point_count = raw.nDataLen / kRawCoordinateSize;
    const std::size_t point_count = std::min(declared_point_count, available_point_count);
    frame.data.resize(point_count * sizeof(float) * 3u);

    for (std::size_t index = 0; index < point_count; ++index) {
        std::uint16_t coordinates[3]{};
        std::memcpy(coordinates, raw.pData + index * kRawCoordinateSize, kRawCoordinateSize);

        float values[3]{};
        if (coordinates[0] == std::numeric_limits<std::uint16_t>::max() ||
            coordinates[1] == std::numeric_limits<std::uint16_t>::max() ||
            coordinates[2] == std::numeric_limits<std::uint16_t>::max()) {
            values[0] = values[1] = values[2] = std::numeric_limits<float>::quiet_NaN();
        } else {
            values[0] = (coordinates[0] * raw.fXScale + raw.nXOffset) / kMicrometersPerMillimeter;
            values[1] = (coordinates[1] * raw.fYScale + raw.nYOffset) / kMicrometersPerMillimeter;
            values[2] = (coordinates[2] * raw.fZScale + raw.nZOffset) / kMicrometersPerMillimeter;
        }
        std::memcpy(frame.data.data() + index * sizeof(values), values, sizeof(values));
    }

    return frame;
}

vendor::ImageDataRaw toVendorFrame(const Frame& frame) {
    vendor::ImageDataRaw raw{};
    raw.enImageType = toVendorImageType(frame.type);
    raw.nWidth = frame.width;
    raw.nHeight = frame.height;
    raw.pData = const_cast<std::uint8_t*>(frame.data.empty() ? nullptr : frame.data.data());
    raw.nDataLen = static_cast<std::uint32_t>(frame.data.size());
    raw.pIntensityData =
        const_cast<std::uint8_t*>(frame.intensity_data.empty() ? nullptr : frame.intensity_data.data());
    raw.nIntensityDataLen = static_cast<std::uint32_t>(frame.intensity_data.size());
    raw.nFrameNum = frame.frame_number;
    raw.nTimeStamp = frame.timestamp;
    raw.bValid = frame.valid ? vendor::kTrue : vendor::kFalse;
    raw.fXScale = frame.x_scale;
    raw.fYScale = frame.y_scale;
    raw.fZScale = frame.z_scale;
    raw.nXOffset = frame.x_offset;
    raw.nYOffset = frame.y_offset;
    raw.nZOffset = frame.z_offset;
    return raw;
}

PointCloudFrame copyPointCloud(const vendor::ImageDataRaw& raw) {
    PointCloudFrame result;
    result.width = raw.nWidth;
    result.height = raw.nHeight;
    result.frame_number = raw.nFrameNum;
    result.timestamp = raw.nTimeStamp;
    result.valid = raw.bValid == vendor::kTrue;

    if (raw.pData == nullptr || raw.nDataLen == 0) {
        return result;
    }

    if (raw.nDataLen % (sizeof(float) * 3u) != 0u) {
        throw std::runtime_error("Unexpected point cloud byte size from vendor SDK.");
    }

    const std::size_t point_count = raw.nDataLen / (sizeof(float) * 3u);
    const float* values = reinterpret_cast<const float*>(raw.pData);

    result.points.resize(point_count);
    for (std::size_t index = 0; index < point_count; ++index) {
        result.points[index].x = values[index * 3u + 0u];
        result.points[index].y = values[index * 3u + 1u];
        result.points[index].z = values[index * 3u + 2u];
    }

    return result;
}

PointCloudFrame frameToPointCloud(const Frame& frame) {
    const vendor::ImageDataRaw raw = toVendorFrame(frame);
    return copyPointCloud(raw);
}

vendor::ParamRaw makeBoolParam(bool value) {
    vendor::ParamRaw param{};
    param.enParamType = vendor::ParamType::bool_value;
    param.ParamInfo.bBoolParam = value ? vendor::kTrue : vendor::kFalse;
    return param;
}

vendor::ParamRaw makeIntParam(std::int64_t value) {
    vendor::ParamRaw param{};
    param.enParamType = vendor::ParamType::int_value;
    param.ParamInfo.stIntParam.nCurValue = value;
    return param;
}

vendor::ParamRaw makeFloatParam(float value) {
    vendor::ParamRaw param{};
    param.enParamType = vendor::ParamType::float_value;
    param.ParamInfo.stFloatParam.fCurValue = value;
    return param;
}

vendor::ParamRaw makeEnumParam(std::uint32_t value) {
    vendor::ParamRaw param{};
    param.enParamType = vendor::ParamType::enum_value;
    param.ParamInfo.stEnumParam.nCurValue = value;
    return param;
}

void throwIfError(vendor::Status status, const std::string& operation) {
    if (status != vendor::kOk) {
        throw SdkError(operation, status);
    }
}

}  // namespace

class Driver::Impl {
public:
    explicit Impl(DriverOptions options_in)
        : options(std::move(options_in)),
          sdk(options.library_path, options.library_search_paths) {
        throwIfError(sdk.initialize(), "MV3D_LP_Initialize failed");
    }

    ~Impl() {
        shutdown();
        const vendor::Status finalize_status = sdk.finalize();
        (void)finalize_status;
    }

    void shutdown() noexcept {
        std::lock_guard<std::mutex> lock(mutex);

        if (acquiring && handle != nullptr) {
            sdk.stopMeasure(handle);
            acquiring = false;
        }

        if (connected && handle != nullptr) {
            sdk.closeDevice(&handle);
            handle = nullptr;
            connected = false;
        }
        profile_callback_seen.store(false, std::memory_order_release);
        image_queue.clear();
        image_condition.notify_all();
    }

    void ensureConnected() const {
        if (!connected || handle == nullptr) {
            throw std::runtime_error("Device is not connected.");
        }
    }

    void ensureAcquiring() const {
        if (!acquiring) {
            throw std::runtime_error("Device acquisition has not been started.");
        }
    }

    void registerExceptionCallbackLocked() {
        ensureConnected();
        throwIfError(
            sdk.registerExceptionCallBack(handle, &Impl::exceptionCallbackThunk, this),
            "MV3D_LP_RegisterExceptionCallBack failed");
    }

    void registerImageCallbackLocked() {
        ensureConnected();
        throwIfError(
            sdk.registerImageDataCallBack(handle, &Impl::imageCallbackThunk, this),
            "MV3D_LP_RegisterImageDataCallBack failed");
    }

    void registerProfileCallbackLocked() {
        ensureConnected();
        throwIfError(
            sdk.registerProfileCallBack(handle, &Impl::profileCallbackThunk, 1u, this),
            "MV3D_LP_RegisterProfileCallBack failed");
    }

    void onException(vendor::ExceptionInfoRaw* info) {
        std::function<void(std::string)> handler_copy;
        std::string message = "Unknown device exception.";

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (info != nullptr) {
                message = fromFixedBuffer(info->chExceptionDesc, sizeof(info->chExceptionDesc));
                if (static_cast<std::uint32_t>(info->enExceptionType) == vendor::kExceptionDisconnect) {
                    acquiring = false;
                    connected = false;
                    handle = nullptr;
                }
            }
            handler_copy = exception_handler;
        }

        if (handler_copy) {
            handler_copy(message);
        }
    }

    static void MV3DLP_CALL exceptionCallbackThunk(vendor::ExceptionInfoRaw* info, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr) {
            self->onException(info);
        }
    }

    void onImage(vendor::ImageDataRaw* raw) {
        if (raw == nullptr) {
            return;
        }
        // In HFR point-cloud mode the image callback still receives the
        // assembled range image (height ~= LSLRangeImgHeight) every few
        // seconds.  Once per-profile output is flowing, discard those large
        // snapshots so they cannot replace the low-latency profile frame in
        // the live-preview queue.
        if (profile_callback_seen.load(std::memory_order_acquire) && raw->nHeight > 1u) {
            return;
        }
        Frame frame;
        try {
            // The SDK owns this buffer only for the callback duration, so copy it immediately.
            frame = copyFrame(*raw);
        } catch (...) {
            return;
        }

        enqueueFrame(std::move(frame));
    }

    static void MV3DLP_CALL imageCallbackThunk(vendor::ImageDataRaw* raw, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr) {
            self->onImage(raw);
        }
    }

    void onProfile(vendor::ProfileDataRaw* raw) {
        if (raw == nullptr) {
            return;
        }
        Frame frame;
        try {
            frame = copyProfileFrame(*raw);
        } catch (...) {
            return;
        }
        if (frame.valid && !frame.data.empty()) {
            profile_callback_seen.store(true, std::memory_order_release);
        }
        enqueueFrame(std::move(frame));
    }

    static void MV3DLP_CALL profileCallbackThunk(vendor::ProfileDataRaw* raw, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self != nullptr) {
            self->onProfile(raw);
        }
    }

    void enqueueFrame(Frame frame) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!connected || handle == nullptr) {
            return;
        }
        constexpr std::size_t kMaxQueuedFrames = 2;
        if (image_queue.size() >= kMaxQueuedFrames) {
            image_queue.pop_front();
        }
        image_queue.push_back(std::move(frame));
        image_condition.notify_one();
    }

    DriverOptions options;
    vendor::VendorSdk sdk;
    mutable std::mutex mutex;
    vendor::Handle handle = nullptr;
    bool connected = false;
    bool acquiring = false;
    std::atomic<bool> profile_callback_seen{false};
    std::function<void(std::string)> exception_handler;
    std::deque<Frame> image_queue;
    std::condition_variable image_condition;
};

SdkError::SdkError(std::string message, std::int32_t status)
    : std::runtime_error([&message, status]() {
          std::ostringstream stream;
          stream << message << " (status=0x" << std::hex << static_cast<std::uint32_t>(status)
                 << ", " << statusToString(status) << ")";
          return stream.str();
      }()),
      status_(status) {}

std::int32_t SdkError::status() const noexcept {
    return status_;
}

std::string statusToString(std::int32_t status) {
    switch (status) {
        case vendor::kOk:
            return "success";
        case vendor::kErrorHandle:
            return "invalid handle";
        case vendor::kErrorSupport:
            return "unsupported function";
        case vendor::kErrorBufferOverrun:
            return "buffer full";
        case vendor::kErrorCallOrder:
            return "invalid call order";
        case vendor::kErrorParameter:
            return "invalid parameter";
        case vendor::kErrorResource:
            return "resource allocation failed";
        case vendor::kErrorNoData:
            return "no data";
        case vendor::kErrorPrecondition:
            return "invalid precondition";
        case vendor::kErrorVersion:
            return "version mismatch";
        case vendor::kErrorNoEnoughBuffer:
            return "insufficient buffer";
        case vendor::kErrorAbnormalImage:
            return "abnormal image";
        case vendor::kErrorLoadLibrary:
            return "library load failed";
        case vendor::kErrorAlgorithm:
            return "algorithm error";
        case vendor::kErrorDeviceOffline:
            return "device offline";
        case vendor::kErrorAccessDenied:
            return "access denied";
        case vendor::kErrorOutOfRange:
            return "value out of range";
        case vendor::kErrorUnknown:
            return "unknown error";
        default: {
            std::ostringstream stream;
            stream << "unmapped status 0x" << std::hex << static_cast<std::uint32_t>(status);
            return stream.str();
        }
    }
}

Driver::Driver(DriverOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

Driver::~Driver() = default;

Driver::Driver(Driver&& other) noexcept = default;
Driver& Driver::operator=(Driver&& other) noexcept = default;

std::string Driver::sdkVersion() const {
    return impl_->sdk.version();
}

std::string Driver::loadedLibraryPath() const {
    return impl_->sdk.loadedLibraryPath();
}

std::vector<DeviceInfo> Driver::enumerateDevices() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    std::uint32_t device_count = 0;
    throwIfError(impl_->sdk.getDeviceNumber(&device_count), "MV3D_LP_GetDeviceNumber failed");

    if (device_count == 0) {
        return {};
    }

    std::vector<vendor::DeviceInfoRaw> raw_devices(device_count);
    std::uint32_t filled_count = device_count;
    throwIfError(
        impl_->sdk.getDeviceList(raw_devices.data(), device_count, &filled_count),
        "MV3D_LP_GetDeviceList failed");

    raw_devices.resize(filled_count);

    std::vector<DeviceInfo> devices;
    devices.reserve(raw_devices.size());
    for (const auto& raw : raw_devices) {
        DeviceInfo info;
        info.manufacturer_name = fromFixedBuffer(raw.chManufacturerName, sizeof(raw.chManufacturerName));
        info.model_name = fromFixedBuffer(raw.chModelName, sizeof(raw.chModelName));
        info.device_version = fromFixedBuffer(raw.chDeviceVersion, sizeof(raw.chDeviceVersion));
        info.manufacturer_specific_info =
            fromFixedBuffer(raw.chManufacturerSpecificInfo, sizeof(raw.chManufacturerSpecificInfo));
        info.serial_number = fromFixedBuffer(raw.chSerialNumber, sizeof(raw.chSerialNumber));
        info.user_defined_name = fromFixedBuffer(raw.chUserDefinedName, sizeof(raw.chUserDefinedName));
        info.current_ip = fromFixedBuffer(raw.chCurrentIp, sizeof(raw.chCurrentIp));
        info.subnet_mask = fromFixedBuffer(raw.chCurrentSubNetMask, sizeof(raw.chCurrentSubNetMask));
        info.gateway = fromFixedBuffer(raw.chDefultGateWay, sizeof(raw.chDefultGateWay));
        info.host_ip = fromFixedBuffer(raw.chNetExport, sizeof(raw.chNetExport));
        info.device_type_info = raw.nDevTypeInfo;
        devices.push_back(std::move(info));
    }

    return devices;
}

void Driver::connectBySerial(const std::string& serial_number) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (impl_->acquiring && impl_->handle != nullptr) {
        throwIfError(impl_->sdk.stopMeasure(impl_->handle), "MV3D_LP_StopMeasure failed before reconnect");
        impl_->acquiring = false;
    }

    if (impl_->connected && impl_->handle != nullptr) {
        throwIfError(impl_->sdk.closeDevice(&impl_->handle), "MV3D_LP_CloseDevice failed before reconnect");
        impl_->connected = false;
    }
    impl_->profile_callback_seen.store(false, std::memory_order_release);

    vendor::Handle handle = nullptr;
    throwIfError(impl_->sdk.openDeviceBySN(&handle, serial_number.c_str()), "MV3D_LP_OpenDeviceBySN failed");

    impl_->handle = handle;
    impl_->connected = true;
    impl_->registerExceptionCallbackLocked();
    impl_->registerImageCallbackLocked();
    impl_->registerProfileCallbackLocked();
}

void Driver::connectByIp(const std::string& ip_address) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (impl_->acquiring && impl_->handle != nullptr) {
        throwIfError(impl_->sdk.stopMeasure(impl_->handle), "MV3D_LP_StopMeasure failed before reconnect");
        impl_->acquiring = false;
    }

    if (impl_->connected && impl_->handle != nullptr) {
        throwIfError(impl_->sdk.closeDevice(&impl_->handle), "MV3D_LP_CloseDevice failed before reconnect");
        impl_->connected = false;
    }
    impl_->profile_callback_seen.store(false, std::memory_order_release);

    vendor::Handle handle = nullptr;
    throwIfError(impl_->sdk.openDeviceByIP(&handle, ip_address.c_str()), "MV3D_LP_OpenDeviceByIP failed");

    impl_->handle = handle;
    impl_->connected = true;
    impl_->registerExceptionCallbackLocked();
    impl_->registerImageCallbackLocked();
    impl_->registerProfileCallbackLocked();
}

void Driver::disconnect() {
    impl_->shutdown();
}

bool Driver::isConnected() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->connected;
}

void Driver::startAcquisition() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();

    if (impl_->acquiring) {
        return;
    }

    throwIfError(impl_->sdk.startMeasure(impl_->handle), "MV3D_LP_StartMeasure failed");
    impl_->acquiring = true;
}

void Driver::stopAcquisition() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();

    if (!impl_->acquiring) {
        return;
    }

    throwIfError(impl_->sdk.stopMeasure(impl_->handle), "MV3D_LP_StopMeasure failed");
    impl_->acquiring = false;
}

bool Driver::isAcquiring() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->acquiring;
}

void Driver::clearBuffer() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    throwIfError(impl_->sdk.clearDataBuffer(impl_->handle), "MV3D_LP_ClearDataBuffer failed");
}

void Driver::setAcquisitionMode(AcquisitionMode mode) {
    setEnumParam(param_keys::kImageMode, static_cast<std::uint32_t>(mode));
}

void Driver::setBoolParam(std::string_view key, bool value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    auto param = makeBoolParam(value);
    const std::string key_string{key};
    throwIfError(impl_->sdk.setParam(impl_->handle, key_string.c_str(), &param), "MV3D_LP_SetParam failed");
}

void Driver::setIntParam(std::string_view key, std::int64_t value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    auto param = makeIntParam(value);
    const std::string key_string{key};
    throwIfError(impl_->sdk.setParam(impl_->handle, key_string.c_str(), &param), "MV3D_LP_SetParam failed");
}

void Driver::setFloatParam(std::string_view key, float value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    auto param = makeFloatParam(value);
    const std::string key_string{key};
    throwIfError(impl_->sdk.setParam(impl_->handle, key_string.c_str(), &param), "MV3D_LP_SetParam failed");
}

void Driver::setEnumParam(std::string_view key, std::uint32_t value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    auto param = makeEnumParam(value);
    const std::string key_string{key};
    throwIfError(impl_->sdk.setParam(impl_->handle, key_string.c_str(), &param), "MV3D_LP_SetParam failed");
}

void Driver::executeCommand(std::string_view key) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    const std::string key_string{key};
    throwIfError(impl_->sdk.execute(impl_->handle, key_string.c_str()), "MV3D_LP_Execute failed");
}

void Driver::softTrigger() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    throwIfError(impl_->sdk.softTrigger(impl_->handle), "MV3D_LP_SoftTrigger failed");
}

std::optional<Frame> Driver::tryFetchFrame(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->ensureConnected();
    impl_->ensureAcquiring();

    const auto wait_time = std::chrono::milliseconds{std::max<std::int64_t>(timeout.count(), 0)};
    if (impl_->image_queue.empty() &&
        !impl_->image_condition.wait_for(lock, wait_time, [&] {
            return !impl_->image_queue.empty() || !impl_->acquiring;
        })) {
        return std::nullopt;
    }
    if (impl_->image_queue.empty()) {
        return std::nullopt;
    }
    // This is a live-preview API: stale frames are worse than dropped
    // frames. Return the newest image and discard everything accumulated
    // behind it so a temporary conversion/UI stall cannot create latency.
    Frame frame = std::move(impl_->image_queue.back());
    impl_->image_queue.clear();
    return frame;
}

Frame Driver::fetchFrame(std::chrono::milliseconds timeout) {
    auto frame = tryFetchFrame(timeout);
    if (!frame.has_value()) {
        throw std::runtime_error("Timed out waiting for an image frame from the device.");
    }
    return *frame;
}

PointCloudFrame Driver::fetchPointCloud(std::chrono::milliseconds timeout) {
    const Frame frame = fetchFrame(timeout);
    if (frame.type == FrameType::depth) {
        return convertDepthToPointCloud(frame);
    }
    return frameToPointCloud(frame);
}

PointCloudFrame Driver::convertDepthToPointCloud(const Frame& frame) const {
    if (frame.type != FrameType::depth &&
        frame.type != FrameType::point_cloud &&
        frame.type != FrameType::profile_abc32) {
        throw std::invalid_argument("Frame type is not convertible to point cloud.");
    }

    if (frame.type == FrameType::point_cloud || frame.type == FrameType::profile_abc32) {
        return frameToPointCloud(frame);
    }

    vendor::ImageDataRaw depth_raw = toVendorFrame(frame);
    vendor::ImageDataRaw point_cloud_raw{};

    std::lock_guard<std::mutex> lock(impl_->mutex);
    throwIfError(
        impl_->sdk.mapDepthToPointCloud(&depth_raw, &point_cloud_raw),
        "MV3D_LP_MapDepthToPointCloud failed");
    return copyPointCloud(point_cloud_raw);
}

void Driver::setExceptionHandler(std::function<void(std::string)> handler) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->exception_handler = std::move(handler);
}

}  // namespace mv3dlp
