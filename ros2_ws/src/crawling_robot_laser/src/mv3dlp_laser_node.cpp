#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "ament_index_cpp/get_package_prefix.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/point_field.hpp"
#include "std_msgs/msg/int64.hpp"

#include "crawling_robot_interfaces/msg/laser_profile.hpp"
#include "mv3dlp_laser_profile/driver.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_laser {
namespace {

mv3dlp::AcquisitionMode parseAcquisitionMode(const std::string& value) {
  if (value == "range_image") {
    return mv3dlp::AcquisitionMode::range_image;
  }
  if (value == "point_cloud_image") {
    return mv3dlp::AcquisitionMode::point_cloud_image;
  }
  if (value == "original_image") {
    return mv3dlp::AcquisitionMode::original_image;
  }
  throw std::invalid_argument("acquisition_mode must be range_image, point_cloud_image, or original_image.");
}

sensor_msgs::msg::PointCloud2 makePointCloud(const mv3dlp::PointCloudFrame& source,
                                             const std_msgs::msg::Header& header) {
  sensor_msgs::msg::PointCloud2 target;
  target.header = header;
  target.is_bigendian = false;
  target.is_dense = false;
  target.fields.resize(3);
  target.fields[0].name = "x";
  target.fields[0].offset = 0;
  target.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
  target.fields[0].count = 1;
  target.fields[1].name = "y";
  target.fields[1].offset = 4;
  target.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
  target.fields[1].count = 1;
  target.fields[2].name = "z";
  target.fields[2].offset = 8;
  target.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
  target.fields[2].count = 1;
  target.point_step = 12;

  const std::size_t expected_points = static_cast<std::size_t>(source.width) * source.height;
  if (source.width > 0 && source.height > 0 && expected_points == source.points.size()) {
    target.width = source.width;
    target.height = source.height;
  } else {
    target.width = static_cast<std::uint32_t>(source.points.size());
    target.height = 1;
  }
  target.row_step = target.width * target.point_step;
  target.data.resize(source.points.size() * target.point_step);
  for (std::size_t index = 0; index < source.points.size(); ++index) {
    std::uint8_t* destination = target.data.data() + index * target.point_step;
    std::memcpy(destination, &source.points[index].x, sizeof(float));
    std::memcpy(destination + 4, &source.points[index].y, sizeof(float));
    std::memcpy(destination + 8, &source.points[index].z, sizeof(float));
  }
  return target;
}

}  // namespace

class Mv3dlpLaserNode final : public rclcpp::Node {
public:
  Mv3dlpLaserNode() : Node("mv3dlp_laser_node") {
    serial_number_ = declare_parameter<std::string>("serial_number", "");
    device_ip_ = declare_parameter<std::string>("device_ip", "");
    sdk_library_path_ = declare_parameter<std::string>("sdk_library_path", "");
    frame_id_ = declare_parameter<std::string>("frame_id", "laser_link");
    acquisition_mode_ = declare_parameter<std::string>("acquisition_mode", "range_image");
    fetch_timeout_ms_ = declare_parameter<int>("fetch_timeout_ms", 100);
    capture_period_ms_ = declare_parameter<int>("capture_period_ms", 33);
    reconnect_delay_ms_ = declare_parameter<int>("reconnect_delay_ms", 2000);
    encoder_ticks_topic_ = declare_parameter<std::string>("encoder_ticks_topic", "/scan_encoder/ticks");
    points_topic_ = declare_parameter<std::string>("points_topic", "/laser_profile/points");
    profile_topic_ = declare_parameter<std::string>("profile_topic", "/laser_profile/frame");

    points_publisher_ = create_publisher<sensor_msgs::msg::PointCloud2>(points_topic_, rclcpp::SensorDataQoS());
    profile_publisher_ = create_publisher<crawling_robot_interfaces::msg::LaserProfile>(
        profile_topic_, rclcpp::SensorDataQoS());
    encoder_subscription_ = create_subscription<std_msgs::msg::Int64>(
        encoder_ticks_topic_, rclcpp::SensorDataQoS(),
        [this](std_msgs::msg::Int64::SharedPtr message) { encoder_ticks_.store(message->data); });
    timer_ = create_wall_timer(std::chrono::milliseconds(std::max(capture_period_ms_, 1)),
                               [this] { capture(); });
  }

