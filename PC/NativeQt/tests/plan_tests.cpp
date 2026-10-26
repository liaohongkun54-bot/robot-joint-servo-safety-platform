#include "plan_run.h"
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTest>
#include <QtEndian>

namespace {
void put16(QByteArray &b,int p,quint16 v){qToLittleEndian(v,b.data()+p);}
void put32(QByteArray &b,int p,quint32 v){qToLittleEndian(v,b.data()+p);}
QByteArray seal(QByteArray b){put16(b,int(b.size())-2,Wire::crc16(b.first(b.size()-2)));return b;}
QByteArray frame(int type,int size,quint32 id=1){QByteArray b(size,0);b.replace(0,2,"TR");b[2]=1;b[3]=char(type);put16(b,4,quint16(size-8));put32(b,6,id);return b;}
QByteArray info(quint32 id=1,quint32 nonce=2,int features=63){auto b=frame(6,48,id);put32(b,10,nonce);put16(b,30,1);put16(b,32,quint16(features));put32(b,38,20261022);put32(b,42,120000);return seal(b);}
QByteArray status(quint32 id=2,quint32 nonce=2,bool permit=false,int count=0,int safety=2,int fault=0){auto b=frame(4,38,id);put32(b,10,nonce);put16(b,18,1);put16(b,20,1);b[22]=char(safety);b[23]=permit;b[24]=!permit;b[25]=fault!=0;put16(b,28,quint16(count));put16(b,30,64);put32(b,32,quint32(fault));return seal(b);}
QByteArray telemetry(quint32 id,quint32 nonce=2,bool fresh=true){auto b=frame(5,42,id);put32(b,10,nonce);put32(b,14,1000);put32(b,18,fresh?990:800);b[33]=1;b[34]=1;b[35]=1;put16(b,36,0x201);return seal(b);}
QByteArray result(quint32 id,bool accepted=true,quint32 tick=1000,int error=0xe2){auto b=frame(2,24,id);put32(b,10,tick);put16(b,14,accepted?0:quint16(error));put16(b,16,2);b[18]=accepted?2:3;b[19]=accepted?6:3;b[20]=2;return seal(b);}
QByteArray stop(quint32 id,quint32 tick=2000,int reason=1,int amp=1000,int fault=0){auto b=frame(3,42,id);put32(b,10,tick);put16(b,14,quint16(amp));b[23]=char(reason);b[24]=2;b[25]=1;put32(b,26,quint32(fault));return seal(b);}
QJsonObject step(const QString &id,const QString &test="feedback_readonly",int command=0,int duration=100,int attempts=1){const bool ro=test=="feedback_readonly";return {{"step_id",id},{"test_id",test},{"mode_id",ro?QJsonValue::Null:QJsonValue(duration==1000?"fixed_positive_run":"bounded_current_test")},{"command_value",ro?QJsonValue::Null:QJsonValue(command)},{"duration_ms",ro?0:duration},{"attempts",attempts}};}
}

