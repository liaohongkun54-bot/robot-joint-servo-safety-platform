#pragma once
#include "history.h"
#include "monitor_link.h"
#include "trend_plot.h"
#include "plan_controller.h"
#include <QMainWindow>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QSlider>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    MainWindow(SerialEndpoint &endpoint,const QString &journalDirectory,QWidget *parent=nullptr);
    bool loadHistory(const QString &path,QString &error);
    bool selfTest(const QString &report,const QString &screenshot);
    void selectPage(int index);
private:
    MonitorLink link;
    PlanController controller;
    QComboBox *ports,*baud,*purpose;
    QLabel *connection,*mcu,*device,*readout,*historyInfo,*cursor;
    QPushButton *connectButton,*capture,*play;
    QPlainTextEdit *diagnostics;
    QTableWidget *events;
    QSlider *progress;
    TrendPlot *liveChart,*historyChart;
    QVector<Wire::Sample> liveSamples;
    History history;
    QTimer replayTimer,freshnessTimer,statusRefresh;
    QElapsedTimer statusAge,sampleAge;
    QString statusText;
    bool capabilityTelemetry=false,capturing=false;
    void setupUi();
    void onFrame(const Wire::Frame &frame);
    void updatePlayback(int position);
    void exportCsv();
    void exportReport();
};
