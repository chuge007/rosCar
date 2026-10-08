#pragma once

#include "laser_gap_detector.h"

#include <QVector>
#include <QVector3D>
#include <QRect>
#include <QWidget>

class QPaintEvent;
class QPainter;
class QMouseEvent;

class RobotProfileView final : public QWidget
{
    Q_OBJECT
public:
    enum class ViewMode {
        XY,
        XZ,
        YZ
    };

    explicit RobotProfileView(QWidget *parent = nullptr);

public slots:
    void setPoints(const QVector<QVector3D> &points);
    void setProfileObservation(const QVector<QVector3D> &points,
                               const crawling::LaserGapDetection &detection,
                               quint32 sourceFrameNumber = 0,
                               qint64 receivedAtEpochMs = 0);
    void clear();
    void setViewMode(int mode);
    void setTemplateSelectionEnabled(bool enabled);
    void clearTemplateSelection();
    QVector<QVector3D> rawPoints() const;
    crawling::LaserGapDetection currentDetection() const;
    bool hasTemplateSelection() const;
    double templateSelectionMinimumX() const;
    double templateSelectionMaximumX() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

signals:
    void templateSelectionChanged(bool available, double minimumX, double maximumX);

private:
    void paintProfile(QPainter &painter, const QRect &area);

    QVector<QVector3D> m_points;
    QVector<QVector3D> m_rawPoints;
    crawling::LaserGapDetection m_detection;
    bool m_hasDetection = false;
    quint32 m_frameNumber = 0;
    qint64 m_receivedAtEpochMs = 0;
    bool m_profileDisplayInitialized = false;
    bool m_displayRangeInitialized = false;
    double m_displayMinVertical = 0.0;
    double m_displayMaxVertical = 1.0;
    bool m_profileUpdatePending = false;
    qint64 m_lastReceiveDiagnosticMs = -1;
    qint64 m_lastPaintDiagnosticMs = -1;
    quint64 m_receivedProfiles = 0;
    quint64 m_profilePaints = 0;
    QRect m_lastPlotArea;
    double m_lastPlotMinimumX = 0.0;
    double m_lastPlotMaximumX = 0.0;
    bool m_lastPlotGeometryValid = false;
    bool m_templateSelectionEnabled = false;
    bool m_templateSelectionDragging = false;
    bool m_hasTemplateSelection = false;
    double m_templateSelectionFirstX = 0.0;
    double m_templateSelectionSecondX = 0.0;
    ViewMode m_viewMode = ViewMode::XZ;
};
