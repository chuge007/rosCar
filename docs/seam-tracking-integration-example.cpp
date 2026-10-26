/**
 * @file seam-tracking-integration-example.cpp
 * @brief 焊道跟踪优化集成示例
 *
 * 展示如何将状态机、自适应PID、激光校准整合到现有的LaserCorrectionController
 */

#include "laser_correction_controller.h"
#include "seam_state_machine.h"
#include "profile_weld_detector.h"

namespace crawling {

/**
 * @brief LaserCorrectionController 增强版本
 *
 * 在现有控制器基础上集成：
 * 1. 焊道状态机
 * 2. 激光中心校准
 * 3. 自适应PID
 * 4. 中心滤波
 */
class LaserCorrectionControllerEnhanced : public LaserCorrectionController {
    Q_OBJECT

public:
    explicit LaserCorrectionControllerEnhanced(QObject* parent = nullptr)
        : LaserCorrectionController(parent)
    {
        // 初始化状态机
        seamStateMachine_ = new SeamStateMachine(this);

        // 连接状态机信号
        connect(seamStateMachine_, &SeamStateMachine::stateChanged,
                this, &LaserCorrectionControllerEnhanced::onSeamStateChanged);
        connect(seamStateMachine_, &SeamStateMachine::seamConfirmed,
                this, &LaserCorrectionControllerEnhanced::onSeamConfirmed);
        connect(seamStateMachine_, &SeamStateMachine::seamLocated,
                this, &LaserCorrectionControllerEnhanced::onSeamLocated);
        connect(seamStateMachine_, &SeamStateMachine::seamLost,
                this, &LaserCorrectionControllerEnhanced::onSeamLost);

        // 配置状态机参数
        seamStateMachine_->setConfirmationFrames(3);
        seamStateMachine_->setStabilityTolerance(0.002);  // 2mm
        seamStateMachine_->setCandidateConfidenceThreshold(0.10);
        seamStateMachine_->setLostConfidenceThreshold(0.05);

        // 配置自适应PID
        adaptivePID_.setThresholds(2.0, 5.0, 10.0);
        adaptivePID_.setLargeErrorParams(2.5, 3.0);
        adaptivePID_.setMediumErrorParams(1.5, 2.0);
        adaptivePID_.setSmallErrorParams(0.5, 1.0);

        // 配置滤波器
        centerFilter_.setAlpha(0.3);
    }

    /**
     * @brief 处理激光轮廓检测结果（主入口）
     */
    void processProfileDetection(const ProfileDetection& detection) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();

        // ========== 第一步：更新状态机 ==========
        seamStateMachine_->update(detection.valid, detection.centerX, detection.confidence);
        SeamLocalizationState currentState = seamStateMachine_->getState();

        // ========== 第二步：激光中心自动校准 ==========
        if (currentState == SeamLocalizationState::Confirmed &&
            !centerCalibration_.isCalibrated()) {
            // 收集多帧样本后取平均值校准
            calibrationSamples_.push_back(detection.centerX);

            if (calibrationSamples_.size() >= 5) {
                centerCalibration_.calibrateFromSamples(calibrationSamples_);
                seamStateMachine_->notifyCalibrationComplete();

                qInfo() << "[LASER.CALIBRATION] 激光中心校准完成"
                        << " offset=" << centerCalibration_.getOffset() << "mm"
                        << " samples=" << calibrationSamples_.size();

                calibrationSamples_.clear();
            }
        }

        // ========== 第三步：坐标转换到激光系 ==========
        double laserFrameCenterX = 0.0;
        if (detection.valid && centerCalibration_.isCalibrated()) {
            laserFrameCenterX = centerCalibration_.adjustToLaserFrame(detection.centerX);
        }

        // ========== 第四步：滤波降噪 ==========
        double filteredCenterX = centerFilter_.update(laserFrameCenterX);

