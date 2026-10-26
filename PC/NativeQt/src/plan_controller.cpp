#include "plan_controller.h"
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <QUuid>

PlanController::PlanController(MonitorLink &l,QObject *parent):PlanController(l,Clock{},parent) {}
PlanController::PlanController(MonitorLink &l,Clock clock,QObject *parent):QObject(parent),link(l),now(std::move(clock)),
    operatorClient([this](const QByteArray &raw){return link.controlledWrite(raw);},
                   [this]{return link.allocateRequest();},
                   []{return QRandomGenerator::global()->generate();},
                   [this](const QString &event,const QJsonObject &fields){return record(event,fields);}) {
    monotonic.start();if(!now)now=[this]{return monotonic.elapsed();};
    timer.setInterval(25);connect(&timer,&QTimer::timeout,this,&PlanController::advance);timer.start();
    connect(&link,&MonitorLink::observedFrame,this,&PlanController::receive);
    connect(&link,&MonitorLink::frameReceived,this,[this](const Wire::Frame &f){
        if(f.type==6)operatorClient.setCapabilities(quint16(f.fields["feature_mask"].toInt()));
        if(f.type==4&&!f.fields["motion_active"].toInt()&&!f.fields["output_permitted"].toInt()&&f.fields["zero_output_required"].toInt()==1)needsClearance=false;
    });
    connect(&link,&MonitorLink::connectionChanged,this,[this](bool connected){
        if(connected){emit changed();return;}
        operatorClient.disconnected();owns=false;finishing=false;clearanceId=probeId=0;motionTask.reset();
        if(running())run->uncertain("physical_connection_lost_no_retry");
        // A reconnect alone does not establish that a previous task stopped.
        if(run){needsClearance=true;finalized=true;recordHealthy=recordHealthy&&link.journalHealthy;}
        emit changed();
    });
    connect(&operatorClient,&OperatorClient::stateChanged,this,[this]{emit changed();QTimer::singleShot(0,this,&PlanController::advance);});
}
bool PlanController::record(const QString &event,QJsonObject fields) {
    if(event.startsWith("plan_")){
        fields["run_id"]=runId;
        if(!fields.contains("now_ms"))fields["now_ms"]=now();
    }
    if(link.append(event,fields))return true;
    recordHealthy=false;
    link.close();return false;
}
bool PlanController::acquire() {
    if(owns)return link.isOpen();
    owns=link.acquireControl();return owns;
}
void PlanController::release() {if(owns)link.releaseControl();owns=false;probeId=0;emit changed();}
bool PlanController::probe(quint8 type,qint64 time) {
    if(!owns||probeId||clearanceId)return false;
    probeId=link.allocateRequest();probeNonce=QRandomGenerator::global()->generate();probeType=type;probeDeadline=time+300;lastProbe=time;
    if(!probeId||link.controlledWrite(Wire::query(type,probeId,probeNonce))!=30){probeId=0;return false;}
    return true;
}
bool PlanController::refreshStatus() {
    if(!owns){link.query(4);return link.isWaiting();}
    return probe(4,now());
}
bool PlanController::openSession() {
    if(running()||cleanupPending()||needsClearance||!acquire())return false;
    operatorClient.setCapabilities(link.capabilities);
    if(!operatorClient.open(now())){if(!operatorClient.active())release();return false;}
    return true;
}
bool PlanController::clearFault() {return owns&&!running()&&!cleanupPending()&&operatorClient.clearFault(now());}
bool PlanController::closeSession() {
    if(running()||cleanupPending())return false;
    if(!owns||!operatorClient.active()||operatorClient.controlPending())return false;
    const bool sent=operatorClient.close(now());
    if(sent){finishing=true;needsClearance=true;closeAttempted=true;}
    return sent;
}
bool PlanController::stop() {
    if(run&&!run->terminal()){
        if(!record("plan_cancel",{{"now_ms",now()},{"source","operator_stop"}}))return false;
        run->cancel();
    }
    if(!owns||!operatorClient.active()){emit message(QStringLiteral("无有效操作会话；停止请求未发送，当前输出状态需重新查询"));emit changed();return false;}
    needsClearance=true;const bool sent=operatorClient.stop(now());emit changed();return sent;
}
bool PlanController::start(const PlanSnapshot &snapshot,bool motionConfirmed,QString &error) {
    error.clear();
    if(running()||cleanupPending()||needsClearance){error=QStringLiteral("已有执行或停止状态尚未核实，请先刷新MCU状态");return false;}
    if(snapshot.hasMotion()&&(!motionConfirmed||!operatorClient.active())){error=QStringLiteral("运动计划需要本次现场确认和已建立的操作会话");return false;}
    if(!snapshot.hasMotion()&&operatorClient.active()){error=QStringLiteral("请先关闭操作会话并确认许可撤销，再运行非运动计划");return false;}
    if(!acquire()){error=QStringLiteral("请连接串口并等待当前查询结束");return false;}
    run=std::make_unique<PlanRun>(snapshot,motionConfirmed);runId=QUuid::createUuid().toString(QUuid::WithoutBraces);runJournal=link.journalPath();motionTask.reset();grantIndex=-1;finishing=false;closeAttempted=false;finalized=false;lastProbe=now();recordHealthy=true;
    if(!record("plan_start",{{"draft_hex",QString::fromLatin1(snapshot.draftBytes().toHex())},{"profile_hex",QString::fromLatin1(snapshot.profileBytes().toHex())},{"draft_sha256",snapshot.draftSha256()},{"profile_sha256",snapshot.profileSha256()},{"motion_confirmed",motionConfirmed}})) {run->uncertain("log_error");error=QStringLiteral("无法保存执行快照");return false;}
    emit changed();QTimer::singleShot(0,this,&PlanController::advance);return true;
}
void PlanController::cancelScheduling() {
    if(running()){
        if(!record("plan_cancel",{{"now_ms",now()},{"source","scheduling"}}))return;
        run->cancel();
    }
    emit changed();QTimer::singleShot(0,this,&PlanController::advance);
}
void PlanController::receive(const Wire::Frame &f) {
    if(!owns)return;
    const auto time=now();
    if(!record("control_rx",{{"now_ms",time},{"frame_hex",QString::fromLatin1(f.raw.toHex(' '))}}))return;
    if(f.type==7)operatorClient.receive(f.raw,time);
    if(probeId&&f.type==probeType&&f.fields["request_id"].toInteger()==probeId&&f.fields["nonce"].toInteger()==probeNonce&&time<probeDeadline) {
        probeId=0;
        if(f.type==4&&!f.fields["motion_active"].toInt()&&!f.fields["output_permitted"].toInt()&&f.fields["zero_output_required"].toInt()==1)needsClearance=false;
        record("plan_probe",{{"frame_hex",QString::fromLatin1(f.raw.toHex(' '))}});emit frameReceived(f);emit changed();return;
    }
    if(clearanceId&&f.type==4&&f.fields["request_id"].toInteger()==clearanceId&&f.fields["nonce"].toInteger()==clearanceNonce&&time<clearanceDeadline) {
        needsClearance=f.fields["motion_active"].toInt()!=0||f.fields["output_permitted"].toInt()!=0||f.fields["zero_output_required"].toInt()!=1;
        clearanceId=0;finishing=false;
        record("plan_clearance",{{"clear",!needsClearance},{"frame_hex",QString::fromLatin1(f.raw.toHex(' '))}});
        emit frameReceived(f);release();return;
    }
    if(running()&&run->receive(f.raw,time)) {
        if(f.type==6)operatorClient.setCapabilities(quint16(f.fields["feature_mask"].toInt()));
        emit frameReceived(f);
        if(!record("plan_state",run->report()))run->uncertain("log_error");
    }
    emit changed();QTimer::singleShot(0,this,&PlanController::advance);
}
void PlanController::finish() {
    if(run&&run->terminal()&&!finalized){
        finalized=true;needsClearance=true;
        if(!record("plan_end",run->report())){finishing=false;return;}
        finishing=true;
    }
    if(!finishing)return;
    if(operatorClient.controlPending())return;
    if(operatorClient.active()&&!closeAttempted){closeAttempted=true;if(!operatorClient.close(now())){operatorClient.disconnected();}return;}
    if(operatorClient.active()){
        // CLOSE was answered without closing the MCU session. Do not keep that
        // session alive indefinitely or retry a control operation automatically.
        record("operator_close_not_confirmed",{{"state",operatorClient.state()}});
        operatorClient.disconnected();
    }
    if(!clearanceId&&link.isOpen()) {
        clearanceId=link.allocateRequest();clearanceNonce=QRandomGenerator::global()->generate();clearanceDeadline=now()+1000;
        if(!clearanceId||link.controlledWrite(Wire::query(4,clearanceId,clearanceNonce))!=30){clearanceId=0;finishing=false;release();}
    }
}
void PlanController::advance() {
    if(pumping||!owns)return;
    QScopedValueRollback<bool> guard(pumping,true);const auto time=now();operatorClient.tick(time);
    if(probeId&&time>=probeDeadline){probeId=0;emit message(QStringLiteral("辅助监测查询超时，未重试"));}
    if(clearanceId&&time>=clearanceDeadline){clearanceId=0;finishing=false;emit message(QStringLiteral("最终状态查询超时，未重试；串口保持连接，下次执行前须刷新状态"));release();return;}
    if(run&&!run->terminal()) {
        const auto before=run->state();
        run->tick(time);
        if(run->state()!=before&&!record("plan_tick",{{"now_ms",time}}))return;
        if(run->snapshot().hasMotion()&&!operatorClient.active())run->uncertain("operator_session_lost_no_retry");
    }
    if(run&&run->terminal()){finish();emit changed();return;}
    if(!run){if(finishing)finish();else if(!operatorClient.active()&&!operatorClient.controlPending())release();return;}
    const auto operation=run->next();if(!operation){
        if(run->state()=="waiting_stop"&&time-lastProbe>=100&&!finishing)probe(5,time);
        return;
    }
    // GRANT names the exact future task, including its preallocated request ID.
    if(operation->actionIndex>=0&&run->snapshot().actions()[operation->actionIndex].motion()) {
        const auto &a=run->snapshot().actions()[operation->actionIndex];
        if(grantIndex!=operation->actionIndex) {
            const auto id=link.allocateRequest();if(!id){run->uncertain("request_id_exhausted");return;}
            motionTask=a.task(id);grantIndex=operation->actionIndex;
            if(!operatorClient.grant(*motionTask,time))run->uncertain("grant_not_sent_no_retry");
            return;
        }
        if(operatorClient.controlPending())return;
        if(!motionTask||!operatorClient.grantedFor(*motionTask,time)){run->uncertain("exact_task_permission_not_available");finish();return;}
    }
    const auto id=operation->kind==PlanRun::Kind::Task&&motionTask?motionTask->requestId:link.allocateRequest();
    const auto nonce=QRandomGenerator::global()->generate();QString error;const auto raw=run->dispatch(id,nonce,time,error);
    if(raw.isEmpty()){run->uncertain("dispatch_error: "+error);finish();return;}
    if(!record("plan_dispatch",{{"now_ms",time},{"frame_hex",QString::fromLatin1(raw.toHex(' '))}}))return;
    if(operation->kind==PlanRun::Kind::Task&&motionTask) {
        if(!operatorClient.taskSubmitted(*motionTask,time)){run->uncertain("permission_not_consumed");finish();return;}
        motionTask.reset();
    }
    if(link.controlledWrite(raw)!=raw.size())run->uncertain("short_write_or_disconnected_no_retry");
    emit changed();if(run->terminal())finish();
}
QJsonObject PlanController::report() const {
    auto result=run?run->report():QJsonObject{};result["run_id"]=runId;result["session_state"]=operatorClient.state();result["awaiting_final_status"]=cleanupPending();result["stop_state_unverified"]=needsClearance;result["raw_journal"]=run?runJournal:link.journalPath();result["journal_write_ok"]=recordHealthy&&(!owns||link.journalHealthy);return result;
}
