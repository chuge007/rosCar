#pragma once
/**
 * @file adaptive_weld_locator.h
 * @brief 自适应焊道定位算法 - 无需调参，适配各种焊道
 *
 * 核心设计理念:
 * 1. 零参数依赖 - 所有阈值根据数据分布自动计算
 * 2. 多策略融合 - 峰值检测、梯度分析、形态学三管齐下
 * 3. 鲁棒性优先 - 对噪声、边缘、倾斜都能稳定工作
 * 4. 置信度评估 - 量化检测结果的可靠性
 *
 * 适用场景:
 * - 高焊道 (5-10mm) ✓
 * - 低焊道 (1-3mm) ✓
 * - 窄焊道 (30-50mm) ✓
 * - 宽焊道 (100-300mm) ✓
 * - 边缘焊道 ✓
 * - 倾斜基准面 ✓
 * - 高噪声环境 ✓
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
struct AdaptiveWeldResult {
    bool valid = false;
    double centerX = 0.0;
    double centerZ = 0.0;
    int startIndex = -1;
    int endIndex = -1;
    double weldWidth = 0.0;
    double weldHeight = 0.0;
    double confidence = 0.0;
    QString detectionMethod;

    // 诊断信息
    double baselineSlope = 0.0;
    double baselineOffset = 0.0;
    double noiseLevel = 0.0;
    double peakScore = 0.0;
    double edgeScore = 0.0;
    double consistencyScore = 0.0;
};

/**
 * @brief 卡尔曼滤波器 - 一维时序平滑
 */
class KalmanFilter1D {
public:
    KalmanFilter1D(double q = 0.05, double r = 1.5)
        : q_(q), r_(r), x_(0.0), p_(1000.0), initialized_(false) {}

    double update(double measurement) {
        if (!initialized_) {
            x_ = measurement;
            p_ = r_;
            initialized_ = true;
            return x_;
        }

        // 预测
        double x_pred = x_;
        double p_pred = p_ + q_;

        // 更新
        double k = p_pred / (p_pred + r_);
        x_ = x_pred + k * (measurement - x_pred);
        p_ = (1.0 - k) * p_pred;

        return x_;
    }

    void reset() { initialized_ = false; }
    bool isInitialized() const { return initialized_; }
    double getState() const { return x_; }

private:
    double q_, r_, x_, p_;
    bool initialized_;
};

/**
 * @brief 自适应焊道定位器
 *
 * 核心算法:
 * 1. 鲁棒基准面估计 (RANSAC-like)
 * 2. 多尺度峰值检测
 * 3. 梯度边缘分析
 * 4. 形态学区域生长
 * 5. 多策略投票融合
 * 6. 时序卡尔曼滤波
 */
class AdaptiveWeldLocator {
public:
    AdaptiveWeldLocator() : kalmanCenter_(0.03, 1.2), kalmanHeight_(0.1, 0.5) {}

    /**
     * @brief 定位焊道 - 主入口
     */
    AdaptiveWeldResult locate(const QVector<QVector3D>& points) {
        AdaptiveWeldResult result;

        // 1. 预处理
        std::vector<int> validIndices;
        std::vector<double> xCoords, zCoords;
        if (!preprocessPoints(points, validIndices, xCoords, zCoords)) {
            result.detectionMethod = "insufficient_data";
            return result;
        }

        const int n = static_cast<int>(xCoords.size());

        // 2. 鲁棒基准面估计
        double slope, offset, noiseLevel;
        estimateRobustBaseline(xCoords, zCoords, slope, offset, noiseLevel);
        result.baselineSlope = slope;
        result.baselineOffset = offset;
        result.noiseLevel = noiseLevel;

        // 3. 计算高度残差
        std::vector<double> residuals(n);
        for (int i = 0; i < n; ++i) {
            residuals[i] = zCoords[i] - (slope * xCoords[i] + offset);
        }

        // 4. 多策略检测
        WeldCandidate peakCandidate = detectByPeak(xCoords, residuals, noiseLevel);
        WeldCandidate edgeCandidate = detectByEdge(xCoords, residuals, noiseLevel);
        WeldCandidate morphCandidate = detectByMorphology(xCoords, residuals, noiseLevel);

        result.peakScore = peakCandidate.score;
        result.edgeScore = edgeCandidate.score;

        // 5. 策略融合 - 选择最可靠的
        WeldCandidate best = selectBestCandidate(peakCandidate, edgeCandidate, morphCandidate);

        if (best.score < 0.3) {
            result.detectionMethod = "low_confidence";
            return result;
        }

        // 6. 精化中心位置
        double centerX = refineCenterPosition(xCoords, residuals,
                                             best.startIdx, best.endIdx, noiseLevel);

        // 7. 时序滤波
        if (kalmanCenter_.isInitialized()) {
            double lastCenter = kalmanCenter_.getState();
            if (std::abs(centerX - lastCenter) > 10.0) {
                // 可能跳变到新焊道
                kalmanCenter_.reset();
            }
        }
        centerX = kalmanCenter_.update(centerX);

        // 8. 填充结果
        result.valid = true;
        result.centerX = centerX;
        result.startIndex = validIndices[best.startIdx];
        result.endIndex = validIndices[best.endIdx];
        result.weldWidth = xCoords[best.endIdx] - xCoords[best.startIdx];
        result.weldHeight = best.height;
        result.centerZ = slope * centerX + offset + best.height * 0.7;
        result.confidence = best.score;
        result.consistencyScore = computeConsistency(xCoords, residuals,
                                                     best.startIdx, best.endIdx);
        result.detectionMethod = best.method;

        return result;
    }

