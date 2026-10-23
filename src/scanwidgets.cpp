#include "scanwidgets.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QBuffer>
#include <QDataStream>
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
    setMouseTracking(true);
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
    updateSurfaceTracking();

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
    } else if (m_surfaceEnabled) {
        // 表面点和跟踪闸门可能每帧横向移动，需要刷新整个绘图区；静态网格仍复用缓存。
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

void AScanWidget::setActiveGate(int gateIndex)
{
    m_activeGate = gateIndex;
    update();
}

void AScanWidget::beginGatePlacement(int gateIndex)
{
    if (gateIndex < 0 || gateIndex >= m_gates.size())
        return;
    m_activeGate = gateIndex;
    m_placementGate = gateIndex;
    m_placementMode = PlacementMode::Gate;
    m_placementHasStart = false;
    setCursor(Qt::CrossCursor);
    emit interactionHint(QStringLiteral("请在 A 扫图上依次点击闸门左、右边界；右键可取消"));
    update();
}

void AScanWidget::beginSurfaceWindowPlacement()
{
    m_placementMode = PlacementMode::SurfaceWindow;
    m_placementGate = -1;
    m_placementHasStart = false;
    setCursor(Qt::CrossCursor);
    emit interactionHint(QStringLiteral("请在 A 扫图上依次点击表面搜索区域的起点、终点；右键可取消"));
    update();
}

void AScanWidget::cancelPlacement()
{
    m_placementMode = PlacementMode::None;
    m_placementGate = -1;
    m_placementHasStart = false;
    unsetCursor();
    update();
}

void AScanWidget::setSurfaceTracking(bool enabled, double windowStart, double windowEnd,
                                     double thresholdPercent, bool holdMissing,
                                     const QVector<bool> &trackedGates)
{
    const bool windowChanged = !qFuzzyCompare(m_surfaceWindowStart + 1.0, windowStart + 1.0)
                               || !qFuzzyCompare(m_surfaceWindowEnd + 1.0, windowEnd + 1.0)
                               || !qFuzzyCompare(m_surfaceThreshold + 1.0, thresholdPercent + 1.0);
    m_surfaceEnabled = enabled;
    m_surfaceWindowStart = qMin(windowStart, windowEnd);
    m_surfaceWindowEnd = qMax(windowStart, windowEnd);
    m_surfaceThreshold = qBound(0.0, thresholdPercent, 100.0);
    m_surfaceHoldMissing = holdMissing;
    m_surfaceTrackedGates = trackedGates;
    if (!enabled || windowChanged) {
        m_surfaceReferenceValid = false;
        m_surfaceCurrentValid = false;
        m_surfaceLastValid = false;
    }
    updateSurfaceTracking();
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

void AScanWidget::setReviewCursor(double range, bool visible)
{
    m_reviewCursor = range;
    m_reviewCursorVisible = visible;
    update();
}

double AScanWidget::positionToRange(qreal x) const
{
    const QRectF pr = plotRect(this);
    const double ratio = qBound(0.0, (x - pr.left()) / qMax<qreal>(1.0, pr.width()), 1.0);
    return m_rangeStart + ratio * (m_rangeEnd - m_rangeStart);
}

qreal AScanWidget::rangeToPosition(double value) const
{
    const QRectF pr = plotRect(this);
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    return pr.left() + (value - m_rangeStart) / span * pr.width();
}

double AScanWidget::positionToThreshold(qreal y) const
{
    const QRectF pr = plotRect(this);
    const qreal base = m_bipolar ? pr.center().y() : pr.bottom();
    const qreal height = m_bipolar ? pr.height() / 2.0 : pr.height();
    return qBound(0.0, (base - y) / qMax<qreal>(1.0, height) * 100.0, 100.0);
}

qreal AScanWidget::thresholdToPosition(double threshold) const
{
    const QRectF pr = plotRect(this);
    return m_bipolar ? pr.center().y() - threshold / 100.0 * pr.height() / 2.0
                     : pr.bottom() - threshold / 100.0 * pr.height();
}

QVector<double> AScanWidget::displayedGate(int index) const
{
    if (index < 0 || index >= m_gates.size())
        return {};
    QVector<double> gate = m_gates[index];
    if (gate.size() >= 4 && m_surfaceEnabled && m_surfaceCurrentValid
        && m_surfaceReferenceValid && index < m_surfaceTrackedGates.size()
        && m_surfaceTrackedGates[index]) {
        const double delta = m_surfaceCurrent - m_surfaceReference;
        gate[1] += delta;
        gate[2] += delta;
    }
    return gate;
}

void AScanWidget::updateSurfaceTracking()
{
    if (!m_surfaceEnabled || m_samples.isEmpty() || m_surfaceWindowEnd <= m_surfaceWindowStart) {
        m_surfaceCurrentValid = false;
        emit surfaceTrackingUpdated(false, m_surfaceReference, m_surfaceCurrent);
        return;
    }
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    const int count = m_samples.size();
    const double clippedStart = qMax(m_surfaceWindowStart, m_rangeStart);
    const double clippedEnd = qMin(m_surfaceWindowEnd, m_rangeEnd);
    const int first = clippedEnd >= clippedStart
        ? qBound(0, int((clippedStart - m_rangeStart) / span * (count - 1)), count - 1)
        : 1;
    const int last = clippedEnd >= clippedStart
        ? qBound(0, int(qCeil((clippedEnd - m_rangeStart) / span * (count - 1))), count - 1)
        : 0;
    const double trigger = m_scale * m_surfaceThreshold / 100.0;
    bool found = false;
    double current = 0.0;
    if (trigger > 0.0 && last >= first) {
        for (int i = first; i <= last; ++i) {
            if (qAbs(double(m_samples[i])) >= trigger) {
                current = m_rangeStart + span * i / qMax(1, count - 1);
                found = true;
                break;
            }
        }
    }
    if (found) {
        m_surfaceCurrent = current;
        m_surfaceCurrentValid = true;
        m_surfaceLast = current;
        m_surfaceLastValid = true;
        if (!m_surfaceReferenceValid) {
            m_surfaceReference = current;
            m_surfaceReferenceValid = true;
        }
    } else if (m_surfaceHoldMissing && m_surfaceLastValid) {
        m_surfaceCurrent = m_surfaceLast;
        m_surfaceCurrentValid = true;
    } else {
        m_surfaceCurrentValid = false;
    }
    emit surfaceTrackingUpdated(m_surfaceCurrentValid, m_surfaceReference, m_surfaceCurrent);
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

    if (m_surfaceEnabled) {
        const qreal sx = rangeToPosition(m_surfaceWindowStart);
        const qreal ex = rangeToPosition(m_surfaceWindowEnd);
        const QRectF windowRect(QPointF(qMin(sx, ex), pr.top()),
                                QPointF(qMax(sx, ex), pr.bottom()));
        p.fillRect(windowRect.intersected(pr), QColor(3, 169, 244, 25));
        p.setPen(QPen(QColor("#039be5"), 1.4, Qt::DashLine));
        p.drawLine(QPointF(sx, pr.top()), QPointF(sx, pr.bottom()));
        p.drawLine(QPointF(ex, pr.top()), QPointF(ex, pr.bottom()));
        const qreal thresholdY = thresholdToPosition(m_surfaceThreshold);
        p.setPen(QPen(QColor("#00acc1"), 1.2, Qt::DashDotLine));
        p.drawLine(QPointF(qMin(sx, ex), thresholdY),
                   QPointF(qMax(sx, ex), thresholdY));
        if (m_bipolar) {
            const qreal negativeThresholdY = pr.center().y()
                    + m_surfaceThreshold / 100.0 * pr.height() / 2.0;
            p.drawLine(QPointF(qMin(sx, ex), negativeThresholdY),
                       QPointF(qMax(sx, ex), negativeThresholdY));
        }
        p.drawText(QRectF(qMin(sx, ex) + 5, pr.top() + 4, 112, 20),
                   QStringLiteral("表面搜索区域"));
        if (m_surfaceReferenceValid) {
            p.setPen(QPen(QColor("#1565c0"), 1.3, Qt::DotLine));
            const qreal x = rangeToPosition(m_surfaceReference);
            p.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
        }
        if (m_surfaceCurrentValid) {
            p.setPen(QPen(QColor("#00a884"), 2.2));
            const qreal x = rangeToPosition(m_surfaceCurrent);
            p.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
            p.drawText(QRectF(x + 4, pr.top() + 24, 110, 20),
                       QStringLiteral("表面 %1 mm").arg(m_surfaceCurrent, 0, 'f', 2));
        }
    }

    for (int i = 0; i < m_gates.size() && i < 4; ++i) {
        const QVector<double> gate = displayedGate(i);
        if (gate.size() < 4 || gate[0] < 0.5)
            continue;
        const qreal startX = pr.left() + (gate[1] - m_rangeStart) / span * pr.width();
        const qreal endX = pr.left() + (gate[2] - m_rangeStart) / span * pr.width();
        const qreal gateY = thresholdToPosition(gate[3]);
        const bool active = i == m_activeGate;
        p.setPen(QPen(gateColors[i], active ? 3.0 : 2.0));
        p.drawLine(QPointF(startX, gateY), QPointF(endX, gateY));
        p.drawLine(QPointF(startX, gateY - 8), QPointF(startX, gateY + 8));
        p.drawLine(QPointF(endX, gateY - 8), QPointF(endX, gateY + 8));
        if (active) {
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(startX, gateY), 4, 4);
            p.drawEllipse(QPointF(endX, gateY), 4, 4);
        }
        p.setBrush(Qt::NoBrush);
        p.drawText(QRectF(qMin(startX, endX) + 4, gateY - 25, 130, 20),
                   QStringLiteral("Gate %1  %2%").arg(QStringLiteral("ABCI").mid(i, 1))
                       .arg(gate[3], 0, 'f', 1));
    }

    if (m_placementMode != PlacementMode::None && m_placementHasStart) {
        const qreal startX = rangeToPosition(m_placementStart);
        const qreal currentX = qBound(pr.left(), m_mousePosition.x(), pr.right());
        p.setPen(QPen(QColor("#1976d2"), 1.5, Qt::DashLine));
        p.drawLine(QPointF(startX, pr.top()), QPointF(startX, pr.bottom()));
        p.drawLine(QPointF(currentX, pr.top()), QPointF(currentX, pr.bottom()));
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
    if (m_reviewCursorVisible) {
        const qreal x = rangeToPosition(m_reviewCursor);
        p.setPen(QPen(QColor("#ff9800"), 1.6, Qt::DashLine));
        p.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
    }
}

void AScanWidget::mousePressEvent(QMouseEvent *event)
{
    const QRectF pr = plotRect(this);
    if (!pr.contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::RightButton && m_placementMode != PlacementMode::None) {
        cancelPlacement();
        emit interactionHint(QStringLiteral("图上设置已取消"));
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    m_mousePosition = event->position();
    const double value = positionToRange(event->position().x());
    if (m_placementMode != PlacementMode::None) {
        if (!m_placementHasStart) {
            m_placementStart = value;
            m_placementHasStart = true;
            emit interactionHint(QStringLiteral("已记录起点，请点击终点"));
            update();
            return;
        }
        const double start = qMin(m_placementStart, value);
        const double end = qMax(m_placementStart, value);
        if (m_placementMode == PlacementMode::Gate)
            emit gateRangeEdited(m_placementGate, start, qMax(start + 0.001, end));
        else
            emit surfaceWindowEdited(start, qMax(start + 0.001, end));
        cancelPlacement();
        return;
    }

    const qreal hit = 8.0;
    for (int i = m_gates.size() - 1; i >= 0; --i) {
        const QVector<double> gate = displayedGate(i);
        if (gate.size() < 4 || gate[0] < 0.5)
            continue;
        const qreal sx = rangeToPosition(gate[1]);
        const qreal ex = rangeToPosition(gate[2]);
        const qreal gy = thresholdToPosition(gate[3]);
        DragPart part = DragPart::None;
        if (qAbs(event->position().x() - sx) <= hit && qAbs(event->position().y() - gy) <= 16)
            part = DragPart::GateStart;
        else if (qAbs(event->position().x() - ex) <= hit && qAbs(event->position().y() - gy) <= 16)
            part = DragPart::GateEnd;
        else if (event->position().x() >= qMin(sx, ex) - hit
                 && event->position().x() <= qMax(sx, ex) + hit
                 && qAbs(event->position().y() - gy) <= hit)
            part = (event->modifiers() & Qt::ShiftModifier) ? DragPart::GateBody
                                                            : DragPart::GateThreshold;
        if (part != DragPart::None) {
            m_activeGate = i;
            m_dragGate = i;
            m_dragPart = part;
            m_dragAnchorRange = value;
            m_dragStart = m_gates[i][1];
            m_dragEnd = m_gates[i][2];
            emit gateSelected(i);
            update();
            return;
        }
    }
}

void AScanWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePosition = event->position();
    if (m_placementMode != PlacementMode::None) {
        update();
        return;
    }
    if (m_dragPart == DragPart::None) {
        updateHoverCursor(event->position());
        return;
    }
    if (m_dragGate < 0 || m_dragGate >= m_gates.size())
        return;
    double value = positionToRange(event->position().x());
    if (m_dragPart == DragPart::GateThreshold) {
        const double threshold = positionToThreshold(event->position().y());
        m_gates[m_dragGate][3] = threshold;
        emit gateThresholdEdited(m_dragGate, threshold);
    } else {
        double start = m_dragStart;
        double end = m_dragEnd;
        const double surfaceOffset = m_surfaceEnabled && m_surfaceCurrentValid
                && m_surfaceReferenceValid && m_dragGate < m_surfaceTrackedGates.size()
                && m_surfaceTrackedGates[m_dragGate]
            ? m_surfaceCurrent - m_surfaceReference : 0.0;
        if (m_dragPart == DragPart::GateStart) {
            value -= surfaceOffset;
            start = qMin(value, end - 0.001);
        } else if (m_dragPart == DragPart::GateEnd) {
            value -= surfaceOffset;
            end = qMax(value, start + 0.001);
        } else if (m_dragPart == DragPart::GateBody) {
            const double delta = value - m_dragAnchorRange;
            start += delta;
            end += delta;
            if (start < m_rangeStart) {
                end += m_rangeStart - start;
                start = m_rangeStart;
            }
            if (end > m_rangeEnd) {
                start -= end - m_rangeEnd;
                end = m_rangeEnd;
            }
        }
        m_gates[m_dragGate][1] = start;
        m_gates[m_dragGate][2] = end;
        emit gateRangeEdited(m_dragGate, start, end);
    }
    update();
}

void AScanWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragPart = DragPart::None;
    m_dragGate = -1;
    updateHoverCursor(m_mousePosition);
}

void AScanWidget::leaveEvent(QEvent *event)
{
    if (m_placementMode == PlacementMode::None && m_dragPart == DragPart::None)
        unsetCursor();
    QWidget::leaveEvent(event);
}

void AScanWidget::updateHoverCursor(const QPointF &position)
{
    const qreal hit = 8.0;
    for (int i = m_gates.size() - 1; i >= 0; --i) {
        const QVector<double> gate = displayedGate(i);
        if (gate.size() < 4 || gate[0] < 0.5)
            continue;
        const qreal sx = rangeToPosition(gate[1]);
        const qreal ex = rangeToPosition(gate[2]);
        const qreal gy = thresholdToPosition(gate[3]);
        if ((qAbs(position.x() - sx) <= hit || qAbs(position.x() - ex) <= hit)
            && qAbs(position.y() - gy) <= 16) {
            setCursor(Qt::SizeHorCursor);
            return;
        }
        if (position.x() >= qMin(sx, ex) - hit && position.x() <= qMax(sx, ex) + hit
            && qAbs(position.y() - gy) <= hit) {
            setCursor(Qt::SizeVerCursor);
            return;
        }
    }
    unsetCursor();
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

void EScanWidget::setImagingGate(bool enabled, double start, double end,
                                 double rangeStart, double rangeEnd)
{
    m_gateEnabled = enabled;
    m_gateStart = qMin(start, end);
    m_gateEnd = qMax(start, end);
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;
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
    if (!m_gateEnabled) {
        p.setPen(QColor("#b42318"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("当前成像闸门未启用"));
        return;
    }
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    const int top = qBound(0, int((m_gateStart - m_rangeStart) / span * m_image.height()),
                           qMax(0, m_image.height() - 1));
    const int bottom = qBound(top + 1,
                              int(qCeil((m_gateEnd - m_rangeStart) / span * m_image.height())),
                              m_image.height());
    p.drawImage(rect().adjusted(40, 20, -20, -40), m_image,
                QRect(0, top, m_image.width(), qMax(1, bottom - top)));
}

BScanWidget::BScanWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
}

void BScanWidget::setEncoderPrecision(int countsPerColumn)
{
    countsPerColumn = qMax(1, countsPerColumn);
    if (m_encoderPrecision == countsPerColumn)
        return;
    m_encoderPrecision = countsPerColumn;
    clear();
}

void BScanWidget::setMmPerCount(double mmPerCount)
{
    m_mmPerCount = qMax(0.0, mmPerCount);
    update();
}

void BScanWidget::setRange(double rangeStart, double rangeEnd)
{
    const double low = qMin(rangeStart, rangeEnd);
    const double high = qMax(rangeStart, rangeEnd);
    rangeStart = low;
    rangeEnd = qMax(low + 0.001, high);
    if (qFuzzyCompare(m_rangeStart + 1.0, rangeStart + 1.0)
        && qFuzzyCompare(m_rangeEnd + 1.0, rangeEnd + 1.0))
        return;
    // Old pixels cannot be relabelled with new acquisition geometry. Keeping
    // them would display correct amplitudes at incorrect depths.
    if (m_usedColumns > 0)
        clear();
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;
    update();
}

void BScanWidget::setImagingGate(bool enabled, double start, double end,
                                 double thresholdPercent)
{
    m_gateEnabled = enabled;
    m_gateStart = qMin(start, end);
    m_gateEnd = qMax(start, end);
    m_gateThreshold = qBound(0.0, thresholdPercent, 100.0) / 100.0;
    rebuildImage();
    update();
}

void BScanWidget::setSurfaceTracking(bool enabled, double windowStart,
                                     double windowEnd, double thresholdPercent,
                                     bool holdMissing, bool alignSelectedGate)
{
    m_surfaceEnabled = enabled;
    m_surfaceWindowStart = qMin(windowStart, windowEnd);
    m_surfaceWindowEnd = qMax(windowStart, windowEnd);
    m_surfaceThreshold = qBound(0.0, thresholdPercent, 100.0) / 100.0;
    m_surfaceHoldMissing = holdMissing;
    m_surfaceAlignGate = alignSelectedGate;
    m_surfaceReferenceValid = false;
    m_surfaceLastValid = false;
    for (ColumnData &column : m_columns) {
        if (!column.valid || column.interpolated)
            continue;
        updateSurface(column);
    }
    rebuildImage();
    update();
}

void BScanWidget::setViewMode(int mode)
{
    m_viewMode = mode == ProfileView ? ProfileView : AmplitudeView;
    rebuildImage();
    update();
}

void BScanWidget::setPaletteMode(int mode)
{
    m_paletteMode = qBound(int(HeatPalette), mode, int(RfPalette));
    rebuildColorTable();
    rebuildImage();
    update();
}

void BScanWidget::setScaleMode(int mode)
{
    m_scaleMode = mode == DynamicScale ? DynamicScale : FixedFullScale;
    rebuildImage();
    update();
}

void BScanWidget::setDisplayLevels(double lowerPercent, double upperPercent,
                                   double gamma)
{
    m_displayLower = qBound(0.0, lowerPercent / 100.0, 0.99);
    m_displayUpper = qBound(m_displayLower + 0.01, upperPercent / 100.0, 1.0);
    m_displayGamma = qBound(0.2, gamma, 3.0);
    rebuildImage();
    update();
}

void BScanWidget::setDisplayReject(double percent)
{
    m_displayReject = qBound(0.0, percent, 100.0) / 100.0;
    rebuildImage();
    update();
}

void BScanWidget::setInterpolateMissing(bool enabled)
{
    m_interpolateMissing = enabled;
}

void BScanWidget::resetView()
{
    m_zoom = 1.0;
    m_viewCenter = 0.5;
    update();
}

void BScanWidget::initializeImage(int sampleCount)
{
    const int imageHeight = qBound(1, sampleCount, 1024);
    m_image = QImage(4096, imageHeight, QImage::Format_Indexed8);
    rebuildColorTable();
    m_image.fill(m_paletteMode == RfPalette ? 127 : 0);
    m_columns.clear();
    m_columns.resize(m_image.width());
    m_lastColumn = -1;
    m_usedColumns = 0;
    m_acquiredColumns = 0;
    m_interpolatedColumns = 0;
    m_gapColumns = 0;
    m_largestGap = 0;
    m_dynamicMaximum = 1.0;
}

bool BScanWidget::ensureImageWidth(int requiredColumn)
{
    static constexpr int MaximumStoredColumns = 65536;
    if (requiredColumn < m_image.width())
        return true;
    if (requiredColumn >= MaximumStoredColumns)
        return false;

    const int newWidth = qMin(MaximumStoredColumns,
                              ((requiredColumn / 4096) + 1) * 4096);
    QImage expanded(newWidth, m_image.height(), QImage::Format_Indexed8);
    expanded.setColorTable(m_image.colorTable());
    expanded.fill(m_paletteMode == RfPalette ? 127 : 0);
    for (int y = 0; y < m_image.height(); ++y)
        std::memcpy(expanded.scanLine(y), m_image.constScanLine(y),
                    size_t(m_image.width()));
    m_image = std::move(expanded);
    m_columns.resize(newWidth);
    return true;
}

BScanWidget::ColumnData BScanWidget::makeColumn(const QVector<qint16> &samples,
                                                 double scale) const
{
    ColumnData result;
    result.valid = !samples.isEmpty();
    result.scale = qMax(1.0, scale);
    result.samples.resize(m_image.height());
    for (int y = 0; y < result.samples.size(); ++y) {
        const int begin = int(qint64(y) * samples.size() / result.samples.size());
        const int end = qMax(begin + 1,
            int(qint64(y + 1) * samples.size() / result.samples.size()));
        int selected = 0;
        for (int i = begin; i < end && i < samples.size(); ++i) {
            if (qAbs(int(samples[i])) > qAbs(selected))
                selected = int(samples[i]);
        }
        result.samples[y] = qint16(qBound(-32768, selected, 32767));
        result.score = qMax(result.score, qAbs(selected));
    }
    return result;
}

void BScanWidget::updateSurface(ColumnData &column)
{
    column.surfaceValid = false;
    if (!m_surfaceEnabled || column.samples.isEmpty()
        || m_surfaceWindowEnd <= m_surfaceWindowStart)
        return;
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    const double clippedStart = qMax(m_rangeStart, m_surfaceWindowStart);
    const double clippedEnd = qMin(m_rangeEnd, m_surfaceWindowEnd);
    if (clippedEnd < clippedStart)
        return;
    const int first = qBound(0, int((clippedStart - m_rangeStart)
                                    / span * column.samples.size()),
                             column.samples.size() - 1);
    const int last = qBound(first, int(qCeil((clippedEnd - m_rangeStart)
                                             / span * column.samples.size())),
                            column.samples.size() - 1);
    const double trigger = column.scale * m_surfaceThreshold;
    for (int i = first; i <= last; ++i) {
        if (qAbs(double(column.samples[i])) >= trigger) {
            column.surface = m_rangeStart + span * i
                / qMax(1, column.samples.size() - 1);
            column.surfaceValid = true;
            m_surfaceLast = column.surface;
            m_surfaceLastValid = true;
            if (!m_surfaceReferenceValid) {
                m_surfaceReference = column.surface;
                m_surfaceReferenceValid = true;
            }
            return;
        }
    }
    if (m_surfaceHoldMissing && m_surfaceLastValid) {
        column.surface = m_surfaceLast;
        column.surfaceValid = true;
    }
}

void BScanWidget::interpolateGap(int previousColumn, int nextColumn,
                                 const ColumnData &previous,
                                 const ColumnData &next)
{
    const int gap = nextColumn - previousColumn;
    if (!m_interpolateMissing || gap <= 1 || gap > 64
        || previous.samples.size() != next.samples.size())
        return;
    for (int column = previousColumn + 1; column < nextColumn; ++column) {
        if (column < 0 || column >= m_columns.size() || m_columns[column].valid)
            continue;
        const double alpha = double(column - previousColumn) / gap;
        ColumnData generated;
        generated.valid = true;
        generated.interpolated = true;
        generated.scale = 32767.0;
        generated.samples.resize(previous.samples.size());
        for (int y = 0; y < generated.samples.size(); ++y) {
            const double left = previous.samples[y] / previous.scale;
            const double right = next.samples[y] / next.scale;
            generated.samples[y] = qint16(qBound(-32767,
                qRound((left * (1.0 - alpha) + right * alpha) * 32767.0), 32767));
            generated.score = qMax(generated.score,
                                   qAbs(int(generated.samples[y])));
        }
        if (previous.surfaceValid && next.surfaceValid) {
            generated.surface = previous.surface * (1.0 - alpha)
                                + next.surface * alpha;
            generated.surfaceValid = true;
        }
        m_columns[column] = std::move(generated);
        ++m_interpolatedColumns;
        rebuildColumn(column);
    }
}

void BScanWidget::appendFrame(const DecodedFrame &frame, double scale, int sourceBeam)
{
    if (appendFrameInternal(frame, scale, sourceBeam) && isVisible())
        update();
}

void BScanWidget::appendFrameAtColumn(const DecodedFrame &frame, double scale,
                                      int column, int sourceBeam)
{
    if (appendFrameAtColumnInternal(frame, scale, column, sourceBeam) && isVisible())
        update();
}

bool BScanWidget::appendFrameAtColumnDeferred(const DecodedFrame &frame,
                                               double scale, int column,
                                               int sourceBeam)
{
    return appendFrameAtColumnInternal(frame, scale, column, sourceBeam);
}

void BScanWidget::appendFrames(const QVector<DecodedFrame> &frames, double scale,
                               int sourceBeam)
{
    bool changed = false;
    for (const DecodedFrame &frame : frames)
        changed = appendFrameInternal(frame, scale, sourceBeam) || changed;
    if (changed && isVisible())
        update();
}

bool BScanWidget::appendFrameInternal(const DecodedFrame &frame, double scale,
                                      int sourceBeam)
{
    const qint64 encoder = frame.tail.encoder[0];
    m_currentEncoder = encoder;
    if (!m_hasOrigin) {
        m_originEncoder = encoder;
        m_hasOrigin = true;
    }
    const qint64 rawDelta = encoder - m_originEncoder;
    if (m_direction == 0 && rawDelta != 0)
        m_direction = rawDelta > 0 ? 1 : -1;
    const qint64 directedCounts = rawDelta * (m_direction == 0 ? 1 : m_direction);
    if (directedCounts < 0)
        return false;
    const qint64 absoluteColumn = directedCounts / m_encoderPrecision;
    if (absoluteColumn > std::numeric_limits<int>::max())
        return false;
    return appendFrameAtColumnInternal(frame, scale, int(absoluteColumn), sourceBeam);
}

bool BScanWidget::appendFrameAtColumnInternal(const DecodedFrame &frame,
                                              double scale, int column,
                                              int sourceBeam)
{
    if (column < 0 || frame.beams.isEmpty())
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
    if (!ensureImageWidth(column))
        return false;

    m_currentEncoder = frame.tail.encoder[0];
    if (!m_hasOrigin) {
        m_originEncoder = m_currentEncoder;
        m_hasOrigin = true;
    }
    ColumnData incoming = makeColumn(samples, scale);
    updateSurface(incoming);
    ColumnData &stored = m_columns[column];
    // One complete representative A-scan owns the whole B-scan column. This
    // keeps pixel colours and click-to-review waveform strictly consistent.
    if (stored.valid && !stored.interpolated && incoming.score <= stored.score)
        return false;

    const bool wasValid = stored.valid;
    const bool wasInterpolated = stored.interpolated;
    const int previousColumn = m_lastColumn;
    ColumnData previous;
    if (previousColumn >= 0 && previousColumn < m_columns.size())
        previous = m_columns[previousColumn];
    stored = incoming;
    if (!wasValid)
        ++m_acquiredColumns;
    else if (wasInterpolated) {
        --m_interpolatedColumns;
        ++m_acquiredColumns;
    }

    if (previousColumn >= 0 && column > previousColumn + 1) {
        const int gap = column - previousColumn - 1;
        m_gapColumns += gap;
        m_largestGap = qMax(m_largestGap, gap);
        interpolateGap(previousColumn, column, previous, incoming);
    }
    m_lastColumn = column;
    m_usedColumns = qMax(m_usedColumns, column + 1);

    const double oldMaximum = m_dynamicMaximum;
    for (int y = 0; y < incoming.samples.size(); ++y)
        m_dynamicMaximum = qMax(m_dynamicMaximum == 1.0 && m_acquiredColumns == 1
                                    ? 0.0 : m_dynamicMaximum,
                                normalizedMagnitude(incoming, y));
    if (m_scaleMode == DynamicScale && !qFuzzyCompare(oldMaximum + 1.0,
                                                       m_dynamicMaximum + 1.0))
        rebuildImage();
    else
        rebuildColumn(column);
    return true;
}

double BScanWidget::normalizedMagnitude(const ColumnData &column, int row) const
{
    if (!column.valid || row < 0 || row >= column.samples.size())
        return 0.0;
    return qBound(0.0, qAbs(double(column.samples[row])) / qMax(1.0, column.scale), 1.0);
}

int BScanWidget::sourceRowForDisplay(const ColumnData &column, int displayRow) const
{
    if (!m_surfaceEnabled || !m_surfaceAlignGate || !column.surfaceValid
        || !m_surfaceReferenceValid)
        return displayRow;
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    const double alignedDepth = m_rangeStart + span * displayRow
                                / qMax(1, m_image.height() - 1);
    const double sourceDepth = alignedDepth + column.surface - m_surfaceReference;
    const int sourceRow = qRound((sourceDepth - m_rangeStart) / span
                                 * qMax(1, m_image.height() - 1));
    return sourceRow >= 0 && sourceRow < m_image.height() ? sourceRow : -1;
}

int BScanWidget::profileRow(const ColumnData &column, double *amplitude) const
{
    if (!column.valid || column.samples.isEmpty())
        return -1;
    const double span = qMax(0.001, m_rangeEnd - m_rangeStart);
    const double delta = m_surfaceEnabled && m_surfaceAlignGate
                         && column.surfaceValid && m_surfaceReferenceValid
        ? column.surface - m_surfaceReference : 0.0;
    const int first = qBound(0, qFloor((m_gateStart + delta - m_rangeStart)
                                      / span * column.samples.size()),
                             column.samples.size() - 1);
    const int last = qBound(first, qCeil((m_gateEnd + delta - m_rangeStart)
                                        / span * column.samples.size()),
                            column.samples.size() - 1);
    for (int row = first; row <= last; ++row) {
        const double value = normalizedMagnitude(column, row);
        if (value >= m_gateThreshold) {
            if (amplitude)
                *amplitude = value;
            const double absoluteDepth = m_rangeStart + span * row
                                         / qMax(1, column.samples.size() - 1);
            const double alignedDepth = absoluteDepth - delta;
            return qBound(0, qRound((alignedDepth - m_rangeStart) / span
                                    * qMax(1, m_image.height() - 1)),
                          m_image.height() - 1);
        }
    }
    return -1;
}

void BScanWidget::rebuildColorTable()
{
    if (m_image.isNull())
        return;
    QVector<QRgb> palette(256);
    for (int i = 0; i < 256; ++i) {
        if (m_paletteMode == GrayscalePalette) {
            const int gray = 255 - i;
            palette[i] = QColor(gray, gray, gray).rgb();
        } else if (m_paletteMode == RfPalette) {
            const double signedValue = (i - 127.5) / 127.5;
            const double magnitude = qAbs(signedValue);
            palette[i] = (signedValue < 0.0
                ? QColor::fromRgbF(1.0 - magnitude, 1.0 - magnitude, 1.0)
                : QColor::fromRgbF(1.0, 1.0 - magnitude, 1.0 - magnitude)).rgb();
        } else {
            palette[i] = waveColor(double(i) / 255.0).rgb();
        }
    }
    m_image.setColorTable(palette);
}

void BScanWidget::rebuildColumn(int column)
{
    if (m_image.isNull() || column < 0 || column >= m_image.width())
        return;
    const int emptyIndex = m_paletteMode == RfPalette ? 127 : 0;
    const ColumnData &data = m_columns[column];
    for (int y = 0; y < m_image.height(); ++y)
        m_image.scanLine(y)[column] = uchar(emptyIndex);
    if (!data.valid)
        return;

    const double scaleMaximum = m_scaleMode == DynamicScale
        ? qMax(0.000001, m_dynamicMaximum) : 1.0;
    const double levelSpan = qMax(0.01, m_displayUpper - m_displayLower);
    if (m_viewMode == ProfileView) {
        double rawAmplitude = 0.0;
        const int row = profileRow(data, &rawAmplitude);
        if (row < 0)
            return;
        double normalized = qBound(0.0, rawAmplitude / scaleMaximum, 1.0);
        normalized = qBound(0.0, (normalized - m_displayLower) / levelSpan, 1.0);
        normalized = qPow(normalized, 1.0 / m_displayGamma);
        const int index = qBound(0, qRound(normalized * 255.0), 255);
        for (int y = qMax(0, row - 1); y <= qMin(m_image.height() - 1, row + 1); ++y)
            m_image.scanLine(y)[column] = uchar(index);
        return;
    }

    for (int y = 0; y < m_image.height(); ++y) {
        const int sourceRow = sourceRowForDisplay(data, y);
        if (sourceRow < 0)
            continue;
        const double rawMagnitude = normalizedMagnitude(data, sourceRow);
        if (rawMagnitude < m_displayReject)
            continue;
        double normalized = qBound(0.0, rawMagnitude / scaleMaximum, 1.0);
        normalized = qBound(0.0, (normalized - m_displayLower) / levelSpan, 1.0);
        normalized = qPow(normalized, 1.0 / m_displayGamma);
        int index = qBound(0, qRound(normalized * 255.0), 255);
        if (m_paletteMode == RfPalette) {
            const double sign = data.samples[sourceRow] < 0 ? -1.0 : 1.0;
            index = qBound(0, qRound(127.5 + sign * normalized * 127.5), 255);
        }
        m_image.scanLine(y)[column] = uchar(index);
    }
}

void BScanWidget::rebuildImage()
{
    if (m_image.isNull())
        return;
    rebuildColorTable();
    for (int column = 0; column < m_usedColumns; ++column)
        rebuildColumn(column);
}

void BScanWidget::clear()
{
    m_image = {};
    m_columns.clear();
    m_direction = 0;
    m_lastColumn = -1;
    m_usedColumns = 0;
    m_originEncoder = 0;
    m_currentEncoder = 0;
    m_hasOrigin = false;
    m_surfaceReferenceValid = false;
    m_surfaceLastValid = false;
    m_acquiredColumns = 0;
    m_interpolatedColumns = 0;
    m_gapColumns = 0;
    m_largestGap = 0;
    m_dynamicMaximum = 1.0;
    resetView();
}

void BScanWidget::saveTo(QDataStream &out) const
{
    out << qint32(m_encoderPrecision) << m_mmPerCount << qint32(m_direction)
        << qint32(m_usedColumns) << qint64(m_originEncoder) << qint64(m_currentEncoder)
        << bool(m_hasOrigin) << m_rangeStart << m_rangeEnd
        << bool(m_gateEnabled) << m_gateStart << m_gateEnd;

    QByteArray png;
    if (!m_image.isNull() && m_usedColumns > 0) {
        QImage storage(m_usedColumns, m_image.height(), QImage::Format_ARGB32);
        storage.fill(Qt::transparent);
        for (int x = 0; x < m_usedColumns && x < m_columns.size(); ++x) {
            const ColumnData &column = m_columns[x];
            if (!column.valid)
                continue;
            const int alpha = column.interpolated ? 128 : 255;
            for (int y = 0; y < storage.height(); ++y) {
                const int value = qBound(0, qRound(normalizedMagnitude(column, y) * 255.0), 255);
                storage.setPixelColor(x, y, QColor(value, value, value, alpha));
            }
        }
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        storage.save(&buffer, "PNG");
    }
    out << png;
}

bool BScanWidget::loadFrom(QDataStream &in)
{
    qint32 encoderPrecision = 10, direction = 0, usedColumns = 0;
    double mmPerCount = 0.0, rangeStart = 0.0, rangeEnd = 50.0, gateStart = 0.0, gateEnd = 50.0;
    qint64 originEncoder = 0, currentEncoder = 0;
    bool hasOrigin = false, gateEnabled = true;
    in >> encoderPrecision >> mmPerCount >> direction >> usedColumns
       >> originEncoder >> currentEncoder >> hasOrigin >> rangeStart >> rangeEnd
       >> gateEnabled >> gateStart >> gateEnd;
    if (in.status() != QDataStream::Ok || encoderPrecision <= 0
        || usedColumns < 0 || usedColumns > 65536
        || !qIsFinite(mmPerCount) || mmPerCount < 0.0
        || !qIsFinite(rangeStart) || !qIsFinite(rangeEnd)
        || rangeEnd <= rangeStart)
        return false;

    QByteArray png;
    in >> png;
    if (in.status() != QDataStream::Ok || png.size() > 256 * 1024 * 1024)
        return false;
    const QImage storage = png.isEmpty() ? QImage() : QImage::fromData(png, "PNG");
    if (!png.isEmpty() && (storage.isNull() || storage.width() > 65536
                            || storage.height() > 1024))
        return false;

    m_encoderPrecision = qMax(1, int(encoderPrecision));
    m_mmPerCount = qMax(0.0, mmPerCount);
    m_direction = direction;
    m_usedColumns = qMax(0, int(usedColumns));
    m_originEncoder = originEncoder;
    m_currentEncoder = currentEncoder;
    m_hasOrigin = hasOrigin;
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;
    m_gateEnabled = gateEnabled;
    m_gateStart = gateStart;
    m_gateEnd = gateEnd;
    m_lastColumn = m_usedColumns - 1;
    if (!storage.isNull()) {
        m_acquiredColumns = 0;
        m_interpolatedColumns = 0;
        m_dynamicMaximum = 0.0;
        m_image = QImage(qMax(4096, ((storage.width() + 4095) / 4096) * 4096),
                         storage.height(), QImage::Format_Indexed8);
        rebuildColorTable();
        m_image.fill(m_paletteMode == RfPalette ? 127 : 0);
        m_columns.clear();
        m_columns.resize(m_image.width());
        const bool hasAlpha = storage.hasAlphaChannel();
        for (int x = 0; x < qMin(m_usedColumns, storage.width()); ++x) {
            bool valid = !hasAlpha;
            bool interpolated = false;
            ColumnData column;
            column.scale = 255.0;
            column.samples.resize(storage.height());
            for (int y = 0; y < storage.height(); ++y) {
                const QColor pixel = storage.pixelColor(x, y);
                valid |= pixel.alpha() > 0;
                interpolated |= pixel.alpha() > 0 && pixel.alpha() < 255;
                column.samples[y] = qint16(qGray(pixel.rgb()));
                column.score = qMax(column.score, int(column.samples[y]));
            }
            column.valid = valid;
            column.interpolated = valid && interpolated;
            if (valid) {
                m_dynamicMaximum = qMax(m_dynamicMaximum,
                                        column.score / qMax(1.0, column.scale));
                if (interpolated)
                    ++m_interpolatedColumns;
                else
                    ++m_acquiredColumns;
            }
            m_columns[x] = std::move(column);
        }
        rebuildImage();
    } else {
        m_image = {};
        m_columns.clear();
    }
    update();
    return true;
}

QRect BScanWidget::imageTargetRect() const
{
    return rect().adjusted(72, 40, -82, -58);
}

QRect BScanWidget::visibleSourceRect() const
{
    if (m_image.isNull())
        return {};
    const int total = qMax(2, m_usedColumns);
    const int visible = qBound(2, qCeil(total / m_zoom), total);
    const int left = qBound(0, qRound(m_viewCenter * total - visible / 2.0),
                            qMax(0, total - visible));
    const double rangeSpan = qMax(0.001, m_rangeEnd - m_rangeStart);
    const int top = qBound(0, int((m_gateStart - m_rangeStart) / rangeSpan
                                  * m_image.height()), m_image.height() - 1);
    const int bottom = qBound(top + 1,
        int(qCeil((m_gateEnd - m_rangeStart) / rangeSpan * m_image.height())),
        m_image.height());
    return QRect(left, top, visible, qMax(1, bottom - top));
}

void BScanWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (m_image.isNull() || m_usedColumns <= 0) {
        p.setPen(QColor("#667788"));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("启动 B 扫后，转动编码器 A 开始成像"));
        return;
    }
    if (!m_gateEnabled) {
        p.setPen(QColor("#b42318"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("当前成像闸门未启用"));
        return;
    }
    const QRect target = imageTargetRect();
    const QRect source = visibleSourceRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(target, m_image, source);

    // Missing acquisition cells are not the same as measured zero amplitude.
    for (int column = source.left(); column < source.right() + 1
         && column < m_columns.size(); ++column) {
        if (m_columns[column].valid && !m_columns[column].interpolated)
            continue;
        const qreal x1 = target.left() + (column - source.left())
                         * target.width() / qMax(1.0, double(source.width()));
        const qreal x2 = target.left() + (column + 1 - source.left())
                         * target.width() / qMax(1.0, double(source.width()));
        p.fillRect(QRectF(x1, target.top(), qMax(1.0, x2 - x1), target.height()),
                   m_columns[column].interpolated
                       ? QColor(255, 193, 7, 38) : QColor(95, 105, 115, 85));
    }

    p.setPen(QPen(QColor("#22313f"), 1.2));
    p.drawRect(target);
    p.setPen(QColor("#52606d"));
    const int direction = m_direction == 0 ? 1 : m_direction;
    for (int i = 0; i <= 4; ++i) {
        const qreal x = target.left() + target.width() * i / 4.0;
        const qint64 logical = source.left() + qint64(source.width() - 1) * i / 4;
        const double mm = direction * double(logical * qint64(m_encoderPrecision))
                          * m_mmPerCount;
        p.drawLine(QPointF(x, target.bottom()), QPointF(x, target.bottom() + 5));
        p.drawText(QRectF(x - 55, target.bottom() + 7, 110, 20),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(mm, 'f', 1));
        const qreal y = target.top() + target.height() * i / 4.0;
        const double depth = m_gateStart + (m_gateEnd - m_gateStart) * i / 4.0;
        p.drawLine(QPointF(target.left() - 5, y), QPointF(target.left(), y));
        p.drawText(QRectF(2, y - 10, 64, 20), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(depth, 'f', 1));
    }

    const QRect colorBar(target.right() + 18, target.top(), 16, target.height());
    for (int y = colorBar.top(); y <= colorBar.bottom(); ++y) {
        const int index = qBound(0, qRound((1.0 - double(y - colorBar.top())
                                           / qMax(1, colorBar.height())) * 255.0), 255);
        p.setPen(QColor::fromRgb(m_image.color(index)));
        p.drawLine(colorBar.left(), y, colorBar.right(), y);
    }
    p.setPen(QColor("#52606d"));
    p.drawRect(colorBar);
    const double scaleMaximum = (m_scaleMode == DynamicScale
                                 ? m_dynamicMaximum : 1.0) * 100.0;
    p.drawText(QRect(colorBar.right() + 3, colorBar.top() - 6, 42, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString::number(scaleMaximum, 'f', 0) + "%");
    p.drawText(QRect(colorBar.right() + 3, colorBar.bottom() - 10, 42, 18),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("0%"));

    const int covered = m_acquiredColumns + m_interpolatedColumns;
    const double coverage = 100.0 * covered / qMax(1, m_usedColumns);
    const QString alignment = m_surfaceEnabled && m_surfaceAlignGate
        ? QStringLiteral(" · 表面对齐") : QString();
    p.drawText(QRectF(target.left(), 4, target.width(), 28),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("%1%2　有效列 %3/%4（%5%）　漏列 %6　最大跨格 %7")
                   .arg(m_viewMode == ProfileView ? QStringLiteral("轮廓 B 扫")
                                                   : QStringLiteral("幅值 B 扫"))
                   .arg(alignment).arg(covered).arg(m_usedColumns)
                   .arg(coverage, 0, 'f', 1).arg(qMax(0, m_usedColumns - covered))
                   .arg(m_largestGap));
    p.drawText(QRectF(2, target.top() - 4, 66, 20),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("深度"));
    p.drawText(QRectF(target.left(), target.bottom() + 30, target.width(), 20),
               Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("路径（mm）"));

    if (m_hovering && target.contains(m_mousePosition.toPoint())) {
        p.setPen(QPen(QColor(25, 118, 210, 190), 1));
        p.drawLine(QPointF(m_mousePosition.x(), target.top()),
                   QPointF(m_mousePosition.x(), target.bottom()));
        p.drawLine(QPointF(target.left(), m_mousePosition.y()),
                   QPointF(target.right(), m_mousePosition.y()));
        const double xr = qBound(0.0, (m_mousePosition.x() - target.left())
                                      / qMax(1, target.width()), 1.0);
        const double yr = qBound(0.0, (m_mousePosition.y() - target.top())
                                      / qMax(1, target.height()), 1.0);
        const int column = qBound(source.left(),
            source.left() + qRound(xr * qMax(0, source.width() - 1)),
            qMin(m_usedColumns - 1, source.right()));
        const double depth = m_gateStart + yr * (m_gateEnd - m_gateStart);
        double amplitude = 0.0;
        if (column >= 0 && column < m_columns.size() && m_columns[column].valid) {
            const int displayRow = qBound(0, qRound((depth - m_rangeStart)
                / qMax(0.001, m_rangeEnd - m_rangeStart) * (m_image.height() - 1)),
                m_image.height() - 1);
            const int sourceRow = sourceRowForDisplay(m_columns[column], displayRow);
            amplitude = sourceRow >= 0
                ? normalizedMagnitude(m_columns[column], sourceRow) * 100.0 : 0.0;
        }
        const QString tip = QStringLiteral("%1 mm  深度 %2 mm  幅值 %3%")
            .arg(column * m_encoderPrecision * m_mmPerCount, 0, 'f', 2)
            .arg(depth, 0, 'f', 2).arg(amplitude, 0, 'f', 1);
        const QRectF tipRect(m_mousePosition.x() + 10, m_mousePosition.y() - 26,
                             220, 22);
        p.fillRect(tipRect, QColor(255, 255, 255, 225));
        p.setPen(QColor("#174a75"));
        p.drawText(tipRect.adjusted(5, 0, -3, 0), Qt::AlignVCenter, tip);
    }
}

void BScanWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_image.isNull()
        || !m_gateEnabled || m_usedColumns <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRect target = imageTargetRect();
    if (!target.contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRect source = visibleSourceRect();
    const double xRatio = qBound(0.0,
        (event->position().x() - target.left()) / qMax(1, target.width()), 1.0);
    const double yRatio = qBound(0.0,
        (event->position().y() - target.top()) / qMax(1, target.height()), 1.0);
    const int column = qBound(source.left(),
        source.left() + qRound(xRatio * qMax(0, source.width() - 1)),
        qMin(m_usedColumns - 1, source.right()));
    const double depth = m_gateStart + yRatio * (m_gateEnd - m_gateStart);
    emit columnClicked(column, depth);
}

void BScanWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePosition = event->position();
    m_hovering = imageTargetRect().contains(event->position().toPoint());
    update();
}

void BScanWidget::leaveEvent(QEvent *event)
{
    m_hovering = false;
    update();
    QWidget::leaveEvent(event);
}

void BScanWidget::wheelEvent(QWheelEvent *event)
{
    if (m_usedColumns <= 1) {
        QWidget::wheelEvent(event);
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        const double step = 0.08 / m_zoom;
        m_viewCenter = qBound(0.0, m_viewCenter
            + (event->angleDelta().y() > 0 ? -step : step), 1.0);
    } else {
        const QRect target = imageTargetRect();
        const double cursor = target.contains(event->position().toPoint())
            ? qBound(0.0, (event->position().x() - target.left())
                          / qMax(1, target.width()), 1.0) : 0.5;
        const QRect before = visibleSourceRect();
        const double anchor = (before.left() + cursor * before.width())
                              / qMax(1, m_usedColumns);
        m_zoom = qBound(1.0, m_zoom
            * (event->angleDelta().y() > 0 ? 1.25 : 0.8), 32.0);
        const int nextVisible = qBound(2, qCeil(qMax(2, m_usedColumns) / m_zoom),
                                       qMax(2, m_usedColumns));
        m_viewCenter = qBound(0.0, anchor + (0.5 - cursor)
                              * nextVisible / qMax(1, m_usedColumns), 1.0);
    }
    update();
    event->accept();
}

CScanWidget::CScanWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void CScanWidget::setImagingGate(int gateIndex, bool enabled, double thresholdPercent)
{
    m_imagingGate = qBound(0, gateIndex, 3);
    m_imagingGateEnabled = enabled;
    m_imagingGateThreshold = qBound(0.0, thresholdPercent, 100.0);
    update();
}

void CScanWidget::setEncoderPrecision(int countsPerCell)
{
    countsPerCell = qMax(1, countsPerCell);
    if (m_encoderPrecision == countsPerCell)
        return;
    m_encoderPrecision = countsPerCell;
    clear();
}

void CScanWidget::setMmPerCount(double mmPerCount)
{
    mmPerCount = qMax(0.0, mmPerCount);
    if (qFuzzyCompare(m_mmPerCount + 1.0, mmPerCount + 1.0))
        return;
    m_mmPerCount = mmPerCount;
    update();
}

void CScanWidget::appendFrame(const DecodedFrame &frame, double scale,
                              int sourceBeam)
{
    if (appendFrameInternal(frame, scale, sourceBeam, 0xF) && isVisible())
        update();
}

bool CScanWidget::appendFrameDeferred(const DecodedFrame &frame, double scale,
                                      int sourceBeam, int gateMask)
{
    return appendFrameInternal(frame, scale, sourceBeam, gateMask);
}

void CScanWidget::appendFrames(const QVector<DecodedFrame> &frames, double scale,
                               int sourceBeam)
{
    bool changed = false;
    for (const DecodedFrame &frame : frames)
        changed = appendFrameInternal(frame, scale, sourceBeam, 0xF) || changed;
    if (changed && isVisible())
        update();
}

bool CScanWidget::appendFrameInternal(const DecodedFrame &frame, double scale,
                                      int sourceBeam, int gateMask)
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
    QVector<int> amplitudes(4, 0);
    if (beamIndex >= 0 && beamIndex < frame.measurements.size()) {
        const BeamMeasurement &measurement = frame.measurements[beamIndex];
        amplitudes[0] = int(measurement.amplitudeA);
        amplitudes[1] = int(measurement.amplitudeB);
        amplitudes[2] = int(measurement.amplitudeC);
        amplitudes[3] = int(measurement.amplitudeI);
    }
    // C 扫必须严格使用 SDK 给出的各闸门测量幅值。不能在 Gate A 为 0 时
    // 从整条 A 扫重新找峰值，否则会越过 Gate A 的范围和阈值产生伪成像。
    m_amplitudeScale = qMax(1.0, scale);
    const QPoint cell(x, y);
    bool hadAnyCell = false;
    for (const auto &gateCells : m_gateCells)
        hadAnyCell = hadAnyCell || !gateCells.isEmpty();
    bool changed = false;
    for (int gate = 0; gate < m_gateCells.size(); ++gate) {
        if ((gateMask & (1 << gate)) == 0)
            continue;
        const quint32 amplitude = quint32(qMax(0, amplitudes[gate]));
        auto &cells = m_gateCells[gate];
        const auto existing = cells.constFind(cell);
        if (existing != cells.constEnd() && *existing >= amplitude)
            continue;
        // Each gate keeps the same path independently so switching the dropdown
        // does not discard already acquired C-scan data.
        if (existing == cells.constEnd() && cells.size() >= 262144)
            continue;
        cells.insert(cell, amplitude);
        changed = true;
    }
    if (!changed)
        return false;
    if (!hadAnyCell) {
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
    for (auto &cells : m_gateCells)
        cells.clear();
    m_originEncoderA = 0;
    m_originEncoderB = 0;
    m_currentEncoderA = 0;
    m_currentEncoderB = 0;
    m_hasOrigin = false;
    m_minX = m_maxX = 0;
    m_minY = m_maxY = 0;
    update();
}

void CScanWidget::saveTo(QDataStream &out) const
{
    out << qint32(m_encoderPrecision) << m_mmPerCount << qint32(m_imagingGate)
        << bool(m_imagingGateEnabled) << m_imagingGateThreshold << m_amplitudeScale
        << qint64(m_originEncoderA) << qint64(m_originEncoderB)
        << qint64(m_currentEncoderA) << qint64(m_currentEncoderB)
        << bool(m_hasOrigin) << qint32(m_minX) << qint32(m_maxX)
        << qint32(m_minY) << qint32(m_maxY);
    for (int gate = 0; gate < 4; ++gate) {
        const QHash<QPoint, quint32> &cells = m_gateCells.at(gate);
        out << quint32(cells.size());
        for (auto it = cells.constBegin(); it != cells.constEnd(); ++it)
            out << qint32(it.key().x()) << qint32(it.key().y()) << quint32(it.value());
    }
}

bool CScanWidget::loadFrom(QDataStream &in)
{
    qint32 encoderPrecision = 10, imagingGate = 0, minX = 0, maxX = 0, minY = 0, maxY = 0;
    double mmPerCount = 0.0, threshold = 0.0, amplitudeScale = 4096.0;
    qint64 originA = 0, originB = 0, currentA = 0, currentB = 0;
    bool gateEnabled = true, hasOrigin = false;
    in >> encoderPrecision >> mmPerCount >> imagingGate >> gateEnabled >> threshold >> amplitudeScale
       >> originA >> originB >> currentA >> currentB >> hasOrigin >> minX >> maxX >> minY >> maxY;
    if (in.status() != QDataStream::Ok || encoderPrecision <= 0
        || !qIsFinite(mmPerCount) || mmPerCount < 0.0
        || imagingGate < 0 || imagingGate > 3
        || !qIsFinite(threshold) || !qIsFinite(amplitudeScale)
        || amplitudeScale <= 0.0 || minX > maxX || minY > maxY) {
        return false;
    }

    m_gateCells = QVector<QHash<QPoint, quint32>>(4);
    quint64 totalCells = 0;
    for (int gate = 0; gate < 4; ++gate) {
        quint32 count = 0;
        in >> count;
        totalCells += count;
        if (in.status() != QDataStream::Ok || totalCells > 1048576
            || (in.device() && qint64(count) * 12 > in.device()->bytesAvailable())) {
            return false;
        }
        QHash<QPoint, quint32> &cells = m_gateCells[gate];
        cells.reserve(int(count));
        for (quint32 i = 0; i < count; ++i) {
            qint32 x = 0, y = 0;
            quint32 amp = 0;
            in >> x >> y >> amp;
            cells.insert(QPoint(int(x), int(y)), amp);
        }
        if (in.status() != QDataStream::Ok)
            return false;
    }

    m_encoderPrecision = qMax(1, int(encoderPrecision));
    m_mmPerCount = qMax(0.0, mmPerCount);
    m_imagingGate = qBound(0, int(imagingGate), 3);
    m_imagingGateEnabled = gateEnabled;
    m_imagingGateThreshold = threshold;
    m_amplitudeScale = amplitudeScale;
    m_originEncoderA = originA;
    m_originEncoderB = originB;
    m_currentEncoderA = currentA;
    m_currentEncoderB = currentB;
    m_hasOrigin = hasOrigin;
    m_minX = minX;
    m_maxX = maxX;
    m_minY = minY;
    m_maxY = maxY;
    update();
    return true;
}

void CScanWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!m_imagingGateEnabled) {
        p.setPen(QColor("#b42318"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("当前成像闸门未启用"));
        return;
    }
    const QHash<QPoint, quint32> &cells = m_gateCells[m_imagingGate];
    if (cells.isEmpty()) {
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
    for (auto it = cells.constBegin(); it != cells.constEnd(); ++it) {
        const qreal px = (it.key().x() - visibleMinX) * (raster.width() - 1)
                         / spanX;
        const qreal py = flatY ? (raster.height() - 1) / 2.0
                               : (visibleMaxY - it.key().y())
                                     * (raster.height() - 1) / spanY;
        const double thresholdAmplitude = m_amplitudeScale
                                          * m_imagingGateThreshold / 100.0;
        const double normalized = double(it.value()) >= thresholdAmplitude
            ? double(it.value()) / qMax(1.0, m_amplitudeScale)
            : 0.0;
        rp.fillRect(QRectF(px - 2, py - 2, 5, 5), amplitudeColor(normalized));
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
        const double mmA = double(countA) * m_mmPerCount;
        const double mmB = double(countB) * m_mmPerCount;
        p.drawText(QRectF(x - 55, target.bottom() + 7, 110, 20),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(mmA, 'f', 1));
        if (!flatY || i == 2)
            p.drawText(QRectF(2, y - 10, 74, 20),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(mmB, 'f', 1));
    }
    p.drawText(QRectF(target.left(), 5, target.width(), 28),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("C 扫 Gate %1　阈值 %2%　原点 A/B：%3/%4　当前 A/B：%5/%6　精度：%7 计数/格（%8 mm/格）")
                   .arg(QStringLiteral("ABCI").mid(m_imagingGate, 1))
                   .arg(m_imagingGateThreshold, 0, 'f', 1)
                   .arg(m_originEncoderA).arg(m_originEncoderB)
                   .arg(m_currentEncoderA).arg(m_currentEncoderB)
                   .arg(m_encoderPrecision)
                   .arg(double(m_encoderPrecision) * m_mmPerCount, 0, 'f', 2));
    p.drawText(QRectF(2, target.top() - 4, 76, 22),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("B（mm）"));
    p.drawText(QRectF(target.left(), target.bottom() + 34,
                      target.width(), 20),
               Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("A（mm）"));
}

void CScanWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_imagingGateEnabled
        || m_gateCells[m_imagingGate].isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRect available = rect().adjusted(82, 38, -32, -62);
    const bool flatX = m_minX == m_maxX;
    const bool flatY = m_minY == m_maxY;
    const int visibleMinX = flatX ? m_minX - 1 : m_minX;
    const int visibleMaxX = flatX ? m_maxX + 1 : m_maxX;
    const int visibleMinY = m_minY;
    const int visibleMaxY = flatY ? m_maxY + 1 : m_maxY;
    const double spanX = qMax(1, visibleMaxX - visibleMinX);
    const double spanY = qMax(1, visibleMaxY - visibleMinY);
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
    if (!target.contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    const double xRatio = qBound(0.0,
        (event->position().x() - target.left()) / qMax<qreal>(1.0, target.width()), 1.0);
    const double yRatio = qBound(0.0,
        (event->position().y() - target.top()) / qMax<qreal>(1.0, target.height()), 1.0);
    const int x = qRound(visibleMinX + xRatio * spanX);
    const int y = flatY ? m_minY : qRound(visibleMaxY - yRatio * spanY);
    if (m_gateCells[m_imagingGate].contains(QPoint(x, y)))
        emit cellClicked(x, y);
}
