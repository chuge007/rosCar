#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "trajectory_msgs/msg/joint_trajectory.hpp"

#include "crawling_robot_drivers/servo_v38.hpp"
#include "crawling_robot_interfaces/msg/can_frame.hpp"
#include "crawling_robot_logging/node_logging.hpp"

namespace crawling_robot_drivers {
namespace {

constexpr std::uint32_t kNmtId = 0x000;
constexpr std::uint32_t kSdoRequestBase = 0x600;
constexpr std::uint32_t kSdoResponseBase = 0x580;
constexpr std::uint16_t kControlword = 0x6040;
constexpr std::uint16_t kModeOfOperation = 0x6060;
constexpr std::uint16_t kActualPosition = 0x6064;
constexpr std::uint16_t kActualVelocity = 0x606C;
constexpr std::uint16_t kTargetPosition = 0x607A;
constexpr std::uint16_t kProfileVelocity = 0x6081;
constexpr std::uint16_t kProfileAcceleration = 0x6083;
constexpr std::uint16_t kProfileDeceleration = 0x6084;
constexpr std::uint8_t kProfilePositionMode = 1;

crawling_robot_interfaces::msg::CanFrame toRosFrame(const CanFrame& frame,
                                                     const rclcpp::Time& stamp) {
  crawling_robot_interfaces::msg::CanFrame message;
  message.header.stamp = stamp;
  message.id = frame.id;
  message.dlc = 8;
  message.data = frame.data;
  return message;
}

std::int32_t readInt32Le(const std::array<std::uint8_t, 8>& data) {
  const auto value = static_cast<std::uint32_t>(data[4]) |
                     (static_cast<std::uint32_t>(data[5]) << 8U) |
                     (static_cast<std::uint32_t>(data[6]) << 16U) |
                     (static_cast<std::uint32_t>(data[7]) << 24U);
  return static_cast<std::int32_t>(value);
}

}  // namespace

class CanopenAxisNode final : public rclcpp::Node {
public:
  CanopenAxisNode() : Node("canopen_axis_node") {
    std::fprintf(stderr, "[canopen_axis_node] ctor start\n");
    std::fflush(stderr);
    axis_names_ = declare_parameter<std::vector<std::string>>(
        "axis_names", {"scan_frame_vertical_joint", "scan_frame_lateral_joint", "probe_clamp_joint"});
    node_ids_ = declare_parameter<std::vector<std::int64_t>>(
        "node_ids", std::vector<std::int64_t>{10, 11, 12});
    lower_limits_m_ = declare_parameter<std::vector<double>>("lower_limits_m", {0.0, 0.0, 0.0});
    upper_limits_m_ = declare_parameter<std::vector<double>>("upper_limits_m", {0.0, 0.0, 0.0});
    max_velocities_m_s_ = declare_parameter<std::vector<double>>("max_velocities_m_s", {0.0, 0.0, 0.0});
    max_accelerations_m_s2_ =
        declare_parameter<std::vector<double>>("max_accelerations_m_s2", {0.0, 0.0, 0.0});
    counts_per_meter_ = declare_parameter<std::vector<double>>("counts_per_meter", {0.0, 0.0, 0.0});
    home_offsets_m_ = declare_parameter<std::vector<double>>("home_offsets_m", {0.0, 0.0, 0.0});
    axis_signs_ = declare_parameter<std::vector<std::int64_t>>(
        "axis_signs", std::vector<std::int64_t>{1, 1, 1});
    sdo_timeout_ms_ = declare_parameter<int>("sdo_timeout_ms", 200);
    state_poll_period_ms_ = declare_parameter<int>("state_poll_period_ms", 100);
    can_tx_topic_ = declare_parameter<std::string>("can_tx_topic", "/can/tx");
    can_rx_topic_ = declare_parameter<std::string>("can_rx_topic", "/can/rx");
    dry_run_ = declare_parameter<bool>("dry_run", true);
    protocol_verified_ = declare_parameter<bool>("protocol_verified", false);
    std::fprintf(stderr, "[canopen_axis_node] parameters ready\n");
    std::fflush(stderr);

    state_publisher_ = create_publisher<sensor_msgs::msg::JointState>("/scan_axes/joint_states", 20);
    can_publisher_ = create_publisher<crawling_robot_interfaces::msg::CanFrame>(can_tx_topic_, 100);
    command_subscription_ = create_subscription<trajectory_msgs::msg::JointTrajectory>(
        "/scan_axes/command", 10,
        [this](trajectory_msgs::msg::JointTrajectory::SharedPtr message) { onCommand(*message); });
    can_subscription_ = create_subscription<crawling_robot_interfaces::msg::CanFrame>(
        can_rx_topic_, 100,
        [this](crawling_robot_interfaces::msg::CanFrame::SharedPtr message) { onCanFrame(*message); });
    enable_service_ = create_service<std_srvs::srv::SetBool>(
        "/scan_axes/enable",
        [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
               std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
          onEnable(request->data, *response);
        });
    timer_ = create_wall_timer(std::chrono::milliseconds(2), [this] { processBus(); });
    std::fprintf(stderr, "[canopen_axis_node] pub/sub/service/timer ready\n");
    std::fflush(stderr);
  }

private:
  enum class RequestKind {
    initialize,
    motion,
    position_poll,
    velocity_poll,
  };

