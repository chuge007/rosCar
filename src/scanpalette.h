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

// 双极灰阶（TOFD 默认色表）的唯一实现：0..255 索引中 127.5 ≈ 零信号，
// 负半周压黑、正半周提白。解码端与 B/E 扫着色必须共用同一约定，
// 否则索引含义一旦分叉，E 扫就会退化成「只按幅值上色」。
inline QRgb tofdGrayRgb(double signedValue, double magnitude)
{
    constexpr double zeroGray = 200.0;
    const double gray = signedValue < 0.0
        ? zeroGray * (1.0 - magnitude)
        : zeroGray + (255.0 - zeroGray) * magnitude;
    const int g = qBound(0, qRound(gray), 255);
    return QColor(g, g, g).rgb();
}

inline QRgb tofdGrayRgb(int index)
{
    const double signedValue = (double(index) - 127.5) / 127.5;
    return tofdGrayRgb(signedValue, qAbs(signedValue));
}
}
