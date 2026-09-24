#pragma once

#include <QVector>
#include <QVector3D>
#include <QWidget>

class QPaintEvent;

class RobotProfileView final : public QWidget
{
    Q_OBJECT
public:
    explicit RobotProfileView(QWidget *parent = nullptr);

public slots:
    void setPoints(const QVector<QVector3D> &points);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QVector3D> m_points;
};
