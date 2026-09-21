"""Tk monitor, IMU attitude popup, laser profile view, and keyboard control."""

from __future__ import annotations

import math
import json
import os
import struct
import time
import tkinter as tk
from tkinter import messagebox, ttk
from pathlib import Path
from typing import Any

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Imu, JointState
from std_srvs.srv import SetBool

from crawling_robot_interfaces.msg import DriveCommandStatus, LaserCorrectionStatus, LaserProfile
from crawling_robot_interfaces.srv import (
    SetDriveLimits,
    SetLocalizationReference,
    SetMotorMapping,
)


DEGREES_PER_RADIAN = 180.0 / math.pi
STANDARD_GRAVITY_M_S2 = 9.80665
MAX_POINTS_TO_DRAW = 2400
SERVICE_WAIT_SECONDS = 3.0
MAX_OPERATOR_LOG_LINES = 10000
DRIVE_LIMIT_FIELDS = (
    ("linear_speed_m_s", 0.001, 0.300, 0.001),
    ("angular_speed_rad_s", 0.01, 2.00, 0.01),
    ("linear_accel_m_s2", 0.001, 1.000, 0.001),
    ("angular_accel_rad_s2", 0.01, 5.00, 0.01),
    ("max_wheel_speed_m_s", 0.001, 1.00, 0.001),
    ("minimum_inner_wheel_ratio", 0.0, 1.0, 0.05),
)
# ROS keeps all motion values in SI units. The operator-facing values use the
# units used on the machine drawing and motor test sheet.
DISPLAY_DRIVE_FIELDS = (
    (1.0, 300.0, 1.0),       # mm/s
    (0.6, 114.6, 0.1),       # deg/s
    (1.0, 1000.0, 1.0),      # mm/s2
    (0.6, 286.5, 0.1),       # deg/s2
    (1.0, 1000.0, 1.0),      # mm/s
    (0.0, 1.0, 0.05),
)
DRIVE_DISPLAY_FACTORS = (1000.0, DEGREES_PER_RADIAN, 1000.0, DEGREES_PER_RADIAN, 1000.0, 1.0)


def drive_value_to_display(value: float, index: int) -> float:
    return value * DRIVE_DISPLAY_FACTORS[index]


def drive_value_to_si(value: float, index: int) -> float:
    return value / DRIVE_DISPLAY_FACTORS[index]


def monitor_settings_path() -> Path:
    """Return a per-user path that survives portable-package updates."""
    root = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
    base = Path(root) if root else Path.home()
    return base / "CrawlingRobot" / "monitor_settings.json"


def append_operator_log(event: str, payload: dict[str, Any]) -> Path:
    """Append a monitor event to the shared bounded operator log."""
    log_dir = Path.cwd() / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    path = log_dir / "robot.log"
    entry = {"时间": time.strftime("%Y-%m-%d %H:%M:%S"), "来源": "监控界面", "事件": event, "数据": payload}
    with path.open("a", encoding="utf-8") as output:
        output.write(json.dumps(entry, ensure_ascii=False, separators=(",", ":")) + "\n")
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
        if len(lines) > MAX_OPERATOR_LOG_LINES:
            path.write_text("\n".join(lines[-MAX_OPERATOR_LOG_LINES:]) + "\n", encoding="utf-8")
    except OSError:
        pass
    return path


def quaternion_to_euler_degrees(x: float, y: float, z: float, w: float) -> tuple[float, float, float]:
    """Convert a ROS quaternion to roll, pitch, yaw in degrees."""
    roll = math.atan2(
        2.0 * (w * x + y * z),
        1.0 - 2.0 * (x * x + y * y),
    )
    pitch_argument = 2.0 * (w * y - z * x)
    pitch = math.asin(max(-1.0, min(1.0, pitch_argument)))
    yaw = math.atan2(
        2.0 * (w * z + x * y),
        1.0 - 2.0 * (y * y + z * z),
    )
    return (
        roll * DEGREES_PER_RADIAN,
        pitch * DEGREES_PER_RADIAN,
        yaw * DEGREES_PER_RADIAN,
    )


def _field_info(cloud: Any, name: str) -> tuple[int, int] | None:
    for field in cloud.fields:
        if field.name == name:
            return int(field.offset), int(field.datatype)
    return None


def _read_float(data: bytes, offset: int, datatype: int, big_endian: bool) -> float | None:
    format_code = {7: "f", 8: "d"}.get(datatype)
    if format_code is None:
        return None
    size = struct.calcsize(format_code)
    if offset < 0 or offset + size > len(data):
        return None
    try:
        prefix = ">" if big_endian else "<"
        value = struct.unpack_from(prefix + format_code, data, offset)[0]
    except struct.error:
        return None
    return float(value) if math.isfinite(value) else None