  struct SdoRequest {
    CanFrame frame;
    std::uint8_t node_id = 0;
    std::uint16_t index = 0;
    std::uint8_t subindex = 0;
    bool read = false;
    std::size_t axis = 0;
    RequestKind kind = RequestKind::initialize;
  };

  void onEnable(bool request, std_srvs::srv::SetBool::Response& response) {
    if (!request) {
      enabled_ = false;
      ready_ = false;
      request_queue_.clear();
      pending_request_.reset();
      if (!dry_run_) {
        for (const int node_id : node_ids_) {
          publishSdoWrite(static_cast<std::uint8_t>(node_id), kControlword, 0, 2,
                          {0x02, 0x00, 0x00, 0x00});
        }
      }
      response.success = true;
      response.message = "Scan-axis output disabled and CiA-402 quick-stop sent.";
      return;
    }

    if (!hasValidConfiguration()) {
      response.success = false;
      response.message = "Configure node IDs, mechanical limits, and counts_per_meter first.";
      return;
    }
    if (!protocol_verified_ && !dry_run_) {
      response.success = false;
      response.message = "Set protocol_verified only after commissioning node IDs and homing hardware.";
      return;
    }

    enabled_ = true;
    ready_ = false;
    request_queue_.clear();
    pending_request_.reset();
    if (dry_run_) {
      ready_ = true;
      response.success = true;
      response.message = "Scan axes enabled in dry-run mode; no CAN frame will be sent.";
      return;
    }

    for (const int node_id : node_ids_) {
      publishNmtStart(static_cast<std::uint8_t>(node_id));
    }
    for (std::size_t axis = 0; axis < axis_names_.size(); ++axis) {
      const auto node_id = static_cast<std::uint8_t>(node_ids_[axis]);
      enqueueWrite(node_id, kModeOfOperation, 0, 1, {kProfilePositionMode, 0, 0, 0}, axis,
                   RequestKind::initialize);
      enqueueWrite(node_id, kControlword, 0, 2, {0x06, 0x00, 0, 0}, axis,
                   RequestKind::initialize);
      enqueueWrite(node_id, kControlword, 0, 2, {0x07, 0x00, 0, 0}, axis,
                   RequestKind::initialize);
      enqueueWrite(node_id, kControlword, 0, 2, {0x0F, 0x00, 0, 0}, axis,
                   RequestKind::initialize);
    }
    initializing_ = true;
    response.success = true;
    response.message = "CANopen CiA-402 initialization queued.";
  }