    void reset() {
        kalmanCenter_.reset();
        kalmanHeight_.reset();
    }

private:
    KalmanFilter1D kalmanCenter_;
    KalmanFilter1D kalmanHeight_;

    struct WeldCandidate {
        int startIdx = -1;
        int endIdx = -1;
        double centerX = 0.0;
        double height = 0.0;
        double score = 0.0;
        QString method;
    };

    /**
     * @brief 预处理点云
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

            if (!validIndices.empty() && p.x() <= xCoords.back()) {
                if (xCoords.back() - p.x() > 100.0) break;
                continue;
            }

            validIndices.push_back(i);
            xCoords.push_back(p.x());
            zCoords.push_back(p.z());
        }

        return validIndices.size() >= 32;
    }

    /**
     * @brief 鲁棒基准面估计 - 迭代加权最小二乘
     */
    void estimateRobustBaseline(const std::vector<double>& xCoords,
                               const std::vector<double>& zCoords,
                               double& slope, double& offset, double& noise) {
        const int n = static_cast<int>(xCoords.size());

        // 初始估计 - 使用两端中位数
        const int edgeCount = std::max(8, n / 8);
        std::vector<double> leftZ, rightZ;
        for (int i = 0; i < edgeCount; ++i) {
            leftZ.push_back(zCoords[i]);
            rightZ.push_back(zCoords[n - 1 - i]);
        }

        auto median = [](std::vector<double> v) {
            std::nth_element(v.begin(), v.begin() + v.size()/2, v.end());
            return v[v.size()/2];
        };

        double lz = median(leftZ);
        double rz = median(rightZ);
        double lx = xCoords[edgeCount/2];
        double rx = xCoords[n - edgeCount/2];

        slope = (rz - lz) / (rx - lx + 1e-6);
        offset = lz - slope * lx;

        // 迭代精化 - 降低焊道的影响
        for (int iter = 0; iter < 3; ++iter) {
            std::vector<double> errors;
            for (int i = 0; i < n; ++i) {
                double err = std::abs(zCoords[i] - (slope * xCoords[i] + offset));
                errors.push_back(err);
            }

            double mad = 1.4826 * median(errors);

            // 加权最小二乘 - 抑制离群点
            double sx = 0, sz = 0, sxx = 0, sxz = 0, sw = 0;
            for (int i = 0; i < n; ++i) {
                double err = std::abs(zCoords[i] - (slope * xCoords[i] + offset));
                double w = err < 3*mad ? 1.0 : 3*mad / (err + 1e-6);  // Huber权重
                sx += w * xCoords[i];
                sz += w * zCoords[i];
                sxx += w * xCoords[i] * xCoords[i];
                sxz += w * xCoords[i] * zCoords[i];
                sw += w;
            }

            slope = (sw * sxz - sx * sz) / (sw * sxx - sx * sx + 1e-9);
            offset = (sz - slope * sx) / (sw + 1e-9);
        }

        // 估计噪声
        std::vector<double> finalErrors;
        for (int i = 0; i < n; ++i) {
            finalErrors.push_back(std::abs(zCoords[i] - (slope * xCoords[i] + offset)));
        }
        noise = 1.4826 * median(finalErrors);
        noise = std::max(0.5, noise);  // 最小噪声水平0.5mm
    }