class PlanTests:public QObject {
    Q_OBJECT
    QByteArray profile;
    QJsonObject draft(const QJsonArray &steps) const {
        return {{"schema_version",1},{"status","draft_not_executable"},{"plan_id","native_plan_test"},{"profile_id","c620_m3508_lab_v1"},{"profile_sha256",QString::fromLatin1(QCryptographicHash::hash(profile,QCryptographicHash::Sha256).toHex()).toUpper()},{"purpose","Native finite plan regression"},{"steps",steps}};
    }
    PlanSnapshot compile(const QJsonArray &steps) const {QString e;auto p=PlanSnapshot::compile(QJsonDocument(draft(steps)).toJson(),profile,e);if(!p)qFatal("compile: %s",qPrintable(e));return *p;}
    void preflight(PlanRun &run) {QString e;QVERIFY(!run.dispatch(1,2,0,e).isEmpty());QVERIFY(run.receive(info(),1));QVERIFY(!run.dispatch(2,2,2,e).isEmpty());QVERIFY(run.receive(status(),3));}
    void motionReady(PlanRun &run) {preflight(run);QString e;QVERIFY(!run.dispatch(3,2,4,e).isEmpty());QVERIFY(run.receive(status(3,2,true),5));QCOMPARE(run.next()->kind,PlanRun::Kind::Task);}
private slots:
    void initTestCase(){QFile f(QStringLiteral(NATIVE_PROFILE));QVERIFY(f.open(QIODevice::ReadOnly));profile=f.readAll();}
    void snapshotAndLimits(){
        const auto p=compile({step("feedback_before"),step("motion_one","deadline_stop",1000,1000),step("feedback_after")});
        QCOMPARE(p.actions().size(),3);QCOMPARE(p.reservedRecords(),6);QCOMPARE(p.requiredFeatures(),63);QVERIFY(p.hasMotion());QCOMPARE(p.actions()[1].task(42).limit,1000);
        QString e;auto raw=QJsonDocument(draft({step("zero_one","zero_output")})).toJson();auto snapshot=PlanSnapshot::compile(raw,profile,e);QVERIFY(snapshot);raw.fill('x');QVERIFY(snapshot->draftBytes()!=raw);
        PlanRun blocked(p);QCOMPARE(blocked.state(),"blocked");QVERIFY(!blocked.next());QVERIFY(blocked.dispatch(1,2,0,e).isEmpty());
    }
    void rejectAmbiguousOrUnsupportedDraft(){
        QString e;auto check=[&](QJsonObject d,QByteArray p=QByteArray{}){return PlanSnapshot::compile(QJsonDocument(d).toJson(),p.isEmpty()?profile:p,e).has_value();};
        const auto good=draft({step("feedback_one")});
        auto d=good;d["extra"]=true;QVERIFY(!check(d));d=good;d["schema_version"]=true;QVERIFY(!check(d));d=good;d["profile_sha256"]=QString(64,'0');QVERIFY(!check(d));
        d=good;d["steps"]=QJsonArray{step("same_id"),step("same_id")};QVERIFY(!check(d));
        for(const auto &s:{step("zero_one","zero_output",1),step("motion_one","deadline_stop",0),step("motion_one","deadline_stop",999,1000),step("motion_one","deadline_stop",1001,100),step("feedback_one","feedback_readonly",0,0,17)}) {d=good;d["steps"]=QJsonArray{s};QVERIFY(!check(d));}
        d=good;auto s=step("zero_one","zero_output");s["command_value"]=false;d["steps"]=QJsonArray{s};QVERIFY(!check(d));
        d=good;d["steps"]=QJsonArray{step("feedback_one","feedback_readonly",0,0,16),step("feedback_two","feedback_readonly",0,0,16),step("feedback_three")};QVERIFY(!check(d));
        auto raw=QJsonDocument(good).toJson(QJsonDocument::Compact);raw.replace("\"schema_version\":1","\"schema_version\":1,\"schema_versio\\u006e\":1");QVERIFY(!PlanSnapshot::compile(raw,profile,e));QVERIFY(e.contains("duplicate"));
        QVERIFY(!PlanSnapshot::compile(QByteArray(65537,' '),profile,e));
        auto p=QJsonDocument::fromJson(profile).object();p["adapter_id"]="other_motor";auto bytes=QJsonDocument(p).toJson();d=good;d["profile_sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());QVERIFY(!check(d,bytes));
        p=QJsonDocument::fromJson(profile).object();auto modes=p["control_modes"].toArray();auto mode=modes[0].toObject();mode["max_command"]=2000;modes[0]=mode;p["control_modes"]=modes;bytes=QJsonDocument(p).toJson();d=draft({step("motion_one","limited_motion",1500)});d["profile_sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());QVERIFY(!check(d,bytes));
    }
    void readonlyRejectionReadonlyChain(){
        PlanRun run(compile({step("feedback_before"),step("unarmed_check","unarmed_rejection"),step("feedback_after")}));preflight(run);QString e;
        QVERIFY(!run.dispatch(3,2,4,e).isEmpty());QVERIFY(run.receive(telemetry(3),5));
        QCOMPARE(run.next()->kind,PlanRun::Kind::Status);QVERIFY(!run.dispatch(4,2,6,e).isEmpty());QVERIFY(run.receive(status(4),7));
        const auto task=run.dispatch(5,0,8,e);Wire::Frame f;QVERIFY(Wire::request(task,f,e));QCOMPARE(f.fields["amplitude_raw"].toInt(),0);QVERIFY(run.receive(result(5,false),9));
        QVERIFY(!run.dispatch(6,2,10,e).isEmpty());QVERIFY(run.receive(telemetry(6),11));QCOMPARE(run.state(),"succeeded");QCOMPARE(run.outcomes().size(),3);QCOMPARE(run.outcomes()[1].toObject()["detail"].toString(),"expected_safety_rejection");QVERIFY(!run.next());
    }
    void preflightRejectsFeaturesSafetyCapacityAndPermit(){
        QString e;
        {PlanRun r(compile({step("zero_one","zero_output")}));r.dispatch(1,2,0,e);QVERIFY(r.receive(info(1,2,5),1));QCOMPARE(r.state(),"failed");}
        for(const auto &reply:{status(2,2,false,63),status(2,2,false,0,1),status(2,2,false,0,4,0xe003)}) {PlanRun r(compile({step("zero_one","zero_output")}));r.dispatch(1,2,0,e);r.receive(info(),1);r.dispatch(2,2,2,e);QVERIFY(r.receive(reply,3));QCOMPARE(r.state(),"failed");QVERIFY(r.outcomes().isEmpty());}
        {PlanRun r(compile({step("reject_one","unarmed_rejection")}));preflight(r);r.dispatch(3,2,4,e);r.receive(status(3,2,true),5);QCOMPARE(r.state(),"failed");}
        {PlanRun r(compile({step("motion_one","deadline_stop",1000,1000)}),true);preflight(r);r.dispatch(3,2,4,e);r.receive(status(3),5);QCOMPARE(r.state(),"failed");}
    }
    void matchingDeadlineStopAndTickWrap(){
        for(quint32 start:{1000u,0xfffffff0u}){PlanRun r(compile({step("motion_one","deadline_stop",1000,1000)}),true);motionReady(r);QString e;r.dispatch(4,0,6,e);QVERIFY(!r.receive(stop(4,start+1000),7));QVERIFY(r.receive(result(4,true,start),8));QCOMPARE(r.state(),"waiting_stop");QVERIFY(!r.next());QVERIFY(!r.receive(stop(5,start+1000),9));QVERIFY(r.receive(stop(4,start+1000),10));QCOMPARE(r.state(),"succeeded");QCOMPARE(r.outcomes()[0].toObject()["elapsed_ms"].toInt(),1000);QVERIFY(!r.receive(stop(4,start+1000),11));}
    }
    void earlyFaultWrongAmplitudeCannotPass(){
        const QVector<QByteArray> replies{stop(4,1500),stop(4,2000,4),stop(4,2000,1,1000,0xe003),stop(4,2000,1,999),stop(4,999)};
        for(int i=0;i<replies.size();++i){PlanRun r(compile({step("motion_one","deadline_stop",1000,1000),step("feedback_after")}),true);motionReady(r);QString e;r.dispatch(4,0,6,e);r.receive(result(4),7);QVERIFY(r.receive(replies[i],8));QCOMPARE(r.state(),i>=3?"uncertain":"failed");QVERIFY(!r.next());QCOMPARE(r.report()["actions"].toArray()[1].toObject()["outcome"].toString(),"not_executed");}
    }
    void nonceDuplicateIdsAndLateReceipt(){
        PlanRun r(compile({step("feedback_one")}));QString e;r.dispatch(1,2,0,e);QVERIFY(!r.receive(info(1,3),1));QVERIFY(r.waiting());QVERIFY(r.dispatch(2,2,2,e).isEmpty());QVERIFY(r.receive(info(),3));QVERIFY(r.dispatch(1,2,4,e).isEmpty());QVERIFY(!r.dispatch(2,2,5,e).isEmpty());QVERIFY(r.receive(status(),6));r.dispatch(3,2,7,e);QVERIFY(!r.receive(telemetry(3),307));QCOMPARE(r.state(),"uncertain");QVERIFY(!r.next());QCOMPARE(r.outcomes().size(),1);
    }
    void cancellationRetainsInflightStopAndDoesNotInventStop(){
        PlanRun r(compile({step("motion_one","deadline_stop",1000,1000),step("feedback_after")}),true);motionReady(r);QString e;r.dispatch(4,0,6,e);r.receive(result(4),7);r.cancel();QCOMPARE(r.state(),"waiting_stop");QVERIFY(!r.next());r.receive(stop(4,1500,4),8);QCOMPARE(r.state(),"cancelled");QCOMPARE(r.outcomes()[0].toObject()["detail"].toString(),"early_or_fault_stop");QCOMPARE(r.report()["actions"].toArray()[1].toObject()["outcome"].toString(),"not_executed");
        PlanRun idle(compile({step("feedback_one")}));idle.cancel();QCOMPARE(idle.state(),"cancelled");QVERIFY(idle.outcomes().isEmpty());
    }
    void staleTimeoutClockAndStopDeadline(){
        QString e;
        {PlanRun r(compile({step("feedback_one")}));preflight(r);r.dispatch(3,2,4,e);r.receive(telemetry(3,2,false),5);QCOMPARE(r.state(),"failed");}
        {PlanRun r(compile({step("feedback_one")}));r.dispatch(1,2,10,e);r.tick(9);QCOMPARE(r.state(),"uncertain");}
        {PlanRun r(compile({step("motion_one","deadline_stop",1000,1000)}),true);motionReady(r);r.dispatch(4,0,6,e);r.receive(result(4),7);r.tick(4006);QCOMPARE(r.state(),"waiting_stop");r.tick(4007);QCOMPARE(r.state(),"uncertain");QVERIFY(!r.receive(stop(4),4008));}
    }
    void admissionRejectedAndZeroAck(){
        QString e;
        {PlanRun r(compile({step("zero_one","zero_output")}));preflight(r);r.dispatch(3,2,4,e);r.receive(status(3,2,true),5);r.dispatch(4,0,6,e);r.receive(result(4),7);QCOMPARE(r.state(),"succeeded");QCOMPARE(r.outcomes()[0].toObject()["detail"].toString(),"zero_output_ack");}
        {PlanRun r(compile({step("unarmed_one","unarmed_rejection")}));preflight(r);r.dispatch(3,2,4,e);r.receive(status(3),5);r.dispatch(4,0,6,e);r.receive(result(4,false,1000,0xe003),7);QCOMPARE(r.state(),"failed");}
    }
    void syntheticSessionLossStopIsNotDeadlinePass(){
        QFile f(QStringLiteral(NATIVE_PLAN_SYNTHETIC_VECTORS));QVERIFY(f.open(QIODevice::ReadOnly));const auto rows=QJsonDocument::fromJson(f.readAll()).object()["responses"].toArray();QByteArray admission,stopped;quint32 id=0;
        for(const auto &v:rows){const auto r=v.toObject();if(r["type"].toInt()==2){admission=QByteArray::fromHex(r["hex"].toString().toLatin1());id=quint32(r["fields"].toObject()["request_id"].toInteger());}if(r["type"].toInt()==3)stopped=QByteArray::fromHex(r["hex"].toString().toLatin1());}
        QVERIFY(!admission.isEmpty()&&!stopped.isEmpty());PlanRun run(compile({step("motion_one","deadline_stop",1000,1000)}),true);motionReady(run);QString e;QVERIFY(!run.dispatch(id,0,6,e).isEmpty());QVERIFY(run.receive(admission,7));QVERIFY(run.receive(stopped,8));QCOMPARE(run.state(),"failed");QCOMPARE(run.outcomes()[0].toObject()["detail"].toString(),"early_or_fault_stop");QCOMPARE(run.outcomes()[0].toObject()["elapsed_ms"].toInt(),697);
    }
    void zeroOutputCannotSkipActualPermissionCheck(){
        PlanRun r(compile({step("zero_one","zero_output")}));preflight(r);QString error;QCOMPARE(r.next()->kind,PlanRun::Kind::Status);r.dispatch(3,2,4,error);QVERIFY(r.receive(status(3),5));QCOMPARE(r.state(),"failed");QCOMPARE(r.detail(),"MCU has not permitted task output");QVERIFY(!r.next());QVERIFY(r.outcomes().isEmpty());
    }
};
QTEST_GUILESS_MAIN(PlanTests)
#include "plan_tests.moc"
