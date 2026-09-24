#include "scanwidgets.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cstring>
#include <limits>

namespace {
// C 扫热力图：0 幅值为深蓝（该格有数据但无回波），高幅值为亮红。
QColor amplitudeColor(double normalized)
{
    normalized = qBound(0.0, normalized, 1.0);
    const double hue = (1.0 - normalized) * 0.66;
    return QColor::fromHsvF(float(hue), 0.96f,
                            float(0.72 + normalized * 0.28));
}

// B/E 扫成像：0 幅值为白色（无回波 = 空白），高幅值为热力图色。
QColor waveColor(double normalized)
{
    normalized = qBound(0.0, normalized, 1.0);
    const double hue = (1.0 - normalized) * 0.66;
    return QColor::fromHsvF(float(hue),
                            float(normalized > 0.02 ? 0.92 : 0.0),
                            float(normalized > 0.02 ? 0.95 : 1.0));
}

QRectF plotRect(const QWidget *w)
{
    return QRectF(62, 22, qMax(1, w->width() - 82), qMax(1, w->height() - 68));
}
}

AScanWidget::AScanWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    // paintEvent covers the full widget; suppress Qt's separate background
    // erase to avoid a second full-surface paint and visible flicker.
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void AScanWidget::setWaveform(const QVector<qint16> &samples, double scale,
                              double rangeStart, double rangeEnd)
{
    const bool axisChanged = !qFuzzyCompare(m_rangeStart + 1.0, rangeStart + 1.0)
                             || !qFuzzyCompare(m_rangeEnd + 1.0, rangeEnd + 1.0);
    m_samples = samples;
    m_scale = qMax(1.0, scale);
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;

    const QRectF pr = plotRect(this);
    qreal minY = pr.bottom();
    qreal maxY = pr.bottom();
    for (qint16 sample : m_samples) {
        const double percent = qBound(-1.0, double(sample) / m_scale, 1.0);
        const qreal y = m_bipolar ? pr.center().y() - percent * pr.height() / 2.0
                                  : pr.bottom() - qMax(0.0, percent) * pr.height();
        minY = qMin(minY, y);
        maxY = qMax(maxY, y);
    }
    const QRect nextWaveformRect = QRectF(pr.left() - 3.0, minY - 3.0,
                                          pr.width() + 6.0,
                                          qMax<qreal>(6.0, maxY - minY + 6.0))
                                         .toAlignedRect().intersected(rect());
    if (axisChanged) {
        m_staticBackground = {};
        m_waveformDirtyRect = nextWaveformRect;
        update();
    } else {
        const QRect dirty = m_waveformDirtyRect.isNull()
                                ? nextWaveformRect
                                : m_waveformDirtyRect.united(nextWaveformRect);
        m_waveformDirtyRect = nextWaveformRect;
        update(dirty);
    }
}

void AScanWidget::setGates(const QVector<QVector<double>> &gates)
{
    m_gates = gates;
    update();
}

void AScanWidget::setBipolar(bool bipolar)
{
    if (m_bipolar == bipolar)
        return;
    m_bipolar = bipolar;
    m_staticBackground = {};
    m_waveformDirtyRect = {};
    update();
}

