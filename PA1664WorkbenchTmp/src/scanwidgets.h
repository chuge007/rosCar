#pragma once

#include "packetdecoder.h"
#include <QImage>
#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QWidget>

class QMouseEvent;
class QWheelEvent;
class QDataStream;

class AScanWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AScanWidget(QWidget *parent = nullptr);
    void setWaveform(const QVector<qint16> &samples, double scale, double rangeStart, double rangeEnd);
    void setGates(const QVector<QVector<double>> &gates);
    void setBipolar(bool bipolar);
    void setActiveGate(int gateIndex);
    void beginGatePlacement(int gateIndex);
    void beginSurfaceWindowPlacement();
    void cancelPlacement();
    void setSurfaceTracking(bool enabled, double windowStart, double windowEnd,
                            double thresholdPercent, bool holdMissing,
                            const QVector<bool> &trackedGates);
    void setReviewCursor(double range, bool visible);
    quint64 paintCount() const { return m_paintCount; }

signals:
    void gateSelected(int gateIndex);
    void gateRangeEdited(int gateIndex, double start, double end);
    void gateThresholdEdited(int gateIndex, double thresholdPercent);
    void surfaceWindowEdited(double start, double end);
    void surfaceTrackingUpdated(bool valid, double reference, double current);
    void interactionHint(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class PlacementMode { None, Gate, SurfaceWindow };
    enum class DragPart { None, GateStart, GateEnd, GateThreshold, GateBody };

    double positionToRange(qreal x) const;
    qreal rangeToPosition(double value) const;
    double positionToThreshold(qreal y) const;
    qreal thresholdToPosition(double threshold) const;
    QVector<double> displayedGate(int index) const;
    void updateSurfaceTracking();
    void updateHoverCursor(const QPointF &position);

    QVector<qint16> m_samples;
    QVector<QVector<double>> m_gates;
    double m_scale = 4096.0;
    double m_rangeStart = 0.0;
    double m_rangeEnd = 50.0;
    bool m_bipolar = false;
    QPixmap m_staticBackground;
    QRect m_waveformDirtyRect;
    quint64 m_paintCount = 0;
    int m_activeGate = -1;
    PlacementMode m_placementMode = PlacementMode::None;
    int m_placementGate = -1;
    bool m_placementHasStart = false;
    double m_placementStart = 0.0;
    QPointF m_mousePosition;
    DragPart m_dragPart = DragPart::None;
    int m_dragGate = -1;
    double m_dragAnchorRange = 0.0;
    double m_dragStart = 0.0;
    double m_dragEnd = 0.0;
    bool m_surfaceEnabled = false;
    double m_surfaceWindowStart = 0.0;
    double m_surfaceWindowEnd = 0.0;
    double m_surfaceThreshold = 20.0;
    bool m_surfaceHoldMissing = true;
    QVector<bool> m_surfaceTrackedGates;
    bool m_surfaceReferenceValid = false;
    bool m_surfaceCurrentValid = false;
    bool m_surfaceLastValid = false;
    double m_surfaceReference = 0.0;
    double m_surfaceCurrent = 0.0;
    double m_surfaceLast = 0.0;
    double m_reviewCursor = 0.0;
    bool m_reviewCursorVisible = false;
};

class EScanWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EScanWidget(QWidget *parent = nullptr);
    void setFrame(const DecodedFrame &frame, double scale);
    void setImagingGate(bool enabled, double start, double end,
                        double rangeStart, double rangeEnd);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage m_image;
    bool m_gateEnabled = true;
    double m_gateStart = 0.0;
    double m_gateEnd = 50.0;
    double m_rangeStart = 0.0;
    double m_rangeEnd = 50.0;
};

class BScanWidget : public QWidget
{
    Q_OBJECT
public:
    enum ViewMode { AmplitudeView = 0, ProfileView = 1 };
    enum PaletteMode { HeatPalette = 0, GrayscalePalette = 1, RfPalette = 2 };
    enum ScaleMode { FixedFullScale = 0, DynamicScale = 1 };

    explicit BScanWidget(QWidget *parent = nullptr);
    void appendFrame(const DecodedFrame &frame, double scale, int sourceBeam = 0);
    void appendFrameAtColumn(const DecodedFrame &frame, double scale, int column,
                             int sourceBeam = 0);
    void appendFrames(const QVector<DecodedFrame> &frames, double scale,
                      int sourceBeam = 0);
    void setEncoderPrecision(int countsPerColumn);
    void setMmPerCount(double mmPerCount);
    void setRange(double rangeStart, double rangeEnd);
    void setImagingGate(bool enabled, double start, double end,
                        double thresholdPercent = 0.0);
    void setSurfaceTracking(bool enabled, double windowStart, double windowEnd,
                            double thresholdPercent, bool holdMissing,
                            bool alignSelectedGate);
    void setViewMode(int mode);
    void setPaletteMode(int mode);
    void setScaleMode(int mode);
    void setDisplayLevels(double lowerPercent, double upperPercent, double gamma);
    void setDisplayReject(double percent);
    void setInterpolateMissing(bool enabled);
    void resetView();
    void clear();
    void saveTo(QDataStream &out) const;
    bool loadFrom(QDataStream &in);

signals:
    void columnClicked(int column, double depth);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct ColumnData {
        QVector<qint16> samples;
        double scale = 1.0;
        double surface = 0.0;
        int score = 0;
        bool valid = false;
        bool interpolated = false;
        bool surfaceValid = false;
    };