        // ========== 第五步：纠偏控制（仅Located状态） ==========
        if (currentState == SeamLocalizationState::Located &&
            correctionActive_ &&
            detection.valid) {

            // 计算误差和微分
            double error = filteredCenterX;  // 目标中心 = 0
            double dt = (now - lastCorrectionTimeMs_) / 1000.0;
            double derivative = 0.0;

            if (dt > 0.001 && dt < 1.0) {  // 防止除零和异常dt
                derivative = (error - lastError_) / dt;
            }

            // 获取自适应PID参数
            auto pidParams = adaptivePID_.getParams(error);
            double kp = pidParams.kp;
            double kd = pidParams.kd;

            // PID计算
            double correctionSpeed = kp * error + kd * derivative;

            // 限幅
            double maxSpeed = config_.maxCorrectionSpeed;
            correctionSpeed = std::clamp(correctionSpeed, -maxSpeed, maxSpeed);

            // 应用纠偏
            applyCorrectionSpeed(correctionSpeed);

            // 日志记录
            if (now - lastControlLogMs_ > 100) {  // 100ms间隔
                qInfo() << "[LASER.CONTROL]"
                        << " state=" << seamStateMachine_->getStateDescription()
                        << " raw_center=" << detection.centerX
                        << " laser_center=" << laserFrameCenterX
                        << " filtered=" << filteredCenterX
                        << " error=" << error
                        << " kp=" << kp
                        << " kd=" << kd
                        << " speed=" << correctionSpeed;

                lastControlLogMs_ = now;
            }

            // 更新历史
            lastError_ = error;
            lastCorrectionTimeMs_ = now;
        }

        // ========== 第六步：详细日志（可选） ==========
        if (now - lastDetectionLogMs_ > 500) {
            logDetailedDetection(detection, currentState,
                                laserFrameCenterX, filteredCenterX);
            lastDetectionLogMs_ = now;
        }
    }

    /**
     * @brief 启动自动纠偏（UI调用）
     */
    bool startAutoCorrection() override {
        if (!seamStateMachine_->canStartCorrection()) {
            QString reason = QStringLiteral("无法启动纠偏：%1")
                            .arg(seamStateMachine_->getStateDescription());
            qWarning() << "[LASER.CONTROL]" << reason;
            emit correctionStartFailed(reason);
            return false;
        }

        correctionActive_ = true;
        lastError_ = 0.0;
        lastCorrectionTimeMs_ = QDateTime::currentMSecsSinceEpoch();

        qInfo() << "[LASER.CONTROL] 自动纠偏已启动"
                << " offset=" << centerCalibration_.getOffset() << "mm";

        emit correctionStarted();
        return true;
    }

    /**
     * @brief 停止自动纠偏
     */
    void stopAutoCorrection() override {
        correctionActive_ = false;
        qInfo() << "[LASER.CONTROL] 自动纠偏已停止";
        emit correctionStopped();
    }

    /**
     * @brief 重置状态（新任务开始时调用）
     */
    void resetState() {
        seamStateMachine_->reset();
        centerCalibration_.reset();
        centerFilter_.reset();
        calibrationSamples_.clear();
        lastError_ = 0.0;
        correctionActive_ = false;

        qInfo() << "[LASER.CONTROL] 状态已重置";
    }

    /**
     * @brief 配置自适应PID参数（UI设置）
     */
    void configureAdaptivePID(double largeKp, double largeKd,
                              double mediumKp, double mediumKd,
                              double smallKp, double smallKd) {
        adaptivePID_.setLargeErrorParams(largeKp, largeKd);
        adaptivePID_.setMediumErrorParams(mediumKp, mediumKd);
        adaptivePID_.setSmallErrorParams(smallKp, smallKd);

        qInfo() << "[LASER.CONFIG] 自适应PID已更新"
                << " large(" << largeKp << "," << largeKd << ")"
                << " medium(" << mediumKp << "," << mediumKd << ")"
                << " small(" << smallKp << "," << smallKd << ")";
    }

    /**
     * @brief 配置滤波参数
     */
    void configureCenterFilter(double alpha) {
        centerFilter_.setAlpha(alpha);
        qInfo() << "[LASER.CONFIG] 中心滤波系数已更新 alpha=" << alpha;
    }