void AScanWidget::paintEvent(QPaintEvent *)
{
    ++m_paintCount;
    QPainter p(this);
    const QRectF pr = plotRect(this);

    if (m_staticBackground.isNull() || m_staticBackground.size() != size()) {
        m_staticBackground = QPixmap(size());
        m_staticBackground.fill(QColor("#ffffff"));
        QPainter background(&m_staticBackground);
        background.setPen(QColor("#d9e0e7"));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = pr.top() + pr.height() * i / 4.0;
            background.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
            background.setPen(QColor("#52606d"));
            background.drawText(QRectF(4, y - 10, 52, 20),
                                Qt::AlignRight | Qt::AlignVCenter,
                                QString::number(m_bipolar ? 100 - i * 50
                                                          : 100 - i * 25) + "%");
            background.setPen(QColor("#d9e0e7"));
        }
        for (int i = 0; i <= 4; ++i) {
            const qreal x = pr.left() + pr.width() * i / 4.0;
            background.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
            const double value = m_rangeStart
                                 + (m_rangeEnd - m_rangeStart) * i / 4.0;
            background.setPen(QColor("#52606d"));
            background.drawText(QRectF(x - 35, pr.bottom() + 6, 70, 22),
                                Qt::AlignHCenter,
                                QString::number(value, 'f', 1) + " mm");
            background.setPen(QColor("#d9e0e7"));
        }
        background.setPen(QPen(QColor("#22313f"), 1.2));
        background.drawRect(pr);
    }
    p.drawPixmap(0, 0, m_staticBackground);

    const QColor gateColors[] = { QColor("#e53935"), QColor("#fb8c00"),
                                  QColor("#8e24aa"), QColor("#43a047") };
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    for (int i = 0; i < m_gates.size() && i < 4; ++i) {
        if (m_gates[i].size() < 4 || m_gates[i][0] < 0.5)
            continue;
        const double start = (m_gates[i][1] - m_rangeStart) / span;
        const double end = (m_gates[i][2] - m_rangeStart) / span;
        const double threshold = m_gates[i][3] / 100.0;
        const qreal gateY = m_bipolar ? pr.center().y() - threshold * pr.height() / 2.0
                                      : pr.bottom() - threshold * pr.height();
        p.setPen(QPen(gateColors[i], 2));
        p.drawLine(QPointF(pr.left() + start * pr.width(), gateY),
                   QPointF(pr.left() + end * pr.width(), gateY));
    }

    if (m_samples.size() < 2)
        return;
    QPainterPath path;
    auto appendSample = [&](int i, bool move) {
        const qreal x = pr.left() + pr.width() * i / qMax(1, m_samples.size() - 1);
        const double percent = qBound(-1.0, double(m_samples[i]) / m_scale, 1.0);
        const qreal y = m_bipolar ? pr.center().y() - percent * pr.height() / 2.0
                                  : pr.bottom() - qMax(0.0, percent) * pr.height();
        if (move) path.moveTo(x, y); else path.lineTo(x, y);
    };

    const int sampleCount = m_samples.size();
    const int columns = qMax(1, int(pr.width()));
    if (sampleCount <= columns * 2) {
        for (int i = 0; i < sampleCount; ++i)
            appendSample(i, i == 0);
    } else {
        // Min/max envelope per physical screen column.  Simple stride
        // decimation can erase a one-sample ultrasonic echo; preserving both
        // extrema keeps narrow positive and RF-negative peaks visible while
        // bounding the painted path to roughly twice the viewport width.
        bool firstPoint = true;
        for (int column = 0; column < columns; ++column) {
            const int begin = int((qint64(column) * sampleCount) / columns);
            const int end = int((qint64(column + 1) * sampleCount) / columns);
            if (end <= begin)
                continue;
            int minIndex = begin;
            int maxIndex = begin;
            for (int i = begin + 1; i < end; ++i) {
                if (m_samples[i] < m_samples[minIndex]) minIndex = i;
                if (m_samples[i] > m_samples[maxIndex]) maxIndex = i;
            }
            const int first = qMin(minIndex, maxIndex);
            const int second = qMax(minIndex, maxIndex);
            appendSample(first, firstPoint);
            firstPoint = false;
            if (second != first)
                appendSample(second, false);
        }
    }
    p.setClipRect(pr);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#0788d1"), 1.4));
    p.drawPath(path);
}

EScanWidget::EScanWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
}

void EScanWidget::setFrame(const DecodedFrame &frame, double scale)
{
    if (!frame.eScanImage.isNull()) {
        m_image = frame.eScanImage;
        update();
        return;
    }
    if (frame.beams.isEmpty() || frame.beams.first().isEmpty())
        return;
    const int beams = frame.beams.size();
    const int points = frame.beams.first().size();
    QImage image(beams, points, QImage::Format_RGB32);
    for (int y = 0; y < points; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < beams; ++x) {
            const double n = qAbs(double(frame.beams[x][y])) / qMax(1.0, scale);
            // E 扫：0 信号显示白色，不是 C 扫热力图的深蓝。
            line[x] = waveColor(n).rgb();
        }
    }
    m_image = image;
    update();
}

void EScanWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (m_image.isNull()) {
        p.setPen(QColor("#667788"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待 E 扫数据"));
        return;
    }
    p.drawImage(rect().adjusted(40, 20, -20, -40), m_image);
}

BScanWidget::BScanWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void BScanWidget::setEncoderPrecision(int countsPerColumn)
{
    countsPerColumn = qMax(1, countsPerColumn);
    if (m_encoderPrecision == countsPerColumn)
        return;
    m_encoderPrecision = countsPerColumn;
    clear();
}

void BScanWidget::setRange(double rangeStart, double rangeEnd)
{
    if (qFuzzyCompare(m_rangeStart + 1.0, rangeStart + 1.0)
        && qFuzzyCompare(m_rangeEnd + 1.0, rangeEnd + 1.0))
        return;
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;
    update();
}

void BScanWidget::initializeImage(int sampleCount)
{
    // A very large acquisition point count must not create an unbounded B-scan
    // surface.  Up to 1024 depth pixels are retained; each pixel takes the peak
    // of its source sample interval so narrow echoes are not lost.
    const int imageHeight = qBound(1, sampleCount, 1024);
    m_image = QImage(1200, imageHeight, QImage::Format_Indexed8);
    QVector<QRgb> palette(256);
    for (int i = 0; i < palette.size(); ++i)
        // B 扫：0 信号显示白色，不是 C 扫热力图的深蓝。
        palette[i] = waveColor(double(i) / 255.0).rgb();
    m_image.setColorTable(palette);
    m_image.fill(0);
    m_lastColumn = -1;
    m_usedColumns = 0;
}

bool BScanWidget::ensureImageWidth(int requiredColumn)
{
    static constexpr int MaximumStoredColumns = 65536;
    if (requiredColumn < m_image.width())
        return true;
    if (requiredColumn >= MaximumStoredColumns)
        return false;

    const int newWidth = qMin(MaximumStoredColumns,
                              ((requiredColumn + 256) / 256) * 256);
    QImage expanded(newWidth, m_image.height(), QImage::Format_Indexed8);
    expanded.setColorTable(m_image.colorTable());
    expanded.fill(0);
    for (int y = 0; y < m_image.height(); ++y)
        std::memcpy(expanded.scanLine(y), m_image.constScanLine(y),
                    size_t(m_image.width()));
    m_image = std::move(expanded);
    return true;
}

void BScanWidget::appendFrame(const DecodedFrame &frame, double scale, int sourceBeam)
{
    if (appendFrameInternal(frame, scale, sourceBeam))
        update();
}

void BScanWidget::appendFrames(const QVector<DecodedFrame> &frames, double scale,
                               int sourceBeam)
{
    bool changed = false;
    for (const DecodedFrame &frame : frames)
        changed = appendFrameInternal(frame, scale, sourceBeam) || changed;
    if (changed)
        update();
}

bool BScanWidget::appendFrameInternal(const DecodedFrame &frame, double scale,
                                      int sourceBeam)
{
    if (frame.beams.isEmpty())
        return false;
    const int beamIndex = frame.selectedSourceBeam >= 0
                              ? 0
                              : qBound(0, sourceBeam, frame.beams.size() - 1);
    const QVector<qint16> &samples = frame.beams[beamIndex];
    if (samples.isEmpty())
        return false;
    const int expectedHeight = qBound(1, samples.size(), 1024);
    if (m_image.isNull() || m_image.height() != expectedHeight)
        initializeImage(samples.size());

    const qint64 encoder = frame.tail.encoder[0];
    m_currentEncoder = encoder;
    if (!m_hasOrigin) {
        m_originEncoder = encoder;
        m_hasOrigin = true;
    }

    const qint64 rawDelta = encoder - m_originEncoder;
    if (m_direction == 0 && rawDelta != 0)
        m_direction = rawDelta > 0 ? 1 : -1;
    const int direction = m_direction == 0 ? 1 : m_direction;
    const qint64 directedCounts = rawDelta * direction;
    // If the encoder first moves opposite to the established scan direction,
    // it is outside this scan's origin. Returning toward an existing position
    // still updates that position normally.
    if (directedCounts < 0)
        return false;

    const qint64 absoluteColumn = directedCounts / m_encoderPrecision;
    if (absoluteColumn < 0 || absoluteColumn > std::numeric_limits<int>::max())
        return false;
    const int column = int(absoluteColumn);
    if (!ensureImageWidth(column))
        return false;

    const int outputHeight = m_image.height();
    const double denominator = qMax(1.0, scale);
    bool changed = false;
    for (int y = 0; y < outputHeight; ++y) {
        const int begin = int((qint64(y) * samples.size()) / outputHeight);
        const int end = qMax(begin + 1,
                             int((qint64(y + 1) * samples.size()) / outputHeight));
        int peak = 0;
        for (int i = begin; i < end && i < samples.size(); ++i)
            peak = qMax(peak, qAbs(int(samples[i])));
        const int intensity = qBound(0, int(peak * 255.0 / denominator), 255);
        uchar *line = m_image.scanLine(y);
        if (intensity <= line[column])
            continue;
        line[column] = uchar(intensity);
        changed = true;
    }
    m_lastColumn = column;
    m_usedColumns = qMax(m_usedColumns, column + 1);
    return changed;
}

void BScanWidget::clear()
{
    m_image = {};
    m_direction = 0;
    m_lastColumn = -1;
    m_usedColumns = 0;
    m_originEncoder = 0;
    m_currentEncoder = 0;
    m_hasOrigin = false;
    update();
}

void BScanWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (m_image.isNull()) {
        p.setPen(QColor("#667788"));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("启动 B 扫后，转动编码器 A 开始成像"));
        return;
    }
    const QRect target = rect().adjusted(72, 32, -22, -54);
    // Always fit the complete acquired path (origin through newest position)
    // into the viewport.  The origin never scrolls away; as the path grows,
    // all existing columns are proportionally compressed.
    const int displayColumns = qMax(2, m_usedColumns);
    p.drawImage(target, m_image,
                QRect(0, 0, qMin(displayColumns, m_image.width()), m_image.height()));

    p.setPen(QPen(QColor("#22313f"), 1.2));
    p.drawRect(target);
    p.setPen(QColor("#52606d"));
    const int direction = m_direction == 0 ? 1 : m_direction;
    for (int i = 0; i <= 4; ++i) {
        const qreal x = target.left() + target.width() * i / 4.0;
        const qint64 logical = qint64(displayColumns - 1) * i / 4;
        const qint64 count = direction * logical * qint64(m_encoderPrecision);
        p.drawLine(QPointF(x, target.bottom()), QPointF(x, target.bottom() + 5));
        p.drawText(QRectF(x - 55, target.bottom() + 7, 110, 20),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(count));

        const qreal y = target.top() + target.height() * i / 4.0;
        const double depth = m_rangeStart
                             + (m_rangeEnd - m_rangeStart) * i / 4.0;
        p.drawLine(QPointF(target.left() - 5, y), QPointF(target.left(), y));
        p.drawText(QRectF(2, y - 10, 64, 20), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(depth, 'f', 1));
    }
    p.drawText(QRectF(target.left(), 4, target.width(), 24),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("B 扫全路径　起点 A：%1　当前 A：%2　精度：%3 计数/格")
                   .arg(m_originEncoder).arg(m_currentEncoder)
                   .arg(m_encoderPrecision));
    p.drawText(QRectF(2, target.top() - 4, 66, 20),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("深度"));
    p.drawText(QRectF(target.left(), target.bottom() + 30, target.width(), 20),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("路径（相对起点计数）"));

    if (m_lastColumn >= 0) {
        const qreal x = target.left()
                        + target.width() * m_lastColumn / qMax(1, displayColumns - 1);
        p.setPen(QPen(QColor(255, 193, 7, 180), 1));
        p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
    }
}

