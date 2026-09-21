Robot Control Suite portable package

This package is configured for local ROS 2 testing on the same Windows computer.
Start the local motor stack first with target_board_release\run_drive_test.cmd;
it auto-detects the USB-CAN adapter and keeps DDS on localhost. Then run
start_robot_control.cmd from this folder.

The package includes Qt, ROS 2, Python, the ROS interface overlay, and DDS settings.
No remote host or LAN discovery is used by the packaged control software.
