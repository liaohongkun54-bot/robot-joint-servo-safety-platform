#include "plan_controller.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include "memory_endpoint_fixture.h"
class ControllerTests:public QObject {
    Q_OBJECT
    QByteArray profile;
    PlanSnapshot snapshot(bool motion) {
        auto step=[](QString id,QString test,bool ro){return QJsonObject{{"step_id",id},{"test_id",test},{"mode_id",ro?QJsonValue::Null:QJsonValue(test=="deadline_stop"?"fixed_positive_run":"bounded_current_test")},{"command_value",ro?QJsonValue::Null:QJsonValue(test=="deadline_stop"?1000:0)},{"duration_ms",ro?0:test=="deadline_stop"?1000:100},{"attempts",1}};};
        QJsonObject d{{"schema_version",1},{"status","draft_not_executable"},{"plan_id","controller_plan"},{"profile_id","c620_m3508_lab_v1"},{"profile_sha256",QString::fromLatin1(QCryptographicHash::hash(profile,QCryptographicHash::Sha256).toHex())},{"purpose","Memory transport integration"},{"steps",QJsonArray{step("feedback_before","feedback_readonly",true),step("task_one",motion?"deadline_stop":"unarmed_rejection",false),step("feedback_after","feedback_readonly",true)}}};
        QString e;auto p=PlanSnapshot::compile(QJsonDocument(d).toJson(),profile,e);if(!p)qFatal("%s",qPrintable(e));return *p;
    }
    void identify(MonitorLink &link){link.query(6);}
private slots:
    void initTestCase(){QFile f(QStringLiteral(NATIVE_PROFILE));QVERIFY(f.open(QIODevice::ReadOnly));profile=f.readAll();}
    void readonlyPlanKeepsPortAndCanRepeat(){
        QTemporaryDir dir;MemoryEndpoint endpoint;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;QVERIFY(link.open("MEMORY:",115200,e));QVERIFY(c.start(snapshot(false),false,e));
        // Manual monitoring cannot interleave with an exclusive plan.
        const auto before=endpoint.writes.size();link.query(4);link.setCapture(true);QCOMPARE(endpoint.writes.size(),before);QVERIFY(!link.isCapturing());
        QTRY_VERIFY_WITH_TIMEOUT(c.report()["state"]=="succeeded"&&!c.report()["awaiting_final_status"].toBool(),1000);QVERIFY(link.isOpen());QCOMPARE(endpoint.opens,1);QCOMPARE(endpoint.count(1),1);QCOMPARE(endpoint.count(7),0);QVERIFY(!c.report()["stop_state_unverified"].toBool());
        QVERIFY(c.start(snapshot(false),false,e));QTRY_COMPARE_WITH_TIMEOUT(endpoint.count(1),2,1000);QTRY_VERIFY_WITH_TIMEOUT(c.report()["state"]=="succeeded"&&!c.report()["awaiting_final_status"].toBool(),1000);QCOMPARE(endpoint.opens,1);
    }
    void explicitSessionExactGrantThenTaskThenClose(){
        QTemporaryDir dir;MemoryEndpoint endpoint;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);QVERIFY(!c.start(snapshot(true),true,e));identify(link);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());QVERIFY(!c.start(snapshot(true),false,e));QVERIFY(c.start(snapshot(true),true,e));
        QTRY_VERIFY_WITH_TIMEOUT(c.report()["state"]=="succeeded"&&!c.report()["awaiting_final_status"].toBool(),1000);QVERIFY(link.isOpen());QCOMPARE(endpoint.count(1),1);QCOMPARE(endpoint.count(7,1),1);QCOMPARE(endpoint.count(7,3),1);QCOMPARE(endpoint.count(7,6),1);
        QByteArray grant,task;for(const auto &b:endpoint.writes){if(quint8(b[3])==7&&quint8(b[30])==3)grant=b;if(quint8(b[3])==1)task=b;}
        QCOMPARE(grant.mid(32,22),task.mid(6,22));QVERIFY(!c.report()["stop_state_unverified"].toBool());
    }
    void stopDuringFlightAndHeartbeatAreIndependent(){
        QTemporaryDir dir;MemoryEndpoint endpoint;endpoint.holdTask=true;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);identify(link);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());QVERIFY(c.start(snapshot(true),true,e));QTRY_VERIFY(endpoint.task!=0);time=250;c.advance();QTRY_COMPARE(endpoint.count(7,2),1);QVERIFY(c.stop());
        QTRY_VERIFY_WITH_TIMEOUT(c.report()["state"]=="cancelled"&&!c.report()["awaiting_final_status"].toBool(),1000);QVERIFY(link.isOpen());QCOMPARE(endpoint.count(7,4),1);QCOMPARE(endpoint.count(1),1);const auto rows=c.report()["actions"].toArray();QCOMPARE(rows[1].toObject()["detail"].toString(),"early_or_fault_stop");QCOMPARE(rows[2].toObject()["outcome"].toString(),"not_executed");
    }
    void taskShortWriteOrDropNeverRetries(){
        for(bool drop:{false,true}){QTemporaryDir dir;MemoryEndpoint endpoint;endpoint.shortTask=!drop;endpoint.failTask=drop;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);identify(link);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());QVERIFY(c.start(snapshot(true),true,e));QTRY_COMPARE(c.report()["state"].toString(),"uncertain");QVERIFY(!link.isOpen());QCOMPARE(endpoint.count(1),1);time=5000;c.advance();QCOMPARE(endpoint.count(1),1);}
    }
    void timeoutPreservesPortAndRequiresFinalState(){
        QTemporaryDir dir;MemoryEndpoint endpoint;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);endpoint.dropQuery=true;QVERIFY(c.start(snapshot(false),false,e));QTRY_COMPARE(endpoint.count(4),1);time=1000;c.advance();QTRY_COMPARE(c.report()["state"].toString(),"uncertain");time=2000;c.advance();QVERIFY(link.isOpen());QVERIFY(c.report()["stop_state_unverified"].toBool());QVERIFY(!c.start(snapshot(false),false,e));endpoint.dropQuery=false;link.query(4);QTRY_VERIFY(!c.report()["stop_state_unverified"].toBool());QVERIFY(c.start(snapshot(false),false,e));
    }
    void heartbeatLossAndCloseRejectionDoNotKeepSessionAlive(){
        QTemporaryDir dir;MemoryEndpoint endpoint;endpoint.holdTask=true;endpoint.dropHeartbeat=true;endpoint.rejectClose=true;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);identify(link);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());QVERIFY(c.start(snapshot(true),true,e));QTRY_VERIFY(endpoint.task!=0);time=250;c.advance();QCOMPARE(endpoint.count(7,2),1);time=750;c.advance();QTRY_COMPARE(c.report()["state"].toString(),"uncertain");QTRY_VERIFY(!c.report()["awaiting_final_status"].toBool());QVERIFY(!c.sessionActive());QVERIFY(c.report()["stop_state_unverified"].toBool());QVERIFY(link.isOpen());const auto before=endpoint.writes.size();time=2000;c.advance();QCOMPARE(endpoint.writes.size(),before);QCOMPARE(endpoint.count(1),1);
        // Separate idle CLOSE rejection: no endless heartbeat loop or retry.
        link.close();QVERIFY(link.open("MEMORY:",115200,e));endpoint.task=0;endpoint.permit=false;link.query(4);QTRY_VERIFY(!c.report()["stop_state_unverified"].toBool());identify(link);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());QVERIFY(c.closeSession());QTRY_VERIFY(!c.report()["awaiting_final_status"].toBool());QVERIFY(!c.sessionActive());QVERIFY(link.isOpen());
    }
    void reconnectCannotRelabelPreviousRunJournal(){
        QTemporaryDir dir;MemoryEndpoint endpoint;endpoint.dropQuery=true;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);QVERIFY(c.start(snapshot(false),false,e));QTRY_COMPARE(endpoint.count(4),1);const auto original=link.journalPath();link.close();QCOMPARE(c.report()["state"].toString(),"uncertain");QVERIFY(link.open("MEMORY:",115200,e));endpoint.dropQuery=false;link.query(4);QTRY_VERIFY(!c.report()["stop_state_unverified"].toBool());identify(link);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());QCOMPARE(c.report()["raw_journal"].toString(),original);
        QFile file(link.journalPath());QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(!file.readAll().contains("plan_end"));QVERIFY(c.closeSession());QTRY_VERIFY(!c.report()["awaiting_final_status"].toBool());
    }
    void terminalReplyCannotExposeStartBeforeFinalStatus(){
        QTemporaryDir dir;MemoryEndpoint endpoint;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QString e;link.open("MEMORY:",115200,e);bool sawGap=false;
        connect(&c,&PlanController::changed,this,[&]{if(c.report()["state"]=="succeeded"&&c.report()["awaiting_final_status"].toBool()){sawGap=true;QString error;QVERIFY(!c.start(snapshot(false),false,error));QVERIFY(!c.openSession());}});
        QVERIFY(c.start(snapshot(false),false,e));QTRY_VERIFY(sawGap);QTRY_VERIFY(!c.running()&&!c.report()["awaiting_final_status"].toBool());QVERIFY(!c.report()["stop_state_unverified"].toBool());QVERIFY(link.isOpen());
    }
    void zeroOutputWithoutPermissionNeverWritesTaskFrame(){
        auto d=QJsonDocument::fromJson(snapshot(false).draftBytes()).object();auto steps=d["steps"].toArray();auto zero=steps[1].toObject();zero["test_id"]="zero_output";steps[1]=zero;d["steps"]=steps;QString error;auto plan=PlanSnapshot::compile(QJsonDocument(d).toJson(),profile,error);QVERIFY(plan);
        QTemporaryDir dir;MemoryEndpoint endpoint;MonitorLink link(endpoint,dir.path());qint64 time=0;PlanController c(link,[&]{return time;});QVERIFY(link.open("MEMORY:",115200,error));QVERIFY(c.start(*plan,false,error));QTRY_COMPARE(c.report()["state"].toString(),"failed");QTRY_VERIFY(!c.report()["awaiting_final_status"].toBool());QCOMPARE(endpoint.count(1),0);QCOMPARE(endpoint.count(7,3),0);QCOMPARE(c.report()["detail"].toString(),"MCU has not permitted task output");
    }
};
QTEST_GUILESS_MAIN(ControllerTests)
#include "controller_tests.moc"
