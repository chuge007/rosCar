#include "robot_profile_view.h"
#include "robot_app_logger.h"

#include "utf8_compat.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

RobotProfileView::RobotProfileView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(260, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto *freshnessTimer = new QTimer(this);
    connect(freshnessTimer, &QTimer::timeout, this, [this] {
        const qint64 age = m_receivedAtEpochMs > 0
                               ? QDateTime::currentMSecsSinceEpoch() - m_receivedAtEpochMs
                               : 0;
        if (m_profileUpdatePending ||
            (m_receivedAtEpochMs > 0 && age >= 180)) {
            m_profileUpdatePending = false;
            update();
        }
    });
    freshnessTimer->start(150);
}

void RobotProfileView::setPoints(const QVector<QVector3D> &points)
{
    m_points = points;
    m_rawPoints = points;
    m_hasDetection = false;
    m_receivedAtEpochMs = 0;
    m_displayRangeInitialized = false;
    m_profileUpdatePending = true;
    update();
}

void RobotProfileView::setProfileObservation(
    const QVector<QVector3D> &points,
    const crawling::LaserGapDetection &detection,
    quint32 sourceFrameNumber,
    qint64 receivedAtEpochMs)
{
    m_rawPoints = points;
    m_points = points;
    m_detection = detection;
    if (detection.profileAxisHalfSpanX > 0.0 &&
        std::isfinite(detection.profileAxisHalfSpanX))
        m_displayHalfSpanX = detection.profileAxisHalfSpanX;
    m_hasDetection = true;
    m_frameNumber = sourceFrameNumber;
    m_receivedAtEpochMs = receivedAtEpochMs;
    ++m_receivedProfiles;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastReceiveDiagnosticMs < 0 || now - m_lastReceiveDiagnosticMs >= 1000) {
        m_lastReceiveDiagnosticMs = now;
        crawling::AppLogger::write(QStringLiteral("CAMERA.UI"),
            QStringLiteral("event=profile_ui_received frame=%1 samples=%2 age_ms=%3 "
                           "detection_valid=%4 view_mode=%5 visible=%6 widget_size=%7x%8 "
                           "received_profiles=%9 profile_paints=%10 "
                           "coordinate_basis=sdk_x_zero axis_half_span_x=%11 "
                           "center_x=%12 center_ratio=%13")
                .arg(sourceFrameNumber).arg(points.size()).arg(now - receivedAtEpochMs)
                .arg(detection.valid).arg(static_cast<int>(m_viewMode)).arg(isVisible())
                .arg(width()).arg(height()).arg(m_receivedProfiles).arg(m_profilePaints)
                .arg(m_displayHalfSpanX, 0, 'g', 9)
                .arg(detection.profileCenterX, 0, 'g', 9)
                .arg(detection.absoluteCenterRatio, 0, 'f', 6));
    }
    m_profileUpdatePending = true;
    update();
}

void RobotProfileView::clear()
{
    m_points.clear();
    m_rawPoints.clear();
    m_hasDetection = false;
    m_frameNumber = 0;
    m_receivedAtEpochMs = 0;
    m_displayRangeInitialized = false;
    m_displayHalfSpanX = 0.0;
    m_profileUpdatePending = false;
    update();
}

QVector<QVector3D> RobotProfileView::rawPoints() const
{
    return m_rawPoints;
}

crawling::LaserGapDetection RobotProfileView::currentDetection() const
{
    return m_detection;
}

