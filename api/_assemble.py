# -*- coding: utf-8 -*-
"""把最新产物组装进交付目录 api\\小车api（只有头文件 + 静态库 + 动态库）。"""
import os
import shutil

API = r"d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\api"
DST = os.path.join(API, "小车api")

for sub in ("include", "static", "shared"):
    os.makedirs(os.path.join(DST, sub), exist_ok=True)

items = [
    (os.path.join(API, "include", "pa1664_robot_api.h"),
     os.path.join(DST, "include", "pa1664_robot_api.h")),
    (os.path.join(API, "lib", "PA1664RobotApi.lib"),
     os.path.join(DST, "static", "PA1664RobotApi.lib")),
    (os.path.join(API, "build", "shared", "PA1664RobotApi.dll"),
     os.path.join(DST, "shared", "PA1664RobotApi.dll")),
    (os.path.join(API, "build", "shared", "PA1664RobotApi.lib"),
     os.path.join(DST, "shared", "PA1664RobotApi.lib")),
]

for src, dst in items:
    if not os.path.isfile(src):
        raise SystemExit("missing: " + src)
    shutil.copy2(src, dst)
    print("%10d  %s" % (os.path.getsize(dst), os.path.relpath(dst, DST)))
