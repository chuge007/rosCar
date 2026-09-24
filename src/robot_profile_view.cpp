#include "robot_profile_view.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

RobotProfileView::RobotProfileView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(260, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void RobotProfileView::setPoints(const QVector<QVector3D> &points)
{
    m_points = points;
    update();
}

void RobotProfileView::clear()
{
    m_points.clear();
    update();
}

void RobotProfileView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#101b27")));
    const QRect plot = rect().adjusted(36, 24, -12, -28);
    painter.setPen(QColor(QStringLiteral("#33495e")));
    for (int i = 0; i <= 10; ++i) {
        const int x = plot.left() + plot.width() * i / 10;
        const int y = plot.top() + plot.height() * i / 10;
        painter.drawLine(x, plot.top(), x, plot.bottom());
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    QVector<QPointF> valid;
    valid.reserve(m_points.size());
    double minX = 0.0;
    double maxX = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;
    for (const QVector3D &point : m_points) {
        const double x = point.x();
        const double z = point.z();
        if (!std::isfinite(x) || !std::isfinite(z))
            continue;
        if (valid.isEmpty()) {
            minX = maxX = x;
            minZ = maxZ = z;
        } else {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
        valid.append(QPointF(x, z));
    }

    if (valid.size() < 2 || maxX <= minX) {
        painter.setPen(QColor(QStringLiteral("#aebed0")));
        painter.drawText(plot, Qt::AlignCenter,
                         QStringLiteral("等待激光 SDK 轮廓数据"));
        return;
    }

    const double xPadding = std::max(0.001, (maxX - minX) * 0.05);
    const double zPadding = std::max(0.001, (maxZ - minZ) * 0.08);
    minX -= xPadding;
    maxX += xPadding;
    minZ -= zPadding;
    maxZ += zPadding;
    const auto mapPoint = [&](const QPointF &point) {
        return QPointF(plot.left() + (point.x() - minX) / (maxX - minX) * plot.width(),
                       plot.bottom() - (point.y() - minZ) / (maxZ - minZ) * plot.height());
    };

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#59d8ff")), 2));
    QPointF previous;
    bool hasPrevious = false;
    for (const QPointF &point : valid) {
        const QPointF mapped = mapPoint(point);
        if (hasPrevious)
            painter.drawLine(previous, mapped);
        previous = mapped;
        hasPrevious = true;
    }

    painter.setPen(QColor(QStringLiteral("#aebed0")));
    painter.drawText(8, 18, QStringLiteral("X/Z 轮廓"));
    painter.drawText(plot.left(), height() - 8,
                     QStringLiteral("X %1 .. %2    Z %3 .. %4")
                         .arg(minX, 0, 'f', 2)
                         .arg(maxX, 0, 'f', 2)
                         .arg(minZ, 0, 'f', 2)
                         .arg(maxZ, 0, 'f', 2));
}