    void initializeImage(int sampleCount);
    bool appendFrameInternal(const DecodedFrame &frame, double scale,
                             int sourceBeam);
    bool appendFrameAtColumnInternal(const DecodedFrame &frame, double scale,
                                     int column, int sourceBeam);
    bool ensureImageWidth(int requiredColumn);
    ColumnData makeColumn(const QVector<qint16> &samples, double scale) const;
    void updateSurface(ColumnData &column);
    void interpolateGap(int previousColumn, int nextColumn,
                        const ColumnData &previous, const ColumnData &next);
    void rebuildImage();
    void rebuildColumn(int column);
    void rebuildColorTable();
    double normalizedMagnitude(const ColumnData &column, int row) const;
    int sourceRowForDisplay(const ColumnData &column, int displayRow) const;
    int profileRow(const ColumnData &column, double *amplitude = nullptr) const;
    QRect imageTargetRect() const;
    QRect visibleSourceRect() const;

    QImage m_image;
    QVector<ColumnData> m_columns;
    int m_encoderPrecision = 10;
    double m_mmPerCount = 0.0;
    int m_direction = 0;
    int m_lastColumn = -1;
    int m_usedColumns = 0;
    qint64 m_originEncoder = 0;
    qint64 m_currentEncoder = 0;
    bool m_hasOrigin = false;
    double m_rangeStart = 0.0;
    double m_rangeEnd = 50.0;
    bool m_gateEnabled = true;
    double m_gateStart = 0.0;
    double m_gateEnd = 50.0;
    double m_gateThreshold = 0.0;
    bool m_surfaceEnabled = false;
    bool m_surfaceAlignGate = false;
    bool m_surfaceHoldMissing = true;
    double m_surfaceWindowStart = 0.0;
    double m_surfaceWindowEnd = 0.0;
    double m_surfaceThreshold = 20.0;
    double m_surfaceReference = 0.0;
    double m_surfaceLast = 0.0;
    bool m_surfaceReferenceValid = false;
    bool m_surfaceLastValid = false;
    int m_viewMode = AmplitudeView;
    int m_paletteMode = HeatPalette;
    int m_scaleMode = FixedFullScale;
    double m_displayLower = 0.0;
    double m_displayUpper = 1.0;
    double m_displayGamma = 1.0;
    double m_displayReject = 0.0;
    double m_dynamicMaximum = 1.0;
    bool m_interpolateMissing = false;
    int m_acquiredColumns = 0;
    int m_interpolatedColumns = 0;
    int m_gapColumns = 0;
    int m_largestGap = 0;
    double m_zoom = 1.0;
    double m_viewCenter = 0.5;
    QPointF m_mousePosition;
    bool m_hovering = false;
};

class CScanWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CScanWidget(QWidget *parent = nullptr);
    void appendFrame(const DecodedFrame &frame, double scale, int sourceBeam = 0);
    void appendFrames(const QVector<DecodedFrame> &frames, double scale,
                      int sourceBeam = 0);
    void setEncoderPrecision(int countsPerCell);
    void setMmPerCount(double mmPerCount);
    void setImagingGate(int gateIndex, bool enabled, double thresholdPercent);
    int imagingGate() const { return m_imagingGate; }
    void clear();
    void saveTo(QDataStream &out) const;
    bool loadFrom(QDataStream &in);

signals:
    void cellClicked(int x, int y);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    bool appendFrameInternal(const DecodedFrame &frame, double scale,
                             int sourceBeam);

    QVector<QHash<QPoint, quint32>> m_gateCells = QVector<QHash<QPoint, quint32>>(4);
    int m_imagingGate = 0;
    bool m_imagingGateEnabled = true;
    double m_imagingGateThreshold = 0.0;
    double m_amplitudeScale = 4096.0;
    int m_encoderPrecision = 10;
    double m_mmPerCount = 0.0;
    qint64 m_originEncoderA = 0;
    qint64 m_originEncoderB = 0;
    qint64 m_currentEncoderA = 0;
    qint64 m_currentEncoderB = 0;
    bool m_hasOrigin = false;
    int m_minX = 0;
    int m_maxX = 0;
    int m_minY = 0;
    int m_maxY = 0;
};
