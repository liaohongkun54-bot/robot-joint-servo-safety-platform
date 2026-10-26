#pragma once
#include "monitor_link.h"
#include "operator_client.h"
#include "plan_run.h"
#include <QElapsedTimer>
#include <functional>

// Binds finite plans and operator sessions to MonitorLink's one physical port.
// Construction and timers never connect, OPEN, GRANT or start a plan by themselves.
class PlanController : public QObject {
    Q_OBJECT
public:
    using Clock=std::function<qint64()>;
    explicit PlanController(MonitorLink &link,QObject *parent=nullptr);
    PlanController(MonitorLink &link,Clock clock,QObject *parent=nullptr);
    bool openSession();
    bool clearFault();
    bool stop();
    bool closeSession();
    bool refreshStatus();
    bool start(const PlanSnapshot &snapshot,bool motionConfirmed,QString &error);
    void cancelScheduling();
    bool running() const {return run&&!run->terminal();}
    QString sessionState() const {return operatorClient.state();}
    bool sessionActive() const {return operatorClient.active();}
    QJsonObject report() const;
    void advance(); // monotonic-clock tick; also driven by the GUI event loop
signals:
    void changed();
    void frameReceived(const Wire::Frame &frame);
    void message(const QString &message);
private:
    MonitorLink &link;
    QElapsedTimer monotonic;
    Clock now;
    OperatorClient operatorClient;
    QTimer timer;
    std::unique_ptr<PlanRun> run;
    QString runJournal,runId;
    std::optional<Wire::Task> motionTask;
    int grantIndex=-1;
    bool owns=false,pumping=false,finishing=false,closeAttempted=false,needsClearance=false,finalized=false,recordHealthy=true;
    quint32 clearanceId=0,clearanceNonce=0;
    qint64 clearanceDeadline=0;
    quint32 probeId=0,probeNonce=0;
    quint8 probeType=0;
    qint64 probeDeadline=0,lastProbe=0;
    bool acquire();
    bool record(const QString &event,QJsonObject fields);
    void receive(const Wire::Frame &frame);
    void finish();
    void release();
    bool probe(quint8 type,qint64 time);
    bool cleanupPending() const {return finishing||(run&&run->terminal()&&!finalized);}
};
