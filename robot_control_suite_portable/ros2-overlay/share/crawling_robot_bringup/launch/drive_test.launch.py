from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_parameters = (
        get_package_share_directory("crawling_robot_bringup")
        + "/config/drive_test.yaml"
    )
    parameters_file = LaunchConfiguration("parameters_file")
    can_serial_port = LaunchConfiguration("can_serial_port")

    return LaunchDescription([
        DeclareLaunchArgument("parameters_file", default_value=default_parameters),
        DeclareLaunchArgument("can_serial_port", default_value=""),
        Node(
            package="crawling_robot_drivers",
            executable="base_drive_node",
            name="base_drive_node",
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_drivers",
            executable="slcan_can_bridge_node",
            name="slcan_can_bridge_node",
            parameters=[parameters_file, {"auto_open": True, "serial_port": can_serial_port}],
            output="screen",
        ),
    ])
