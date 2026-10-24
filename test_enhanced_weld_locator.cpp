/**
 * @file test_enhanced_weld_locator.cpp
 * @brief 测试增强型焊道定位算法
 *
 * 使用实际PLY点云文件进行测试
 */

#include "src/enhanced_weld_locator.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QVector>
#include <QVector3D>
#include <cmath>

using namespace crawling;

/**
 * @brief 从PLY文件加载点云
 */
bool loadPLY(const QString& filename, QVector<QVector3D>& points) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开文件:" << filename;
        return false;
    }

    QTextStream in(&file);

    // 解析头部
    int vertexCount = 0;
    bool inHeader = true;
    while (!in.atEnd() && inHeader) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("element vertex")) {
            vertexCount = line.section(' ', 2, 2).toInt();
        }
        if (line == "end_header") {
            inHeader = false;
        }
    }

    if (vertexCount == 0) {
        qWarning() << "无效的PLY文件格式";
        return false;
    }

    // 读取顶点数据
    points.clear();
    points.reserve(vertexCount);

    int loadedCount = 0;
    while (!in.atEnd() && loadedCount < vertexCount) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
        if (parts.size() >= 3) {
            float x = parts[0].toFloat();
            float y = parts[1].toFloat();
            float z = parts[2].toFloat();
            points.append(QVector3D(x, y, z));
            ++loadedCount;
        }
    }

    qInfo() << "成功加载" << points.size() << "个点从" << filename;
    return true;
}

/**
 * @brief 分析点云统计信息
 */
void analyzePointCloud(const QVector<QVector3D>& points) {
    if (points.isEmpty()) return;

    double minX = points[0].x(), maxX = points[0].x();
    double minZ = points[0].z(), maxZ = points[0].z();
    int validCount = 0;

    for (const auto& p : points) {
        if (std::isfinite(p.x()) && std::isfinite(p.z()) && p.z() > 0.0) {
            minX = std::min(minX, double(p.x()));
            maxX = std::max(maxX, double(p.x()));
            minZ = std::min(minZ, double(p.z()));
            maxZ = std::max(maxZ, double(p.z()));
            ++validCount;
        }
    }

    qInfo() << "点云统计:";
    qInfo() << "  总点数:" << points.size();
    qInfo() << "  有效点数:" << validCount;
    qInfo() << "  X范围: [" << minX << "," << maxX << "]";
    qInfo() << "  Z范围: [" << minZ << "," << maxZ << "]";
    qInfo() << "  宽度:" << (maxX - minX) << "mm";
    qInfo() << "  高度变化:" << (maxZ - minZ) << "mm";
}

/**
 * @brief 测试单个PLY文件
 */
void testSingleFile(const QString& filename, EnhancedWeldLocator& locator) {
    qInfo() << "\n========================================";
    qInfo() << "测试文件:" << filename;
    qInfo() << "========================================";

    QVector<QVector3D> points;
    if (!loadPLY(filename, points)) {
        return;
    }

    analyzePointCloud(points);

    // 执行定位
    WeldLocationResult result = locator.locate(points);

    qInfo() << "\n定位结果:";
    qInfo() << "  状态:" << (result.valid ? "成功" : "失败");

    if (result.valid) {
        qInfo() << "  中心X:" << result.centerX << "mm";
        qInfo() << "  中心Z:" << result.centerZ << "mm";
        qInfo() << "  焊道宽度:" << result.weldWidth << "mm";
        qInfo() << "  焊道高度:" << result.weldHeight << "mm";
        qInfo() << "  置信度:" << (result.confidence * 100.0) << "%";
        qInfo() << "  起始索引:" << result.startIndex;
        qInfo() << "  结束索引:" << result.endIndex;
        qInfo() << "  基准斜率:" << result.baselineSlope;
        qInfo() << "  基准偏移:" << result.baselineOffset;
        qInfo() << "  噪声水平:" << result.noiseLevel << "mm";
        qInfo() << "  检测方法:" << result.detectionMethod;
    } else {
        qInfo() << "  失败原因:" << result.detectionMethod;
    }
}

/**
 * @brief 测试多帧连续性
 */