    /**
     * @brief 手动重新校准激光中心
     */
    void recalibrateManually() {
        centerCalibration_.reset();
        calibrationSamples_.clear();
        seamStateMachine_->notifyCalibrationComplete();  // 重置校准标志

        qInfo() << "[LASER.CONFIG] 激光中心校准已重置，等待新检测";
    }

    /**
     * @brief 获取当前状态信息（供UI显示）
     */
    struct StatusInfo {
        SeamLocalizationState state;
        QString stateDescription;
        bool canStartCorrection;
        bool isCalibrated;
        double calibrationOffset;
        double currentError;
        double currentKp;
        double currentKd;
    };

    StatusInfo getStatusInfo() const {
        StatusInfo info;
        info.state = seamStateMachine_->getState();
        info.stateDescription = seamStateMachine_->getStateDescription();
        info.canStartCorrection = seamStateMachine_->canStartCorrection();
        info.isCalibrated = centerCalibration_.isCalibrated();
        info.calibrationOffset = centerCalibration_.getOffset();
        info.currentError = lastError_;

        auto pidParams = adaptivePID_.getParams(lastError_);
        info.currentKp = pidParams.kp;
        info.currentKd = pidParams.kd;

        return info;
    }

signals:
    void seamStateChanged(SeamLocalizationState state, QString description);
    void correctionStarted();
    void correctionStopped();
    void correctionStartFailed(QString reason);
    void calibrationCompleted(double offset);

private slots:
    /**
     * @brief 状态机状态变化回调
     */
    void onSeamStateChanged(SeamLocalizationState newState) {
        QString desc = seamStateMachine_->getStateDescription();
        qInfo() << "[LASER.STATE] 焊道状态变更:" << desc;
        emit seamStateChanged(newState, desc);
    }

    void onSeamConfirmed() {
        qInfo() << "[LASER.STATE] 焊道已确认，开始激光中心校准";
    }

    void onSeamLocated() {
        qInfo() << "[LASER.STATE] 焊道已定位，可以启动纠偏";
    }

    void onSeamLost() {
        qWarning() << "[LASER.STATE] 焊道丢失，纠偏暂停";
        if (correctionActive_) {
            stopAutoCorrection();
        }
    }

private:
    /**
     * @brief 应用纠偏速度（调用底层执行器）
     */
    void applyCorrectionSpeed(double speedMmps) {
        // 调用父类的实际执行方法
        // 这里假设父类有相应接口，实际集成时需根据现有代码调整
        // commandMotorSpeed(speedMmps);
    }

    /**
     * @brief 详细检测日志
     */
    void logDetailedDetection(const ProfileDetection& det,
                             SeamLocalizationState state,
                             double laserCenter,
                             double filtered) {
        qInfo() << "[LASER.DETECT]"
                << " frame=" << det.frameId
                << " state=" << static_cast<int>(state)
                << " valid=" << det.valid
                << " confidence=" << det.confidence
                << " raw_x=" << det.centerX
                << " laser_x=" << laserCenter
                << " filtered_x=" << filtered
                << " calibrated=" << centerCalibration_.isCalibrated()
                << " offset=" << centerCalibration_.getOffset();
    }

    // 核心组件
    SeamStateMachine* seamStateMachine_;
    LaserCenterCalibration centerCalibration_;
    AdaptivePIDParams adaptivePID_;
    CenterFilter centerFilter_;

    // 校准采样
    std::vector<double> calibrationSamples_;

    // 控制状态
    bool correctionActive_ = false;
    double lastError_ = 0.0;
    qint64 lastCorrectionTimeMs_ = 0;

    // 日志节流
    qint64 lastControlLogMs_ = 0;
    qint64 lastDetectionLogMs_ = 0;

    // 配置
    struct Config {
        double maxCorrectionSpeed = 10.0;  // mm/s
    } config_;
};

} // namespace crawling

// ========== UI 集成示例 ==========

/**
 * @brief 在 RobotControlPanel 中的集成示例
 */
