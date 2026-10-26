#pragma once
#include "wire_codec.h"
#include "qcustomplot.h"
#include <QWidget>
#include <array>

class TrendPlot : public QWidget {
    Q_OBJECT
public:
    explicit TrendPlot(QWidget *parent=nullptr);
    void setSamples(const QVector<Wire::Sample> &samples);
    void setChannel(int channel,bool visible);
    void setWindow(int seconds);
    void followLatest();
    QCustomPlot *plot() const {return canvas;}
    qsizetype sampleCount() const {return points.size();}
signals:
    void selected(const QString &text);
private:
    QCustomPlot *canvas;
    std::array<QCPAxisRect*,4> axes{};
    std::array<QCPGraph*,4> graphs{};
    std::array<QCPItemTracer*,4> tracers{};
    std::array<QCPItemText*,4> tags{};
    QVector<Wire::Sample> points;
    QVector<double> times;
    int window=10;
    bool follow=true,syncing=false;
    void pick(QMouseEvent *event);
};
