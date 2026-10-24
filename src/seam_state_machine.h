#ifndef SEAM_STATE_MACHINE_H
#define SEAM_STATE_MACHINE_H

#include <QString>
#include <QObject>
#include <algorithm>
#include <vector>
#include <cmath>

namespace crawling {

/**
 * @brief 焊道定位状态
 *
 * 状态转换链路：NotDetected → Candidate → Confirmed → Located
 *
 * Confirmed 负责采集稳定中心样本；校准完成后由控制器显式通知进入 Located。
 * 同一次任务内短暂丢检恢复时，已有校准结果可以被复用。
 */
enum class SeamLocalizationState {
    NotDetected,  // 未检测到焊道
    Candidate,    // 候选焊道（检测到但未稳定）
    Confirmed,    // 已连续稳定，正在建立/复用激光中心基准
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
        cameraToLaserOffset_ = sum / samples.size();
        calibrated_ = true;
        calibrationSamples_ = samples;
        emit calibrationChanged(cameraToLaserOffset_);
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
 * 根据偏差大小分四档调整Kp和Kd：
 * - <2 mm：弱响应平滑保持
 * - 2~5 mm：精细调整
 * - 5~10 mm：稳定逼近
 * - >10 mm：强响应快速纠正
 *
 * 档位边界使用 0.5 mm 的平滑过渡，避免滤波后的误差在阈值附近
 * 反复切换增益而引入抖动。
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
        const double absError = std::isfinite(errorMm)
                                    ? std::abs(errorMm)
                                    : 0.0;
        const auto smoothStep = [](double edge0, double edge1, double value) {
            const double ratio = std::clamp(
                (value - edge0) / (edge1 - edge0), 0.0, 1.0);
            return ratio * ratio * (3.0 - 2.0 * ratio);
        };
        const double fineBlend = smoothStep(
            smallErrorThreshold_ - kGainTransitionHalfWidthMm,
            smallErrorThreshold_ + kGainTransitionHalfWidthMm, absError);
        const double mediumBlend = smoothStep(
            mediumErrorThreshold_ - kGainTransitionHalfWidthMm,
            mediumErrorThreshold_ + kGainTransitionHalfWidthMm, absError);
        const double largeBlend = smoothStep(
            largeErrorThreshold_ - kGainTransitionHalfWidthMm,
            largeErrorThreshold_ + kGainTransitionHalfWidthMm, absError);

        const double kp = smallKp_ + fineBlend * (fineKp_ - smallKp_) +
                          mediumBlend * (mediumKp_ - fineKp_) +
                          largeBlend * (largeKp_ - mediumKp_);
        const double kd = smallKd_ + fineBlend * (fineKd_ - smallKd_) +
                          mediumBlend * (mediumKd_ - fineKd_) +
                          largeBlend * (largeKd_ - mediumKd_);
        return {kp, kd};
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

    // 精细偏差参数（2~5mm）
    void setFineErrorParams(double kp, double kd) {
        fineKp_ = kp;
        fineKd_ = kd;
    }

    // 小偏差参数
    void setSmallErrorParams(double kp, double kd) {
        smallKp_ = kp;
        smallKd_ = kd;
    }

private:
    static constexpr double kGainTransitionHalfWidthMm = 0.5;

    // 偏差阈值（mm）
    double smallErrorThreshold_ = 2.0;
    double mediumErrorThreshold_ = 5.0;
    double largeErrorThreshold_ = 10.0;

    // PID参数
    double largeKp_ = 2.5;
    double largeKd_ = 3.0;
    double mediumKp_ = 1.5;
    double mediumKd_ = 2.0;
    double fineKp_ = 1.0;
    double fineKd_ = 1.5;
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
     * @param centerMm 检测到的中心位置（mm）
     * @param confidence 检测置信度
     */
    void update(bool detectionValid, double centerMm, double confidence) {
        const bool wasLocated = currentState_ == SeamLocalizationState::Located;
        if (!detectionValid || !std::isfinite(centerMm) ||
            !std::isfinite(confidence)) {
            transitionTo(SeamLocalizationState::NotDetected);
            recentCenters_.clear();
            confirmationCount_ = 0;
            if (wasLocated) emit seamLost();
            return;
        }

        if (confidence < lostConfidenceThreshold_) {
            transitionTo(SeamLocalizationState::NotDetected);
            recentCenters_.clear();
            confirmationCount_ = 0;
            if (wasLocated) emit seamLost();
            return;
        }

        if (confidence < candidateConfidenceThreshold_) {
            // 已定位的焊道降为候选；尚未定位时保持原状态，避免低置信度
            // 单帧把 NotDetected 误报成候选。
            if (currentState_ == SeamLocalizationState::Located) {
                transitionTo(SeamLocalizationState::Candidate);
                emit seamLost();
            } else if (currentState_ == SeamLocalizationState::Confirmed) {
                transitionTo(SeamLocalizationState::Candidate);
            }
            recentCenters_.clear();
            confirmationCount_ = 0;
            return;
        }

        recentCenters_.push_back(centerMm);
        if (recentCenters_.size() > maxHistoryFrames_) {
            recentCenters_.erase(recentCenters_.begin());
        }

        switch (currentState_) {
        case SeamLocalizationState::NotDetected:
            // 检测到候选焊道
            transitionTo(SeamLocalizationState::Candidate);
            confirmationCount_ = 1;
            break;

        case SeamLocalizationState::Candidate:
            // 候选 → 确认：连续稳定后才允许建立/复用激光中心基准。
            if (isCenterStable(minConfirmationFrames_, centerStabilityTolerance_)) {
                transitionTo(SeamLocalizationState::Confirmed);
                emit seamConfirmed();
            }
            break;

        case SeamLocalizationState::Confirmed:
            // 校准器由控制器驱动；已有校准结果时可直接恢复定位。
            if (calibrationComplete_) {
                transitionTo(SeamLocalizationState::Located);
                emit seamLocated();
            }
            break;

        case SeamLocalizationState::Located:
            // 检测丢失或置信度下降已在上面统一降级，无需重复发射信号。
            break;
        }
    }

    /**
     * @brief 通知校准已完成
     *
     * Confirmed 收到该通知后立即进入 Located。校准结果在本次任务内保留，
     * 因此短暂丢检恢复时无需重新采样即可再次定位。
     */
    void notifyCalibrationComplete() {
        calibrationComplete_ = true;
        if (currentState_ == SeamLocalizationState::Confirmed) {
            transitionTo(SeamLocalizationState::Located);
            emit seamLocated();
        }
    }

    bool isCalibrationComplete() const { return calibrationComplete_; }

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
        case SeamLocalizationState::Confirmed:
            return QStringLiteral("焊道确认中（校准激光中心）");
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
        calibrationComplete_ = false;
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
    void seamConfirmed();
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
    double centerStabilityTolerance_ = 2.0;  // mm
    double candidateConfidenceThreshold_ = 0.10;
    double lostConfidenceThreshold_ = 0.05;
    bool calibrationComplete_ = false;
};

} // namespace crawling

#endif // SEAM_STATE_MACHINE_H
