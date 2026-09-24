#pragma once

#include "packetdecoder.h"
#include <QImage>
#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QWidget>

class AScanWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AScanWidget(QWidget *parent = nullptr);
    void setWaveform(const QVector<qint16> &samples, double scale, double rangeStart, double rangeEnd);
    void setGates(const QVector<QVector<double>> &gates);
    void setBipolar(bool bipolar);
    quint64 paintCount() const { return m_paintCount; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<qint16> m_samples;
    QVector<QVector<double>> m_gates;
    double m_scale = 4096.0;
    double m_rangeStart = 0.0;
    double m_rangeEnd = 50.0;
    bool m_bipolar = false;
    QPixmap m_staticBackground;
    QRect m_waveformDirtyRect;
    quint64 m_paintCount = 0;
};

class EScanWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EScanWidget(QWidget *parent = nullptr);
    void setFrame(const DecodedFrame &frame, double scale);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage m_image;
};

class BScanWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BScanWidget(QWidget *parent = nullptr);
    void appendFrame(const DecodedFrame &frame, double scale, int sourceBeam = 0);
    void appendFrames(const QVector<DecodedFrame> &frames, double scale,
                      int sourceBeam = 0);
    void setEncoderPrecision(int countsPerColumn);
    void setRange(double rangeStart, double rangeEnd);
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void initializeImage(int sampleCount);
    bool appendFrameInternal(const DecodedFrame &frame, double scale,
                             int sourceBeam);
    bool ensureImageWidth(int requiredColumn);

    QImage m_image;
    int m_encoderPrecision = 10;
    int m_direction = 0;
    int m_lastColumn = -1;
    int m_usedColumns = 0;
    qint64 m_originEncoder = 0;
    qint64 m_currentEncoder = 0;
    bool m_hasOrigin = false;
    double m_rangeStart = 0.0;
    double m_rangeEnd = 50.0;
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
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool appendFrameInternal(const DecodedFrame &frame, double scale,
                             int sourceBeam);

    QHash<QPoint, uchar> m_cells;
    int m_encoderPrecision = 10;
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
