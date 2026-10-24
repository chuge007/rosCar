#pragma once
/**
 * @file simple_weld_locator.h
 * @brief 极简焊道定位算法 - 抓住本质特征
 *
 * 核心思路：
 * 焊道的本质特征就这三个：
 * 1. 高度突变 - 明显凸起
 * 2. 最高点 - 局部最高
 * 3. 凸起区域 - 连续的高于基准面
 *
 * 算法步骤：
 * 1. 拟合基准面（用两端的点）
 * 2. 找最高点
 * 3. 从最高点向两边扩展，找到下降到基准面的边界
 * 4. 计算中心位置（加权质心）
 * 5. 时序滤波（卡尔曼）
 *
 * 就这么简单！
 */

#include <QVector3D>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <vector>

namespace crawling {

struct SimpleWeldResult {
    bool valid = false;
    double centerX = 0.0;
    double centerZ = 0.0;
    int startIndex = -1;
    int endIndex = -1;
    double weldWidth = 0.0;
    double weldHeight = 0.0;
    double confidence = 0.0;

    // 诊断信息
    double baselineSlope = 0.0;
    double baselineOffset = 0.0;
    double noiseLevel = 0.0;
    int peakIndex = -1;
};

/**
 * @brief 一维卡尔曼滤波器
 */
class SimpleKalman {
public:
    SimpleKalman(double processNoise = 0.05, double measurementNoise = 1.5)
        : q_(processNoise), r_(measurementNoise), x_(0.0), p_(1000.0), init_(false) {}

    double update(double measurement) {
        if (!init_) {
            x_ = measurement;
            p_ = r_;
            init_ = true;
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

    void reset() { init_ = false; }
    bool isInit() const { return init_; }
    double get() const { return x_; }

private:
    double q_, r_, x_, p_;
    bool init_;
};

/**
 * @brief 极简焊道定位器
 *
 * 不依赖复杂参数，只抓住焊道的本质特征
 */
class SimpleWeldLocator {
public:
    SimpleWeldLocator() : kalman_(0.03, 1.2) {}

    /**
     * @brief 定位焊道 - 极简版本
     */
    SimpleWeldResult locate(const QVector<QVector3D>& points) {
        SimpleWeldResult result;

        // 1. 提取有效点
        std::vector<int> indices;
        std::vector<double> xVec, zVec;

        for (int i = 0; i < points.size(); ++i) {
            const auto& p = points[i];
            if (!std::isfinite(p.x()) || !std::isfinite(p.z()) || p.z() <= 0.0) {
                continue;
            }

            // 跳过X坐标回退的点
            if (!indices.empty() && p.x() <= xVec.back()) {
                if (xVec.back() - p.x() > 100.0) break;  // 新一帧开始
                continue;
            }

            indices.push_back(i);
            xVec.push_back(p.x());
            zVec.push_back(p.z());
        }

        int n = static_cast<int>(indices.size());
        if (n < 32) {
            return result;  // 数据太少
        }

        // 2. 估计基准面 - 用两端的中位数
        int edgeCount = std::max(8, n / 10);  // 两端各10%的点

        auto median = [](std::vector<double> v) {
            std::nth_element(v.begin(), v.begin() + v.size()/2, v.end());
            return v[v.size()/2];
        };

        std::vector<double> leftZ, rightZ;
        for (int i = 0; i < edgeCount; ++i) {
            leftZ.push_back(zVec[i]);
            rightZ.push_back(zVec[n - 1 - i]);
        }

        double lx = xVec[edgeCount / 2];
        double rx = xVec[n - edgeCount / 2];
        double lz = median(leftZ);
        double rz = median(rightZ);

        double slope = (rz - lz) / (rx - lx + 1e-6);
        double offset = lz - slope * lx;

        result.baselineSlope = slope;
        result.baselineOffset = offset;

        // 3. 计算高度残差（相对于基准面的凸起）
        std::vector<double> heights(n);
        for (int i = 0; i < n; ++i) {
            heights[i] = zVec[i] - (slope * xVec[i] + offset);
        }

        // 估计噪声水平 - 用边缘区域
        std::vector<double> edgeHeights;
        for (int i = 0; i < edgeCount; ++i) {
            edgeHeights.push_back(std::abs(heights[i]));
            edgeHeights.push_back(std::abs(heights[n - 1 - i]));
        }
        double noise = 1.4826 * median(edgeHeights);
        noise = std::max(0.5, noise);  // 最小噪声0.5mm
        result.noiseLevel = noise;

        // 4. 找最高点 - 这就是焊道的特征！
        int peakIdx = std::max_element(heights.begin(), heights.end()) - heights.begin();
        double peakHeight = heights[peakIdx];

        result.peakIndex = indices[peakIdx];

        // 如果最高点还不如噪声高，那就是没有焊道
        if (peakHeight < 1.5 * noise) {
            return result;
        }

        // 5. 从最高点向两边扩展，找边界
        // 边界定义：高度下降到峰值的30%，或者接近基准面（< 噪声）
        double boundaryThreshold = std::max(noise, peakHeight * 0.3);

        int leftBoundary = peakIdx;
        for (int i = peakIdx - 1; i >= 0; --i) {
            if (heights[i] < boundaryThreshold) {
                break;
            }
            leftBoundary = i;
        }

        int rightBoundary = peakIdx;
        for (int i = peakIdx + 1; i < n; ++i) {
            if (heights[i] < boundaryThreshold) {
                break;
            }
            rightBoundary = i;
        }

        // 6. 计算加权质心 - 高度的二次方作为权重（聚焦峰值）
        double sumWX = 0.0, sumW = 0.0;
        for (int i = leftBoundary; i <= rightBoundary; ++i) {
            double h = std::max(0.0, heights[i] - noise);
            double w = h * h;  // 二次权重
            sumWX += w * xVec[i];
            sumW += w;
        }

        double centerX = sumW > 0 ? sumWX / sumW : xVec[peakIdx];

        // 7. 卡尔曼滤波 - 时序平滑
        if (kalman_.isInit()) {
            double lastCenter = kalman_.get();
            if (std::abs(centerX - lastCenter) > 10.0) {
                // 可能跳到新焊道了
                kalman_.reset();
            }
        }
        centerX = kalman_.update(centerX);

        // 8. 计算置信度
        double widthScore = std::min(1.0, (rightBoundary - leftBoundary) / 50.0);
        double heightScore = std::min(1.0, peakHeight / (5.0 * noise));
        double confidence = (widthScore + heightScore) / 2.0;

        // 9. 填充结果
        result.valid = true;
        result.centerX = centerX;
        result.centerZ = slope * centerX + offset + peakHeight * 0.7;
        result.startIndex = indices[leftBoundary];
        result.endIndex = indices[rightBoundary];
        result.weldWidth = xVec[rightBoundary] - xVec[leftBoundary];
        result.weldHeight = peakHeight;
        result.confidence = confidence;

        return result;
    }

    void reset() {
        kalman_.reset();
    }

private:
    SimpleKalman kalman_;
};

}  // namespace crawling
