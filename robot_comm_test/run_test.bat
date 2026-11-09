@echo off
cd /d "D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmpSeparate\robot_comm_test\release"
set PATH=D:\qt\5.12.4\msvc2017_64\bin;%PATH%
del /q robot_comm_test.log 2>nul
robot_comm_test.exe -H 192.168.1.20 -p 43100 > stdout.txt 2>&1
echo RC=%errorlevel%
echo ---- STDOUT ----
type stdout.txt
echo ---- LOG ----
if exist robot_comm_test.log (type robot_comm_test.log) else (echo NO_LOG_FILE)
