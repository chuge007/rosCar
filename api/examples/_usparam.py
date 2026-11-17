# -*- coding: utf-8 -*-
"""调用 api_test_static.exe --usparam：读写超声参数或直接发 ultrasound.* 操作。

用法：
  python _usparam.py getGain 0
  python _usparam.py setGain 20,0
  python _usparam.py ultrasound.status 0
"""
import os
import subprocess
import sys

BIN = os.path.join(
    r"d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\api\examples",
    "bin_static")
EXE = os.path.join(BIN, "api_test_static.exe")

method = sys.argv[1] if len(sys.argv) > 1 else "getGain"
args = sys.argv[2] if len(sys.argv) > 2 else "0"

env = dict(os.environ)
env["PATH"] = r"D:\qt5.15.2\6.8.3\msvc2022_64\bin;" + env.get("PATH", "")

try:
    proc = subprocess.run([EXE, "--usparam", "192.168.1.20", method, args],
                          capture_output=True, env=env, cwd=BIN, timeout=40)
    print("EXIT", proc.returncode)
    text = (proc.stdout or b"").decode("utf-8", "replace")
    for line in text.splitlines()[:14]:
        print(line)
except subprocess.TimeoutExpired:
    print("TIMEOUT 40s")