CScanWidget::CScanWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void CScanWidget::setEncoderPrecision(int countsPerCell)
{
    countsPerCell = qMax(1, countsPerCell);
    if (m_encoderPrecision == countsPerCell)
        return;
    m_encoderPrecision = countsPerCell;
    clear();
}

void CScanWidget::appendFrame(const DecodedFrame &frame, double scale,
                              int sourceBeam)
{
    if (appendFrameInternal(frame, scale, sourceBeam))
        update();
}

void CScanWidget::appendFrames(const QVector<DecodedFrame> &frames, double scale,
                               int sourceBeam)
{
    bool changed = false;
    for (const DecodedFrame &frame : frames)
        changed = appendFrameInternal(frame, scale, sourceBeam) || changed;
    if (changed)
        update();
}

bool CScanWidget::appendFrameInternal(const DecodedFrame &frame, double scale,
                                      int sourceBeam)
{
    if (frame.beams.isEmpty() && frame.measurements.isEmpty())
        return false;

    m_currentEncoderA = frame.tail.encoder[0];
    m_currentEncoderB = frame.tail.encoder[1];
    if (!m_hasOrigin) {
        m_originEncoderA = m_currentEncoderA;
        m_originEncoderB = m_currentEncoderB;
        m_hasOrigin = true;
    }

    auto cellForDelta = [this](qint64 delta, bool *ok) {
        const qint64 cell = delta >= 0
                                ? delta / m_encoderPrecision
                                : -((-delta + m_encoderPrecision - 1)
                                    / m_encoderPrecision);
        if (cell < std::numeric_limits<int>::min()
            || cell > std::numeric_limits<int>::max()) {
            *ok = false;
            return 0;
        }
        return int(cell);
    };
    bool validCell = true;
    const int x = cellForDelta(m_currentEncoderA - m_originEncoderA, &validCell);
    const int y = cellForDelta(m_currentEncoderB - m_originEncoderB, &validCell);
    if (!validCell)
        return false;

    const int beamIndex = frame.selectedSourceBeam >= 0
                              ? 0
                              : qBound(0, sourceBeam,
                                       qMax(frame.beams.size(),
                                            frame.measurements.size()) - 1);
    int amplitude = 0;
    if (beamIndex >= 0 && beamIndex < frame.measurements.size())
        amplitude = int(frame.measurements[beamIndex].amplitudeA);
    if (amplitude <= 0 && beamIndex >= 0 && beamIndex < frame.beams.size()) {
        for (qint16 sample : frame.beams[beamIndex])
            amplitude = qMax(amplitude, qAbs(int(sample)));
    }
    const uchar intensity = uchar(qBound(0, int(amplitude * 255.0
                                                 / qMax(1.0, scale)), 255));
    const QPoint cell(x, y);
    const auto existing = m_cells.constFind(cell);
    if (existing != m_cells.constEnd() && *existing >= intensity)
        return false;
    // Keep the complete path while bounding pathological memory growth caused
    // by an uncalibrated or corrupt encoder stream.
    if (existing == m_cells.constEnd() && m_cells.size() >= 262144)
        return false;
    m_cells.insert(cell, intensity);
    if (m_cells.size() == 1) {
        m_minX = m_maxX = x;
        m_minY = m_maxY = y;
    } else {
        m_minX = qMin(m_minX, x);
        m_maxX = qMax(m_maxX, x);
        m_minY = qMin(m_minY, y);
        m_maxY = qMax(m_maxY, y);
    }
    return true;
}