void testContinuity(const QStringList& filenames, EnhancedWeldLocator& locator) {
    qInfo() << "\n========================================";
    qInfo() << "测试连续帧稳定性";
    qInfo() << "========================================";

    QVector<double> centerXValues;
    QVector<double> heightValues;
    QVector<double> confidenceValues;

    for (const QString& filename : filenames) {
        QVector<QVector3D> points;
        if (!loadPLY(filename, points)) {
            continue;
        }

        WeldLocationResult result = locator.locate(points);
        if (result.valid) {
            centerXValues.append(result.centerX);
            heightValues.append(result.weldHeight);
            confidenceValues.append(result.confidence);
        }
    }

    if (centerXValues.size() < 2) {
        qWarning() << "有效帧数不足,无法分析连续性";
        return;
    }

    // 计算统计量
    double sumCenterX = 0.0, sumHeight = 0.0, sumConf = 0.0;
    for (int i = 0; i < centerXValues.size(); ++i) {
        sumCenterX += centerXValues[i];
        sumHeight += heightValues[i];
        sumConf += confidenceValues[i];
    }
    double avgCenterX = sumCenterX / centerXValues.size();
    double avgHeight = sumHeight / heightValues.size();
    double avgConf = sumConf / confidenceValues.size();

    // 计算标准差
    double varCenterX = 0.0, varHeight = 0.0;
    for (int i = 0; i < centerXValues.size(); ++i) {
        varCenterX += (centerXValues[i] - avgCenterX) * (centerXValues[i] - avgCenterX);
        varHeight += (heightValues[i] - avgHeight) * (heightValues[i] - avgHeight);
    }
    double stdCenterX = std::sqrt(varCenterX / centerXValues.size());
    double stdHeight = std::sqrt(varHeight / heightValues.size());

    // 计算最大跳变
    double maxJump = 0.0;
    for (int i = 1; i < centerXValues.size(); ++i) {
        double jump = std::abs(centerXValues[i] - centerXValues[i-1]);
        maxJump = std::max(maxJump, jump);
    }

    qInfo() << "\n连续性分析 (共" << centerXValues.size() << "帧):";
    qInfo() << "  中心X平均值:" << avgCenterX << "mm";
    qInfo() << "  中心X标准差:" << stdCenterX << "mm";
    qInfo() << "  最大跳变:" << maxJump << "mm";
    qInfo() << "  高度平均值:" << avgHeight << "mm";
    qInfo() << "  高度标准差:" << stdHeight << "mm";
    qInfo() << "  平均置信度:" << (avgConf * 100.0) << "%";

    // 输出详细序列
    qInfo() << "\n详细序列:";
    for (int i = 0; i < centerXValues.size(); ++i) {
        qInfo() << QString("  帧%1: centerX=%2mm, height=%3mm, confidence=%4%")
                   .arg(i+1, 2)
                   .arg(centerXValues[i], 8, 'f', 2)
                   .arg(heightValues[i], 6, 'f', 2)
                   .arg(confidenceValues[i] * 100.0, 5, 'f', 1);
    }
}

/**
 * @brief 主测试函数
 */
int main(int argc, char* argv[]) {
    // 创建定位器
    EnhancedWeldLocator locator;

    // 配置参数
    locator.config_.enableKalman = true;
    locator.config_.heightThresholdSigma = 2.5;
    locator.config_.minWeldWidth = 50.0;
    locator.config_.maxWeldWidth = 300.0;
    locator.config_.minWeldHeight = 0.5;

    // 测试文件列表
    QStringList testFiles = {
        "C:/Users/a/AppData/Local/Claude-3p/local-agent-mode-sessions/8535f8b1/00000000/c494089d/uploads/bcaccc3b-b4d3-4c66-a0e9-12ffec9a7f0c-1792723306202_Image_Profile_20261023_09_16_36_972.ply",
        "C:/Users/a/AppData/Local/Claude-3p/local-agent-mode-sessions/8535f8b1/00000000/c494089d/uploads/7f04f954-33a8-4e30-986a-a87872e9981b-1792723306203_Image_Profile_20261023_10_38_48_789.ply",
        "C:/Users/a/AppData/Local/Claude-3p/local-agent-mode-sessions/8535f8b1/00000000/c494089d/uploads/0f4cc165-23f5-4e75-a18a-776613705870-1792723306203_Image_Profile_20261023_10_39_48_175.ply",
        "C:/Users/a/AppData/Local/Claude-3p/local-agent-mode-sessions/8535f8b1/00000000/c494089d/uploads/2c85d691-5d49-4b60-b81c-f5b7ff6986db-1792723306203_Image_Profile_20261023_10_40_27_045.ply"
    };

    // 测试每个文件
    for (const QString& file : testFiles) {
        testSingleFile(file, locator);
        qInfo() << ""; // 空行分隔
    }

    // 重置滤波器,测试连续性
    locator.reset();
    testContinuity(testFiles, locator);

    return 0;
}