  void onCommand(const trajectory_msgs::msg::JointTrajectory& trajectory) {
    if (!hasValidConfiguration()) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000,
                            "Configure the three scan axes before commanding them.");
      return;
    }
    if (trajectory.joint_names != axis_names_ || trajectory.points.empty()) {
      RCLCPP_ERROR(get_logger(), "Expected a non-empty trajectory for the configured three scan axes.");
      return;
    }

    const auto& target = trajectory.points.back();
    if (target.positions.size() != 3 || (!target.velocities.empty() && target.velocities.size() != 3) ||
        (!target.accelerations.empty() && target.accelerations.size() != 3)) {
      RCLCPP_ERROR(get_logger(), "Each scan-axis command must contain positions, velocities, and accelerations for three axes.");
      return;
    }

    std::array<std::int32_t, 3> positions{};
    std::array<std::uint32_t, 3> velocities{};
    std::array<std::uint32_t, 3> accelerations{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const double position = target.positions[axis];
      const double velocity = target.velocities.empty() ? max_velocities_m_s_[axis]
                                                          : std::abs(target.velocities[axis]);
      const double acceleration = target.accelerations.empty() ? max_accelerations_m_s2_[axis]
                                                                : std::abs(target.accelerations[axis]);
      if (!std::isfinite(position) || !std::isfinite(velocity) || !std::isfinite(acceleration) ||
          position < lower_limits_m_[axis] || position > upper_limits_m_[axis] ||
          velocity > max_velocities_m_s_[axis] || acceleration > max_accelerations_m_s2_[axis] ||
          !toInt32Checked((position - home_offsets_m_[axis]) * counts_per_meter_[axis] * axis_signs_[axis],
                          positions[axis]) ||
          !toUint32Checked(velocity * counts_per_meter_[axis], velocities[axis]) ||
          !toUint32Checked(acceleration * counts_per_meter_[axis], accelerations[axis])) {
        RCLCPP_ERROR(get_logger(), "Axis %s command violates its configured limits.", axis_names_[axis].c_str());
        return;
      }
    }

