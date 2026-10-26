#pragma once
/**
 * @file enhanced_weld_locator.h
 * @brief 增强型焊道定位算法 - 更高精度和稳定性
 *
 * 核心改进:
 * 1. 多尺度高度分析 - 精确识别焊道拱起区域
 * 2. 鲁棒中心估计 - 基于高度加权+形态学峰值
 * 3. 时序卡尔曼滤波 - 消除测量噪声和抖动
 * 4. 自适应阈值 - 根据焊道特征动态调整
 */

#include <QVector3D>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <vector>
#include <deque>

namespace crawling {

/**
 * @brief 焊道定位结果
 */
struct WeldLocationResult {
    bool valid = false;                // 是否成功定位
    double centerX = 0.0;             // 焊道中心X坐标（SDK坐标系）
    double centerZ = 0.0;             // 焊道中心高度
    int startIndex = -1;              // 焊道起始点索引
    int endIndex = -1;                // 焊道结束点索引
    double weldWidth = 0.0;           // 焊道宽度(mm)
    double weldHeight = 0.0;          // 焊道高度(mm)
    double confidence = 0.0;          // 置信度(0-1)
    double baselineSlope = 0.0;       // 基准面斜率
    double baselineOffset = 0.0;      // 基准面偏移
    double noiseLevel = 0.0;          // 噪声水平
    QString detectionMethod;          // 检测方法描述
};

/**
 * @brief 卡尔曼滤波器 - 用于时序平滑
 */
class KalmanFilter1D {
public:
    KalmanFilter1D(double processNoise = 0.1, double measurementNoise = 2.0)
        : q_(processNoise), r_(measurementNoise), x_(0.0), p_(1000.0),
          initialized_(false) {}

    double update(double measurement) {
        if (!initialized_) {
            x_ = measurement;
            initialized_ = true;
            return x_;
        }

        // 预测
        double x_pred = x_;
        double p_pred = p_ + q_;

        // 更新
        double k = p_pred / (p_pred + r_);  // 卡尔曼增益
        x_ = x_pred + k * (measurement - x_pred);
        p_ = (1.0 - k) * p_pred;

        return x_;
    }

    void reset() {
        initialized_ = false;
        p_ = 1000.0;
    }

    bool isInitialized() const { return initialized_; }

private:
    double q_;  // 过程噪声方差
    double r_;  // 测量噪声方差
    double x_;  // 状态估计
    double p_;  // 误差协方差
    bool initialized_;
};

/**
 * @brief 增强型焊道定位器
 *
 * 解决问题:
 * - 焊道中心位置波动 (±2mm)
 * - 边缘干扰和噪声影响
 * - 时序连续性差
 */
class EnhancedWeldLocator {
public:
    EnhancedWeldLocator()
        : kalmanCenter_(0.05, 1.5),  // 过程噪声0.05, 测量噪声1.5mm
          kalmanHeight_(0.1, 0.5) {
        config_.heightThresholdSigma = 1.8;      // 降低高度阈值 - 对低矮焊道更敏感
        config_.minWeldWidth = 30.0;             // 降低最小宽度 30mm - 接受更窄的焊道
        config_.maxWeldWidth = 500.0;            // 增大最大宽度 500mm - 允许更宽范围
        config_.minWeldHeight = 0.2;             // 降低最小高度 0.2mm - 检测微小拱起
        config_.shoulderRatio = 0.15;            // 增大肩部比例 - 更好捕获边缘
        config_.centerWeightThreshold = 0.2;     // 降低权重阈值 - 更容易触发检测
        config_.enableKalman = true;             // 启用卡尔曼滤波
        config_.maxCenterJump = 8.0;             // 增大跳变容限 8mm - 适应边缘位置
    }