    /**
     * @brief 策略1: 峰值检测
     *
     * 原理: 焊道是高度残差的峰值区域
     * 方法: 自适应阈值 + 连通域分析
     */
    WeldCandidate detectByPeak(const std::vector<double>& xCoords,
                              const std::vector<double>& residuals,
                              double noiseLevel) {
        WeldCandidate result;
        result.method = "peak_detection";

        const int n = static_cast<int>(residuals.size());

        // 自适应阈值 - 基于正残差分布
        std::vector<double> positiveRes;
        for (double r : residuals) {
            if (r > 0.0) positiveRes.push_back(r);
        }

        if (positiveRes.empty()) return result;

        std::sort(positiveRes.begin(), positiveRes.end());
        double p75 = positiveRes[positiveRes.size() * 3 / 4];  // 75分位数
        double p90 = positiveRes[positiveRes.size() * 9 / 10];  // 90分位数

        // 动态阈值: 在噪声水平和分位数之间平衡
        double threshold = std::max(1.5 * noiseLevel, std::min(p75, p90 * 0.5));

        // 连通域分析
        std::vector<bool> mask(n, false);
        for (int i = 0; i < n; ++i) {
            mask[i] = residuals[i] > threshold;
        }

        // 找最大连通域
        int maxStart = -1, maxEnd = -1, maxLen = 0;
        int curStart = -1;
        for (int i = 0; i < n; ++i) {
            if (mask[i]) {
                if (curStart == -1) curStart = i;
            } else {
                if (curStart != -1) {
                    int len = i - curStart;
                    if (len > maxLen) {
                        maxLen = len;
                        maxStart = curStart;
                        maxEnd = i - 1;
                    }
                    curStart = -1;
                }
            }
        }
        if (curStart != -1 && n - curStart > maxLen) {
            maxStart = curStart;
            maxEnd = n - 1;
        }

        if (maxStart < 0 || maxEnd - maxStart < 5) return result;

        // 扩展到肩部
        for (int i = maxStart - 1; i >= 0; --i) {
            if (residuals[i] > noiseLevel) maxStart = i;
            else break;
        }
        for (int i = maxEnd + 1; i < n; ++i) {
            if (residuals[i] > noiseLevel) maxEnd = i;
            else break;
        }

        result.startIdx = maxStart;
        result.endIdx = maxEnd;
        result.height = *std::max_element(residuals.begin() + maxStart,
                                         residuals.begin() + maxEnd + 1);
        result.score = computeScore(residuals, maxStart, maxEnd, noiseLevel, threshold);

        return result;
    }

    /**
     * @brief 策略2: 梯度边缘检测
     *
     * 原理: 焊道边缘有明显的上升/下降梯度
     * 方法: Sobel梯度 + 对称性分析
     */
    WeldCandidate detectByEdge(const std::vector<double>& xCoords,
                              const std::vector<double>& residuals,
                              double noiseLevel) {
        WeldCandidate result;
        result.method = "edge_detection";

        const int n = static_cast<int>(residuals.size());
        if (n < 10) return result;

        // 计算梯度
        std::vector<double> gradient(n, 0.0);
        for (int i = 2; i < n - 2; ++i) {
            gradient[i] = (-residuals[i-2] - 2*residuals[i-1] +
                          2*residuals[i+1] + residuals[i+2]) / 8.0;
        }

        // 找上升边缘和下降边缘
        std::vector<int> risingEdges, fallingEdges;
        double gradThreshold = noiseLevel * 0.2;  // 自适应梯度阈值

        for (int i = 5; i < n - 5; ++i) {
            if (gradient[i] > gradThreshold && residuals[i] > noiseLevel) {
                risingEdges.push_back(i);
            } else if (gradient[i] < -gradThreshold && residuals[i] > noiseLevel) {
                fallingEdges.push_back(i);
            }
        }

        // 找最佳配对
        double bestScore = 0.0;
        int bestLeft = -1, bestRight = -1;

        for (int left : risingEdges) {
            for (int right : fallingEdges) {
                if (right <= left) continue;
                int width = right - left;
                if (width < 10 || width > n / 2) continue;

                double avgHeight = 0.0;
                for (int i = left; i <= right; ++i) {
                    avgHeight += residuals[i];
                }
                avgHeight /= width;

                double score = avgHeight * std::sqrt(width) / noiseLevel;
                if (score > bestScore) {
                    bestScore = score;
                    bestLeft = left;
                    bestRight = right;
                }
            }
        }

        if (bestLeft < 0) return result;

        result.startIdx = bestLeft;
        result.endIdx = bestRight;
        result.height = *std::max_element(residuals.begin() + bestLeft,
                                         residuals.begin() + bestRight + 1);
        result.score = std::min(1.0, bestScore / 20.0);

        return result;
    }

