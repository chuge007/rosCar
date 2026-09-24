#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mv3dlp {

enum class FrameType : std::uint32_t {
    unknown = 0,
    mono8,
    depth,
    point_cloud,
    rgb24,
    jpeg,
    profile_abc32,
};

enum class AcquisitionMode : std::uint32_t {
    original_image = 1,
    point_cloud_image = 4,
    range_image = 7,
};

struct DeviceInfo {
    std::string manufacturer_name;
    std::string model_name;
    std::string device_version;
    std::string manufacturer_specific_info;
    std::string serial_number;
    std::string user_defined_name;
    std::string current_ip;
    std::string subnet_mask;
    std::string gateway;
    std::string host_ip;
    std::uint32_t device_type_info = 0;
};

struct Frame {
    FrameType type = FrameType::unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> intensity_data;
    std::uint32_t frame_number = 0;
    std::int64_t timestamp = 0;
    bool valid = false;
    float x_scale = 0.0F;
    float y_scale = 0.0F;
    float z_scale = 0.0F;
    std::int32_t x_offset = 0;
    std::int32_t y_offset = 0;
    std::int32_t z_offset = 0;
};

struct PointXYZ {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct PointCloudFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<PointXYZ> points;
    std::uint32_t frame_number = 0;
    std::int64_t timestamp = 0;
    bool valid = false;
};

}  // namespace mv3dlp
