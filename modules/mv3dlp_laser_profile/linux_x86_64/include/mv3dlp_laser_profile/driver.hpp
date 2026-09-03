#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mv3dlp_laser_profile/types.hpp"

namespace mv3dlp {

struct DriverOptions {
    std::string library_path;
    std::vector<std::string> library_search_paths;
};

class SdkError : public std::runtime_error {
public:
    SdkError(std::string message, std::int32_t status);

    std::int32_t status() const noexcept;

private:
    std::int32_t status_ = 0;
};

std::string statusToString(std::int32_t status);

namespace param_keys {

inline constexpr std::string_view kWidth = "Width";
inline constexpr std::string_view kHeight = "Height";
inline constexpr std::string_view kPixelFormat = "PixelFormat";
inline constexpr std::string_view kImageMode = "ImageMode";
inline constexpr std::string_view kGain = "Gain";
inline constexpr std::string_view kExposureTime = "ExposureTime";
inline constexpr std::string_view kAcquisitionFrameRate = "AcquisitionFrameRate";
inline constexpr std::string_view kTriggerSelector = "TriggerSelector";
inline constexpr std::string_view kTriggerMode = "TriggerMode";
inline constexpr std::string_view kTriggerSource = "TriggerSource";
inline constexpr std::string_view kTriggerDelay = "TriggerDelay";
inline constexpr std::string_view kSdkCoordinateType = "SDKCoordinateType";
inline constexpr std::string_view kSdkEmptyPoint = "SDKEmptyPoint";

}  // namespace param_keys

class Driver {
public:
    explicit Driver(DriverOptions options = {});
    ~Driver();

    Driver(Driver&& other) noexcept;
    Driver& operator=(Driver&& other) noexcept;

    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    std::string sdkVersion() const;
    std::string loadedLibraryPath() const;

    std::vector<DeviceInfo> enumerateDevices() const;

    void connectBySerial(const std::string& serial_number);
    void connectByIp(const std::string& ip_address);
    void disconnect();

    bool isConnected() const noexcept;

    void startAcquisition();
    void stopAcquisition();
    bool isAcquiring() const noexcept;

    void clearBuffer();

    void setAcquisitionMode(AcquisitionMode mode);
    void setBoolParam(std::string_view key, bool value);
    void setIntParam(std::string_view key, std::int64_t value);
    void setFloatParam(std::string_view key, float value);
    void setEnumParam(std::string_view key, std::uint32_t value);
    void executeCommand(std::string_view key);
    void softTrigger();

    std::optional<Frame> tryFetchFrame(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
    Frame fetchFrame(std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
    PointCloudFrame fetchPointCloud(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
    PointCloudFrame convertDepthToPointCloud(const Frame& frame) const;

    void setExceptionHandler(std::function<void(std::string)> handler);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mv3dlp
