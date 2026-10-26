#include "trend_plot.h"
#include <QVBoxLayout>
#include <limits>

namespace {
const std::array<QString,4> names{QStringLiteral("转速 / rpm"),QStringLiteral("反馈电流 / raw"),QStringLiteral("编码器 / raw"),QStringLiteral("电调温度 / °C")};
const std::array<QColor,4> colors{QColor("#007c83"),QColor("#cf761b"),QColor("#376cb7"),QColor("#925eab")};
int value(const Wire::Sample &s,int c) {switch(c){case 0:return s.speed;case 1:return s.current;case 2:return s.encoder;default:return s.temperature;}}
}
TrendPlot::TrendPlot(QWidget *parent):QWidget(parent),canvas(new QCustomPlot(this)) {
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->addWidget(canvas);
    canvas->plotLayout()->clear();canvas->setBackground(QColor("#ffffff"));
    canvas->setInteractions(QCP::iRangeDrag|QCP::iRangeZoom|QCP::iSelectPlottables);
    auto *margin=new QCPMarginGroup(canvas);
    for(int i=0;i<4;++i) {
        axes[i]=new QCPAxisRect(canvas);canvas->plotLayout()->addElement(i,0,axes[i]);
        axes[i]->setMinimumSize(120,85);axes[i]->setMarginGroup(QCP::msLeft|QCP::msRight,margin);
        axes[i]->setRangeDrag(Qt::Horizontal|Qt::Vertical);axes[i]->setRangeZoom(Qt::Horizontal|Qt::Vertical);
        auto *x=axes[i]->axis(QCPAxis::atBottom),*y=axes[i]->axis(QCPAxis::atLeft);
        x->setRange(0,10);x->setTickLabels(i==3);if(i==3)x->setLabel(QStringLiteral("MCU样本相对时间 / s"));
        y->setLabel(names[i]);y->setLabelColor(colors[i]);y->setNumberFormat("gb");y->setNumberPrecision(4);
        x->grid()->setPen(QPen(QColor("#e8edf1"),1,Qt::DashLine));y->grid()->setPen(QPen(QColor("#e8edf1"),1,Qt::DashLine));
        graphs[i]=canvas->addGraph(x,y);graphs[i]->setName(names[i]);graphs[i]->setPen(QPen(colors[i],1.5));graphs[i]->setAdaptiveSampling(true);
        tracers[i]=new QCPItemTracer(canvas);tracers[i]->setStyle(QCPItemTracer::tsCircle);tracers[i]->setPen(QPen(colors[i]));tracers[i]->setBrush(colors[i]);tracers[i]->setSize(6);tracers[i]->setVisible(false);
        tags[i]=new QCPItemText(canvas);tags[i]->setClipAxisRect(axes[i]);tags[i]->position->setType(QCPItemPosition::ptAxisRectRatio);tags[i]->position->setAxisRect(axes[i]);tags[i]->position->setCoords(0.985,0.12);tags[i]->setPositionAlignment(Qt::AlignRight|Qt::AlignTop);tags[i]->setColor(colors[i]);tags[i]->setBrush(QColor(255,255,255,230));tags[i]->setPadding(QMargins(5,2,5,2));tags[i]->setText(QStringLiteral("等待反馈"));
        connect(x,qOverload<const QCPRange&>(&QCPAxis::rangeChanged),this,[this,i](const QCPRange &r){if(syncing)return;syncing=true;for(int j=0;j<4;++j)if(i!=j)axes[j]->axis(QCPAxis::atBottom)->setRange(r);syncing=false;});
    }
    connect(canvas,&QCustomPlot::mousePress,this,[this](QMouseEvent *event){if(event->button()==Qt::RightButton)follow=false;});
    connect(canvas,&QCustomPlot::mouseWheel,this,[this](QWheelEvent*){follow=false;});
    connect(canvas,&QCustomPlot::mouseMove,this,[this](QMouseEvent *event){if(event->buttons()!=Qt::NoButton)follow=false;});
    connect(canvas,&QCustomPlot::mouseDoubleClick,this,[this](QMouseEvent*){followLatest();});
    connect(canvas,&QCustomPlot::mouseRelease,this,&TrendPlot::pick);
}
void TrendPlot::setSamples(const QVector<Wire::Sample> &samples) {
    points=samples;times.clear();std::array<QVector<double>,4> values;
    double elapsed=0;quint32 previous=points.isEmpty()?0:points.first().tick;
    for(int j=0;j<points.size();++j) {
        const auto &s=points[j];const quint32 delta=s.tick-previous;
        const bool discontinuity=j&&delta>1000;
        elapsed+=j?(delta<0x80000000u?delta/1000.0:0.001):0;previous=s.tick;
        if(discontinuity) {times.push_back(elapsed-0.0001);for(auto &v:values)v.push_back(std::numeric_limits<double>::quiet_NaN());}
        times.push_back(elapsed);for(int i=0;i<4;++i)values[i].push_back(s.fresh?value(s,i):std::numeric_limits<double>::quiet_NaN());
    }
    for(int i=0;i<4;++i) {
        graphs[i]->setData(times,values[i],true);tracers[i]->setVisible(false);
        tracers[i]->setGraph(times.isEmpty()?nullptr:graphs[i]);
        if(!points.isEmpty()) tags[i]->setText(points.last().fresh?QStringLiteral("最新  %1 %2").arg(value(points.last(),i)).arg(names[i].section(" / ",1)):QStringLiteral("样本失效"));
        else tags[i]->setText(QStringLiteral("等待反馈"));
        if(follow) {graphs[i]->rescaleValueAxis(false,true);const auto r=axes[i]->axis(QCPAxis::atLeft)->range();if(r.size()<2)axes[i]->axis(QCPAxis::atLeft)->setRange(r.center()-1,r.center()+1);else axes[i]->axis(QCPAxis::atLeft)->scaleRange(1.12);}
    }
    if(follow) {const auto right=times.isEmpty()?double(window):qMax(double(window),times.last());axes[0]->axis(QCPAxis::atBottom)->setRange(qMax(0.0,right-window),right);}
    canvas->replot(QCustomPlot::rpQueuedReplot);
}
void TrendPlot::setChannel(int c,bool visible) {if(c<0||c>=4)return;graphs[c]->setVisible(visible);tags[c]->setVisible(visible);tracers[c]->setVisible(false);canvas->replot(QCustomPlot::rpQueuedReplot);}
void TrendPlot::setWindow(int seconds) {window=seconds;followLatest();}
void TrendPlot::followLatest() {follow=true;setSamples(points);}
void TrendPlot::pick(QMouseEvent *event) {
    if(points.isEmpty()||event->button()!=Qt::LeftButton) return;
    int lane=-1;for(int i=0;i<4;++i)if(axes[i]->rect().contains(event->position().toPoint()))lane=i;
    if(lane<0)return;
    const auto key=axes[lane]->axis(QCPAxis::atBottom)->pixelToCoord(event->position().x());
    double nearest=std::numeric_limits<double>::max(),elapsed=0,selectedTime=0;int at=0;
    for(int j=0;j<points.size();++j) {
        if(j) {const quint32 delta=points[j].tick-points[j-1].tick;elapsed+=delta<0x80000000u?delta/1000.0:0.001;}
        if(qAbs(elapsed-key)<nearest) {nearest=qAbs(elapsed-key);at=j;selectedTime=elapsed;}
    }
    const auto &s=points[at];
    for(int i=0;i<4;++i) {tracers[i]->setGraphKey(selectedTime);tracers[i]->setVisible(s.fresh&&graphs[i]->visible());tags[i]->setText(s.fresh?QStringLiteral("游标  %1 %2").arg(value(s,i)).arg(names[i].section(" / ",1)):QStringLiteral("游标样本失效"));}
    emit selected(QStringLiteral("采样 %1/%2 · MCU %3 ms · 请求 %4 · %5").arg(at+1).arg(points.size()).arg(s.tick).arg(s.request).arg(s.fresh?QStringLiteral("有效反馈"):QStringLiteral("失效反馈")));
    canvas->replot(QCustomPlot::rpQueuedReplot);
}
