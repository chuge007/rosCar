#ifndef SEAM_STATE_MACHINE_H
#define SEAM_STATE_MACHINE_H

#include <QString>
#include <QObject>
#include <vector>
#include <cmath>

namespace crawling {

/**
 * @brief 焊道定位状态
 *
 * 状态转换链路：NotDetected → Candidate → Located
 *
 * 【2026-10-23 简化】删除Confirmed状态，Candidate稳定后直接进Located
 */
enum class SeamLocalizationState {
    NotDetected,  // 未检测到焊道
    Candidate,    // 候选焊道（检测到但未稳定）
    Located       // 已定位（稳定且可用于纠偏控制）
};

/**
 * @brief 激光中心校准器
 *
 * 将相机坐标系下的焊道中心转换到激光坐标系
 */
class LaserCenterCalibration : public QObject {
    Q_OBJECT

public:
    explicit LaserCenterCalibration(QObject* parent = nullptr) : QObject(parent) {}

    /**
     * @brief 执行校准，建立相机到激光坐标系的偏移
     * @param detectedCenterX 相机坐标系下检测到的焊道中心（mm）
     * @param laserCenterX 激光坐标系中心（默认0）
     */
    void calibrate(double detectedCenterX, double laserCenterX = 0.0) {
        cameraToLaserOffset_ = detectedCenterX - laserCenterX;
        calibrated_ = true;
        calibrationSamples_.push_back(detectedCenterX);
        emit calibrationChanged(cameraToLaserOffset_);
    }

    /**
     * @brief 使用多次采样的平均值进行校准
     * @param samples 多次检测的中心位置
     */
    void calibrateFromSamples(const std::vector<double>& samples) {
        if (samples.empty()) return;

        double sum = 0.0;
        for (double s : samples) {
            sum += s;
        }
        double avgCenter = sum / samples.size();
        calibrate(avgCenter);
    }

    /**
     * @brief 将相机坐标系的x转换到激光坐标系
     * @param rawCameraX 相机坐标系下的x坐标（mm）
     * @return 激光坐标系下的x坐标（mm）
     */
    double adjustToLaserFrame(double rawCameraX) const {
        if (!calibrated_) return rawCameraX;
        return rawCameraX - cameraToLaserOffset_;
    }

    /**
     * @brief 检查是否已完成校准
     */
    bool isCalibrated() const { return calibrated_; }

    /**
     * @brief 获取当前偏移量
     */
    double getOffset() const { return cameraToLaserOffset_; }

    /**
     * @brief 重置校准
     */
    void reset() {
        calibrated_ = false;
        cameraToLaserOffset_ = 0.0;
        calibrationSamples_.clear();
    }

signals:
    void calibrationChanged(double offset);

private:
    bool calibrated_ = false;
    double cameraToLaserOffset_ = 0.0;
    std::vector<double> calibrationSamples_;
};

/**
 * @brief 自适应PID参数计算器
 *
 * 根据偏差大小动态调整Kp和Kd，实现：
 * - 大偏差：强响应快速纠正
 * - 小偏差：弱响应平滑保持
 */
class AdaptivePIDParams {
public:
    struct Params {
        double kp;
        double kd;
    };

    /**
     * @brief 根据误差获取自适应PID参数
     * @param errorMm 焊道中心偏差（mm）
     * @return 当前偏差对应的Kp和Kd
     */
    Params getParams(double errorMm) const {
        double absError = std::abs(errorMm);

        if (absError > largeErrorThreshold_) {
            return {largeKp_, largeKd_};
        } else if (absError > mediumErrorThreshold_) {
            // 线性插值
            double ratio = (absError - mediumErrorThreshold_) /
                          (largeErrorThreshold_ - mediumErrorThreshold_);
            return {
                mediumKp_ + ratio * (largeKp_ - mediumKp_),
                mediumKd_ + ratio * (largeKd_ - mediumKd_)
            };
        } else if (absError > smallErrorThreshold_) {
            double ratio = (absError - smallErrorThreshold_) /
                          (mediumErrorThreshold_ - smallErrorThreshold_);
            return {
                smallKp_ + ratio * (mediumKp_ - smallKp_),
                smallKd_ + ratio * (mediumKd_ - smallKd_)
            };
        } else {
            return {smallKp_, smallKd_};
        }
    }

    // 阈值设置
    void setThresholds(double small, double medium, double large) {
        smallErrorThreshold_ = small;
        mediumErrorThreshold_ = medium;
        largeErrorThreshold_ = large;
    }

    // 大偏差参数
    void setLargeErrorParams(double kp, double kd) {
        largeKp_ = kp;
        largeKd_ = kd;
    }

    // 中等偏差参数
    void setMediumErrorParams(double kp, double kd) {
        mediumKp_ = kp;
        mediumKd_ = kd;
    }

    // 小偏差参数
    void setSmallErrorParams(double kp, double kd) {
        smallKp_ = kp;
        smallKd_ = kd;
    }

private:
    // 偏差阈值（mm）
    double smallErrorThreshold_ = 2.0;
    double mediumErrorThreshold_ = 5.0;
    double largeErrorThreshold_ = 10.0;

    // PID参数
    double largeKp_ = 2.5;
    double largeKd_ = 3.0;
    double mediumKp_ = 1.5;
    double mediumKd_ = 2.0;
    double smallKp_ = 0.5;
    double smallKd_ = 1.0;
};

/**
 * @brief 中心位置滤波器
 *
 * 使用指数滑动平均抑制测量噪声
 */
class CenterFilter {
public:
    explicit CenterFilter(double alpha = 0.3) : alpha_(alpha) {}