    if (!enabled_) {
      RCLCPP_WARN(get_logger(), "Rejected scan-axis command because output is disabled.");
      return;
    }
    if (dry_run_) {
      publishRequestedState(target);
      return;
    }
    if (!ready_) {
      RCLCPP_WARN(get_logger(), "Rejected scan-axis command while CiA-402 initialization is incomplete.");
      return;
    }

    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto node_id = static_cast<std::uint8_t>(node_ids_[axis]);
      enqueueWrite(node_id, kProfileVelocity, 0, 4, toBytes(velocities[axis]), axis, RequestKind::motion);
      enqueueWrite(node_id, kProfileAcceleration, 0, 4, toBytes(accelerations[axis]), axis,
                   RequestKind::motion);
      enqueueWrite(node_id, kProfileDeceleration, 0, 4, toBytes(accelerations[axis]), axis,
                   RequestKind::motion);
      enqueueWrite(node_id, kTargetPosition, 0, 4, toBytes(positions[axis]), axis, RequestKind::motion);
      enqueueWrite(node_id, kControlword, 0, 2, {0x1F, 0x00, 0, 0}, axis, RequestKind::motion);
      enqueueWrite(node_id, kControlword, 0, 2, {0x0F, 0x00, 0, 0}, axis, RequestKind::motion);
    }
  }

  void processBus() {
    if (!enabled_ || dry_run_) {
      return;
    }
    const auto current_time = std::chrono::steady_clock::now();
    if (pending_request_) {
      if (current_time - pending_sent_at_ > std::chrono::milliseconds(sdo_timeout_ms_)) {
        RCLCPP_ERROR(get_logger(), "CANopen SDO timed out for node %u, index 0x%04X.",
                     pending_request_->node_id, pending_request_->index);
        sendQuickStops();
        enabled_ = false;
        ready_ = false;
        initializing_ = false;
        request_queue_.clear();
        pending_request_.reset();
      }
      return;
    }
    if (!request_queue_.empty()) {
      pending_request_ = request_queue_.front();
      request_queue_.pop_front();
      can_publisher_->publish(toRosFrame(pending_request_->frame, now()));
      pending_sent_at_ = current_time;
      return;
    }
    if (initializing_) {
      initializing_ = false;
      ready_ = true;
      RCLCPP_INFO(get_logger(), "All three CANopen axes are in CiA-402 profile-position mode.");
      return;
    }
    if (ready_ && current_time - last_state_poll_ >= std::chrono::milliseconds(state_poll_period_ms_)) {
      last_state_poll_ = current_time;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto node_id = static_cast<std::uint8_t>(node_ids_[axis]);
        enqueueRead(node_id, kActualPosition, 0, axis, RequestKind::position_poll);
        enqueueRead(node_id, kActualVelocity, 0, axis, RequestKind::velocity_poll);
      }
    }
  }

  void onCanFrame(const crawling_robot_interfaces::msg::CanFrame& message) {
    if (!pending_request_ || message.dlc < 8 || message.id != kSdoResponseBase + pending_request_->node_id ||
        message.data[1] != static_cast<std::uint8_t>(pending_request_->index & 0xFFU) ||
        message.data[2] != static_cast<std::uint8_t>(pending_request_->index >> 8U) ||
        message.data[3] != pending_request_->subindex) {
      return;
    }
    if (message.data[0] == 0x80U) {
      RCLCPP_ERROR(get_logger(), "CANopen abort at index 0x%04X for node %u.", pending_request_->index,
                   pending_request_->node_id);
      sendQuickStops();
      enabled_ = false;
      ready_ = false;
      initializing_ = false;
      request_queue_.clear();
      pending_request_.reset();
      return;
    }
    if ((!pending_request_->read && message.data[0] != 0x60U) ||
        (pending_request_->read && message.data[0] != 0x43U)) {
      return;
    }

    if (pending_request_->read) {
      const auto axis = pending_request_->axis;
      if (pending_request_->kind == RequestKind::position_poll) {
        actual_positions_[axis] = readInt32Le(message.data);
      } else if (pending_request_->kind == RequestKind::velocity_poll) {
        actual_velocities_[axis] = readInt32Le(message.data);
      }
      publishActualState();
    }
    pending_request_.reset();
  }

  bool hasValidConfiguration() const {
    if (axis_names_.size() != 3 || node_ids_.size() != 3 || lower_limits_m_.size() != 3 ||
        upper_limits_m_.size() != 3 || max_velocities_m_s_.size() != 3 ||
        max_accelerations_m_s2_.size() != 3 || counts_per_meter_.size() != 3 ||
        home_offsets_m_.size() != 3 || axis_signs_.size() != 3 || sdo_timeout_ms_ <= 0 ||
        state_poll_period_ms_ <= 0) {
      return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (axis_names_[axis].empty() || node_ids_[axis] < 1 || node_ids_[axis] > 127 ||
          (axis > 0 && node_ids_[axis] == node_ids_[0]) ||
          (axis > 1 && node_ids_[axis] == node_ids_[1]) ||
          !std::isfinite(lower_limits_m_[axis]) || !std::isfinite(upper_limits_m_[axis]) ||
          lower_limits_m_[axis] > upper_limits_m_[axis] || !std::isfinite(max_velocities_m_s_[axis]) ||
          max_velocities_m_s_[axis] <= 0.0 || !std::isfinite(max_accelerations_m_s2_[axis]) ||
          max_accelerations_m_s2_[axis] <= 0.0 || !std::isfinite(counts_per_meter_[axis]) ||
          counts_per_meter_[axis] <= 0.0 || !std::isfinite(home_offsets_m_[axis]) ||
          (axis_signs_[axis] != -1 && axis_signs_[axis] != 1)) {
        return false;
      }
    }
    return true;
  }

  static bool toInt32Checked(double value, std::int32_t& output) {
    if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
      return false;
    }
    output = static_cast<std::int32_t>(std::llround(value));
    return true;
  }

  static bool toUint32Checked(double value, std::uint32_t& output) {
    if (!std::isfinite(value) || value < 0.0 || value > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
      return false;
    }
    output = static_cast<std::uint32_t>(std::llround(value));
    return true;
  }

  static std::array<std::uint8_t, 4> toBytes(std::uint32_t value) {
    return {static_cast<std::uint8_t>(value & 0xFFU), static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
            static_cast<std::uint8_t>((value >> 16U) & 0xFFU), static_cast<std::uint8_t>((value >> 24U) & 0xFFU)};
  }

  static std::array<std::uint8_t, 4> toBytes(std::int32_t value) {
    return toBytes(static_cast<std::uint32_t>(value));
  }

  void enqueueWrite(std::uint8_t node_id, std::uint16_t index, std::uint8_t subindex, std::uint8_t size,
                    const std::array<std::uint8_t, 4>& value, std::size_t axis, RequestKind kind) {
    SdoRequest request;
    request.frame = makeSdoWrite(node_id, index, subindex, size, value);
    request.node_id = node_id;
    request.index = index;
    request.subindex = subindex;
    request.axis = axis;
    request.kind = kind;
    request_queue_.push_back(request);
  }

  void enqueueRead(std::uint8_t node_id, std::uint16_t index, std::uint8_t subindex, std::size_t axis,
                   RequestKind kind) {
    SdoRequest request;
    request.frame = makeSdoRead(node_id, index, subindex);
    request.node_id = node_id;
    request.index = index;
    request.subindex = subindex;
    request.read = true;
    request.axis = axis;
    request.kind = kind;
    request_queue_.push_back(request);
  }

  static CanFrame makeSdoWrite(std::uint8_t node_id, std::uint16_t index, std::uint8_t subindex,
                               std::uint8_t size, const std::array<std::uint8_t, 4>& value) {
    CanFrame frame;
    frame.id = kSdoRequestBase + node_id;
    frame.data[0] = size == 1 ? 0x2F : (size == 2 ? 0x2B : 0x23);
    frame.data[1] = static_cast<std::uint8_t>(index & 0xFFU);
    frame.data[2] = static_cast<std::uint8_t>(index >> 8U);
    frame.data[3] = subindex;
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
      frame.data[4 + offset] = value[offset];
    }
    return frame;
  }

  static CanFrame makeSdoRead(std::uint8_t node_id, std::uint16_t index, std::uint8_t subindex) {
    CanFrame frame;
    frame.id = kSdoRequestBase + node_id;
    frame.data[0] = 0x40;
    frame.data[1] = static_cast<std::uint8_t>(index & 0xFFU);
    frame.data[2] = static_cast<std::uint8_t>(index >> 8U);
    frame.data[3] = subindex;
    return frame;
  }

  void publishNmtStart(std::uint8_t node_id) {
    CanFrame frame;
    frame.id = kNmtId;
    frame.data[0] = 0x01;
    frame.data[1] = node_id;
    can_publisher_->publish(toRosFrame(frame, now()));
  }

  void publishSdoWrite(std::uint8_t node_id, std::uint16_t index, std::uint8_t subindex, std::uint8_t size,
                       const std::array<std::uint8_t, 4>& value) {
    can_publisher_->publish(toRosFrame(makeSdoWrite(node_id, index, subindex, size, value), now()));
  }

  void sendQuickStops() {
    for (const int node_id : node_ids_) {
      publishSdoWrite(static_cast<std::uint8_t>(node_id), kControlword, 0, 2, {0x02, 0x00, 0, 0});
    }
  }

  void publishRequestedState(const trajectory_msgs::msg::JointTrajectoryPoint& target) {
    sensor_msgs::msg::JointState state;
    state.header.stamp = now();
    state.name = axis_names_;
    state.position = target.positions;
    state.velocity = target.velocities;
    state_publisher_->publish(state);
  }

  void publishActualState() {
    sensor_msgs::msg::JointState state;
    state.header.stamp = now();
    state.name = axis_names_;
    state.position.resize(3);
    state.velocity.resize(3);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      state.position[axis] = static_cast<double>(actual_positions_[axis]) /
                                 (counts_per_meter_[axis] * static_cast<double>(axis_signs_[axis])) +
                             home_offsets_m_[axis];
      state.velocity[axis] = static_cast<double>(actual_velocities_[axis]) /
                             (counts_per_meter_[axis] * static_cast<double>(axis_signs_[axis]));
    }
    state_publisher_->publish(state);
  }

  std::vector<std::string> axis_names_;
  std::vector<std::int64_t> node_ids_;
  std::vector<double> lower_limits_m_;
  std::vector<double> upper_limits_m_;
  std::vector<double> max_velocities_m_s_;
  std::vector<double> max_accelerations_m_s2_;
  std::vector<double> counts_per_meter_;
  std::vector<double> home_offsets_m_;
  std::vector<std::int64_t> axis_signs_;
  int sdo_timeout_ms_ = 200;
  int state_poll_period_ms_ = 100;
  std::string can_tx_topic_;
  std::string can_rx_topic_;
  bool dry_run_ = true;
  bool protocol_verified_ = false;
  bool enabled_ = false;
  bool ready_ = false;
  bool initializing_ = false;
  std::array<std::int32_t, 3> actual_positions_{};
  std::array<std::int32_t, 3> actual_velocities_{};
  std::deque<SdoRequest> request_queue_;
  std::optional<SdoRequest> pending_request_;
  std::chrono::steady_clock::time_point pending_sent_at_{};
  std::chrono::steady_clock::time_point last_state_poll_{};
  rclcpp::Publisher<sensor_msgs::msg::JointState>::SharedPtr state_publisher_;
  rclcpp::Publisher<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_publisher_;
  rclcpp::Subscription<trajectory_msgs::msg::JointTrajectory>::SharedPtr command_subscription_;
  rclcpp::Subscription<crawling_robot_interfaces::msg::CanFrame>::SharedPtr can_subscription_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr enable_service_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace crawling_robot_drivers

