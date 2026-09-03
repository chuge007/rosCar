"""Tk monitor, IMU attitude popup, laser profile view, and keyboard control."""

from __future__ import annotations

import math
import struct
import time
import tkinter as tk
from tkinter import messagebox, ttk
from typing import Any

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Imu

from crawling_robot_interfaces.msg import LaserProfile


DEGREES_PER_RADIAN = 180.0 / math.pi
STANDARD_GRAVITY_M_S2 = 9.80665
MAX_POINTS_TO_DRAW = 2400


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

        self._cmd_publisher = self.create_publisher(Twist, cmd_vel_topic, 20)
        self.create_subscription(Imu, imu_topic, self._on_imu, qos_profile_sensor_data)
        self.create_subscription(
            LaserProfile,
            profile_topic,
            self._on_profile,
            qos_profile_sensor_data,
        )

    def _on_imu(self, message: Imu) -> None:
        self._app.update_imu(message)

    def _on_profile(self, message: LaserProfile) -> None:
        self._app.update_profile(message)

    def publish_command(self, linear: float, angular: float) -> None:
        message = Twist()
        message.linear.x = float(linear)
        message.angular.z = float(angular)
        self._cmd_publisher.publish(message)


class ImuDialog(tk.Toplevel):
    def __init__(self, parent: "MonitorApp") -> None:
        super().__init__(parent)
        self.title("陀螺仪 / IMU 姿态")
        self.geometry("430x430")
        self.minsize(390, 360)
        self.configure(bg="#101820")
        self.protocol("WM_DELETE_WINDOW", self.withdraw)

        self.status_var = tk.StringVar(value="等待 /imu/data ...")
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
            (("roll", "横滚 Roll"), ("pitch", "俯仰 Pitch"), ("yaw", "航向 Yaw"))
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
        self.status_var.set(f"在线 | frame_id: {message.header.frame_id or '--'}")
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
        self.info_var = tk.StringVar(value="等待 /laser_profile/frame ...")
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
            f"有效点 {len(points)} | encoder_ticks {int(message.encoder_ticks)} | "
            f"范围 {x_min:.3f}..{x_max:.3f} m, {y_min:.3f}..{y_max:.3f} m"
        )


class MonitorApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("爬壁机器人监控与手动控制")
        self.geometry("560x430")
        self.minsize(500, 380)
        self.configure(bg="#101820")

        self._node: MonitorNode | None = None
        self._pressed: set[str] = set()
        self._last_imu: tuple[Imu, float] | None = None
        self._last_profile: tuple[LaserProfile, list[tuple[float, float]]] | None = None
        self._closing = False
        self._drive_enabled = False
        self._imu_dialog: ImuDialog | None = None
        self._point_cloud_dialog: PointCloudDialog | None = None
        self.status_var = tk.StringVar(value="ROS 已启动，等待传感器数据...")
        self.drive_var = tk.StringVar(value="底盘未使能")
        self.imu_var = tk.StringVar(value="IMU: 等待数据")
        self.laser_var = tk.StringVar(value="激光: 等待点云")
        self.command_var = tk.StringVar(value="当前指令: 线速度 0.000 m/s | 角速度 0.000 rad/s")

        self._build_ui()
        # A monitor launched from CMD can otherwise open behind that console.
        self.after_idle(self._bring_to_front)
        self.bind_all("<KeyPress>", self._on_key_down, add="+")
        self.bind_all("<KeyRelease>", self._on_key_up, add="+")
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.after(100, self._spin_ros)
        self.after(50, self._control_tick)
        self.after(500, self._open_sensor_windows)

    def _bring_to_front(self) -> None:
        self.deiconify()
        self.lift()
        self.attributes("-topmost", True)
        self.after(800, lambda: self.attributes("-topmost", False))
        self.focus_force()

    def attach_node(self, node: MonitorNode) -> None:
        self._node = node

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("Root.TFrame", background="#101820")
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
        style.configure("Accent.TButton", background="#1e7f9b", foreground="#ffffff", padding=8)
        style.configure("Danger.TButton", background="#b84444", foreground="#ffffff", padding=8)

        root = ttk.Frame(self, padding=18, style="Root.TFrame")
        root.pack(fill="both", expand=True)
        ttk.Label(root, text="爬壁机器人监控与手动控制", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            root,
            text="启动 ROS 后默认打开 IMU 姿态和激光点云窗口",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(3, 14))

        ttk.Label(root, textvariable=self.status_var, style="Info.TLabel").pack(
            fill="x", pady=(0, 10)
        )
        ttk.Label(root, textvariable=self.imu_var, style="Info.TLabel").pack(
            fill="x", pady=4
        )
        ttk.Label(root, textvariable=self.laser_var, style="Info.TLabel").pack(
            fill="x", pady=4
        )
        ttk.Label(root, textvariable=self.command_var, style="Info.TLabel").pack(
            fill="x", pady=4
        )

        controls = ttk.Frame(root, style="Root.TFrame")
        controls.pack(fill="x", pady=(18, 10))
        ttk.Button(
            controls,
            text="底盘使能",
            style="Accent.TButton",
            command=self._enable_drive,
        ).pack(side="left", padx=(0, 8))
        ttk.Button(
            controls,
            text="底盘禁用",
            command=self._disable_drive,
        ).pack(side="left", padx=(0, 8))
        ttk.Button(
            controls,
            text="IMU 姿态弹窗",
            command=self._show_imu,
        ).pack(side="left", padx=(0, 8))
        ttk.Button(
            controls,
            text="激光点云",
            command=self._show_point_cloud,
        ).pack(side="left")
        ttk.Label(root, textvariable=self.drive_var, style="Muted.TLabel").pack(
            anchor="w", pady=(4, 8)
        )

        pad = ttk.Frame(root, style="Root.TFrame")
        pad.pack(pady=4)
        self._make_drive_button(pad, "前进\n↑", "up", 0, 1)
        self._make_drive_button(pad, "左转\n←", "left", 1, 0)
        self._make_drive_button(pad, "停止\n空格", "stop", 1, 1, danger=True)
        self._make_drive_button(pad, "右转\n→", "right", 1, 2)
        self._make_drive_button(pad, "后退\n↓", "down", 2, 1)
        ttk.Label(
            root,
            text="方向键：前后左右移动/转向；松开按键立即发零速。W/A/S/D 同样可用。",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(14, 0))

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

    def _open_sensor_windows(self) -> None:
        self._show_imu()
        self._show_point_cloud()

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
            f"IMU: 姿态 Roll {quaternion_to_euler_degrees(message.orientation.x, message.orientation.y, message.orientation.z, message.orientation.w)[0]:+.2f}° | "
            f"Pitch {quaternion_to_euler_degrees(message.orientation.x, message.orientation.y, message.orientation.z, message.orientation.w)[1]:+.2f}° | "
            f"Yaw {quaternion_to_euler_degrees(message.orientation.x, message.orientation.y, message.orientation.z, message.orientation.w)[2]:+.2f}°"
        )
        if self._imu_dialog is not None and self._imu_dialog.winfo_exists():
            self._imu_dialog.update_message(message, received_at)

    def update_profile(self, message: LaserProfile) -> None:
        points = decode_profile(
            message,
            self._point_cloud_dialog.horizontal_axis_var.get()
            if self._point_cloud_dialog is not None and self._point_cloud_dialog.winfo_exists()
            else "y",
            self._point_cloud_dialog.vertical_axis_var.get()
            if self._point_cloud_dialog is not None and self._point_cloud_dialog.winfo_exists()
            else "z",
        )
        self._last_profile = (message, points)
        self.laser_var.set(
            f"激光: 有效点 {len(points)} | 编码器 ticks {int(message.encoder_ticks)} | "
            f"frame {message.header.frame_id or '--'}"
        )
        if self._point_cloud_dialog is not None and self._point_cloud_dialog.winfo_exists():
            self._point_cloud_dialog.update_message(message, points)

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

    def _release(self, key: str) -> None:
        self._pressed.discard(key)
        if not self._pressed:
            self._publish_zero()

    def _on_key_down(self, event: tk.Event) -> None:
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

    def _control_tick(self) -> None:
        if self._closing:
            return
        linear = 0.0
        angular = 0.0
        if "up" in self._pressed:
            linear += self._node.max_linear_speed if self._node is not None else 0.0
        if "down" in self._pressed:
            linear -= self._node.max_linear_speed if self._node is not None else 0.0
        if "left" in self._pressed:
            angular += self._node.max_angular_speed if self._node is not None else 0.0
        if "right" in self._pressed:
            angular -= self._node.max_angular_speed if self._node is not None else 0.0
        if self._node is not None:
            self._node.publish_command(linear, angular)
        self.command_var.set(
            f"当前指令: 线速度 {linear:+.3f} m/s | 角速度 {angular:+.3f} rad/s"
        )
        self.after(50, self._control_tick)

    def _publish_zero(self) -> None:
        if self._node is not None:
            self._node.publish_command(0.0, 0.0)
        self.command_var.set("当前指令: 线速度 +0.000 m/s | 角速度 +0.000 rad/s")

    def _call_drive_service(self, enabled: bool) -> None:
        if self._node is None:
            return
        from std_srvs.srv import SetBool

        client = self._node.create_client(SetBool, "/drive/enable")
        if not client.wait_for_service(timeout_sec=0.5):
            messagebox.showerror("底盘服务", "未发现 /drive/enable 服务，请确认 ROS 已启动。")
            return
        request = SetBool.Request()
        request.data = enabled
        future = client.call_async(request)
        deadline = time.monotonic() + 1.0
        while not future.done() and time.monotonic() < deadline and rclpy.ok():
            rclpy.spin_once(self._node, timeout_sec=0.05)
        if not future.done():
            messagebox.showerror("底盘服务", "调用 /drive/enable 超时。")
            return
        response = future.result()
        if response is None or not response.success:
            messagebox.showerror("底盘服务", str(response.message if response else "调用失败"))
            return
        self._drive_enabled = enabled
        self.drive_var.set("底盘已使能，可用方向键控制" if enabled else "底盘已禁用")

    def _enable_drive(self) -> None:
        self._call_drive_service(True)

    def _disable_drive(self) -> None:
        self._pressed.clear()
        self._publish_zero()
        self._call_drive_service(False)

    def _close(self) -> None:
        if self._closing:
            return
        self._closing = True
        self._pressed.clear()
        self._publish_zero()
        if self._node is not None and self._drive_enabled:
            self._call_drive_service(False)
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