    /**
     * @brief 定位焊道
     * @param points 点云数据(SDK坐标系)
     * @return 定位结果
     */
    WeldLocationResult locate(const QVector<QVector3D>& points) {
        WeldLocationResult result;

        // 1. 数据预处理 - 筛选有效点
        std::vector<int> validIndices;
        std::vector<double> xCoords, zCoords;
        if (!preprocessPoints(points, validIndices, xCoords, zCoords)) {
            result.detectionMethod = "preprocessing_failed";
            return result;
        }

        // 2. 估计基准面
        double baselineSlope, baselineOffset, noiseLevel;
        estimateBaseline(xCoords, zCoords, baselineSlope, baselineOffset, noiseLevel);
        result.baselineSlope = baselineSlope;
        result.baselineOffset = baselineOffset;
        result.noiseLevel = noiseLevel;

        // 3. 计算高度残差
        std::vector<double> residuals;
        for (size_t i = 0; i < xCoords.size(); ++i) {
            double expectedZ = baselineSlope * xCoords[i] + baselineOffset;
            residuals.push_back(zCoords[i] - expectedZ);
        }

        // 4. 多尺度高度分析 - 识别焊道区域
        std::vector<bool> weldMask;
        double adaptiveThreshold = detectWeldRegion(residuals, noiseLevel, weldMask);

        // 5. 提取焊道边界
        int startIdx, endIdx;
        if (!extractWeldBoundary(weldMask, xCoords, residuals,
                                startIdx, endIdx)) {
            result.detectionMethod = "no_valid_weld_region";
            return result;
        }

        // 6. 精确中心定位 - 鲁棒估计
        double centerX = computeRobustCenter(xCoords, residuals, startIdx, endIdx,
                                            adaptiveThreshold);

        // 7. 时序滤波
        if (config_.enableKalman) {
            // 跳变检测
            if (kalmanCenter_.isInitialized()) {
                double lastCenter = kalmanCenter_.update(centerX);
                if (std::abs(centerX - lastCenter) > config_.maxCenterJump) {
                    // 可能的跳变,重新初始化
                    kalmanCenter_.reset();
                }
            }
            centerX = kalmanCenter_.update(centerX);
        }

        // 8. 计算焊道特征
        double weldWidth = xCoords[endIdx] - xCoords[startIdx];
        double maxHeight = *std::max_element(
            residuals.begin() + startIdx, residuals.begin() + endIdx + 1);
        double centerZ = baselineSlope * centerX + baselineOffset + maxHeight * 0.7;

        // 9. 计算置信度
        double confidence = computeConfidence(residuals, startIdx, endIdx,
                                             noiseLevel, adaptiveThreshold);

        // 填充结果
        result.valid = true;
        result.centerX = centerX;
        result.centerZ = centerZ;
        result.startIndex = validIndices[startIdx];
        result.endIndex = validIndices[endIdx];
        result.weldWidth = weldWidth;
        result.weldHeight = maxHeight;
        result.confidence = confidence;
        result.detectionMethod = "enhanced_multi_scale";

        return result;
    }

    /**
     * @brief 重置滤波器状态
     */
    void reset() {
        kalmanCenter_.reset();
        kalmanHeight_.reset();
    }

    // 配置参数
    struct Config {
        double heightThresholdSigma;
        double minWeldWidth;
        double maxWeldWidth;
        double minWeldHeight;
        double shoulderRatio;
        double centerWeightThreshold;
        bool enableKalman;
        double maxCenterJump;
    } config_;

private:
    KalmanFilter1D kalmanCenter_;
    KalmanFilter1D kalmanHeight_;

    /**
     * @brief 预处理点云数据
     */
    bool preprocessPoints(const QVector<QVector3D>& points,
                         std::vector<int>& validIndices,
                         std::vector<double>& xCoords,
                         std::vector<double>& zCoords) {
        validIndices.clear();
        xCoords.clear();
        zCoords.clear();

        for (int i = 0; i < points.size(); ++i) {
            const auto& p = points[i];
            if (!std::isfinite(p.x()) || !std::isfinite(p.z()) || p.z() <= 0.0) {
                continue;
            }

            // 检查X单调性 - 处理SDK数据中的跳变
            if (!validIndices.empty()) {
                double lastX = xCoords.back();
                if (p.x() <= lastX) {
                    // X坐标回退,可能是新一帧开始
                    if (lastX - p.x() > 100.0) break;
                    continue;
                }
            }

            validIndices.push_back(i);
            xCoords.push_back(p.x());
            zCoords.push_back(p.z());
        }

        return validIndices.size() >= 32;
    }

