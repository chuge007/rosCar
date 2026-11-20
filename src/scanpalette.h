#pragma once

#include <QColor>

// B/E 扫共用的阈值分段着色类型。
namespace ScanPalette {
struct ThresholdColorBand
{
    double lower = 0.0;
    double upper = 1.0;
    QColor color = Qt::black;
};
enum ColorRuleMode {
    ContinuousColorRule = 0,
    ThresholdBandsColorRule = 1
};
enum ThresholdValueMode {
    PercentageThresholdValues = 0,
    AbsoluteThresholdValues = 1
};
}