  ~Mv3dlpLaserNode() override {
    disconnect();
  }

private:
  void capture() {
    if (!ensureConnected()) {
      return;
    }

    try {
      const auto cloud =
          driver_->fetchPointCloud(std::chrono::milliseconds(fetch_timeout_ms_));
      std_msgs::msg::Header header;
      header.stamp = now();
      header.frame_id = frame_id_;
      const auto points = makePointCloud(cloud, header);
      points_publisher_->publish(points);

      crawling_robot_interfaces::msg::LaserProfile profile;
      profile.header = header;
      profile.points = points;
      profile.encoder_ticks = encoder_ticks_.load();
      profile_publisher_->publish(profile);
    } catch (const std::exception& error) {
      RCLCPP_ERROR(get_logger(), "Laser capture failed: %s", error.what());
      disconnect();
    }
  }

  bool ensureConnected() {
    if (driver_ && driver_->isConnected() && driver_->isAcquiring()) {
      return true;
    }
    if (serial_number_.empty() && device_ip_.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
                           "Laser is inactive. Set serial_number or device_ip first.");
      return false;
    }
    const auto now_steady = std::chrono::steady_clock::now();
    if (now_steady - last_connect_attempt_ < std::chrono::milliseconds(reconnect_delay_ms_)) {
      return false;
    }
    last_connect_attempt_ = now_steady;

    try {
      mv3dlp::DriverOptions options;
      options.library_path = resolveLibraryPath();
      driver_ = std::make_unique<mv3dlp::Driver>(std::move(options));
      if (!serial_number_.empty()) {
        driver_->connectBySerial(serial_number_);
      } else {
        driver_->connectByIp(device_ip_);
      }
      driver_->setAcquisitionMode(parseAcquisitionMode(acquisition_mode_));
      driver_->setEnumParam(mv3dlp::param_keys::kTriggerMode, 0U);
      driver_->startAcquisition();
      RCLCPP_INFO(get_logger(), "Connected to laser profile sensor with SDK %s.",
                  driver_->sdkVersion().c_str());
      return true;
    } catch (const std::exception& error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Laser connection failed: %s", error.what());
      disconnect();
      return false;
    }
  }

  std::string resolveLibraryPath() const {
    if (!sdk_library_path_.empty()) {
      return sdk_library_path_;
    }
    const auto prefix = ament_index_cpp::get_package_prefix("crawling_robot_laser");
    const auto library = std::filesystem::path(prefix) / "lib" / "crawling_robot_laser" /
                         "mv3dlp_sdk" / "Mv3dLp.dll";
    return library.string();
  }

  void disconnect() noexcept {
    if (!driver_) {
      return;
    }
    try {
      if (driver_->isAcquiring()) {
        driver_->stopAcquisition();
      }
      if (driver_->isConnected()) {
        driver_->disconnect();
      }
    } catch (const std::exception&) {
    }
    driver_.reset();
  }

  std::string serial_number_;
  std::string device_ip_;
  std::string sdk_library_path_;
  std::string frame_id_;
  std::string acquisition_mode_;
  int fetch_timeout_ms_ = 100;
  int capture_period_ms_ = 33;
  int reconnect_delay_ms_ = 2000;
  std::string encoder_ticks_topic_;
  std::string points_topic_;
  std::string profile_topic_;
  std::atomic<std::int64_t> encoder_ticks_{0};
  std::chrono::steady_clock::time_point last_connect_attempt_{};
  std::unique_ptr<mv3dlp::Driver> driver_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr points_publisher_;
  rclcpp::Publisher<crawling_robot_interfaces::msg::LaserProfile>::SharedPtr profile_publisher_;
  rclcpp::Subscription<std_msgs::msg::Int64>::SharedPtr encoder_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace crawling_robot_laser

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  crawling_robot_logging::installNodeFileLogger("mv3dlp_laser_node");
  rclcpp::spin(std::make_shared<crawling_robot_laser::Mv3dlpLaserNode>());
  rclcpp::shutdown();
  return 0;
}