void CScanWidget::clear()
{
    m_cells.clear();
    m_originEncoderA = 0;
    m_originEncoderB = 0;
    m_currentEncoderA = 0;
    m_currentEncoderB = 0;
    m_hasOrigin = false;
    m_minX = m_maxX = 0;
    m_minY = m_maxY = 0;
    update();
}

void CScanWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (m_cells.isEmpty()) {
        p.setPen(QColor("#667788"));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("启动 C 扫后，编码器 A/B 的位置将生成平面路径图"));
        return;
    }

    const QRect available = rect().adjusted(82, 38, -32, -62);
    const bool flatX = m_minX == m_maxX;
    const bool flatY = m_minY == m_maxY;
    const int visibleMinX = flatX ? m_minX - 1 : m_minX;
    const int visibleMaxX = flatX ? m_maxX + 1 : m_maxX;
    const int visibleMinY = flatY ? m_minY : m_minY;
    const int visibleMaxY = flatY ? m_maxY + 1 : m_maxY;
    const double spanX = qMax(1, visibleMaxX - visibleMinX);
    const double spanY = qMax(1, visibleMaxY - visibleMinY);

    // With one encoder the C-scan is necessarily a one-dimensional track, so
    // show it centred across the full plot. Once both axes move, use one common
    // pixels-per-count scale and centre the fitted rectangle. This preserves
    // geometry instead of stretching X and Y independently.
    QRectF target = available;
    if (!flatX && !flatY) {
        const qreal pixelsPerCell = qMin(available.width() / spanX,
                                         available.height() / spanY);
        const QSizeF fitted(qMax<qreal>(1.0, spanX * pixelsPerCell),
                            qMax<qreal>(1.0, spanY * pixelsPerCell));
        target = QRectF(QPointF(available.center().x() - fitted.width() / 2.0,
                                available.center().y() - fitted.height() / 2.0),
                        fitted);
    }

    QImage raster(qMax(1, qRound(target.width())),
                  qMax(1, qRound(target.height())),
                  QImage::Format_RGB32);
    // 未扫过的区域保持白色；已扫到的格子用热力图色（0 幅值 = 深蓝）。
    raster.fill(Qt::white);
    QPainter rp(&raster);
    rp.setRenderHint(QPainter::Antialiasing, false);
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const qreal px = (it.key().x() - visibleMinX) * (raster.width() - 1)
                         / spanX;
        const qreal py = flatY ? (raster.height() - 1) / 2.0
                               : (visibleMaxY - it.key().y())
                                     * (raster.height() - 1) / spanY;
        rp.fillRect(QRectF(px - 2, py - 2, 5, 5),
                    amplitudeColor(double(it.value()) / 255.0));
    }
    rp.end();
    p.drawImage(target, raster);

    // 白色网格线会盖在热力图上方，已按要求移除；仅保留外框与轴标注。
    p.setPen(QPen(QColor("#22313f"), 1.2));
    p.drawRect(target);
    p.setPen(QColor("#52606d"));
    for (int i = 0; i <= 4; ++i) {
        const qreal x = target.left() + target.width() * i / 4.0;
        const qreal y = target.top() + target.height() * i / 4.0;
        const qint64 countA = qRound64((visibleMinX + spanX * i / 4.0)
                                        * m_encoderPrecision);
        const qint64 countB = flatY ? qint64(m_minY) * m_encoderPrecision
                                    : qRound64((visibleMaxY - spanY * i / 4.0)
                                               * m_encoderPrecision);
        p.drawText(QRectF(x - 55, target.bottom() + 7, 110, 20),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(countA));
        if (!flatY || i == 2)
            p.drawText(QRectF(2, y - 10, 74, 20),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(countB));
    }
    p.drawText(QRectF(target.left(), 5, target.width(), 28),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("C 扫完整路径　原点 A/B：%1/%2　当前 A/B：%3/%4　精度：%5 计数/格")
                   .arg(m_originEncoderA).arg(m_originEncoderB)
                   .arg(m_currentEncoderA).arg(m_currentEncoderB)
                   .arg(m_encoderPrecision));
    p.drawText(QRectF(2, target.top() - 4, 76, 22),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("B 相对计数"));
    p.drawText(QRectF(target.left(), target.bottom() + 34,
                      target.width(), 20),
               Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("A 相对计数"));
}