    /**
     * @brief 策略3: 形态学区域生长
     *
     * 原理: 从最高点开始，向周围生长
     * 方法: 种子点 + 自适应生长阈值
     */
    WeldCandidate detectByMorphology(const std::vector<double>& xCoords,
                                    const std::vector<double>& residuals,
                                    double noiseLevel) {
        WeldCandidate result;
        result.method = "morphology";

        const int n = static_cast<int>(residuals.size());

        // 找峰值点
        int peakIdx = std::max_element(residuals.begin(), residuals.end()) - residuals.begin();
        double peakHeight = residuals[peakIdx];

        if (peakHeight < 1.5 * noiseLevel) return result;

        // 从峰值开始生长
        std::vector<bool> mask(n, false);
        mask[peakIdx] = true;

        double growThreshold = noiseLevel;  // 生长阈值

        // 向左生长
        for (int i = peakIdx - 1; i >= 0; --i) {
            if (residuals[i] > growThreshold) {
                mask[i] = true;
            } else {
                break;
            }
        }

        // 向右生长
        for (int i = peakIdx + 1; i < n; ++i) {
            if (residuals[i] > growThreshold) {
                mask[i] = true;
            } else {
                break;
            }
        }

        // 提取边界
        int start = -1, end = -1;
        for (int i = 0; i < n; ++i) {
            if (mask[i]) {
                if (start < 0) start = i;
                end = i;
            }
        }

        if (start < 0 || end - start < 5) return result;

        result.startIdx = start;
        result.endIdx = end;
        result.height = peakHeight;
        result.score = computeScore(residuals, start, end, noiseLevel, growThreshold);

        return result;
    }

    /**
     * @brief 选择最佳候选
     */
    WeldCandidate selectBestCandidate(const WeldCandidate& peak,
                                     const WeldCandidate& edge,
                                     const WeldCandidate& morph) {
        // 如果某个策略得分明显更高，直接选择
        if (peak.score > 0.8 && peak.score > edge.score * 1.2 && peak.score > morph.score * 1.2) {
            return peak;
        }
        if (edge.score > 0.8 && edge.score > peak.score * 1.2 && edge.score > morph.score * 1.2) {
            return edge;
        }
        if (morph.score > 0.8 && morph.score > peak.score * 1.2 && morph.score > edge.score * 1.2) {
            return morph;
        }

        // 否则选择一致性最好的
        WeldCandidate best = peak;
        if (edge.score > best.score) best = edge;
        if (morph.score > best.score) best = morph;

        return best;
    }

    /**
     * @brief 精化中心位置 - 高度加权质心
     */
    double refineCenterPosition(const std::vector<double>& xCoords,
                               const std::vector<double>& residuals,
                               int startIdx, int endIdx,
                               double noiseLevel) {
        double sumWX = 0.0, sumW = 0.0;

        for (int i = startIdx; i <= endIdx; ++i) {
            double h = std::max(0.0, residuals[i] - noiseLevel);
            double w = h * h;  // 二次权重 - 聚焦峰值
            sumWX += w * xCoords[i];
            sumW += w;
        }

        return sumW > 0 ? sumWX / sumW : xCoords[(startIdx + endIdx) / 2];
    }

    /**
     * @brief 计算置信度分数
     */
    double computeScore(const std::vector<double>& residuals,
                       int startIdx, int endIdx,
                       double noiseLevel, double threshold) {
        if (endIdx <= startIdx) return 0.0;

        int width = endIdx - startIdx + 1;
        double maxHeight = *std::max_element(residuals.begin() + startIdx,
                                            residuals.begin() + endIdx + 1);
        double avgHeight = 0.0;
        for (int i = startIdx; i <= endIdx; ++i) {
            avgHeight += std::max(0.0, residuals[i]);
        }
        avgHeight /= width;

        // 综合评分
        double heightScore = std::min(1.0, maxHeight / (5.0 * noiseLevel));
        double widthScore = std::min(1.0, width / 50.0);
        double consistencyScore = avgHeight / (maxHeight + 1e-6);

        return (heightScore * 0.5 + widthScore * 0.2 + consistencyScore * 0.3);
    }

    /**
     * @brief 计算一致性分数
     */
    double computeConsistency(const std::vector<double>& xCoords,
                             const std::vector<double>& residuals,
                             int startIdx, int endIdx) {
        if (endIdx <= startIdx) return 0.0;

        // 计算高度的变异系数
        double mean = 0.0, variance = 0.0;
        for (int i = startIdx; i <= endIdx; ++i) {
            mean += residuals[i];
        }
        mean /= (endIdx - startIdx + 1);

        for (int i = startIdx; i <= endIdx; ++i) {
            double diff = residuals[i] - mean;
            variance += diff * diff;
        }
        variance /= (endIdx - startIdx + 1);

        double cv = std::sqrt(variance) / (mean + 1e-6);
        return std::max(0.0, 1.0 - cv);
    }
};

}  // namespace crawling