void RobotProfileView::setTemplateSelectionEnabled(bool enabled)
{
    m_templateSelectionEnabled = enabled;
    m_templateSelectionDragging = false;
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void RobotProfileView::clearTemplateSelection()
{
    m_templateSelectionDragging = false;
    m_hasTemplateSelection = false;
    m_templateSelectionFirstX = 0.0;
    m_templateSelectionSecondX = 0.0;
    emit templateSelectionChanged(false, 0.0, 0.0);
    update();
}

bool RobotProfileView::hasTemplateSelection() const
{
    return m_hasTemplateSelection &&
           std::abs(m_templateSelectionSecondX - m_templateSelectionFirstX) > 1e-9;
}

double RobotProfileView::templateSelectionMinimumX() const
{
    return std::min(m_templateSelectionFirstX, m_templateSelectionSecondX);
}

double RobotProfileView::templateSelectionMaximumX() const
{
    return std::max(m_templateSelectionFirstX, m_templateSelectionSecondX);
}

void RobotProfileView::setViewMode(int mode)
{
    if (mode < static_cast<int>(ViewMode::XY) ||
        mode > static_cast<int>(ViewMode::YZ)) {
        return;
    }
    m_viewMode = static_cast<ViewMode>(mode);
    m_displayRangeInitialized = false;
    m_profileUpdatePending = true;
    update();
}

void RobotProfileView::paintProfile(QPainter &painter, const QRect &area)
{
    m_lastPlotGeometryValid = false;
    ++m_profilePaints;
    const qint64 age = m_receivedAtEpochMs > 0
                           ? QDateTime::currentMSecsSinceEpoch() - m_receivedAtEpochMs
                           : 0;
    const bool fresh = m_receivedAtEpochMs > 0 && age >= 0 && age <= 180;
    QVector<QPointF> valid;
    QVector<int> validIndices;
    valid.reserve(m_points.size());
    validIndices.reserve(m_points.size());
    double minHorizontal = 0.0;
    double maxHorizontal = 0.0;
    double minVertical = 0.0;
    double maxVertical = 0.0;
    QString viewTitle;
    QString horizontalName;
    QString verticalName;
    int nonfinitePoints = 0;
    int nonpositiveZPoints = 0;
    int nonincreasingXPoints = 0;
    switch (m_viewMode) {
    case ViewMode::XY:
        viewTitle = QStringLiteral("X/Y 轮廓");
        horizontalName = QStringLiteral("X");
        verticalName = QStringLiteral("Y");
        break;
    case ViewMode::YZ:
        viewTitle = QStringLiteral("Y/Z 轮廓");
        horizontalName = QStringLiteral("Y");
        verticalName = QStringLiteral("Z");
        break;
    case ViewMode::XZ:
    default:
        viewTitle = QStringLiteral("X/Z 轮廓");
        horizontalName = QStringLiteral("X");
        verticalName = QStringLiteral("Z");
        break;
    }
    for (int index = 0; index < m_points.size(); ++index) {
        const QVector3D &point = m_points[index];
        double horizontal = 0.0;
        double vertical = 0.0;
        switch (m_viewMode) {
        case ViewMode::XY:
            horizontal = point.x();
            vertical = point.y();
            break;
        case ViewMode::YZ:
            horizontal = point.y();
            vertical = point.z();
            break;
        case ViewMode::XZ:
        default:
            horizontal = point.x();
            vertical = point.z();
            break;
        }
        if (!std::isfinite(horizontal) || !std::isfinite(vertical)) {
            ++nonfinitePoints;
            continue;
        }
        if (m_viewMode == ViewMode::XZ && vertical <= 0.0) {
            ++nonpositiveZPoints;
        }
        if (m_viewMode == ViewMode::XZ && !validIndices.isEmpty() &&
            horizontal <= valid.last().x()) {
            ++nonincreasingXPoints;
        }
        if (valid.isEmpty()) {
            minHorizontal = maxHorizontal = horizontal;
            minVertical = maxVertical = vertical;
        } else {
            minHorizontal = std::min(minHorizontal, horizontal);
            maxHorizontal = std::max(maxHorizontal, horizontal);
            minVertical = std::min(minVertical, vertical);
            maxVertical = std::max(maxVertical, vertical);
        }
        valid.append(QPointF(horizontal, vertical));
        validIndices.append(index);
    }

    const qint64 diagnosticNow = QDateTime::currentMSecsSinceEpoch();
    if (m_lastPaintDiagnosticMs < 0 || diagnosticNow - m_lastPaintDiagnosticMs >= 1000) {
        m_lastPaintDiagnosticMs = diagnosticNow;
        const QString reason = m_points.isEmpty() ? QStringLiteral("no_ui_samples")
            : valid.size() < 2 ? QStringLiteral("too_few_points_after_filter")
            : maxHorizontal <= minHorizontal ? QStringLiteral("zero_horizontal_span")
            : area.width() <= 0 || area.height() <= 0 ? QStringLiteral("plot_too_small")
            : fresh ? QStringLiteral("OK") : QStringLiteral("stale_trace");
        crawling::AppLogger::write(QStringLiteral("CAMERA.UI"),
            QStringLiteral("event=profile_paint frame=%1 samples=%2 plotted_points=%3 "
                           "filtered_points=%4 age_ms=%5 fresh=%6 view_mode=%7 "
                           "horizontal_range=%8,%9 vertical_range=%10,%11 "
                           "area=%12x%13 reason=%14 received_profiles=%15 profile_paints=%16 "
                           "nonfinite=%17 nonpositive_z=%18 nonincreasing_x=%19 detection_valid=%20 "
                           "display_data=raw_xyz first_plotted_column=%21 last_plotted_column=%22")
                .arg(m_frameNumber).arg(m_points.size()).arg(valid.size())
                .arg(m_points.size() - valid.size()).arg(age).arg(fresh)
                .arg(static_cast<int>(m_viewMode))
                .arg(minHorizontal, 0, 'g', 9).arg(maxHorizontal, 0, 'g', 9)
                .arg(minVertical, 0, 'g', 9).arg(maxVertical, 0, 'g', 9)
                .arg(area.width()).arg(area.height()).arg(reason)
                .arg(m_receivedProfiles).arg(m_profilePaints)
                .arg(nonfinitePoints).arg(nonpositiveZPoints).arg(nonincreasingXPoints)
                .arg(m_detection.valid)
                .arg(validIndices.isEmpty() ? -1 : validIndices.front())
                .arg(validIndices.isEmpty() ? -1 : validIndices.back()));
    }

    if (valid.size() < 2) {
        painter.setPen(QColor(QStringLiteral("#aebed0")));
        painter.drawText(area, Qt::AlignCenter,
                         QStringLiteral("等待激光 SDK 轮廓数据"));
        return;
    }

    if (m_viewMode == ViewMode::XZ) {
        if (!m_hasDetection || m_detection.profileAxisHalfSpanX <= 0.0)
            m_displayHalfSpanX = std::max(
                m_displayHalfSpanX,
                std::max(std::abs(minHorizontal), std::abs(maxHorizontal)));
        const double halfSpanX = std::max(m_displayHalfSpanX,
            std::max(std::abs(minHorizontal), std::abs(maxHorizontal)));
        minHorizontal = -halfSpanX;
        maxHorizontal = halfSpanX;
        if (!m_displayRangeInitialized) {
            m_displayMinVertical = minVertical;
            m_displayMaxVertical = maxVertical;
            m_displayRangeInitialized = true;
        } else {
            constexpr double rangeAlpha = 0.20;
            m_displayMinVertical +=
                rangeAlpha * (minVertical - m_displayMinVertical);
            m_displayMaxVertical +=
                rangeAlpha * (maxVertical - m_displayMaxVertical);
        }
        minVertical = std::min(minVertical, m_displayMinVertical);
        maxVertical = std::max(maxVertical, m_displayMaxVertical);
    }

    const double horizontalPadding = m_viewMode == ViewMode::XZ
        ? 0.0 : std::max(0.001, (maxHorizontal - minHorizontal) * 0.05);
    const double verticalPadding =
        std::max(0.001, (maxVertical - minVertical) * 0.08);
    minHorizontal -= horizontalPadding;
    maxHorizontal += horizontalPadding;
    minVertical -= verticalPadding;
    maxVertical += verticalPadding;
    if (maxHorizontal <= minHorizontal || maxVertical <= minVertical) {
        painter.setPen(QColor(QStringLiteral("#aebed0")));
        painter.drawText(area, Qt::AlignCenter,
                         QStringLiteral("等待激光 SDK 轮廓数据"));
        return;
    }
    const QRect plot = area;
    m_lastPlotArea = plot;
    m_lastPlotMinimumX = minHorizontal;
    m_lastPlotMaximumX = maxHorizontal;
    m_lastPlotGeometryValid = m_viewMode == ViewMode::XZ;
    const auto mapPoint = [&](const QPointF &point) {
        return QPointF(
            plot.left() + (point.x() - minHorizontal) /
                              (maxHorizontal - minHorizontal) * plot.width(),
            plot.bottom() - (point.y() - minVertical) /
                                (maxVertical - minVertical) * plot.height());
    };

    painter.setPen(QColor(QStringLiteral("#33495e")));
    for (int index = 0; index <= 10; ++index) {
        const int x = plot.left() + plot.width() * index / 10;
        const int y = plot.top() + plot.height() * index / 10;
        painter.drawLine(x, plot.top(), x, plot.bottom());
        painter.drawLine(plot.left(), y, plot.right(), y);
    }
    if (m_viewMode == ViewMode::XZ) {
        const double zeroX = mapPoint(QPointF(0.0, minVertical)).x();
        painter.setPen(QPen(QColor(QStringLiteral("#9aa5af")), 1, Qt::DashLine));
        painter.drawLine(QPointF(zeroX, plot.top()), QPointF(zeroX, plot.bottom()));
    }

    painter.save();
    painter.setClipRect(plot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool located = m_viewMode == ViewMode::XZ && fresh &&
                         m_hasDetection && m_detection.valid &&
                         m_detection.profileContour &&
                         m_detection.gapStartPx >= 0 &&
                         m_detection.gapEndPx > m_detection.gapStartPx &&
                         m_detection.gapEndPx < m_points.size();
    if (located) {
        const int start = m_detection.gapStartPx;
        const int end = m_detection.gapEndPx;
        const QPointF left = mapPoint(QPointF(m_points[start].x(), minVertical));
        const QPointF right = mapPoint(QPointF(m_points[end].x(), minVertical));
        if (std::isfinite(left.x()) && std::isfinite(right.x())) {
            painter.fillRect(QRectF(left.x(), plot.top(), right.x() - left.x(),
                                    plot.height()),
                             QColor(83, 238, 130, 25));
        }
    }
    if (m_viewMode == ViewMode::XZ && hasTemplateSelection()) {
        const double leftX = mapPoint(QPointF(templateSelectionMinimumX(), minVertical)).x();
        const double rightX = mapPoint(QPointF(templateSelectionMaximumX(), minVertical)).x();
        if (std::isfinite(leftX) && std::isfinite(rightX)) {
            const QRectF selected(std::min(leftX, rightX), plot.top(),
                                  std::abs(rightX - leftX), plot.height());
            painter.fillRect(selected, QColor(244, 75, 75, 34));
            painter.setPen(QPen(QColor(QStringLiteral("#f44b4b")), 2));
            painter.drawRect(selected);
        }
    }
    if (m_viewMode == ViewMode::XZ && m_detection.profileNoise > 0.0) {
        painter.setPen(QPen(QColor(QStringLiteral("#f6cb54")), 1,
                            Qt::DashLine));
        painter.drawLine(
            mapPoint(QPointF(minHorizontal,
                             m_detection.profileBaselineOffset +
                                 m_detection.profileBaselineSlope * minHorizontal)),
            mapPoint(QPointF(maxHorizontal,
                             m_detection.profileBaselineOffset +
                                 m_detection.profileBaselineSlope * maxHorizontal)));
    }

    painter.setPen(QPen(fresh ? QColor(QStringLiteral("#59d8ff"))
                              : QColor(QStringLiteral("#78848b")),
                       2));
    int previousIndex = -2;
    for (int validIndex = 0; validIndex < valid.size(); ++validIndex) {
        const QPointF mapped = mapPoint(valid[validIndex]);
        if (validIndices[validIndex] == previousIndex + 1 &&
            (m_viewMode != ViewMode::XZ || valid[validIndex].x() > valid[validIndex - 1].x())) {
            const QPointF previous = mapPoint(valid[validIndex - 1]);
            painter.drawLine(previous, mapped);
        }
        painter.drawPoint(mapped);
        previousIndex = validIndices[validIndex];
    }

    if (located) {
        painter.setPen(QPen(QColor(QStringLiteral("#53ee82")), 2));
        for (int index : {m_detection.gapStartPx, m_detection.gapEndPx}) {
            const double x = mapPoint(QPointF(m_points[index].x(), minVertical)).x();
            if (std::isfinite(x))
                painter.drawLine(QPointF(x, plot.top()),
                                 QPointF(x, plot.bottom()));
        }
        if (std::isfinite(m_detection.profileCenterX)) {
            const double x = mapPoint(QPointF(m_detection.profileCenterX, minVertical)).x();
            painter.setPen(QPen(QColor(QStringLiteral("#ff668a")), 2,
                                Qt::DashLine));
            painter.drawLine(QPointF(x, plot.top()),
                             QPointF(x, plot.bottom()));
        }
    }
    painter.restore();

    painter.setPen(QColor(QStringLiteral("#aebed0")));
    const QString state = !fresh
                              ? QStringLiteral("数据过期，定位线已隐藏")
                              : (m_hasDetection && m_detection.valid
                                     ? QStringLiteral("本帧焊道候选")
                                     : QStringLiteral("本帧未确认焊道"));
    painter.drawText(plot.left() + 6, plot.top() + 18, state);
    painter.drawText(8, 18, viewTitle);
    painter.drawText(plot.left(), height() - 8,
                     QStringLiteral("%1 %2 .. %3    %4 %5 .. %6    SDK #%7 | %8/%9 | %10 ms")
                         .arg(horizontalName)
                         .arg(minHorizontal, 0, 'f', 2)
                         .arg(maxHorizontal, 0, 'f', 2)
                         .arg(verticalName)
                         .arg(minVertical, 0, 'f', 2)
                         .arg(maxVertical, 0, 'f', 2)
                         .arg(m_frameNumber)
                         .arg(valid.size())
                         .arg(m_points.size())
                         .arg(age));
}

void RobotProfileView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#101b27")));
    paintProfile(painter, rect().adjusted(36, 24, -12, -28));
}

void RobotProfileView::mousePressEvent(QMouseEvent *event)
{
    if (!m_templateSelectionEnabled || !m_lastPlotGeometryValid ||
        !m_lastPlotArea.contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }
    const double ratio = std::clamp(
        (event->position().x() - m_lastPlotArea.left()) /
            std::max(1, m_lastPlotArea.width()),
        0.0, 1.0);
    m_templateSelectionFirstX = m_lastPlotMinimumX +
        ratio * (m_lastPlotMaximumX - m_lastPlotMinimumX);
    m_templateSelectionSecondX = m_templateSelectionFirstX;
    m_templateSelectionDragging = true;
    m_hasTemplateSelection = true;
    update();
    event->accept();
}

void RobotProfileView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_templateSelectionDragging || !m_lastPlotGeometryValid) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const double ratio = std::clamp(
        (event->position().x() - m_lastPlotArea.left()) /
            std::max(1, m_lastPlotArea.width()),
        0.0, 1.0);
    m_templateSelectionSecondX = m_lastPlotMinimumX +
        ratio * (m_lastPlotMaximumX - m_lastPlotMinimumX);
    update();
    event->accept();
}

void RobotProfileView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_templateSelectionDragging) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    mouseMoveEvent(event);
    m_templateSelectionDragging = false;
    emit templateSelectionChanged(hasTemplateSelection(), templateSelectionMinimumX(),
                                  templateSelectionMaximumX());
    event->accept();
}
