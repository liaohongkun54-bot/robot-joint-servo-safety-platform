#include "native_audit.h"
#include "memory_endpoint_fixture.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDir>
#include <QTest>

namespace {
QByteArray encoded(const QVector<QJsonObject> &rows){QByteArray raw;for(const auto &r:rows)raw+=QJsonDocument(r).toJson(QJsonDocument::Compact)+'\n';return raw;}
QVector<QJsonObject> decoded(const QByteArray &raw){QVector<QJsonObject> rows;for(const auto &line:raw.split('\n'))if(!line.trimmed().isEmpty())rows.append(QJsonDocument::fromJson(line).object());return rows;}
}
class AuditTests:public QObject {
    Q_OBJECT
    QByteArray profile,readonlyTrace,motionTrace,stopTrace,timeoutTrace,heartbeatTrace;
    PlanSnapshot snapshot(bool motion) {
        auto step=[](QString id,QString test,bool ro){return QJsonObject{{"step_id",id},{"test_id",test},{"mode_id",ro?QJsonValue::Null:QJsonValue(test=="deadline_stop"?"fixed_positive_run":"bounded_current_test")},{"command_value",ro?QJsonValue::Null:QJsonValue(test=="deadline_stop"?1000:0)},{"duration_ms",ro?0:test=="deadline_stop"?1000:100},{"attempts",1}};};
        QJsonObject d{{"schema_version",1},{"status","draft_not_executable"},{"plan_id","audit_plan"},{"profile_id","c620_m3508_lab_v1"},{"profile_sha256",QString::fromLatin1(QCryptographicHash::hash(profile,QCryptographicHash::Sha256).toHex())},{"purpose","Memory-only audit regression"},{"steps",QJsonArray{step("feedback_before","feedback_readonly",true),step("task_one",motion?"deadline_stop":"unarmed_rejection",false),step("feedback_after","feedback_readonly",true)}}};
        QString e;auto p=PlanSnapshot::compile(QJsonDocument(d).toJson(),profile,e);if(!p)qFatal("%s",qPrintable(e));return *p;
    }
    void generate(QByteArray &trace,bool motion,bool stopped=false,bool timeout=false,bool repeat=false,bool lostHeartbeat=false) {
        QTemporaryDir dir;MemoryEndpoint endpoint;endpoint.holdTask=stopped||lostHeartbeat;endpoint.dropHeartbeat=lostHeartbeat;MonitorLink link(endpoint,dir.path());qint64 now=0;PlanController c(link,[&]{return now;});QString error;QVERIFY(link.open("MEMORY:",115200,error));
        if(motion){link.query(6);QVERIFY(c.openSession());QTRY_VERIFY(c.sessionActive());}
        endpoint.dropQuery=timeout;QVERIFY(c.start(snapshot(motion),motion,error));
        if(stopped){QTRY_VERIFY(endpoint.task!=0);now=250;c.advance();QTRY_COMPARE(endpoint.count(7,2),1);QVERIFY(c.stop());}
        if(lostHeartbeat){QTRY_VERIFY(endpoint.task!=0);now=250;c.advance();QTRY_COMPARE(endpoint.count(7,2),1);now=750;c.advance();QTRY_COMPARE(c.report()["state"].toString(),"uncertain");}
        if(timeout){QTRY_COMPARE(endpoint.count(4),1);now=1000;c.advance();QTRY_COMPARE(c.report()["state"].toString(),"uncertain");now=2000;c.advance();}
        else {QTRY_VERIFY(!c.running()&&!c.report()["awaiting_final_status"].toBool());}
        if(repeat){QVERIFY(c.start(snapshot(false),false,error));QTRY_COMPARE(endpoint.count(1),2);QTRY_VERIFY(!c.running()&&!c.report()["awaiting_final_status"].toBool());}
        link.close();QFile f(link.journalPath());QVERIFY(f.open(QIODevice::ReadOnly));trace=f.readAll();QVERIFY(!trace.isEmpty());
    }
    void invalid(const QByteArray &raw){NativeAudit audit;QString error;QVERIFY(!audit.audit(raw,error));QCOMPARE(audit.report()["evidence"].toString(),"invalid");QVERIFY(!error.isEmpty());}
private slots:
    void initTestCase(){
        QFile f(QStringLiteral(NATIVE_PROFILE));QVERIFY(f.open(QIODevice::ReadOnly));profile=f.readAll();generate(readonlyTrace,false,false,false,true);generate(motionTrace,true);generate(stopTrace,true,true);generate(timeoutTrace,false,false,true);generate(heartbeatTrace,true,false,false,false,true);
        const auto out=qEnvironmentVariable("NATIVE_AUDIT_FIXTURE_OUT");
        if(!out.isEmpty()){QVERIFY(QDir().mkpath(out));const QVector<QPair<QString,QByteArray>> fixtures{{"synthetic-readonly.jsonl",readonlyTrace},{"synthetic-motion.jsonl",motionTrace},{"synthetic-stop.jsonl",stopTrace},{"synthetic-timeout.jsonl",timeoutTrace},{"synthetic-heartbeat-loss.jsonl",heartbeatTrace}};for(const auto &item:fixtures){QFile file(QDir(out).filePath(item.first));QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(file.write(item.second),item.second.size());}}
    }
    void fullRawPlansAndRepeatedConnection(){
        for(const auto &trace:{readonlyTrace,motionTrace}){NativeAudit a;QString e;QVERIFY2(a.audit(trace,e),qPrintable(e));QCOMPARE(a.report()["evidence"].toString(),"complete");for(const auto &plan:a.report()["plans"].toArray()){const auto p=plan.toObject();QCOMPARE(p["state"].toString(),"succeeded");QVERIFY(p["final_status_verified"].toBool());}}
        NativeAudit a;QString e;QVERIFY(a.audit(readonlyTrace,e));QCOMPARE(a.report()["plans"].toArray().size(),2);
    }
    void cancelledIsNotDeadlineSuccessAndTimeoutIsPartial(){
        NativeAudit a;QString e;QVERIFY2(a.audit(stopTrace,e),qPrintable(e));QCOMPARE(a.report()["evidence"].toString(),"complete");const auto p=a.report()["plans"].toArray()[0].toObject();QCOMPARE(p["state"].toString(),"cancelled");QCOMPARE(p["actions"].toArray()[1].toObject()["detail"].toString(),"early_or_fault_stop");QCOMPARE(p["actions"].toArray()[2].toObject()["outcome"].toString(),"not_executed");
        QVERIFY2(a.audit(timeoutTrace,e),qPrintable(e));QCOMPARE(a.report()["evidence"].toString(),"partial");QCOMPARE(a.report()["plans"].toArray()[0].toObject()["state"].toString(),"uncertain");
    }
    void removedWireProofOrQueueCannotBeAccepted(){
        for(const auto &event:{"rx_chunk","tx_queued","operator_task_submitted"}){auto rows=decoded(motionTrace);for(int i=0;i<rows.size();++i)if(rows[i]["event"]==event){rows.removeAt(i);break;}invalid(encoded(rows));}
    }
    void forgedOutcomeAndClearanceAreRejected(){
        auto rows=decoded(motionTrace);for(auto &row:rows)if(row["event"]=="plan_end"){row["detail"]="forged success";break;}invalid(encoded(rows));
        rows=decoded(motionTrace);for(auto &row:rows)if(row["event"]=="plan_clearance"){row["clear"]=false;break;}invalid(encoded(rows));
    }
    void alteredGrantSnapshotAndRepeatedPermissionAreRejected(){
        auto rows=decoded(motionTrace);for(auto &row:rows)if(row["event"]=="operator_tx_intent"){auto b=QByteArray::fromHex(row["frame_hex"].toString().toLatin1());if(quint8(b[30])==3){u16(b,40,999);row["frame_hex"]=QString::fromLatin1(seal(b).toHex(' '));break;}}invalid(encoded(rows));
        rows=decoded(motionTrace);for(auto &row:rows)if(row["event"]=="plan_start"){row["draft_sha256"]=QString(64,'0');break;}invalid(encoded(rows));
        rows=decoded(motionTrace);for(int i=0;i<rows.size();++i)if(rows[i]["event"]=="operator_task_submitted"){rows.insert(i,rows[i]);break;}invalid(encoded(rows));
    }
    void missingEndOrFinalStatusRemainsPartial(){
        auto rows=decoded(motionTrace);int end=-1;for(int i=0;i<rows.size();++i)if(rows[i]["event"]=="plan_end"){end=i;break;}QVERIFY(end>0);rows.resize(end+1);NativeAudit a;QString e;QVERIFY2(a.audit(encoded(rows),e),qPrintable(e));QCOMPARE(a.report()["evidence"].toString(),"partial");QCOMPARE(a.report()["plans"].toArray()[0].toObject()["state"].toString(),"succeeded");
        rows.resize(end);QVERIFY(a.audit(encoded(rows),e));QCOMPARE(a.report()["evidence"].toString(),"partial");
    }
    void malformedHexDuplicatesAndTimeCannotBecomeEvidence(){
        auto rows=decoded(motionTrace);for(auto &row:rows)if(row["event"]=="tx_intent"){row["frame_hex"]="4b GG";break;}invalid(encoded(rows));
        auto raw=motionTrace;const auto at=raw.indexOf("\"journal_ms\":");QVERIFY(at>=0);raw.insert(at,"\"journal_ms\":0,");invalid(raw);
        rows=decoded(stopTrace);bool changed=false;for(auto &row:rows)if(row["event"]=="plan_end"){row["now_ms"]=0;changed=true;break;}QVERIFY(changed);invalid(encoded(rows));
    }
    void fileLimitsAndUnsupportedDoNotClaimCompletion(){
        NativeAudit a;QString e;QVERIFY(a.audit("{\"event\":\"session_start\",\"protocol\":\"native-monitor-v1\"}\n",e));QCOMPARE(a.report()["evidence"].toString(),"unsupported_format");QVERIFY(!a.audit(QByteArray(64*1024*1024+1,'x'),e));QVERIFY(!a.load("missing-native-audit-file.jsonl",e));QCOMPARE(a.report()["evidence"].toString(),"invalid");
        QVERIFY(a.audit("{\"event\":\"session_start\",\"protocol\":\"native-monitor-v2\",\"journal_ms\":0}\n{\"event\":\"session_end\",\"journal_ms\":1}\n",e));QCOMPARE(a.report()["evidence"].toString(),"not_applicable");
    }
    void coherentRawStopOrNonceTamperCannotKeepSuccess(){
        for(int type:{3,5}){
            auto rows=decoded(motionTrace);QByteArray original,altered;
            for(const auto &row:rows)if(row["event"]=="rx_frame"){const auto b=QByteArray::fromHex(row["frame_hex"].toString().toLatin1());if(quint8(b[3])==type){original=b;altered=b;u32(altered,type==3?10:10,type==3?1500:read32(b,10)+1);altered=seal(altered);break;}}
            QVERIFY(!original.isEmpty());for(auto &row:rows)if(row.contains("frame_hex")){auto b=QByteArray::fromHex(row["frame_hex"].toString().toLatin1());if(b.contains(original)){b.replace(original,altered);row["frame_hex"]=QString::fromLatin1(b.toHex(' '));}}
            invalid(encoded(rows));
        }
    }
    void duplicateReplyIsExcludedWithoutLosingValidPlan(){
        auto rows=decoded(motionTrace);QJsonObject rx,frameRow,control;QByteArray feedback;int insert=-1;
        for(int i=0;i<rows.size();++i){const auto &row=rows[i];if(row["event"]=="rx_frame"){const auto b=QByteArray::fromHex(row["frame_hex"].toString().toLatin1());if(quint8(b[3])==5&&feedback.isEmpty()){feedback=b;frameRow=row;}}if(!feedback.isEmpty()&&row["event"]=="control_rx"&&QByteArray::fromHex(row["frame_hex"].toString().toLatin1())==feedback)control=row;if(!control.isEmpty()&&row["event"]=="plan_state"){insert=i+1;break;}}
        QVERIFY(insert>0);rx=frameRow;rx["event"]="rx_chunk";frameRow["journal_ms"]=rows[insert-1]["journal_ms"];rx["journal_ms"]=frameRow["journal_ms"];control["journal_ms"]=frameRow["journal_ms"];control["now_ms"]=rows[insert-1]["now_ms"];
        rows.insert(insert,rx);rows.insert(insert+1,frameRow);rows.insert(insert+2,control);NativeAudit a;QString e;QVERIFY2(a.audit(encoded(rows),e),qPrintable(e));QCOMPARE(a.report()["evidence"].toString(),"complete");QVERIFY(a.report()["unmatched_or_duplicate"].toInt()>=1);
    }
    void finalStatusMustAnswerQuerySentAfterPlanEnd(){
        auto rows=decoded(motionTrace);int end=-1,tx=-1;for(int i=0;i<rows.size();++i){if(rows[i]["event"]=="plan_end")end=i;if(end>=0&&rows[i]["event"]=="tx_intent"){const auto b=QByteArray::fromHex(rows[i]["frame_hex"].toString().toLatin1());if(quint8(b[3])==4){tx=i;break;}}}
        QVERIFY(end>=0&&tx>end);QCOMPARE(rows[tx+1]["event"].toString(),"tx_queued");const auto intent=rows[tx],queued=rows[tx+1];rows.removeAt(tx+1);rows.removeAt(tx);rows.insert(end,intent);rows.insert(end+1,queued);for(int i=0;i<rows.size();++i)rows[i]["journal_ms"]=i;invalid(encoded(rows));
    }
    void heartbeatLossCannotBecomeSuccessfulOrVerifiedStop(){
        NativeAudit a;QString e;QVERIFY2(a.audit(heartbeatTrace,e),qPrintable(e));QCOMPARE(a.report()["evidence"].toString(),"partial");const auto p=a.report()["plans"].toArray()[0].toObject();QCOMPARE(p["state"].toString(),"uncertain");QVERIFY(!p["final_status_verified"].toBool());QCOMPARE(a.report()["task_writes"].toInt(),1);QCOMPARE(a.report()["operator_writes_by_opcode"].toObject()["2"].toInt(),1);
    }
};
QTEST_GUILESS_MAIN(AuditTests)
#include "audit_tests.moc"