def decode_profile(message: LaserProfile, horizontal_axis: str, vertical_axis: str) -> list[tuple[float, float]]:
    """Extract two axes from PointCloud2 without depending on sensor_msgs_py."""
    if horizontal_axis == vertical_axis:
        return []
    cloud = message.points
    horizontal = _field_info(cloud, horizontal_axis)
    vertical = _field_info(cloud, vertical_axis)
    point_step = int(cloud.point_step)
    if horizontal is None or vertical is None or point_step <= 0:
        return []

    width = int(cloud.width)
    height = int(cloud.height)
    row_step = int(cloud.row_step or width * point_step)
    result: list[tuple[float, float]] = []
    for row in range(height):
        for column in range(width):
            base = row * row_step + column * point_step
            horizontal_value = _read_float(
                cloud.data,
                base + horizontal[0],
                horizontal[1],
                bool(cloud.is_bigendian),
            )
            vertical_value = _read_float(
                cloud.data,
                base + vertical[0],
                vertical[1],
                bool(cloud.is_bigendian),
            )
            if horizontal_value is None or vertical_value is None:
                continue
            if abs(horizontal_value) < 1000.0 and abs(vertical_value) < 1000.0:
                result.append((horizontal_value, vertical_value))

    if len(result) > MAX_POINTS_TO_DRAW:
        step = max(1, len(result) // MAX_POINTS_TO_DRAW)
        result = result[::step]
    return result


class MonitorNode(Node):
    def __init__(self, app: "MonitorApp") -> None:
        super().__init__("crawling_robot_monitor")
        self._app = app
        imu_topic = self.declare_parameter("imu_topic", "/imu/data").value
        profile_topic = self.declare_parameter("profile_topic", "/laser_profile/frame").value
        cmd_vel_topic = self.declare_parameter("cmd_vel_topic", "/cmd_vel").value
        self.max_linear_speed = float(
            self.declare_parameter("max_linear_speed_m_s", 0.025).value
        )
        self.max_angular_speed = float(
            self.declare_parameter("max_angular_speed_rad_s", 0.30).value
        )
        self.max_linear_accel = float(
            self.declare_parameter("max_linear_accel_m_s2", 0.50).value
        )
        self.max_angular_accel = float(
            self.declare_parameter("max_angular_accel_rad_s2", 2.00).value
        )
        self.max_wheel_speed = float(
            self.declare_parameter("max_wheel_speed_m_s", 0.30).value
        )
        self.minimum_inner_wheel_ratio = float(
            self.declare_parameter("minimum_inner_wheel_ratio", 0.50).value
        )
        self.left_motor_id = int(self.declare_parameter("left_motor_id", 1).value)
        self.right_motor_id = int(self.declare_parameter("right_motor_id", 2).value)
        self.left_motor_sign = int(self.declare_parameter("left_motor_sign", -1).value)
        self.right_motor_sign = int(self.declare_parameter("right_motor_sign", 1).value)
        self.can_serial_port = str(
            self.declare_parameter("can_serial_port", os.environ.get("CRAWLING_ROBOT_CAN_PORT", "")).value
        )
        self.can_bitrate = int(self.declare_parameter("can_bitrate", 1000000).value)
        self.open_sensor_windows = bool(
            self.declare_parameter("open_sensor_windows", True).value
        )

        self._cmd_publisher = self.create_publisher(Twist, cmd_vel_topic, 20)
        self._enable_client = self.create_client(SetBool, "/drive/enable")
        self._auto_client = self.create_client(SetBool, "/laser_correction/enable")
        self._limits_client = self.create_client(SetDriveLimits, "/drive/set_limits")
        self._mapping_client = self.create_client(SetMotorMapping, "/drive/set_motor_mapping")
        self._reference_client = self.create_client(
            SetLocalizationReference, "/laser_correction/set_reference"
        )
        self.create_subscription(Imu, imu_topic, self._on_imu, qos_profile_sensor_data)
        self.create_subscription(
            LaserProfile,
            profile_topic,
            self._on_profile,
            qos_profile_sensor_data,
        )
        self.create_subscription(
            JointState, "/drive/joint_states", self._on_joint_state, qos_profile_sensor_data
        )
        self.create_subscription(
            DriveCommandStatus, "/drive/command_status", self._on_drive_command_status,
            qos_profile_sensor_data,
        )
        self.create_subscription(
            LaserCorrectionStatus,
            "/laser_correction/status",
            self._on_correction_status,
            qos_profile_sensor_data,
        )
        self.last_joint_state: JointState | None = None
        self._joint_values: dict[str, tuple[float, float, float]] = {}
        self.last_correction_status: LaserCorrectionStatus | None = None

    def _on_imu(self, message: Imu) -> None:
        self._app.update_imu(message)

    def _on_profile(self, message: LaserProfile) -> None:
        self._app.update_profile(message)

    def _on_joint_state(self, message: JointState) -> None:
        self.last_joint_state = message
        self._app.update_joint_state(message)

    def _on_drive_command_status(self, message: DriveCommandStatus) -> None:
        self._app.update_drive_command_status(message)

    def _on_correction_status(self, message: LaserCorrectionStatus) -> None:
        self.last_correction_status = message
        self._app.update_correction_status(message)

    def publish_command(self, linear: float, angular: float) -> None:
        message = Twist()
        message.linear.x = float(linear)
        message.angular.z = float(angular)
        self._cmd_publisher.publish(message)

    def apply_drive_limits(
        self,
        linear_speed: float,
        angular_speed: float,
        linear_accel: float,
        angular_accel: float,
        wheel_speed: float,
        inner_wheel_ratio: float,
    ) -> tuple[bool, str]:
        if not self._limits_client.wait_for_service(timeout_sec=SERVICE_WAIT_SECONDS):
            return False, "未发现 /drive/set_limits 服务，请确认 ROS 已启动。"
        request = SetDriveLimits.Request()
        request.max_linear_speed_m_s = linear_speed
        request.max_angular_speed_rad_s = angular_speed
        request.max_linear_accel_m_s2 = linear_accel
        request.max_angular_accel_rad_s2 = angular_accel
        request.max_wheel_speed_m_s = wheel_speed
        request.minimum_inner_wheel_ratio = inner_wheel_ratio
        future = self._limits_client.call_async(request)
        deadline = time.monotonic() + SERVICE_WAIT_SECONDS
        while not future.done() and time.monotonic() < deadline and rclpy.ok():
            rclpy.spin_once(self, timeout_sec=0.05)
        if not future.done():
            return False, "调用 /drive/set_limits 超时。"
        response = future.result()
        if response is None or not response.success:
            return False, str(response.message if response else "调用失败")
        self.max_linear_speed = linear_speed
        self.max_angular_speed = angular_speed
        self.max_linear_accel = linear_accel
        self.max_angular_accel = angular_accel
        self.max_wheel_speed = wheel_speed
        self.minimum_inner_wheel_ratio = inner_wheel_ratio
        return True, str(response.message)

    def apply_motor_mapping(
        self,
        left_motor_id: int,
        right_motor_id: int,
        left_motor_sign: int,
        right_motor_sign: int,
    ) -> tuple[bool, str]:
        if not self._mapping_client.wait_for_service(timeout_sec=SERVICE_WAIT_SECONDS):
            return False, "未发现 /drive/set_motor_mapping 服务，底盘节点可能尚未启动。"
        request = SetMotorMapping.Request()
        request.left_motor_id = left_motor_id
        request.right_motor_id = right_motor_id
        request.left_motor_sign = left_motor_sign
        request.right_motor_sign = right_motor_sign
        future = self._mapping_client.call_async(request)
        deadline = time.monotonic() + SERVICE_WAIT_SECONDS
        while not future.done() and time.monotonic() < deadline and rclpy.ok():
            rclpy.spin_once(self, timeout_sec=0.05)
        if not future.done():
            return False, "调用 /drive/set_motor_mapping 超时。"
        response = future.result()
        if response is None or not response.success:
            return False, str(response.message if response else "调用失败")
        self.left_motor_id = left_motor_id
        self.right_motor_id = right_motor_id
        self.left_motor_sign = left_motor_sign
        self.right_motor_sign = right_motor_sign
        return True, str(response.message)

    def set_auto_enabled(self, enabled: bool) -> tuple[bool, str]:
        if not self._auto_client.wait_for_service(timeout_sec=SERVICE_WAIT_SECONDS):
            return False, "未发现 /laser_correction/enable 服务。"
        request = SetBool.Request()
        request.data = enabled
        future = self._auto_client.call_async(request)
        deadline = time.monotonic() + SERVICE_WAIT_SECONDS
        while not future.done() and time.monotonic() < deadline and rclpy.ok():
            rclpy.spin_once(self, timeout_sec=0.05)
        if not future.done():
            return False, "调用 /laser_correction/enable 超时。"
        response = future.result()
        if response is None or not response.success:
            return False, str(response.message if response else "调用失败")
        return True, str(response.message)

    def set_localization_reference(self, contour_lateral: float, heading_reference: float) -> tuple[bool, str]:
        if not self._reference_client.wait_for_service(timeout_sec=SERVICE_WAIT_SECONDS):
            return False, "未发现 /laser_correction/set_reference 服务。"
        request = SetLocalizationReference.Request()
        request.contour_lateral_m = float(contour_lateral)
        request.heading_reference_rad = float(heading_reference)
        future = self._reference_client.call_async(request)
        deadline = time.monotonic() + SERVICE_WAIT_SECONDS
        while not future.done() and time.monotonic() < deadline and rclpy.ok():
            rclpy.spin_once(self, timeout_sec=0.05)
        if not future.done():
            return False, "调用 /laser_correction/set_reference 超时。"
        response = future.result()
        if response is None or not response.success:
            return False, str(response.message if response else "调用失败")
        return True, str(response.message)


class ImuDialog(tk.Toplevel):
    def __init__(self, parent: "MonitorApp") -> None:
        super().__init__(parent)
        self.title("陀螺仪 / IMU 姿态")
        self.geometry("430x430")
        self.minsize(390, 360)
        self.configure(bg="#101820")
        self.protocol("WM_DELETE_WINDOW", self.withdraw)

        self.status_var = tk.StringVar(value="等待 IMU 数据...")
        self.age_var = tk.StringVar(value="数据延迟: --")
        self.values: dict[str, tk.StringVar] = {
            key: tk.StringVar(value="--")
            for key in (
                "roll",
                "pitch",
                "yaw",
                "qx",
                "qy",
                "qz",
                "qw",
                "gx",
                "gy",
                "gz",
                "ax",
                "ay",
                "az",
            )
        }
        self._build_ui()

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.configure("Imu.TFrame", background="#101820")
        style.configure(
            "ImuTitle.TLabel",
            background="#101820",
            foreground="#f4f7f8",
            font=("Segoe UI", 18, "bold"),
        )
        style.configure(
            "ImuValue.TLabel",
            background="#182632",
            foreground="#5fe3ef",
            font=("Consolas", 15, "bold"),
            padding=(10, 8),
        )
        style.configure(
            "ImuName.TLabel",
            background="#182632",
            foreground="#91a7b5",
            font=("Segoe UI", 10),
            padding=(10, 8),
        )
        root = ttk.Frame(self, padding=16, style="Imu.TFrame")
        root.pack(fill="both", expand=True)
        ttk.Label(root, text="陀螺仪姿态与原始量", style="ImuTitle.TLabel").pack(anchor="w")
        ttk.Label(root, textvariable=self.status_var, style="ImuName.TLabel").pack(
            fill="x", pady=(10, 4)
        )

        attitude = tk.Frame(root, bg="#182632")
        attitude.pack(fill="x", pady=(4, 10))
        for row, (key, label) in enumerate(
            (("roll", "横滚"), ("pitch", "俯仰"), ("yaw", "航向"))
        ):
            tk.Label(
                attitude,
                text=label,
                bg="#182632",
                fg="#91a7b5",
                font=("Segoe UI", 11),
                anchor="w",
                width=15,
                padx=10,
                pady=8,
            ).grid(row=row, column=0, sticky="ew")
            ttk.Label(attitude, textvariable=self.values[key], style="ImuValue.TLabel").grid(
                row=row, column=1, sticky="ew", padx=(0, 10), pady=4
            )
        attitude.columnconfigure(1, weight=1)

        raw = ttk.Frame(root, style="Imu.TFrame")
        raw.pack(fill="x")
        for column, (title, keys) in enumerate(
            (
                ("角速度 °/s", ("gx", "gy", "gz")),
                ("加速度 g", ("ax", "ay", "az")),
                ("四元数", ("qx", "qy", "qz", "qw")),
            )
        ):
            box = tk.Frame(raw, bg="#182632")
            box.grid(row=0, column=column, sticky="nsew", padx=(0 if column == 0 else 6, 0))
            tk.Label(
                box,
                text=title,
                bg="#182632",
                fg="#91a7b5",
                font=("Segoe UI", 10, "bold"),
                anchor="w",
                padx=8,
                pady=7,
            ).pack(fill="x")
            for key in keys:
                ttk.Label(box, textvariable=self.values[key], style="ImuValue.TLabel").pack(
                    fill="x", padx=6, pady=2
                )
        for column in range(3):
            raw.columnconfigure(column, weight=1)
        ttk.Label(root, textvariable=self.age_var, style="ImuName.TLabel").pack(
            fill="x", pady=(12, 0)
        )

    def update_message(self, message: Imu, received_at: float) -> None:
        roll, pitch, yaw = quaternion_to_euler_degrees(
            float(message.orientation.x),
            float(message.orientation.y),
            float(message.orientation.z),
            float(message.orientation.w),
        )
        self.values["roll"].set(f"{roll:+8.3f} °")
        self.values["pitch"].set(f"{pitch:+8.3f} °")
        self.values["yaw"].set(f"{yaw:+8.3f} °")
        for key, value in (
            ("qx", message.orientation.x),
            ("qy", message.orientation.y),
            ("qz", message.orientation.z),
            ("qw", message.orientation.w),
            ("gx", message.angular_velocity.x * DEGREES_PER_RADIAN),
            ("gy", message.angular_velocity.y * DEGREES_PER_RADIAN),
            ("gz", message.angular_velocity.z * DEGREES_PER_RADIAN),
            ("ax", message.linear_acceleration.x / STANDARD_GRAVITY_M_S2),
            ("ay", message.linear_acceleration.y / STANDARD_GRAVITY_M_S2),
            ("az", message.linear_acceleration.z / STANDARD_GRAVITY_M_S2),
        ):
            self.values[key].set(f"{float(value):+.5f}")
        self.status_var.set(f"在线 | 坐标系: {message.header.frame_id or '--'}")
        self.age_var.set(f"接收时间: {time.strftime('%H:%M:%S')} | 本地延迟: {(time.monotonic() - received_at) * 1000:.0f} ms")


class PointCloudDialog(tk.Toplevel):
    def __init__(self, parent: "MonitorApp") -> None:
        super().__init__(parent)
        self.title("激光轮廓仪点云")
        self.geometry("820x620")
        self.minsize(580, 420)
        self.configure(bg="#0c141a")
        self.protocol("WM_DELETE_WINDOW", self.withdraw)
        self.canvas = tk.Canvas(self, bg="#0c141a", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True, padx=12, pady=(12, 6))
        self.info_var = tk.StringVar(value="等待激光点云数据...")
        self.horizontal_axis_var = tk.StringVar(value="y")
        self.vertical_axis_var = tk.StringVar(value="z")

        controls = ttk.Frame(self)
        controls.pack(fill="x", padx=14, pady=(0, 5))
        ttk.Label(controls, text="横轴").pack(side="left")
        ttk.Combobox(
            controls,
            textvariable=self.horizontal_axis_var,
            values=("x", "y", "z"),
            width=4,
            state="readonly",
        ).pack(side="left", padx=(5, 14))
        ttk.Label(controls, text="纵轴").pack(side="left")
        ttk.Combobox(
            controls,
            textvariable=self.vertical_axis_var,
            values=("x", "y", "z"),
            width=4,
            state="readonly",
        ).pack(side="left", padx=5)
        ttk.Label(
            controls,
            text="点云显示为所选两轴投影",
            foreground="#91a7b5",
        ).pack(side="right")
        tk.Label(
            self,
            textvariable=self.info_var,
            bg="#0c141a",
            fg="#d8e4ea",
            anchor="w",
        ).pack(fill="x", padx=14, pady=(0, 12))

    def update_message(self, message: LaserProfile, points: list[tuple[float, float]]) -> None:
        if not self.winfo_exists():
            return
        self.canvas.delete("all")
        width = max(100, self.canvas.winfo_width())
        height = max(100, self.canvas.winfo_height())
        margin_left, margin_top, margin_right, margin_bottom = 56, 20, 20, 42
        plot_width = max(1, width - margin_left - margin_right)
        plot_height = max(1, height - margin_top - margin_bottom)

        if len(points) < 1:
            self.info_var.set("未收到有效点云数据")
            return

        x_values = [point[0] for point in points]
        y_values = [point[1] for point in points]
        x_min = min(-0.05, min(x_values))
        x_max = max(0.05, max(x_values))
        y_min = min(y_values)
        y_max = max(y_values)
        x_span = max(0.001, x_max - x_min)
        y_span = max(0.001, y_max - y_min)
        x_min -= 0.08 * x_span
        x_max += 0.08 * x_span
        y_min -= 0.08 * y_span
        y_max += 0.08 * y_span

        def sx(value: float) -> float:
            return margin_left + (value - x_min) / max(1e-9, x_max - x_min) * plot_width

        def sy(value: float) -> float:
            return margin_top + (y_max - value) / max(1e-9, y_max - y_min) * plot_height

        self.canvas.create_rectangle(
            margin_left,
            margin_top,
            width - margin_right,
            height - margin_bottom,
            outline="#30414d",
        )
        for fraction in (0.25, 0.5, 0.75):
            grid_x = margin_left + fraction * plot_width
            grid_y = margin_top + fraction * plot_height
            self.canvas.create_line(
                grid_x,
                margin_top,
                grid_x,
                height - margin_bottom,
                fill="#1c2b35",
            )
            self.canvas.create_line(
                margin_left,
                grid_y,
                width - margin_right,
                grid_y,
                fill="#1c2b35",
            )

        if x_min <= 0.0 <= x_max:
            origin_x = sx(0.0)
            self.canvas.create_line(
                origin_x,
                margin_top,
                origin_x,
                height - margin_bottom,
                fill="#e5b84b",
                dash=(5, 4),
            )
        if y_min <= 0.0 <= y_max:
            origin_y = sy(0.0)
            self.canvas.create_line(
                margin_left,
                origin_y,
                width - margin_right,
                origin_y,
                fill="#e5b84b",
                dash=(5, 4),
            )

        radius = 2
        for x_value, y_value in points:
            x_pixel = sx(x_value)
            y_pixel = sy(y_value)
            self.canvas.create_oval(
                x_pixel - radius,
                y_pixel - radius,
                x_pixel + radius,
                y_pixel + radius,
                fill="#4ed7e8",
                outline="",
            )
        self.canvas.create_text(
            margin_left,
            height - 18,
            text=f"{self.horizontal_axis_var.get()} (m)",
            fill="#8ea0ad",
            anchor="w",
        )
        self.canvas.create_text(
            12,
            margin_top,
            text=f"{self.vertical_axis_var.get()} (m)",
            fill="#8ea0ad",
            anchor="w",
        )
        self.info_var.set(
            f"有效点 {len(points)} | 编码器计数 {int(message.encoder_ticks)} | "
            f"范围 {x_min:.3f}..{x_max:.3f} m, {y_min:.3f}..{y_max:.3f} m"
        )


class MonitorApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("爬壁机器人监控与手动控制")
        self.geometry("1180x780")
        self.minsize(940, 640)
        self.configure(bg="#101820")

        self._node: MonitorNode | None = None
        self._pressed: set[str] = set()
        self._last_imu: tuple[Imu, float] | None = None
        self._last_profile: tuple[LaserProfile, list[tuple[float, float]]] | None = None
        self._closing = False
        self._drive_enabled = False
        self._current_linear = 0.0
        self._current_angular = 0.0
        self._imu_dialog: ImuDialog | None = None
        self._point_cloud_dialog: PointCloudDialog | None = None
        self._imu_panel_values: dict[str, tk.StringVar] = {}
        self._imu_panel_status = tk.StringVar(value="等待 IMU 数据...")
        self._imu_panel_age = tk.StringVar(value="数据延迟: --")
        self._profile_info = tk.StringVar(value="等待激光点云数据...")
        self._profile_horizontal_axis = tk.StringVar(value="y")
        self._profile_vertical_axis = tk.StringVar(value="z")
        self._profile_canvas: tk.Canvas | None = None
        self.status_var = tk.StringVar(value="ROS 已启动，等待传感器数据...")
        self.drive_var = tk.StringVar(value="底盘未使能")
        self.auto_var = tk.StringVar(value="自动纠偏未启动")
        self.imu_var = tk.StringVar(value="IMU：等待数据")
        self.laser_var = tk.StringVar(value="激光：等待点云")
        self.motor_var = tk.StringVar(value="电机状态: 等待反馈")
        self.correction_var = tk.StringVar(value="纠偏状态: 等待数据")
        self.command_var = tk.StringVar(value="底盘实际下发: 线速度 0.0 mm/s | 角速度 0.0 deg/s")
        self.linear_speed_var = tk.StringVar(value="5.0")
        self.angular_speed_var = tk.StringVar(value="1.7")
        self.linear_accel_var = tk.StringVar(value="500.0")
        self.angular_accel_var = tk.StringVar(value="114.6")
        self.wheel_speed_var = tk.StringVar(value="300.0")
        self.inner_wheel_ratio_var = tk.StringVar(value="0.500")
        self.limits_var = tk.StringVar(value="运动参数等待应用")
        self.left_motor_id_var = tk.StringVar(value="1")
        self.right_motor_id_var = tk.StringVar(value="2")
        self.left_motor_sign_var = tk.StringVar(value="+1")
        self.right_motor_sign_var = tk.StringVar(value="-1")
        self.mapping_var = tk.StringVar(value="输出方向符号：+1 保持协议正向，-1 反向")
        self._settings_save_after: str | None = None
        self._restoring_settings = False
        self._loaded_limit_keys = self._load_settings()
        self._settings_loaded = bool(self._loaded_limit_keys)
        self._restore_limits_pending = self._settings_loaded
        self._restore_mapping_pending = self._settings_loaded

        self._build_ui()
        for variable in (
            self.left_motor_id_var, self.right_motor_id_var,
            self.left_motor_sign_var, self.right_motor_sign_var,
        ):
            variable.trace_add("write", self._schedule_settings_save)
        # A monitor launched from CMD can otherwise open behind that console.
        self.after_idle(self._bring_to_front)
        self.bind_all("<KeyPress>", self._on_key_down, add="+")
        self.bind_all("<KeyRelease>", self._on_key_up, add="+")
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.after(100, self._spin_ros)
        self.after(20, self._control_tick)
        self.after(500, self._check_service_status)

    def _bring_to_front(self) -> None:
        self.deiconify()
        self.lift()
        self.attributes("-topmost", True)
        self.after(800, lambda: self.attributes("-topmost", False))
        self.focus_force()

    def attach_node(self, node: MonitorNode) -> None:
        self._node = node
        node_values = (
            node.max_linear_speed, node.max_angular_speed, node.max_linear_accel,
            node.max_angular_accel, node.max_wheel_speed, node.minimum_inner_wheel_ratio,
        )
        limit_variables = (
            self.linear_speed_var, self.angular_speed_var, self.linear_accel_var,
            self.angular_accel_var, self.wheel_speed_var, self.inner_wheel_ratio_var,
        )
        self._restoring_settings = True
        try:
            for index, ((key, _lower, _upper, _step), variable, node_value) in enumerate(
                zip(DRIVE_LIMIT_FIELDS, limit_variables, node_values)
            ):
                if key not in self._loaded_limit_keys:
                    variable.set(f"{drive_value_to_display(node_value, index):.3f}")
            if not self._settings_loaded:
                self.left_motor_id_var.set(str(node.left_motor_id))
                self.right_motor_id_var.set(str(node.right_motor_id))
                self.left_motor_sign_var.set(f"{node.left_motor_sign:+d}")
                self.right_motor_sign_var.set(f"{node.right_motor_sign:+d}")
        finally:
            self._restoring_settings = False
        if self._settings_loaded:
            self.limits_var.set(
                "已恢复上次输入；底盘服务就绪后将自动应用运动参数"
            )
            self._restore_limits_pending = True
            self._schedule_settings_save()
        else:
            self.limits_var.set(
                f"当前节点参数：线速度 {drive_value_to_display(node.max_linear_speed, 0):.1f} mm/s，"
                f"角速度 {drive_value_to_display(node.max_angular_speed, 1):.1f} deg/s"
            )

    def _load_settings(self) -> set[str]:
        path = monitor_settings_path()
        try:
            values = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError, TypeError):
            return set()
        if not isinstance(values, dict):
            return set()
        variables = {
            "linear_speed_m_s": self.linear_speed_var,
            "angular_speed_rad_s": self.angular_speed_var,
            "linear_accel_m_s2": self.linear_accel_var,
            "angular_accel_rad_s2": self.angular_accel_var,
            "max_wheel_speed_m_s": self.wheel_speed_var,
            "minimum_inner_wheel_ratio": self.inner_wheel_ratio_var,
        }
        ranges = {key: (lower, upper) for key, lower, upper, _step in DRIVE_LIMIT_FIELDS}
        loaded_limit_keys: set[str] = set()
        for index, (key, variable) in enumerate(variables.items()):
            if key in values:
                try:
                    value = float(values[key])
                except (TypeError, ValueError):
                    continue
                lower, upper = ranges[key]
                if math.isfinite(value) and lower <= value <= upper:
                    variable.set(f"{drive_value_to_display(value, index):.3f}")
                    loaded_limit_keys.add(key)
        for key, variable in (
            ("left_motor_id", self.left_motor_id_var),
            ("right_motor_id", self.right_motor_id_var),
        ):
            try:
                value = int(values[key])
            except (KeyError, TypeError, ValueError):
                continue
            if 1 <= value <= 32:
                variable.set(str(value))
        for key, variable in (
            ("left_motor_sign", self.left_motor_sign_var),
            ("right_motor_sign", self.right_motor_sign_var),
        ):
            try:
                value = int(values[key])
            except (KeyError, TypeError, ValueError):
                continue
            if value in (-1, 1):
                variable.set(f"{value:+d}")
        return loaded_limit_keys

    def _schedule_settings_save(self, *_args: Any) -> None:
        if self._closing or self._restoring_settings:
            return
        if self._settings_save_after is not None:
            self.after_cancel(self._settings_save_after)
        self._settings_save_after = self.after(400, self._save_settings)

    def _save_settings(self) -> None:
        self._settings_save_after = None
        limit_variables = (
            self.linear_speed_var, self.angular_speed_var, self.linear_accel_var,
            self.angular_accel_var, self.wheel_speed_var, self.inner_wheel_ratio_var,
        )
        values = {
            field[0]: drive_value_to_si(float(variable.get()), index)
            for index, (field, variable) in enumerate(zip(DRIVE_LIMIT_FIELDS, limit_variables))
        }
        values.update({
            "left_motor_id": self.left_motor_id_var.get(),
            "right_motor_id": self.right_motor_id_var.get(),
            "left_motor_sign": self.left_motor_sign_var.get(),
            "right_motor_sign": self.right_motor_sign_var.get(),
        })
        path = monitor_settings_path()
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(values, ensure_ascii=False, indent=2), encoding="utf-8")
        except (OSError, ValueError):
            # Saving preferences must never prevent an emergency stop or exit.
            pass

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("Root.TFrame", background="#101820")
        style.configure("Status.TFrame", background="#182632")
        style.configure(
            "Title.TLabel",
            background="#101820",
            foreground="#f4f7f8",
            font=("Segoe UI", 19, "bold"),
        )
        style.configure(
            "Info.TLabel",
            background="#182632",
            foreground="#d8e4ea",
            font=("Segoe UI", 11),
            padding=10,
        )
        style.configure("Muted.TLabel", background="#101820", foreground="#91a7b5")
        style.configure(
            "CompactStatus.TLabel",
            background="#182632",
            foreground="#b9cad3",
            font=("Segoe UI", 9),
            padding=(6, 3),
        )
        style.configure("Accent.TButton", background="#1e7f9b", foreground="#ffffff", padding=8)
        style.configure("Danger.TButton", background="#b84444", foreground="#ffffff", padding=8)

        root = ttk.Frame(self, padding=18, style="Root.TFrame")
        root.pack(fill="both", expand=True)
        ttk.Label(root, text="爬壁机器人监控与手动控制", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            root,
            text="ROS、CAN、底盘和传感器状态统一显示在本窗口",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(3, 6))

        status_strip = ttk.Frame(root, padding=3, style="Status.TFrame")
        status_strip.pack(fill="x", pady=(0, 8))
        compact_status = (
            (self.status_var, 0, 0, 2),
            (self.drive_var, 0, 2, 1),
            (self.auto_var, 0, 3, 1),
            (self.imu_var, 1, 0, 1),
            (self.laser_var, 1, 1, 1),
            (self.correction_var, 1, 2, 1),
            (self.command_var, 1, 3, 1),
            (self.motor_var, 2, 0, 4),
        )
        for variable, row, column, columnspan in compact_status:
            ttk.Label(
                status_strip,
                textvariable=variable,
                style="CompactStatus.TLabel",
                anchor="w",
            ).grid(row=row, column=column, columnspan=columnspan, sticky="ew")
        ttk.Label(
            status_strip,
            text="方向键或 W/A/S/D 控制；松开立即减速，空格停止",
            style="CompactStatus.TLabel",
            anchor="w",
        ).grid(row=3, column=0, columnspan=4, sticky="ew")
        for column in range(4):
            status_strip.columnconfigure(column, weight=1, uniform="status")

        content = ttk.Panedwindow(root, orient="horizontal")
        content.pack(fill="both", expand=True)
        drive_panel = ttk.Frame(content, padding=(0, 0, 8, 0), style="Root.TFrame")
        sensor_panel = ttk.Frame(content, padding=(8, 0, 0, 0), style="Root.TFrame")
        content.add(drive_panel, weight=3)
        content.add(sensor_panel, weight=2)
        self._build_drive_controls(drive_panel)
        self._build_sensor_tabs(sensor_panel)

    def _build_drive_controls(self, parent: ttk.Frame) -> None:
        mapping = ttk.LabelFrame(parent, text="电机节点映射", padding=8)
        mapping.pack(fill="x", pady=(4, 6))
        ttk.Label(mapping, text="左轮节点 ID").grid(row=0, column=0, sticky="w", pady=2)
        ttk.Spinbox(mapping, textvariable=self.left_motor_id_var, from_=1, to=32,
                    increment=1, width=5).grid(row=0, column=1, padx=(6, 12), pady=2)
        ttk.Label(mapping, text="输出方向符号").grid(row=0, column=2, sticky="e", padx=(0, 3))
        ttk.Combobox(mapping, textvariable=self.left_motor_sign_var,
                     values=("+1", "-1"), width=4, state="readonly").grid(row=0, column=3)
        ttk.Label(mapping, text="右轮节点 ID").grid(row=1, column=0, sticky="w", pady=2)
        ttk.Spinbox(mapping, textvariable=self.right_motor_id_var, from_=1, to=32,
                    increment=1, width=5).grid(row=1, column=1, padx=(6, 12), pady=2)
        ttk.Label(mapping, text="输出方向符号").grid(row=1, column=2, sticky="e", padx=(0, 3))
        ttk.Combobox(mapping, textvariable=self.right_motor_sign_var,
                     values=("+1", "-1"), width=4, state="readonly").grid(row=1, column=3)
        ttk.Button(mapping, text="应用电机映射", command=self._apply_motor_mapping).grid(
            row=0, column=4, rowspan=2, sticky="nsew", padx=(14, 0), pady=2
        )
        ttk.Label(mapping, textvariable=self.mapping_var, style="Muted.TLabel").grid(
            row=2, column=0, columnspan=5, sticky="w", pady=(5, 0)
        )

        limits = ttk.LabelFrame(parent, text="运动参数", padding=8)
        limits.pack(fill="x", pady=(4, 6))
        labels = (
            "线速度 (mm/s)",
            "角速度 (deg/s)",
            "线加速度 (mm/s²)",
            "角加速度 (deg/s²)",
            "车轮速度上限 (mm/s)",
            "转弯内/外轮比例",
        )
        variables = (
            self.linear_speed_var,
            self.angular_speed_var,
            self.linear_accel_var,
            self.angular_accel_var,
            self.wheel_speed_var,
            self.inner_wheel_ratio_var,
        )
        fields = tuple(
            (labels[index], variables[index], *DISPLAY_DRIVE_FIELDS[index])
            for index in range(len(DISPLAY_DRIVE_FIELDS))
        )
        numeric_validator = (self.register(self._validate_drive_input), "%P", "%V", "%W")
        for index, (label, variable, lower, upper, step) in enumerate(fields):
            row = index % 2
            column = (index // 2) * 2
            ttk.Label(limits, text=label).grid(row=row, column=column, sticky="w", pady=2)
            spinbox = ttk.Spinbox(
                limits,
                textvariable=variable,
                from_=lower,
                to=upper,
                increment=step,
                width=8,
                format="%.3f",
                validate="all",
                validatecommand=numeric_validator,
            )
            spinbox.grid(row=row, column=column + 1, sticky="ew", padx=(6, 12), pady=2)
            spinbox.bind("<FocusOut>", self._normalize_drive_input, add="+")
            variable.trace_add("write", self._mark_drive_limits_pending)
        ttk.Button(limits, text="应用运动参数", command=self._apply_drive_limits).grid(
            row=0, column=6, rowspan=2, sticky="nsew", padx=(4, 0), pady=2
        )
        ttk.Label(limits, textvariable=self.limits_var, style="Muted.TLabel").grid(
            row=2, column=0, columnspan=7, sticky="w", pady=(5, 0)
        )
        ttk.Label(
            limits,
            text="显示与输入使用 mm、度；底盘内部自动换算为 m、rad 并按相同数值应用。",
            style="Muted.TLabel",
        ).grid(row=3, column=0, columnspan=7, sticky="w", pady=(2, 0))

        controls = ttk.Frame(parent, style="Root.TFrame")
        controls.pack(fill="x", pady=(8, 4))
        ttk.Button(controls, text="底盘使能", style="Accent.TButton",
                   command=self._enable_drive).pack(side="left", padx=(0, 8))
        ttk.Button(controls, text="底盘禁用", command=self._disable_drive).pack(
            side="left", padx=(0, 8)
        )
        ttk.Button(controls, text="自动纠偏行走", style="Accent.TButton",
                   command=self._start_auto_walk).pack(side="left", padx=(0, 8))
        ttk.Button(controls, text="停止自动行走", style="Danger.TButton",
                   command=self._stop_auto_walk).pack(side="left", padx=(0, 8))
        ttk.Button(controls, text="定位感知", command=self._capture_localization).pack(
            side="left", padx=(0, 8)
        )
        ttk.Label(controls, text="IMU/点云在下方页签", style="Muted.TLabel").pack(
            side="left", padx=(8, 0)
        )
        pad = ttk.Frame(parent, style="Root.TFrame")
        pad.pack(expand=True, pady=4)
        self._make_drive_button(pad, "前进\n↑", "up", 0, 1)
        self._make_drive_button(pad, "左转\n←", "left", 1, 0)
        self._make_drive_button(pad, "停止\n空格", "stop", 1, 1, danger=True)
        self._make_drive_button(pad, "右转\n→", "right", 1, 2)
        self._make_drive_button(pad, "后退\n↓", "down", 2, 1)

    def _build_sensor_tabs(self, parent: ttk.Frame) -> None:
        notebook = ttk.Notebook(parent)
        notebook.pack(fill="both", expand=True, pady=(4, 0))

        imu_tab = ttk.Frame(notebook, padding=10)
        notebook.add(imu_tab, text="IMU 姿态")
        self._imu_panel_status = tk.StringVar(value="等待 IMU 数据...")
        self._imu_panel_age = tk.StringVar(value="数据延迟: --")
        ttk.Label(imu_tab, textvariable=self._imu_panel_status, style="Info.TLabel").pack(fill="x")
        values = {}
        for key, label in (("roll", "横滚"), ("pitch", "俯仰"), ("yaw", "航向")):
            row = ttk.Frame(imu_tab)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=label, width=16).pack(side="left")
            value = tk.StringVar(value="--")
            values[key] = value
            ttk.Label(row, textvariable=value, style="Info.TLabel").pack(side="left", fill="x", expand=True)
        self._imu_panel_values = values
        ttk.Label(imu_tab, textvariable=self._imu_panel_age, style="Muted.TLabel").pack(anchor="w", pady=(5, 0))

        profile_tab = ttk.Frame(notebook, padding=8)
        notebook.add(profile_tab, text="激光点云")
        toolbar = ttk.Frame(profile_tab)
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="横轴").pack(side="left")
        ttk.Combobox(toolbar, textvariable=self._profile_horizontal_axis,
                     values=("x", "y", "z"), width=4, state="readonly").pack(side="left", padx=(4, 12))
        ttk.Label(toolbar, text="纵轴").pack(side="left")
        ttk.Combobox(toolbar, textvariable=self._profile_vertical_axis,
                     values=("x", "y", "z"), width=4, state="readonly").pack(side="left", padx=4)
        self._profile_canvas = tk.Canvas(profile_tab, height=145, bg="#0c141a", highlightthickness=0)
        self._profile_canvas.pack(fill="both", expand=True, pady=(6, 3))
        ttk.Label(profile_tab, textvariable=self._profile_info, style="Muted.TLabel").pack(fill="x")

    def _make_drive_button(
        self,
        parent: ttk.Frame,
        text: str,
        key: str,
        row: int,
        column: int,
        danger: bool = False,
    ) -> None:
        button = ttk.Button(
            parent,
            text=text,
            width=10,
            style="Danger.TButton" if danger else "TButton",
        )
        button.grid(row=row, column=column, padx=4, pady=4, ipadx=4, ipady=4)
        button.bind("<ButtonPress-1>", lambda _event: self._press(key))
        button.bind("<ButtonRelease-1>", lambda _event: self._release(key))

    def _check_service_status(self) -> None:
        if self._closing or self._node is None:
            return
        drive_ready = self._node._enable_client.service_is_ready()
        self.status_var.set(
            "ROS 已连接，底盘使能服务已就绪。" if drive_ready else
            "ROS 已连接，等待底盘节点 /drive/enable；USB-CAN 将由启动脚本自动检测。"
        )
        if self._restore_limits_pending and self._node._limits_client.service_is_ready():
            if self._apply_drive_limits(automatic=True):
                self._restore_limits_pending = False
        if self._restore_mapping_pending and self._node._mapping_client.service_is_ready():
            self._apply_motor_mapping(automatic=True)
        self.after(1000, self._check_service_status)

    def _show_imu(self) -> None:
        if self._imu_dialog is None or not self._imu_dialog.winfo_exists():
            self._imu_dialog = ImuDialog(self)
        else:
            self._imu_dialog.deiconify()
            self._imu_dialog.lift()
        if self._last_imu is not None:
            self._imu_dialog.update_message(*self._last_imu)

    def _show_point_cloud(self) -> None:
        if self._point_cloud_dialog is None or not self._point_cloud_dialog.winfo_exists():
            self._point_cloud_dialog = PointCloudDialog(self)
        else:
            self._point_cloud_dialog.deiconify()
            self._point_cloud_dialog.lift()
        if self._last_profile is not None:
            self._point_cloud_dialog.update_message(*self._last_profile)

    def update_imu(self, message: Imu) -> None:
        received_at = time.monotonic()
        self._last_imu = (message, received_at)
        self.imu_var.set(
            f"IMU 姿态：横滚 {quaternion_to_euler_degrees(message.orientation.x, message.orientation.y, message.orientation.z, message.orientation.w)[0]:+.2f}° | "
            f"俯仰 {quaternion_to_euler_degrees(message.orientation.x, message.orientation.y, message.orientation.z, message.orientation.w)[1]:+.2f}° | "
            f"航向 {quaternion_to_euler_degrees(message.orientation.x, message.orientation.y, message.orientation.z, message.orientation.w)[2]:+.2f}°"
        )
        if self._imu_panel_values:
            roll, pitch, yaw = quaternion_to_euler_degrees(
                message.orientation.x, message.orientation.y,
                message.orientation.z, message.orientation.w
            )
            self._imu_panel_values["roll"].set(f"{roll:+.3f} °")
            self._imu_panel_values["pitch"].set(f"{pitch:+.3f} °")
            self._imu_panel_values["yaw"].set(f"{yaw:+.3f} °")
            self._imu_panel_status.set(f"在线 | 坐标系: {message.header.frame_id or '--'}")
            self._imu_panel_age.set(f"接收时间: {time.strftime('%H:%M:%S')} | 延迟: {(time.monotonic() - received_at) * 1000:.0f} ms")
        if self._imu_dialog is not None and self._imu_dialog.winfo_exists():
            self._imu_dialog.update_message(message, received_at)

    def update_profile(self, message: LaserProfile) -> None:
        points = decode_profile(
            message,
            self._profile_horizontal_axis.get(),
            self._profile_vertical_axis.get(),
        )
        self._last_profile = (message, points)
        self.laser_var.set(
            f"激光：有效点 {len(points)} | 编码器计数 {int(message.encoder_ticks)} | "
            f"坐标系 {message.header.frame_id or '--'}"
        )
        self._profile_info.set(
            f"有效点 {len(points)} | 编码器计数 {int(message.encoder_ticks)} | "
            f"坐标系 {message.header.frame_id or '--'}"
        )
        self._draw_profile(points)
        if self._point_cloud_dialog is not None and self._point_cloud_dialog.winfo_exists():
            self._point_cloud_dialog.update_message(message, points)

    def update_joint_state(self, message: JointState) -> None:
        for index, name in enumerate(message.name):
            position = float(message.position[index]) if index < len(message.position) else 0.0
            velocity = float(message.velocity[index]) if index < len(message.velocity) else 0.0
            effort = float(message.effort[index]) if index < len(message.effort) else 0.0
            self._joint_values[name] = (position, velocity, effort)
        left = self._joint_values.get("left_drive_wheel_joint")
        right = self._joint_values.get("right_drive_wheel_joint")
        if left is None and right is None:
            return
        def format_wheel(value: tuple[float, float, float] | None) -> str:
            return "--" if value is None else f"v={value[1]:+.3f} m/s p={value[0]:+.3f} rad T={value[2]:+.2f} A"
        self.motor_var.set(f"电机状态: 左轮 {format_wheel(left)} | 右轮 {format_wheel(right)}")

    def update_drive_command_status(self, message: DriveCommandStatus) -> None:
        state = "实际下发" if message.drive_enabled and message.command_fresh else "底盘停止"
        self.command_var.set(
            f"{state}: 线速度 {drive_value_to_display(message.applied_linear_m_s, 0):+.1f} mm/s | "
            f"角速度 {drive_value_to_display(message.applied_angular_rad_s, 1):+.1f} deg/s | "
            f"左/右轮 {drive_value_to_display(message.left_wheel_m_s, 4):+.1f}/"
            f"{drive_value_to_display(message.right_wheel_m_s, 4):+.1f} mm/s"
        )

    def update_correction_status(self, message: LaserCorrectionStatus) -> None:
        self.auto_var.set("自动纠偏行走：运行中" if message.active else "自动纠偏行走：已停止")
        self.correction_var.set(
            f"纠偏状态: 轮廓 {'有效' if message.contour_valid else '无效'} | "
            f"几何 {'有效' if message.geometry_valid else '建立中'} | "
            f"误差 {message.lateral_error_m:+.4f} m | "
            f"线速度 {message.linear_command_m_s:+.3f} m/s | "
            f"角速度 {message.angular_command_rad_s:+.3f} rad/s"
        )

    def _draw_profile(self, points: list[tuple[float, float]]) -> None:
        canvas = self._profile_canvas
        if canvas is None:
            return
        canvas.delete("all")
        width = max(100, canvas.winfo_width())
        height = max(100, canvas.winfo_height())
        left, top, right, bottom = 42, 12, 12, 24
        if not points:
            canvas.create_text(width / 2, height / 2, text="等待有效点云", fill="#91a7b5")
            return
        xs, ys = zip(*points)
        x_min, x_max = min(xs), max(xs)
        y_min, y_max = min(ys), max(ys)
        if x_max - x_min < 1e-6:
            x_min, x_max = x_min - 0.5, x_max + 0.5
        if y_max - y_min < 1e-6:
            y_min, y_max = y_min - 0.5, y_max + 0.5
        plot_w = max(1, width - left - right)
        plot_h = max(1, height - top - bottom)
        sx = lambda value: left + (value - x_min) / (x_max - x_min) * plot_w
        sy = lambda value: top + (y_max - value) / (y_max - y_min) * plot_h
        canvas.create_rectangle(left, top, width - right, height - bottom, outline="#30414d")
        for first, second in zip(points, points[1:]):
            canvas.create_line(sx(first[0]), sy(first[1]), sx(second[0]), sy(second[1]), fill="#4ed7e8")

    def _spin_ros(self) -> None:
        if self._closing:
            return
        if self._node is not None and rclpy.ok():
            rclpy.spin_once(self._node, timeout_sec=0.0)
        self.after(20, self._spin_ros)

    def _press(self, key: str) -> None:
        if key == "stop":
            self._pressed.clear()
            self._publish_zero()
            return
        self._pressed.add(key)
        self._publish_pressed_command()

    def _release(self, key: str) -> None:
        self._pressed.discard(key)
        self._publish_pressed_command()

    def _on_key_down(self, event: tk.Event) -> None:
        if isinstance(event.widget, (tk.Entry, tk.Spinbox, ttk.Entry, ttk.Spinbox, ttk.Combobox)):
            return None
        mapping = {
            "Up": "up",
            "Down": "down",
            "Left": "left",
            "Right": "right",
            "w": "up",
            "W": "up",
            "s": "down",
            "S": "down",
            "a": "left",
            "A": "left",
            "d": "right",
            "D": "right",
            "space": "stop",
        }
        key = mapping.get(event.keysym)
        if key:
            self._press(key)
            return "break"
        return None

    def _on_key_up(self, event: tk.Event) -> None:
        if isinstance(event.widget, (tk.Entry, tk.Spinbox, ttk.Entry, ttk.Spinbox, ttk.Combobox)):
            return None
        mapping = {
            "Up": "up",
            "Down": "down",
            "Left": "left",
            "Right": "right",
            "w": "up",
            "W": "up",
            "s": "down",
            "S": "down",
            "a": "left",
            "A": "left",
            "d": "right",
            "D": "right",
        }
        key = mapping.get(event.keysym)
        if key:
            self._release(key)
            return "break"
        return None

    def _apply_drive_limits(self, automatic: bool = False) -> bool:
        if self._node is None:
            self.limits_var.set("ROS 节点尚未就绪")
            return False
        try:
            display_values = tuple(
                float(variable.get())
                for variable in (
                    self.linear_speed_var,
                    self.angular_speed_var,
                    self.linear_accel_var,
                    self.angular_accel_var,
                    self.wheel_speed_var,
                    self.inner_wheel_ratio_var,
                )
            )
        except ValueError:
            self.limits_var.set("速度和加速度必须是有效数字")
            return False
        values = tuple(
            drive_value_to_si(value, index) for index, value in enumerate(display_values)
        )
        for value, (_key, lower, upper, _step) in zip(values, DRIVE_LIMIT_FIELDS):
            if not math.isfinite(value) or not lower <= value <= upper:
                self.limits_var.set(
                    "参数超出安全范围：线速度 1--300 mm/s，角速度 0.6--114.6 deg/s，"
                    "车轮上限最大 1000 mm/s"
                )
                return False
        success, message = self._node.apply_drive_limits(*values)
        if not success:
            self.limits_var.set(f"未应用：{message}")
            return False
        self._pressed.clear()
        self._publish_zero()
        prefix = "已自动恢复并应用" if automatic else "参数已应用"
        self.limits_var.set(
            f"{prefix}：线速度 {display_values[0]:.1f} mm/s，角速度 {display_values[1]:.1f} deg/s，"
            f"车轮上限 {display_values[4]:.1f} mm/s"
        )
        self._node.get_logger().info(
            f"Monitor {'automatically restored' if automatic else 'applied'} drive limits: "
            f"linear={display_values[0]:.1f} mm/s angular={display_values[1]:.1f} deg/s "
            f"linear_accel={display_values[2]:.1f} mm/s^2 "
            f"angular_accel={display_values[3]:.1f} deg/s^2 "
            f"wheel_cap={display_values[4]:.1f} mm/s ratio={display_values[5]:.3f}"
        )
        # A manual apply is authoritative. Do not let the startup restore
        # check submit the same request again and overwrite the command state.
        self._restore_limits_pending = False
        self._save_settings()
        return True

    def _validate_drive_input(self, proposed: str, reason: str, widget_name: str) -> bool:
        if proposed in ("", "."):
            return reason != "focusout"
        try:
            value = float(proposed)
        except ValueError:
            return False
        widget = self.nametowidget(widget_name)
        lower = float(widget.cget("from"))
        upper = float(widget.cget("to"))
        if reason == "focusout":
            return math.isfinite(value) and lower <= value <= upper
        return math.isfinite(value) and 0.0 <= value <= upper

    def _normalize_drive_input(self, event: tk.Event) -> None:
        spinbox = event.widget
        try:
            value = float(spinbox.get())
        except ValueError:
            value = float(spinbox.cget("from"))
        value = min(float(spinbox.cget("to")), max(float(spinbox.cget("from")), value))
        spinbox.delete(0, "end")
        spinbox.insert(0, f"{value:.3f}")

    def _mark_drive_limits_pending(self, *_args: Any) -> None:
        if hasattr(self, "limits_var"):
            if self._restoring_settings:
                return
            self._restore_limits_pending = False
            self.limits_var.set("参数已修改，点击“应用运动参数”后生效")
            self._schedule_settings_save()

    def _apply_motor_mapping(self, automatic: bool = False) -> None:
        if self._node is None:
            self.mapping_var.set("ROS 节点尚未就绪")
            return
        try:
            left_id = int(self.left_motor_id_var.get())
            right_id = int(self.right_motor_id_var.get())
            left_sign = int(self.left_motor_sign_var.get())
            right_sign = int(self.right_motor_sign_var.get())
        except ValueError:
            self.mapping_var.set("节点 ID 或方向无效")
            return
        if left_id == right_id or left_id not in range(1, 33) or right_id not in range(1, 33):
            self.mapping_var.set("两个电机节点必须不同且在 1--32 范围内")
            return
        success, message = self._node.apply_motor_mapping(left_id, right_id, left_sign, right_sign)
        self.mapping_var.set(
            "映射已应用；方向符号仅校正电机安装朝向" if success else f"未应用：{message}"
        )
        if success:
            self._restore_mapping_pending = False
            self._drive_enabled = False
            self.drive_var.set("电机映射已更新，底盘已禁用，请重新使能")
            self._node.get_logger().info(
                f"Monitor applied motor mapping: left=id{left_id} sign={left_sign:+d} "
                f"right=id{right_id} sign={right_sign:+d}"
            )
            self._save_settings()
        elif automatic:
            self.mapping_var.set(f"已保存的电机映射未应用：{message}")

    def _control_tick(self) -> None:
        if self._closing:
            return
        # Keep a nonzero command alive while a key is held. Once released,
        # _release sends one immediate zero and this loop stays silent; that
        # prevents idle stop frames from flooding the CAN bus.
        if self._pressed:
            self._publish_pressed_command()
        self.after(20, self._control_tick)

    def _publish_pressed_command(self) -> None:
        linear = 0.0
        angular = 0.0
        forward = "up" in self._pressed
        reverse = "down" in self._pressed
        turning = "left" in self._pressed or "right" in self._pressed
        # Use the values currently shown in the controls. The node values are
        # the last successfully applied limits and can intentionally lag while
        # the operator edits a pending profile.
        speed = self._node.max_linear_speed if self._node is not None else 0.0
        angular_speed = self._node.max_angular_speed if self._node is not None else 0.0
        try:
            speed = drive_value_to_si(float(self.linear_speed_var.get()), 0)
            angular_speed = drive_value_to_si(float(self.angular_speed_var.get()), 1)
            if not math.isfinite(speed) or speed < 0.0:
                raise ValueError
            if not math.isfinite(angular_speed) or angular_speed < 0.0:
                raise ValueError
        except (TypeError, ValueError):
            pass
        if forward and not reverse:
            linear += speed
        elif reverse and not forward:
            linear -= speed
        elif turning:
            # Turn buttons command a forward arc; the driver supplies the
            # left/right speed difference from angular velocity.
            linear += speed
        if "left" in self._pressed:
            angular += angular_speed
        if "right" in self._pressed:
            angular -= angular_speed
        if self._node is not None:
            self._node.publish_command(linear, angular)
        self._current_linear = linear
        self._current_angular = angular
        self.command_var.set(
            f"目标请求: 线速度 {drive_value_to_display(linear, 0):+.1f} mm/s | "
            f"角速度 {drive_value_to_display(angular, 1):+.1f} deg/s；等待底盘确认"
        )

    def _publish_zero(self) -> None:
        if self._node is not None:
            self._node.publish_command(0.0, 0.0)
        self._current_linear = 0.0
        self._current_angular = 0.0
        self.command_var.set("目标请求: 停止；等待底盘确认")

    def _call_drive_service(self, enabled: bool) -> bool:
        if self._node is None:
            return False

        client = self._node._enable_client
        if not client.wait_for_service(timeout_sec=SERVICE_WAIT_SECONDS):
            self.status_var.set("底盘服务未就绪，正在等待 /drive/enable；请确认 ROS 和 CAN 节点已启动。")
            return False
        request = SetBool.Request()
        request.data = enabled
        future = client.call_async(request)
        deadline = time.monotonic() + SERVICE_WAIT_SECONDS
        while not future.done() and time.monotonic() < deadline and rclpy.ok():
            rclpy.spin_once(self._node, timeout_sec=0.05)
        if not future.done():
            self.status_var.set("底盘使能请求超时，ROS 服务仍未响应。")
            return False
        response = future.result()
        if response is None or not response.success:
            self.status_var.set(str(response.message if response else "底盘服务调用失败。"))
            return False
        self._drive_enabled = enabled
        self.drive_var.set("底盘已使能，可用方向键控制" if enabled else "底盘已禁用")
        return True

    def _enable_drive(self) -> None:
        self._call_drive_service(True)

    def _disable_drive(self) -> None:
        self._pressed.clear()
        self._publish_zero()
        self._call_drive_service(False)

    def _start_auto_walk(self) -> None:
        if self._node is None:
            self.auto_var.set("ROS 节点尚未就绪")
            return
        self._pressed.clear()
        self._publish_zero()
        if not self._drive_enabled and not self._call_drive_service(True):
            self.auto_var.set("自动纠偏未启动：底盘使能失败")
            return
        success, message = self._node.set_auto_enabled(True)
        if success:
            self.auto_var.set("自动纠偏行走：运行中")
            self.status_var.set("自动纠偏已启动，等待有效轮廓")
        else:
            self.auto_var.set(f"自动纠偏未启动：{message}")
            self._call_drive_service(False)

    def _stop_auto_walk(self) -> None:
        self._pressed.clear()
        self._publish_zero()
        if self._node is None:
            return
        success, message = self._node.set_auto_enabled(False)
        self.auto_var.set("自动纠偏行走：已停止" if success else f"停止纠偏失败：{message}")
        self._call_drive_service(False)

    @staticmethod
    def _stamp_seconds(stamp: Any) -> float:
        return float(stamp.sec) + float(stamp.nanosec) * 1e-9

    def _capture_localization(self) -> None:
        if self._node is None:
            self.status_var.set("ROS 节点尚未就绪，无法定位感知")
            return
        correction = self._node.last_correction_status
        contour_lateral = float(correction.contour_lateral_m) if correction else 0.0
        heading_reference = float(correction.heading_error_rad) if correction else 0.0
        reference_ok, reference_message = self._node.set_localization_reference(
            contour_lateral, heading_reference
        )
        imu = self._last_imu[0] if self._last_imu else None
        profile = self._last_profile[0] if self._last_profile else None
        joints = self._node.last_joint_state
        def stamp_dict(header: Any) -> dict[str, Any]:
            return {"sec": int(header.stamp.sec), "nanosec": int(header.stamp.nanosec),
                    "frame_id": str(header.frame_id)}
        payload: dict[str, Any] = {
            "captured_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "ros": {"domain_id": os.environ.get("ROS_DOMAIN_ID", "0"),
                    "localhost_only": os.environ.get("ROS_LOCALHOST_ONLY", ""),
                    "automatic_discovery_range": os.environ.get("ROS_AUTOMATIC_DISCOVERY_RANGE", "")},
            "can": {"serial_port": self._node.can_serial_port,
                    "bitrate": self._node.can_bitrate},
            "drive": {
                "enabled": bool(self._drive_enabled),
                "command": {"linear_x_m_s": self._current_linear,
                             "angular_z_rad_s": self._current_angular},
                "limits": {"max_linear_speed_m_s": self._node.max_linear_speed,
                           "max_angular_speed_rad_s": self._node.max_angular_speed,
                           "max_linear_accel_m_s2": self._node.max_linear_accel,
                "max_angular_accel_rad_s2": self._node.max_angular_accel,
                "max_wheel_speed_m_s": self._node.max_wheel_speed,
                "minimum_inner_wheel_ratio": self._node.minimum_inner_wheel_ratio},
                "motor_mapping": {"left_id": self._node.left_motor_id,
                                   "right_id": self._node.right_motor_id,
                                   "left_sign": self._node.left_motor_sign,
                                   "right_sign": self._node.right_motor_sign},
            },
            "localization_reference": {"contour_lateral_m": contour_lateral,
                                        "heading_reference_rad": heading_reference,
                                        "service_success": reference_ok,
                                        "service_message": reference_message},
        }
        if imu is not None:
            roll, pitch, yaw = quaternion_to_euler_degrees(
                imu.orientation.x, imu.orientation.y, imu.orientation.z, imu.orientation.w
            )
            payload["imu"] = {"header": stamp_dict(imu.header),
                              "orientation": {"x": imu.orientation.x, "y": imu.orientation.y,
                                               "z": imu.orientation.z, "w": imu.orientation.w},
                              "rpy_deg": {"roll": roll, "pitch": pitch, "yaw": yaw},
                              "angular_velocity": {"x": imu.angular_velocity.x, "y": imu.angular_velocity.y, "z": imu.angular_velocity.z},
                              "linear_acceleration": {"x": imu.linear_acceleration.x, "y": imu.linear_acceleration.y, "z": imu.linear_acceleration.z}}
        if profile is not None:
            payload["laser_profile"] = {"header": stamp_dict(profile.header),
                                         "encoder_ticks": int(profile.encoder_ticks),
                                         "sampled_points": [[float(x), float(y)] for x, y in self._last_profile[1][::max(1, len(self._last_profile[1]) // 300)]]}
        if joints is not None:
            payload["drive_joint_state"] = {"header": stamp_dict(joints.header),
                                             "name": list(joints.name),
                                             "position": [float(v) for v in joints.position],
                                             "velocity": [float(v) for v in joints.velocity],
                                             "effort": [float(v) for v in joints.effort]}
        if correction is not None:
            payload["correction_status"] = {key: getattr(correction, key) for key in (
                "active", "contour_valid", "geometry_valid", "lateral_error_m",
                "preview_lateral_error_m", "heading_error_rad", "curvature_1pm",
                "angular_command_rad_s", "angular_accel_rad_s2", "linear_command_m_s",
                "contour_lateral_m", "confidence", "fit_residual_m", "trajectory_points")}
        try:
            path = append_operator_log("定位感知记录", payload)
            self.status_var.set(f"定位感知已写入统一日志：{path.name}")
        except OSError as error:
            self.status_var.set(f"定位感知写入失败：{error}")

    def _close(self) -> None:
        if self._closing:
            return
        self._closing = True
        self._pressed.clear()
        self._publish_zero()
        if self._node is not None:
            self._node.set_auto_enabled(False)
        if self._node is not None and self._drive_enabled:
            self._call_drive_service(False)
        self._save_settings()
        self.destroy()


def main() -> None:
    rclpy.init()
    app = MonitorApp()
    node = MonitorNode(app)
    app.attach_node(node)
    try:
        app.mainloop()
    finally:
        if rclpy.ok():
            node.publish_command(0.0, 0.0)
            node.destroy_node()
            rclpy.shutdown()
