"""Low-latency /cmd_vel bridge for the Qt desktop controller.

The Qt application keeps this process alive and sends one JSON command per
line. rclpy publishes the latest command at 50 Hz, avoiding a new ros2 CLI
startup for every key-repeat tick.
"""

from __future__ import annotations

import json
import queue
import sys
import threading
import time

import rclpy
from geometry_msgs.msg import Twist


def read_commands(commands: queue.Queue[tuple[float, float]]) -> None:
    for line in sys.stdin:
        try:
            value = json.loads(line)
            linear = float(value["linear"])
            angular = float(value["angular"])
            if not (abs(linear) < 1000.0 and abs(angular) < 1000.0):
                continue
            while True:
                try:
                    commands.get_nowait()
                except queue.Empty:
                    break
            commands.put((linear, angular))
        except (ValueError, TypeError, KeyError, json.JSONDecodeError):
            print("invalid cmd_vel bridge input", file=sys.stderr, flush=True)


def main() -> int:
    rclpy.init(args=None)
    node = rclpy.create_node("robot_control_suite_cmd_vel_bridge")
    publisher = node.create_publisher(Twist, "/cmd_vel", 20)
    commands: queue.Queue[tuple[float, float]] = queue.Queue(maxsize=1)
    reader = threading.Thread(target=read_commands, args=(commands,), daemon=True)
    reader.start()
    linear = 0.0
    angular = 0.0
    last_input = time.monotonic()

    def publish_latest() -> None:
        nonlocal linear, angular, last_input
        try:
            linear, angular = commands.get_nowait()
            last_input = time.monotonic()
        except queue.Empty:
            pass
        # Never let a disconnected or hung desktop keep the drive alive.
        if time.monotonic() - last_input > 0.18:
            linear = 0.0
            angular = 0.0
        message = Twist()
        message.linear.x = linear
        message.angular.z = angular
        publisher.publish(message)

    timer = node.create_timer(0.02, publish_latest)
    try:
        rclpy.spin(node)
    finally:
        timer.cancel()
        message = Twist()
        publisher.publish(message)
        node.destroy_node()
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