    /**
     * @brief 估计基准面参数 - 鲁棒双边拟合
     */
    void estimateBaseline(const std::vector<double>& xCoords,
                         const std::vector<double>& zCoords,
                         double& slope, double& offset, double& noise) {
        const int n = static_cast<int>(xCoords.size());
        const int edgeCount = std::max(8, n / 10);  // 两端各10%

        // 计算两端中位数
        auto median = [](std::vector<double> v) {
            if (v.empty()) return 0.0;
            auto mid = v.begin() + v.size() / 2;
            std::nth_element(v.begin(), mid, v.end());
            return *mid;
        };

        std::vector<double> leftX, leftZ, rightX, rightZ;
        for (int i = 0; i < edgeCount; ++i) {
            leftX.push_back(xCoords[i]);
            leftZ.push_back(zCoords[i]);
            rightX.push_back(xCoords[n - 1 - i]);
            rightZ.push_back(zCoords[n - 1 - i]);
        }

        double lx = median(leftX), lz = median(leftZ);
        double rx = median(rightX), rz = median(rightZ);

        slope = (rz - lz) / (rx - lx + 1e-6);
        offset = lz - slope * lx;

        // 估计噪声水平 - MAD方法
        std::vector<double> errors;
        for (size_t i = 0; i < xCoords.size(); ++i) {
            double expected = slope * xCoords[i] + offset;
            errors.push_back(std::abs(zCoords[i] - expected));
        }
        noise = 1.4826 * median(errors);  // MAD到标准差转换
    }

    /**
     * @brief 多尺度焊道区域检测
     *
     * 使用自适应高度阈值识别焊道区域
     */
    double detectWeldRegion(const std::vector<double>& residuals,
                           double noiseLevel,
                           std::vector<bool>& weldMask) {
        const int n = static_cast<int>(residuals.size());
        weldMask.assign(n, false);

        // 计算自适应阈值 - 基于整体高度分布
        std::vector<double> positiveResiduals;
        for (double r : residuals) {
            if (r > 0.0) positiveResiduals.push_back(r);
        }

        if (positiveResiduals.empty()) {
            return config_.heightThresholdSigma * noiseLevel;
        }

        std::sort(positiveResiduals.begin(), positiveResiduals.end());
        double p75 = positiveResiduals[positiveResiduals.size() * 3 / 4];
        double p95 = positiveResiduals[positiveResiduals.size() * 95 / 100];

        // 自适应阈值: 如果有明显焊道,使用较低阈值
        double adaptiveThreshold;
        if (p95 > config_.heightThresholdSigma * noiseLevel) {
            // 有明显焊道,使用中位数方法
            adaptiveThreshold = std::max(
                config_.minWeldHeight,
                p75 * 0.4
            );
        } else {
            // 没有明显焊道,使用噪声阈值
            adaptiveThreshold = config_.heightThresholdSigma * noiseLevel;
        }

        // 标记超过阈值的点
        for (int i = 0; i < n; ++i) {
            weldMask[i] = residuals[i] > adaptiveThreshold;
        }

        return adaptiveThreshold;
    }

    /**
     * @brief 提取焊道边界
     */
    bool extractWeldBoundary(const std::vector<bool>& weldMask,
                            const std::vector<double>& xCoords,
                            const std::vector<double>& residuals,
                            int& startIdx, int& endIdx) {
        const int n = static_cast<int>(weldMask.size());

        // 找到所有连续的焊道候选区域
        std::vector<std::pair<int, int>> candidates;
        int start = -1;
        for (int i = 0; i < n; ++i) {
            if (weldMask[i] && start < 0) {
                start = i;
            } else if (!weldMask[i] && start >= 0) {
                candidates.emplace_back(start, i - 1);
                start = -1;
            }
        }
        if (start >= 0) {
            candidates.emplace_back(start, n - 1);
        }

        if (candidates.empty()) return false;

        // 选择最佳候选 - 基于高度和宽度
        int bestIdx = -1;
        double bestScore = 0.0;

        for (size_t i = 0; i < candidates.size(); ++i) {
            int s = candidates[i].first;
            int e = candidates[i].second;
            double width = xCoords[e] - xCoords[s];

            // 宽度过滤
            if (width < config_.minWeldWidth || width > config_.maxWeldWidth) {
                continue;
            }

            // 计算平均高度和峰值高度
            double sumHeight = 0.0;
            double maxHeight = 0.0;
            for (int j = s; j <= e; ++j) {
                sumHeight += residuals[j];
                maxHeight = std::max(maxHeight, residuals[j]);
            }
            double avgHeight = sumHeight / (e - s + 1);

            // 综合评分
            double score = avgHeight * std::sqrt(width / 100.0) + maxHeight * 0.5;

            if (score > bestScore) {
                bestScore = score;
                bestIdx = static_cast<int>(i);
            }
        }

        if (bestIdx < 0) return false;

        startIdx = candidates[bestIdx].first;
        endIdx = candidates[bestIdx].second;

        // 扩展到肩部
        expandToShoulders(residuals, startIdx, endIdx);

        return true;
    }

