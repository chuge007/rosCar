import os

ROOT = r"d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\src"
TARGETS = [
    r"robotcontrolpanel.cpp",
    r"probeadjustmentpanel.cpp",
    os.path.join("robot", "clamp_motor_widgets.cpp"),
    os.path.join("robot", "clamp_motor_controller.cpp"),
]

OLD = "夹子".encode("utf-8")
NEW = "前架".encode("utf-8")

for rel in TARGETS:
    path = os.path.join(ROOT, rel)
    with open(path, "rb") as handle:
        data = handle.read()
    count = data.count(OLD)
    if count == 0:
        print("skip %s" % rel)
        continue
    with open(path, "wb") as handle:
        handle.write(data.replace(OLD, NEW))
    print("%s replaced=%d" % (rel, count))
