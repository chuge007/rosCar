// 快速语法检查测试
#include "src/laser_correction_controller.h"
#include "src/seam_state_machine.h"

void test() {
    crawling::LaserCorrectionStatus status;
    status.seamState = SeamLocalizationState::Located;
    status.seamStateDescription = QString("测试");
    status.laserCenterCalibrated = true;
    status.laserCenterOffset = 1.5;
    status.gapValid = true;
}
