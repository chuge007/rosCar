"""Desktop control panel for the crawling robot.

The panel deliberately uses only Python's standard library. It talks to the
ROS 2 development board through ros2 CLI subprocesses, so it can run on a
Windows operator laptop without installing rclpy or rebuilding the workspace.
"""

from __future__ import annotations

import queue
import subprocess
import struct
import threading
import time
import tkinter as tk
from dataclasses import dataclass
from tkinter import messagebox, ttk


@dataclass
class DriveCommand:
    linear: float = 0.0
    angular: float = 0.0


class Ros2Bridge:
    def __init__(self, domain_id: str, status_queue: queue.Queue[str], profile_queue: queue.Queue[object]) -> None:
        self.domain_id = domain_id
        self.status_queue = status_queue
        self.profile_queue = profile_queue
        self.native = False
        self._rclpy = None
        self._node = None
        self._publisher = None
        self._status_type = None
        self._clients: dict[str, object] = {}
        self._try_native_ros()

    def _try_native_ros(self) -> None:
        """Use rclpy when the ROS 2 Python environment is available.

        This keeps command publication in-process at 10 Hz. The CLI fallback
        remains useful on a plain operator laptop, though it has more startup
        overhead per command.
        """
        try:
            import os
            import rclpy
            from crawling_robot_interfaces.msg import LaserCorrectionStatus
            from crawling_robot_interfaces.msg import LaserProfile
            from geometry_msgs.msg import Twist

            if self.domain_id.strip():
                os.environ["ROS_DOMAIN_ID"] = self.domain_id.strip()
            if not rclpy.ok():
                rclpy.init(args=None)
            self._rclpy = rclpy
            self._node = rclpy.create_node("robot_control_panel")
            self._publisher = self._node.create_publisher(Twist, "/cmd_vel", 20)
            self._status_type = LaserCorrectionStatus
            self._node.create_subscription(
                LaserCorrectionStatus,
                "/laser_correction/status",
                lambda message: self.status_queue.put(message),
                20,
            )
            self._node.create_subscription(
                LaserProfile,
                "/laser_profile/frame",
                self._put_latest_profile,
                5,
            )
            self._twist_type = Twist
            self.native = True
        except (ImportError, ModuleNotFoundError, RuntimeError):
            self.native = False

    def _put_latest_profile(self, message: object) -> None:
        try:
            self.profile_queue.put_nowait(message)
        except queue.Full:
            try:
                self.profile_queue.get_nowait()
            except queue.Empty:
                pass
            try:
                self.profile_queue.put_nowait(message)
            except queue.Full:
                pass

    def spin_once(self) -> None:
        if self.native and self._rclpy and self._node:
            self._rclpy.spin_once(self._node, timeout_sec=0.0)

    def shutdown(self) -> None:
        if self.native and self._rclpy:
            if self._node:
                self._node.destroy_node()
            if self._rclpy.ok():
                self._rclpy.shutdown()
            self.native = False

    def _run(self, args: list[str], timeout: float = 2.0) -> tuple[bool, str]:
        env = None
        if self.domain_id.strip():
            import os

            env = os.environ.copy()
            env["ROS_DOMAIN_ID"] = self.domain_id.strip()
        try:
            result = subprocess.run(
                ["ros2", *args], capture_output=True, text=True, timeout=timeout, env=env
            )
            output = (result.stdout + result.stderr).strip()
            return result.returncode == 0, output
        except (OSError, subprocess.TimeoutExpired) as error:
            return False, str(error)

    def publish_twist(self, command: DriveCommand) -> None:
        if self.native:
            message = self._twist_type()
            message.linear.x = command.linear
            message.angular.z = command.angular
            self._publisher.publish(message)
            return
        payload = (
            "{linear: {x: %.5f, y: 0.0, z: 0.0}, "
            "angular: {x: 0.0, y: 0.0, z: %.5f}}" % (command.linear, command.angular)
        )
        self._run(["topic", "pub", "--once", "/cmd_vel", "geometry_msgs/msg/Twist", payload])

    def set_bool(self, service: str, enabled: bool) -> tuple[bool, str]:
        if self.native:
            from std_srvs.srv import SetBool

            client = self._clients.get(service)
            if client is None:
                client = self._node.create_client(SetBool, service)
                self._clients[service] = client
            if not client.wait_for_service(timeout_sec=0.5):
                return False, "服务未发现: %s" % service
            request = SetBool.Request()
            request.data = enabled
            future = client.call_async(request)
            deadline = time.monotonic() + 1.0
            while not future.done() and time.monotonic() < deadline:
                self._rclpy.spin_once(self._node, timeout_sec=0.05)
            if not future.done():
                return False, "服务调用超时: %s" % service
            result = future.result()
            return bool(result.success), str(result.message)
        return self._run(
            ["service", "call", service, "std_srvs/srv/SetBool", "{data: %s}" % str(enabled).lower()]
        )

    def trigger(self, service: str) -> tuple[bool, str]:
        if self.native:
            from std_srvs.srv import Trigger

            client = self._clients.get(service)
            if client is None:
                client = self._node.create_client(Trigger, service)
                self._clients[service] = client
            if not client.wait_for_service(timeout_sec=0.5):
                return False, "服务未发现: %s" % service
            future = client.call_async(Trigger.Request())
            deadline = time.monotonic() + 1.0
            while not future.done() and time.monotonic() < deadline:
                self._rclpy.spin_once(self._node, timeout_sec=0.05)
            if not future.done():
                return False, "服务调用超时: %s" % service
            result = future.result()
            return bool(result.success), str(result.message)
        return self._run(["service", "call", service, "std_srvs/srv/Trigger", "{}"]) 

    def read_status_once(self) -> None:
        if self.native:
            return
        ok, output = self._run(
            [
                "topic",
                "echo",
                "--once",
                "/laser_correction/status",
                "crawling_robot_interfaces/msg/LaserCorrectionStatus",
            ],
            timeout=1.2,
        )
        if ok and output:
            self.status_queue.put(output)