namespace {

const char* gFatalNodeName = "canopen_axis_node";

void fatalTerminateHandler() noexcept {
  std::fprintf(stderr, "[%s] fatal: std::terminate called\n", gFatalNodeName);
  std::fflush(stderr);
  std::abort();
}

void installFatalHandler(const char* node_name) {
  gFatalNodeName = node_name;
  std::set_terminate(&fatalTerminateHandler);
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    installFatalHandler("canopen_axis_node");
    std::fprintf(stderr, "[canopen_axis_node] main start\n");
    std::fflush(stderr);
    rclcpp::init(argc, argv);
    crawling_robot_logging::installNodeFileLogger("canopen_axis_node");
    std::fprintf(stderr, "[canopen_axis_node] node construction start\n");
    std::fflush(stderr);
    auto node = std::make_shared<crawling_robot_drivers::CanopenAxisNode>();
    std::fprintf(stderr, "[canopen_axis_node] spin start\n");
    std::fflush(stderr);
    rclcpp::spin(node);
    std::fprintf(stderr, "[canopen_axis_node] spin end\n");
    std::fflush(stderr);
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& ex) {
    std::fprintf(stderr, "[canopen_axis_node] fatal exception: %s\n", ex.what());
    std::fflush(stderr);
  } catch (...) {
    std::fprintf(stderr, "[canopen_axis_node] fatal unknown exception\n");
    std::fflush(stderr);
  }
  try {
    rclcpp::shutdown();
  } catch (...) {
  }
  return 1;
}
