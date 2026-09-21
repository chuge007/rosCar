from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import EnvironmentVariable, LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_parameters = (
        get_package_share_directory("crawling_robot_bringup") + "/config/robot.yaml"
    )
    parameters_file = LaunchConfiguration("parameters_file")
    use_slcan_can_bridge = LaunchConfiguration("use_slcan_can_bridge")
    use_canopen_axis = LaunchConfiguration("use_canopen_axis")
    use_laser_path_follower = LaunchConfiguration("use_laser_path_follower")
    use_monitor = LaunchConfiguration("use_monitor")
    can_serial_port = LaunchConfiguration("can_serial_port")

    return LaunchDescription([
        DeclareLaunchArgument("parameters_file", default_value=default_parameters),
        DeclareLaunchArgument("use_slcan_can_bridge", default_value="false"),
        DeclareLaunchArgument("use_canopen_axis", default_value="true"),
        DeclareLaunchArgument("use_laser_path_follower", default_value="true"),
        DeclareLaunchArgument("use_monitor", default_value="true"),
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
            executable="rim302_imu_node",
            name="rim302_imu_node",
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_drivers",
            executable="modbus_encoder_node",
            name="modbus_encoder_node",
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_drivers",
            executable="encoder_odom_node",
            name="encoder_odom_node",
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_drivers",
            executable="canopen_axis_node",
            name="canopen_axis_node",
            condition=IfCondition(use_canopen_axis),
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_laser",
            executable="mv3dlp_laser_node",
            name="mv3dlp_laser_node",
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_control",
            executable="laser_path_follower_node",
            name="laser_path_follower_node",
            condition=IfCondition(use_laser_path_follower),
            parameters=[parameters_file],
            output="screen",
        ),
        Node(
            package="crawling_robot_drivers",
            executable="slcan_can_bridge_node",
            name="slcan_can_bridge_node",
            condition=IfCondition(use_slcan_can_bridge),
            parameters=[parameters_file, {"auto_open": use_slcan_can_bridge,
                                          "serial_port": can_serial_port}],
            output="screen",
        ),
        Node(
            package="crawling_robot_monitor",
            # The generated Windows .exe embeds the development Python path.
            # Run the installed script with the relocatable target Python.
            executable="robot_monitor-script.py",
            prefix=[EnvironmentVariable(
                "COLCON_PYTHON_EXECUTABLE", default_value="python"
            )],
            name="crawling_robot_monitor",
            condition=IfCondition(use_monitor),
            parameters=[parameters_file],
            output="screen",
        ),
    ])