class ControlPanel(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("爬壁机器人控制台")
        self.geometry("850x560")
        self.minsize(760, 500)
        self.configure(bg="#111820")

        self.status_queue: queue.Queue[object] = queue.Queue()
        self.profile_queue: queue.Queue[object] = queue.Queue(maxsize=3)
        self.bridge = Ros2Bridge("", self.status_queue, self.profile_queue)
        self.command = DriveCommand()
        self.domain_var = tk.StringVar(value="")
        self.speed_var = tk.DoubleVar(value=0.025)
        self.turn_rate_var = tk.DoubleVar(value=0.30)
        self.manual_enabled = False
        self.auto_enabled = False
        self.estop_latched = False
        self._pressed: set[str] = set()
        self._speed_value = self.speed_var.get()
        self._turn_rate_value = self.turn_rate_var.get()
        self._state_lock = threading.Lock()
        self._last_status_query = 0.0
        self._stop_event = threading.Event()
        self._worker = threading.Thread(target=self._command_loop, daemon=True)
        self._worker.start()

        self.status_var = tk.StringVar(value="未连接")
        self.error_var = tk.StringVar(value="纠偏误差 --")
        self.preview_error_var = tk.StringVar(value="前视误差 --")
        self.heading_var = tk.StringVar(value="角度误差 --")
        self.curvature_var = tk.StringVar(value="曲率 --")
        self.angular_accel_var = tk.StringVar(value="角加速度 --")
        self.linear_command_var = tk.StringVar(value="线速度 --")
        self.angular_command_var = tk.StringVar(value="角速度 --")
        self.confidence_var = tk.StringVar(value="置信度 --")
        self.fit_var = tk.StringVar(value="轨迹拟合 --")
        self.auto_var = tk.StringVar(value="自动纠偏：关闭")
        self.contour_window: ContourWindow | None = None
        self.latest_status: object | None = None
        self._build_ui()
        self.bind_all("<KeyPress>", self._on_key_down)
        self.bind_all("<KeyRelease>", self._on_key_up)
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.after(250, self._poll_status)

    def _build_ui(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background="#111820")
        style.configure("TLabel", background="#111820", foreground="#e8eef2", font=("Segoe UI", 10))
        style.configure("Header.TLabel", font=("Segoe UI", 19, "bold"), foreground="#ffffff")
        style.configure("Muted.TLabel", foreground="#8ea0ad")
        style.configure("TButton", font=("Segoe UI", 10, "bold"), padding=8)
        style.configure("Danger.TButton", background="#c0392b", foreground="white")
        style.configure("Accent.TButton", background="#1976a8", foreground="white")

        root = ttk.Frame(self, padding=18)
        root.pack(fill="both", expand=True)
        ttk.Label(root, text="爬壁机器人控制台", style="Header.TLabel").pack(anchor="w")
        ttk.Label(root, text="开发板 ROS 2 控制接口 / 线激光焊缝纠偏", style="Muted.TLabel").pack(anchor="w", pady=(2, 16))

        connection = ttk.Frame(root)
        connection.pack(fill="x", pady=(0, 12))
        ttk.Label(connection, text="ROS_DOMAIN_ID").pack(side="left")
        domain_entry = ttk.Entry(connection, width=10, textvariable=self.domain_var)
        domain_entry.pack(side="left", padx=(8, 14))
        ttk.Button(connection, text="应用网络设置", command=self._apply_domain).pack(side="left")
        ttk.Label(connection, textvariable=self.status_var, style="Muted.TLabel").pack(side="right")

        body = ttk.Frame(root)
        body.pack(fill="both", expand=True)
        left = ttk.Frame(body)
        left.pack(side="left", fill="both", expand=True, padx=(0, 16))
        right = ttk.Frame(body)
        right.pack(side="right", fill="y")

        ttk.Label(left, text="手动驾驶", style="Header.TLabel").pack(anchor="w")
        ttk.Label(left, text="W/A/S/D 或方向键；松开按键即减速停车", style="Muted.TLabel").pack(anchor="w", pady=(2, 10))
        pad = ttk.Frame(left)
        pad.pack(pady=8)
        self._drive_button(pad, "前进", "up", 1, 0)
        self._drive_button(pad, "左转", "left", 2, 0)
        self._drive_button(pad, "停止", "stop", 1, 1, accent=True)
        self._drive_button(pad, "右转", "right", 2, 1)
        self._drive_button(pad, "后退", "down", 1, 2)

        speed_frame = ttk.Frame(left)
        speed_frame.pack(fill="x", pady=(18, 8))
        ttk.Label(speed_frame, text="基础速度").pack(anchor="w")
        ttk.Scale(speed_frame, from_=0.005, to=0.12, variable=self.speed_var, orient="horizontal",
                  command=self._on_speed_change).pack(fill="x", pady=6)
        ttk.Label(speed_frame, textvariable=tk.StringVar(value="拖动滑块设置 m/s；自动纠偏会在误差大时自动降速"), style="Muted.TLabel").pack(anchor="w")
        ttk.Label(speed_frame, text="转向角速度").pack(anchor="w", pady=(10, 0))
        ttk.Scale(speed_frame, from_=0.05, to=0.80, variable=self.turn_rate_var, orient="horizontal",
                  command=self._on_turn_rate_change).pack(fill="x", pady=6)

        actions = ttk.Frame(left)
        actions.pack(fill="x", pady=(18, 0))
        ttk.Button(actions, text="底盘使能", style="Accent.TButton", command=self._toggle_drive).pack(side="left", padx=(0, 8))
        ttk.Button(actions, text="自动纠偏", command=self._toggle_auto).pack(side="left", padx=(0, 8))
        ttk.Button(actions, text="复位滤波", command=self._reset_correction).pack(side="left")
        ttk.Button(actions, text="打开轮廓成像", command=self._open_contour_window).pack(side="left", padx=(8, 0))

        ttk.Label(right, text="状态", style="Header.TLabel").pack(anchor="w")
        status_box = tk.Frame(right, bg="#1d2933", padx=16, pady=14)
        status_box.pack(fill="x", pady=(10, 12))
        self._status_label(status_box, "模式", self.auto_var)
        self._status_label(status_box, "横向误差", self.error_var)
        self._status_label(status_box, "前视误差", self.preview_error_var)
        self._status_label(status_box, "角度误差", self.heading_var)
        self._status_label(status_box, "轨迹曲率", self.curvature_var)
        self._status_label(status_box, "角加速度", self.angular_accel_var)
        self._status_label(status_box, "实际线速度", self.linear_command_var)
        self._status_label(status_box, "实际角速度", self.angular_command_var)
        self._status_label(status_box, "轮廓置信度", self.confidence_var)
        self._status_label(status_box, "拟合残差", self.fit_var)
        ttk.Label(right, text="安全操作", style="Header.TLabel").pack(anchor="w", pady=(10, 0))
        ttk.Button(right, text="急停 / 清除速度", style="Danger.TButton", command=self._estop).pack(fill="x", pady=(10, 6))
        ttk.Button(right, text="解除急停", command=self._release_estop).pack(fill="x", pady=(0, 6))
        ttk.Label(right, text="急停会持续发布零速度。解除急停后需重新点击底盘使能。", style="Muted.TLabel", wraplength=220).pack(anchor="w")

    def _status_label(self, parent: tk.Widget, name: str, variable: tk.StringVar) -> None:
        row = tk.Frame(parent, bg="#1d2933")
        row.pack(fill="x", pady=3)
        tk.Label(row, text=name, bg="#1d2933", fg="#8ea0ad", width=10, anchor="w").pack(side="left")
        tk.Label(row, textvariable=variable, bg="#1d2933", fg="#ffffff", anchor="w").pack(side="left")

    def _drive_button(self, parent: ttk.Frame, text: str, key: str, row: int, column: int, accent: bool = False) -> None:
        button = ttk.Button(parent, text=text, style="Accent.TButton" if accent else "TButton", width=9)
        button.grid(row=row, column=column, padx=5, pady=5, ipadx=6, ipady=5)
        button.bind("<ButtonPress-1>", lambda _event, value=key: self._press(value))
        button.bind("<ButtonRelease-1>", lambda _event, value=key: self._release(value))

    def _apply_domain(self) -> None:
        domain_id = self.domain_var.get()
        if domain_id != self.bridge.domain_id:
            self.bridge.shutdown()
            self.bridge = Ros2Bridge(domain_id, self.status_queue, self.profile_queue)
        self.status_var.set("网络参数已应用")

    def _press(self, key: str) -> None:
        if key == "stop":
            with self._state_lock:
                self._pressed.clear()
            return
        with self._state_lock:
            self._pressed.add(key)

    def _release(self, key: str) -> None:
        with self._state_lock:
            self._pressed.discard(key)

    def _on_speed_change(self, value: str) -> None:
        self._speed_value = float(value)

    def _on_turn_rate_change(self, value: str) -> None:
        self._turn_rate_value = float(value)

    def _on_key_down(self, event: tk.Event) -> None:
        mapping = {"w": "up", "Up": "up", "s": "down", "Down": "down", "a": "left", "Left": "left", "d": "right", "Right": "right", "space": "stop"}
        key = mapping.get(event.keysym, mapping.get(event.keysym.lower(), ""))
        if key:
            self._press(key)

    def _on_key_up(self, event: tk.Event) -> None:
        mapping = {"w": "up", "Up": "up", "s": "down", "Down": "down", "a": "left", "Left": "left", "d": "right", "Right": "right"}
        key = mapping.get(event.keysym, mapping.get(event.keysym.lower(), ""))
        if key:
            self._release(key)

    def _toggle_drive(self) -> None:
        enabled = not self.manual_enabled
        ok, output = self.bridge.set_bool("/drive/enable", enabled)
        if ok:
            self.manual_enabled = enabled
            self.status_var.set("底盘已使能" if enabled else "底盘已禁用")
        else:
            messagebox.showerror("底盘服务失败", output or "无法调用 /drive/enable")

    def _toggle_auto(self) -> None:
        enabled = not self.auto_enabled
        ok, output = self.bridge.set_bool("/laser_correction/enable", enabled)
        if ok:
            self.auto_enabled = enabled
            self.auto_var.set("自动纠偏：开启" if enabled else "自动纠偏：关闭")
        else:
            messagebox.showerror("纠偏服务失败", output or "无法调用 /laser_correction/enable")

    def _reset_correction(self) -> None:
        ok, output = self.bridge.trigger("/laser_correction/reset")
        if not ok:
            messagebox.showerror("复位失败", output or "无法调用 /laser_correction/reset")

    def _estop(self) -> None:
        self.estop_latched = True
        self.manual_enabled = False
        self.auto_enabled = False
        self.auto_var.set("自动纠偏：关闭")
        with self._state_lock:
            self._pressed.clear()
        self.bridge.set_bool("/drive/enable", False)
        self.bridge.set_bool("/laser_correction/enable", False)
        self.status_var.set("急停已触发")

    def _release_estop(self) -> None:
        self.estop_latched = False
        self.status_var.set("急停已解除，请重新底盘使能")

    def _command_loop(self) -> None:
        while not self._stop_event.is_set():
            if self.estop_latched:
                command = DriveCommand()
            elif self.auto_enabled:
                # The board's follower owns /cmd_vel while auto mode is enabled.
                command = None
            else:
                linear = 0.0
                angular = 0.0
                with self._state_lock:
                    pressed = set(self._pressed)
                if "up" in pressed:
                    linear += self._speed_value
                if "down" in pressed:
                    linear -= self._speed_value
                if "left" in pressed:
                    angular += self._turn_rate_value
                if "right" in pressed:
                    angular -= self._turn_rate_value
                command = DriveCommand(linear, angular)
            if command is not None:
                self.bridge.publish_twist(command)
            now = time.monotonic()
            if now - self._last_status_query >= 1.0:
                self._last_status_query = now
                self.bridge.read_status_once()
            time.sleep(0.1)

    def _poll_status(self) -> None:
        self.bridge.spin_once()
        try:
            while True:
                status = self.bridge.status_queue.get_nowait()
                self.latest_status = status
                self._parse_status(status)
        except queue.Empty:
            pass
        if self.contour_window is not None:
            try:
                while True:
                    profile = self.profile_queue.get_nowait()
                    self.contour_window.show_profile(profile, self.latest_status)
            except queue.Empty:
                pass
        self.after(250, self._poll_status)

    def _parse_status(self, status: object) -> None:
        if hasattr(status, "contour_valid"):
            if hasattr(status, "active"):
                self.auto_enabled = bool(status.active)
                self.auto_var.set("自动纠偏：开启" if self.auto_enabled else "自动纠偏：关闭")
            if not status.contour_valid:
                self.status_var.set("等待有效轮廓")
            elif hasattr(status, "geometry_valid") and not status.geometry_valid:
                self.status_var.set("轮廓正常，正在建立轨迹")
            else:
                self.status_var.set("轨迹纠偏正常")
            self.error_var.set("%.5f m" % getattr(status, "lateral_error_m", 0.0))
            self.preview_error_var.set("%.5f m" % getattr(status, "preview_lateral_error_m", 0.0))
            self.heading_var.set("%.2f deg" % (getattr(status, "heading_error_rad", 0.0) * 57.2957795))
            self.curvature_var.set("%.3f 1/m" % getattr(status, "curvature_1pm", 0.0))
            self.angular_accel_var.set("%.3f rad/s²" % getattr(status, "angular_accel_rad_s2", 0.0))
            self.linear_command_var.set("%.4f m/s" % getattr(status, "linear_command_m_s", 0.0))
            self.angular_command_var.set("%.3f rad/s" % getattr(status, "angular_command_rad_s", 0.0))
            self.confidence_var.set("%.2f" % getattr(status, "confidence", 0.0))
            self.fit_var.set("%.4f m / %d 点" % (getattr(status, "fit_residual_m", 0.0), getattr(status, "trajectory_points", 0)))
            return
        text = str(status)
        # ros2 topic echo uses YAML-like output; normalize the few scalar fields
        # we display without depending on PyYAML.
        values: dict[str, str] = {}
        for line in text.splitlines():
            if ":" not in line:
                continue
            key, value = line.strip().split(":", 1)
            values[key.strip()] = value.strip()
        if "contour_valid" in values:
            contour_ok = values["contour_valid"].lower() == "true"
            geometry_ok = values.get("geometry_valid", "false").lower() == "true"
            self.status_var.set(
                "等待有效轮廓" if not contour_ok else
                "轨迹纠偏正常" if geometry_ok else "轮廓正常，正在建立轨迹"
            )
        if "active" in values:
            self.auto_enabled = values["active"].lower() == "true"
            self.auto_var.set("自动纠偏：开启" if self.auto_enabled else "自动纠偏：关闭")
        if "lateral_error_m" in values:
            self.error_var.set("%s m" % values["lateral_error_m"])
        if "confidence" in values:
            self.confidence_var.set(values["confidence"])
        if "heading_error_rad" in values:
            try:
                self.heading_var.set("%.2f deg" % (float(values["heading_error_rad"]) * 57.2957795))
            except ValueError:
                pass
        if "preview_lateral_error_m" in values:
            self.preview_error_var.set("%s m" % values["preview_lateral_error_m"])
        if "curvature_1pm" in values:
            self.curvature_var.set("%s 1/m" % values["curvature_1pm"])
        if "angular_accel_rad_s2" in values:
            self.angular_accel_var.set("%s rad/s²" % values["angular_accel_rad_s2"])
        if "linear_command_m_s" in values:
            self.linear_command_var.set("%s m/s" % values["linear_command_m_s"])
        if "angular_command_rad_s" in values:
            self.angular_command_var.set("%s rad/s" % values["angular_command_rad_s"])
        if "fit_residual_m" in values:
            points = values.get("trajectory_points", "--")
            self.fit_var.set("%s m / %s 点" % (values["fit_residual_m"], points))

    def _open_contour_window(self) -> None:
        if not self.bridge.native:
            messagebox.showinfo("轮廓成像", "实时轮廓窗口需要桌面端 ROS 2 Python 环境（rclpy）。当前仅检测到 ros2 CLI。")
            return
        if self.contour_window is None or not self.contour_window.winfo_exists():
            self.contour_window = ContourWindow(self)
        else:
            self.contour_window.deiconify()
            self.contour_window.lift()

    def _close(self) -> None:
        self._stop_event.set()
        self.bridge.publish_twist(DriveCommand())
        self.bridge.shutdown()
        self.destroy()


def _point_field(message: object, name: str) -> tuple[int, int] | None:
    for field in message.points.fields:
        if field.name == name:
            return int(field.offset), int(field.datatype)
    return None


def _read_float(data: bytes, offset: int, datatype: int, big_endian: bool) -> float | None:
    formats = {7: "f", 8: "d"}  # PointField.FLOAT32 / FLOAT64
    code = formats.get(datatype)
    if code is None:
        return None
    size = struct.calcsize(code)
    if offset < 0 or offset + size > len(data):
        return None
    try:
        return float(struct.unpack_from((">" if big_endian else "<") + code, data, offset)[0])
    except struct.error:
        return None


def decode_profile(message: object, lateral_axis: str = "y", height_axis: str = "z") -> list[tuple[float, float]]:
    if lateral_axis == height_axis:
        return []
    cloud = message.points
    lateral = _point_field(message, lateral_axis)
    height = _point_field(message, height_axis)
    if lateral is None or height is None or cloud.point_step <= 0:
        return []
    width = int(cloud.width)
    height_count = int(cloud.height)
    row_step = int(cloud.row_step or width * cloud.point_step)
    result: list[tuple[float, float]] = []
    for row in range(height_count):
        for column in range(width):
            base = row * row_step + column * int(cloud.point_step)
            x_value = _read_float(cloud.data, base + lateral[0], lateral[1], cloud.is_bigendian)
            y_value = _read_float(cloud.data, base + height[0], height[1], cloud.is_bigendian)
            if x_value is not None and y_value is not None and abs(x_value) < 1000 and abs(y_value) < 1000:
                result.append((x_value, y_value))
    return sorted(result, key=lambda point: point[0])


class ContourWindow(tk.Toplevel):
    def __init__(self, parent: ControlPanel) -> None:
        super().__init__(parent)
        self.parent_panel = parent
        self.title("线激光轮廓成像")
        self.geometry("760x560")
        self.minsize(600, 420)
        self.configure(bg="#0d141a")
        self.canvas = tk.Canvas(self, bg="#0d141a", highlightthickness=0)
        self.canvas.pack(fill="both", expand=True, padx=14, pady=(14, 6))
        self.info_var = tk.StringVar(value="等待轮廓数据...")
        self.lateral_axis_var = tk.StringVar(value="y")
        self.height_axis_var = tk.StringVar(value="z")
        axis_bar = ttk.Frame(self)
        axis_bar.pack(fill="x", padx=16, pady=(0, 6))
        ttk.Label(axis_bar, text="横向轴").pack(side="left")
        ttk.Combobox(axis_bar, textvariable=self.lateral_axis_var, values=("x", "y", "z"), width=4,
                     state="readonly").pack(side="left", padx=(5, 16))
        ttk.Label(axis_bar, text="高度轴").pack(side="left")
        ttk.Combobox(axis_bar, textvariable=self.height_axis_var, values=("x", "y", "z"), width=4,
                     state="readonly").pack(side="left", padx=5)
        tk.Label(self, textvariable=self.info_var, bg="#0d141a", fg="#d8e4ea", anchor="w").pack(fill="x", padx=16, pady=(0, 12))
        self.protocol("WM_DELETE_WINDOW", self.withdraw)

    def show_profile(self, message: object, status: object | None) -> None:
        points = decode_profile(message, self.lateral_axis_var.get(), self.height_axis_var.get())
        if len(points) < 2 or not self.winfo_exists():
            return
        width = max(100, self.canvas.winfo_width())
        height = max(100, self.canvas.winfo_height())
        self.canvas.delete("all")
        margin_left, margin_top, margin_right, margin_bottom = 58, 24, 22, 42
        plot_width = max(1, width - margin_left - margin_right)
        plot_height = max(1, height - margin_top - margin_bottom)
        lateral_values = [point[0] for point in points]
        height_values = [point[1] for point in points]
        x_min = min(-0.05, min(lateral_values))
        x_max = max(0.05, max(lateral_values))
        y_min = min(height_values)
        y_max = max(height_values)
        span = max(0.001, y_max - y_min)
        y_min -= 0.12 * span
        y_max += 0.12 * span

        def sx(value: float) -> float:
            return margin_left + (value - x_min) / max(1e-9, x_max - x_min) * plot_width

        def sy(value: float) -> float:
            return margin_top + (y_max - value) / max(1e-9, y_max - y_min) * plot_height

        self.canvas.create_rectangle(margin_left, margin_top, width - margin_right, height - margin_bottom,
                                     outline="#30414d")
        target_x = sx(0.0)
        self.canvas.create_line(target_x, margin_top, target_x, height - margin_bottom,
                                fill="#e5b84b", dash=(5, 4), width=2)
        coords: list[float] = []
        for lateral, profile_height in points:
            coords.extend((sx(lateral), sy(profile_height)))
        self.canvas.create_line(*coords, fill="#4ed7e8", width=2, smooth=False)
        if status is not None and hasattr(status, "contour_lateral_m"):
            seam_x = sx(float(status.contour_lateral_m))
            self.canvas.create_line(seam_x, margin_top, seam_x, height - margin_bottom,
                                    fill="#e85d5d", dash=(7, 3), width=2)
        self.canvas.create_text(margin_left, height - 18, text="横向 / lateral (m)", fill="#8ea0ad", anchor="w")
        self.canvas.create_text(12, margin_top, text="高度", fill="#8ea0ad", anchor="w")
        if status is not None and hasattr(status, "heading_error_rad"):
            self.info_var.set(
                "轮廓点 %d | 焊缝中心 %.4f m | 横向误差 %.4f m | 前视 %.4f m | 角度 %.2f° | 拟合残差 %.4f m" %
                (len(points), float(status.contour_lateral_m), float(status.lateral_error_m),
                 float(getattr(status, "preview_lateral_error_m", 0.0)),
                 float(status.heading_error_rad) * 57.2957795, float(status.fit_residual_m))
            )
        else:
            self.info_var.set("轮廓点 %d | 黄色=目标中心  红色=识别焊缝中心" % len(points))


if __name__ == "__main__":
    ControlPanel().mainloop()