    /**
     * @brief 扩展焊道边界到肩部
     */
    void expandToShoulders(const std::vector<double>& residuals,
                          int& startIdx, int& endIdx) {
        const int n = static_cast<int>(residuals.size());
        int currentWidth = endIdx - startIdx + 1;
        int maxExpansion = static_cast<int>(currentWidth * config_.shoulderRatio);

        // 向左扩展
        for (int i = 1; i <= maxExpansion && startIdx - i >= 0; ++i) {
            if (residuals[startIdx - i] > 0.0) {
                startIdx = startIdx - i;
            } else {
                break;
            }
        }

        // 向右扩展
        for (int i = 1; i <= maxExpansion && endIdx + i < n; ++i) {
            if (residuals[endIdx + i] > 0.0) {
                endIdx = endIdx + i;
            } else {
                break;
            }
        }
    }

    /**
     * @brief 鲁棒中心估计 - 高度加权质心
     *
     * 使用抗离群值的加权方法计算焊道中心
     */
    double computeRobustCenter(const std::vector<double>& xCoords,
                              const std::vector<double>& residuals,
                              int startIdx, int endIdx,
                              double threshold) {
        // 找到峰值高度
        double maxHeight = 0.0;
        int peakIdx = startIdx;
        for (int i = startIdx; i <= endIdx; ++i) {
            if (residuals[i] > maxHeight) {
                maxHeight = residuals[i];
                peakIdx = i;
            }
        }

        // 计算高度加权质心
        // 权重函数: w(h) = (h - floor)^2, floor = max(threshold, maxHeight * 0.2)
        double floor = std::max(threshold, maxHeight * config_.centerWeightThreshold);

        double weightedX = 0.0;
        double totalWeight = 0.0;

        for (int i = startIdx; i <= endIdx; ++i) {
            double height = residuals[i];
            if (height > floor) {
                double weight = (height - floor) * (height - floor);  // 二次权重
                weightedX += xCoords[i] * weight;
                totalWeight += weight;
            }
        }

        if (totalWeight < 1e-9) {
            // 回退到峰值位置
            return xCoords[peakIdx];
        }

        return weightedX / totalWeight;
    }

    /**
     * @brief 计算定位置信度
     */
    double computeConfidence(const std::vector<double>& residuals,
                            int startIdx, int endIdx,
                            double noiseLevel, double threshold) {
        int count = 0;
        double sumHeight = 0.0;
        double maxHeight = 0.0;

        for (int i = startIdx; i <= endIdx; ++i) {
            if (residuals[i] > threshold) {
                ++count;
                sumHeight += residuals[i];
                maxHeight = std::max(maxHeight, residuals[i]);
            }
        }

        if (count == 0) return 0.0;

        int totalWidth = endIdx - startIdx + 1;
        double coverage = double(count) / totalWidth;
        double avgHeight = sumHeight / count;
        double heightRatio = avgHeight / (noiseLevel + 1e-6);

        // 综合置信度
        double confidence = 0.3 * coverage +
                           0.4 * std::min(1.0, heightRatio / 5.0) +
                           0.3 * std::min(1.0, maxHeight / 5.0);

        return std::clamp(confidence, 0.0, 1.0);
    }
};

} // namespace crawling