    /**
     * @brief 更新滤波器并获取滤波后的值
     * @param raw 原始测量值
     * @return 滤波后的值
     */
    double update(double raw) {
        if (!initialized_) {
            filtered_ = raw;
            initialized_ = true;
        } else {
            filtered_ = alpha_ * raw + (1.0 - alpha_) * filtered_;
        }
        return filtered_;
    }

    /**
     * @brief 获取当前滤波值
     */
    double getValue() const { return filtered_; }

    /**
     * @brief 重置滤波器
     */
    void reset() {
        initialized_ = false;
        filtered_ = 0.0;
    }

    /**
     * @brief 设置滤波系数
     * @param alpha 滤波系数（0~1），越大越接近原始值
     */
    void setAlpha(double alpha) {
        alpha_ = std::clamp(alpha, 0.0, 1.0);
    }

private:
    double alpha_;          // 滤波系数
    double filtered_ = 0.0; // 当前滤波值
    bool initialized_ = false;
};

/**
 * @brief 焊道状态机
 *
 * 管理焊道从检测到定位的状态转换
 */
class SeamStateMachine : public QObject {
    Q_OBJECT

public:
    explicit SeamStateMachine(QObject* parent = nullptr)
        : QObject(parent) {}

    /**
     * @brief 更新状态机
     * @param detectionValid 当前帧检测是否有效
     * @param centerX 检测到的中心位置（mm）
     * @param confidence 检测置信度
     */
    void update(bool detectionValid, double centerX, double confidence) {
        if (!detectionValid) {
            transitionTo(SeamLocalizationState::NotDetected);
            recentCenters_.clear();
            confirmationCount_ = 0;
            return;
        }

        recentCenters_.push_back(centerX);
        if (recentCenters_.size() > maxHistoryFrames_) {
            recentCenters_.erase(recentCenters_.begin());
        }

        switch (currentState_) {
        case SeamLocalizationState::NotDetected:
            // 检测到候选焊道
            if (confidence > candidateConfidenceThreshold_) {
                transitionTo(SeamLocalizationState::Candidate);
                confirmationCount_ = 1;
            }
            break;

        case SeamLocalizationState::Candidate:
            // 候选 → 定位：连续稳定帧（简化：直接进Located，无需校准门槛）
            if (isCenterStable(minConfirmationFrames_, centerStabilityTolerance_)) {
                transitionTo(SeamLocalizationState::Located);
                emit seamLocated();
            }
            break;

        case SeamLocalizationState::Located:
            // 已定位状态下持续检查稳定性
            if (confidence < lostConfidenceThreshold_) {
                transitionTo(SeamLocalizationState::Candidate);
                emit seamLost();
            }
            break;
        }
    }

    /**
     * @brief 获取当前状态
     */
    SeamLocalizationState getState() const { return currentState_; }

    /**
     * @brief 获取状态描述文本
     */
    QString getStateDescription() const {
        switch (currentState_) {
        case SeamLocalizationState::NotDetected:
            return QStringLiteral("焊道等待定位");
        case SeamLocalizationState::Candidate:
            return QStringLiteral("本项焊道候选");
        case SeamLocalizationState::Located:
            return QStringLiteral("焊道已定位");
        default:
            return QStringLiteral("未知状态");
        }
    }

    /**
     * @brief 检查是否可以启动纠偏
     */
    bool canStartCorrection() const {
        return currentState_ == SeamLocalizationState::Located;
    }

    /**
     * @brief 重置状态机
     */
    void reset() {
        transitionTo(SeamLocalizationState::NotDetected);
        recentCenters_.clear();
        confirmationCount_ = 0;
    }

    // 参数设置
    void setCandidateConfidenceThreshold(double threshold) {
        candidateConfidenceThreshold_ = threshold;
    }

    void setLostConfidenceThreshold(double threshold) {
        lostConfidenceThreshold_ = threshold;
    }

    void setConfirmationFrames(int frames) {
        minConfirmationFrames_ = frames;
    }

    void setStabilityTolerance(double tolerance) {
        centerStabilityTolerance_ = tolerance;
    }

signals:
    void stateChanged(SeamLocalizationState newState);
    void seamLocated();
    void seamLost();

private:
    /**
     * @brief 状态转换
     */
    void transitionTo(SeamLocalizationState newState) {
        if (newState != currentState_) {
            currentState_ = newState;
            emit stateChanged(newState);
        }
    }

    /**
     * @brief 检查中心位置是否稳定
     */
    bool isCenterStable(int minFrames, double tolerance) const {
        if (static_cast<int>(recentCenters_.size()) < minFrames) {
            return false;
        }

        // 计算最近N帧的标准差
        double sum = 0.0;
        for (size_t i = recentCenters_.size() - minFrames;
             i < recentCenters_.size(); ++i) {
            sum += recentCenters_[i];
        }
        double mean = sum / minFrames;

        double variance = 0.0;
        for (size_t i = recentCenters_.size() - minFrames;
             i < recentCenters_.size(); ++i) {
            double diff = recentCenters_[i] - mean;
            variance += diff * diff;
        }
        double stdDev = std::sqrt(variance / minFrames);

        return stdDev < tolerance;
    }

    SeamLocalizationState currentState_ = SeamLocalizationState::NotDetected;
    std::vector<double> recentCenters_;
    int confirmationCount_ = 0;

    // 参数配置
    static constexpr int maxHistoryFrames_ = 10;
    int minConfirmationFrames_ = 3;
    double centerStabilityTolerance_ = 0.002;  // 2mm
    double candidateConfidenceThreshold_ = 0.10;
    double lostConfidenceThreshold_ = 0.05;
};

} // namespace crawling

#endif // SEAM_STATE_MACHINE_H