void RobotControlPanel::setupLaserCorrection() {
    // 创建增强控制器（替换原有的LaserCorrectionController）
    laserController_ = new LaserCorrectionControllerEnhanced(this);

    // 连接信号到UI更新
    connect(laserController_, &LaserCorrectionControllerEnhanced::seamStateChanged,
            this, &RobotControlPanel::onSeamStateChanged);
    connect(laserController_, &LaserCorrectionControllerEnhanced::calibrationCompleted,
            this, &RobotControlPanel::onCalibrationCompleted);

    // 连接激光轮廓检测器
    connect(profileDetector_, &ProfileWeldDetector::detectionReady,
            laserController_, &LaserCorrectionControllerEnhanced::processProfileDetection);
}

/**
 * @brief UI更新焊道状态显示
 */
void RobotControlPanel::onSeamStateChanged(SeamLocalizationState state, QString description) {
    // 更新状态文本
    ui->lblSeamStatus->setText(description);

    // 根据状态设置样式和按钮可用性
    switch (state) {
    case SeamLocalizationState::NotDetected:
        ui->lblSeamStatus->setStyleSheet("color: gray;");
        ui->btnStartCorrection->setEnabled(false);
        break;

    case SeamLocalizationState::Candidate:
        ui->lblSeamStatus->setStyleSheet("color: orange;");
        ui->btnStartCorrection->setEnabled(false);
        ui->lblSeamStatus->setToolTip("检测到焊道，正在确认稳定性...");
        break;

    case SeamLocalizationState::Confirmed:
        ui->lblSeamStatus->setStyleSheet("color: yellow;");
        ui->btnStartCorrection->setEnabled(false);
        ui->lblSeamStatus->setToolTip("焊道已确认，正在校准激光中心...");
        break;

    case SeamLocalizationState::Located:
        ui->lblSeamStatus->setStyleSheet("color: green; font-weight: bold;");
        ui->btnStartCorrection->setEnabled(true);
        ui->lblSeamStatus->setToolTip("焊道已定位，可以启动纠偏");
        break;
    }
}

/**
 * @brief 激光中心校准完成回调
 */
void RobotControlPanel::onCalibrationCompleted(double offset) {
    QString msg = QString("激光中心校准完成\n偏移量: %1 mm").arg(offset, 0, 'f', 2);
    QMessageBox::information(this, "校准完成", msg);

    // 更新显示
    ui->lblCalibrationOffset->setText(QString("%1 mm").arg(offset, 0, 'f', 2));
}

/**
 * @brief 启动纠偏按钮点击
 */
void RobotControlPanel::on_btnStartCorrection_clicked() {
    if (!laserController_->startAutoCorrection()) {
        // 启动失败，已通过信号反馈原因
        return;
    }

    // 更新UI
    ui->btnStartCorrection->setEnabled(false);
    ui->btnStopCorrection->setEnabled(true);
}

/**
 * @brief 停止纠偏按钮点击
 */
void RobotControlPanel::on_btnStopCorrection_clicked() {
    laserController_->stopAutoCorrection();

    // 更新UI
    ui->btnStartCorrection->setEnabled(true);
    ui->btnStopCorrection->setEnabled(false);
}

/**
 * @brief 配置自适应PID参数
 */
void RobotControlPanel::on_btnApplyPIDConfig_clicked() {
    double largeKp = ui->spinLargeKp->value();
    double largeKd = ui->spinLargeKd->value();
    double mediumKp = ui->spinMediumKp->value();
    double mediumKd = ui->spinMediumKd->value();
    double smallKp = ui->spinSmallKp->value();
    double smallKd = ui->spinSmallKd->value();

    laserController_->configureAdaptivePID(
        largeKp, largeKd,
        mediumKp, mediumKd,
        smallKp, smallKd
    );

    QMessageBox::information(this, "配置已保存", "自适应PID参数已更新");
}

/**
 * @brief 手动重新校准激光中心
 */
void RobotControlPanel::on_btnRecalibrate_clicked() {
    auto reply = QMessageBox::question(
        this,
        "重新校准",
        "确定要重置激光中心校准吗？\n这将清除当前偏移量并重新检测焊道中心。",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        laserController_->recalibrateManually();
        ui->lblCalibrationOffset->setText("等待校准...");
    }
}
