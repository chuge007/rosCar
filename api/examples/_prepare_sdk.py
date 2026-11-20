# -*- coding: utf-8 -*-
"""把交付目录 api\\小车api 复制到 examples\\sdk（ASCII 路径）供测试工程引用。

qmake/nmake 在含中文的路径下会 LNK1181，所以测试工程不能直接引用
api\\小车api；复制一份字节一致的副本，同时校验 SHA256。
"""
import hashlib
import os
import shutil

API = r"d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\api"
SRC = os.path.join(API, "小车api")
DST = os.path.join(API, "examples", "sdk")


def digest(path):
    hash_ = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            hash_.update(chunk)
    return hash_.hexdigest()


pairs = [
    (os.path.join(SRC, "include", "pa1664_robot_api.h"),
     os.path.join(DST, "include", "pa1664_robot_api.h")),
    (os.path.join(SRC, "static", "PA1664RobotApi.lib"),
     os.path.join(DST, "static", "PA1664RobotApi.lib")),
    (os.path.join(SRC, "shared", "PA1664RobotApi.dll"),
     os.path.join(DST, "shared", "PA1664RobotApi.dll")),
    (os.path.join(SRC, "shared", "PA1664RobotApi.lib"),
     os.path.join(DST, "shared", "PA1664RobotApi.lib")),
]

if os.path.isdir(DST):
    shutil.rmtree(DST)

for src, dst in pairs:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)
    same = digest(src) == digest(dst)
    print("%s  same=%d  %s" % (os.path.basename(dst), 1 if same else 0,
                               os.path.relpath(dst, API)))
