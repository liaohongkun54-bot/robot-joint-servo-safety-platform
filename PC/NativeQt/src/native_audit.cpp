#include "native_audit.h"
#include "operator_client.h"
#include "plan_run.h"
#include "strict_json.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QQueue>
#include <QRegularExpression>
#include <memory>
#include <cmath>

namespace {
bool number(const QJsonValue &v,qint64 &out) {
    if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<0||v.toDouble()>9007199254740991.0||std::floor(v.toDouble())!=v.toDouble())return false;
    out=v.toInteger(-1);return out>=0;
}
bool hex(const QJsonValue &value,QByteArray &out,int maximum) {
    if(!value.isString())return false;
    const auto text=value.toString();if(text.size()>maximum*3)return false;
    static const QRegularExpression re(QStringLiteral("^(?:[0-9a-fA-F]{2}(?:[ ]?))*$"));
    if(!re.match(text).hasMatch())return false;
    auto compact=text;compact.remove(' ');if(compact.size()%2)return false;
    out=QByteArray::fromHex(compact.toLatin1());return out.size()<=maximum;
}
QString key(int type,qint64 id){return QString::number(type)+":"+QString::number(id);}
Wire::Task task(const QJsonObject &o) {
    Wire::Task t;t.requestId=quint32(o["request_id"].toInteger());t.amplitude=qint16(o["amplitude_raw"].toInt());t.limit=qint16(o["amplitude_limit_raw"].toInt());t.duration=quint32(o["duration_ms"].toInteger());t.feedbackTimeout=quint32(o["feedback_timeout_ms"].toInteger());t.node=quint8(o["target_node_id"].toInt());t.action=quint8(o["action"].toInt());t.timeoutPolicy=quint8(o["timeout_policy"].toInt());t.permission=quint8(o["permission"].toInt());return t;
}
bool sameReport(const QJsonObject &computed,const QJsonObject &row) {
    for(auto it=computed.begin();it!=computed.end();++it)if(row.value(it.key())!=it.value())return false;
    return true;
}
bool clearStatus(const Wire::Frame &f) {return f.type==4&&f.fields["output_permitted"].toInt()==0&&f.fields["motion_active"].toInt()==0&&f.fields["zero_output_required"].toInt()==1;}
}

bool NativeAudit::load(const QString &path,QString &error) {
    result={};QFile f(path);
    if(!f.open(QIODevice::ReadOnly)||f.size()>64*1024*1024){error=QStringLiteral("日志无法读取或超过64 MiB");result={{"evidence","invalid"},{"error",error},{"source",path}};return false;}
    const bool ok=audit(f.readAll(),error);result["source"]=path;return ok;
}
bool NativeAudit::audit(const QByteArray &raw,QString &error) {
    error.clear();result={{"audit_version",1},{"evidence","partial"},{"source_bytes",raw.size()},{"source_sha256",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex())},{"mechanical_standstill","not_inferred"},{"device_identity","self_report_only_not_authenticated"}};
    QJsonArray plans;int lineNo=0;bool began=false,closed=false,version2=false;
    qint64 lastJournal=-1,lastInput=-1,inputTime=0;
    quint32 allocation=0,nonce=0;
    QByteArray generated;
    QQueue<QPair<QString,QJsonObject>> operatorEvents;
    OperatorClient op([&](const QByteArray &b){generated=b;return b.size();},[&]{return allocation;},[&]{return nonce;},[&](const QString &event,const QJsonObject &fields){operatorEvents.enqueue({event,fields});return true;});
    std::unique_ptr<PlanRun> run;QString runId;QSet<QString> runIds;
    bool ended=false,clear=false,taskConsumed=false,stopRequested=false,stopWritten=false;
    int endLine=0,clearLine=0;
    QJsonObject runDeviceInfo,operationWrites;int taskWrites=0;
    QByteArray planRaw,operatorRaw,physicalRaw;
    QHash<quint32,Wire::Frame> sent;
    QHash<quint32,int> sentLines;
    QSet<QString> responses;
    Wire::Stream parser;QVector<Wire::Frame> batch;int decodedIndex=0;
    std::optional<Wire::Frame> current;
    bool currentMatched=false,currentControlled=false;
    quint64 frames=0,unmatched=0;
    auto addPlan=[&]{
        if(!run)return;
        auto summary=run->report();summary["run_id"]=runId;summary["device_info"]=runDeviceInfo;summary["end_record_present"]=ended;summary["final_status_verified"]=clear;summary["final_status_line"]=clearLine;summary["independent_stop_requested"]=stopRequested;summary["independent_stop_written"]=stopWritten;summary["evidence"]=(ended&&clear&&run->terminal()&&(!stopRequested||stopWritten))?"complete":"partial";plans.append(summary);
    };
    auto fail=[&](const QString &why){
        error=QStringLiteral("第%1行：%2").arg(lineNo).arg(why);addPlan();result["plans"]=plans;result["evidence"]="invalid";result["error"]=error;return false;
    };
    if(raw.size()>64*1024*1024)return fail("input exceeds 64 MiB");
    const QSet<QString> known{"session_start","session_end","tx_intent","tx_queued","rx_chunk","rx_frame","rx_oversized","query_timeout","control_rx","plan_start","plan_dispatch","plan_state","plan_end","plan_tick","plan_cancel","plan_clearance","plan_probe","operator_tx_intent","operator_tx_queued","operator_rx","operator_uncertain","operator_task_submitted","operator_close_not_confirmed"};
    for(const auto &line:raw.split('\n')) {
        if(line.trimmed().isEmpty())continue;
        if(++lineNo>200000||line.size()>1024*1024)return fail("row bound exceeded");
        QJsonObject row;QString parseError;if(!StrictJson::object(line,row,parseError))return fail(parseError);
        const auto event=row["event"].toString();
        if(!began) {
            began=true;
            if(event!="session_start"||row["protocol"]!="native-monitor-v2"){
                result["evidence"]="unsupported_format";result["detail"]="This reader requires native-monitor-v2 raw and monotonic input records; no verified plan is inferred.";return true;
            }
            version2=true;
        } else if(closed||event=="session_start")return fail("data after session end or concatenated sessions");
        if(!known.contains(event))return fail("unsupported journal event: "+event);
        qint64 journal=0;if(!number(row["journal_ms"],journal)||journal<lastJournal)return fail("invalid or reversed journal monotonic time");lastJournal=journal;
        const bool controllerInput=event.startsWith("plan_")||(event.startsWith("operator_")&&event!="operator_close_not_confirmed")||event=="control_rx";
        if(controllerInput){if(!number(row["now_ms"],inputTime)||inputTime<lastInput)return fail("invalid or reversed controller monotonic time");lastInput=inputTime;}
        QByteArray bytes;
        if(row.contains("frame_hex")&&!hex(row["frame_hex"],bytes,event=="rx_chunk"?8192:56))return fail("invalid HEX encoding or frame bound");
        if(event=="plan_start") {
            if(run&&(!ended||!clear))return fail("new plan before previous outcome and output state settled");
            if(!planRaw.isEmpty()||!physicalRaw.isEmpty()||!operatorRaw.isEmpty())return fail("plan overlaps outstanding transmission");
            addPlan();run.reset();
            runId=row["run_id"].toString();if(runId.isEmpty()||runId.size()>64||runIds.contains(runId)||runIds.size()>=1024)return fail("invalid/reused run ID or plan count bound");runIds.insert(runId);
            QByteArray draft,profile;if(!hex(row["draft_hex"],draft,65536)||!hex(row["profile_hex"],profile,131072)||!row["motion_confirmed"].isBool())return fail("invalid plan snapshot inputs");
            QString compileError;auto snapshot=PlanSnapshot::compile(draft,profile,compileError);if(!snapshot)return fail(compileError);
            if(snapshot->draftSha256()!=row["draft_sha256"]||snapshot->profileSha256()!=row["profile_sha256"])return fail("snapshot digest mismatch");
            if(snapshot->hasMotion()&&(!row["motion_confirmed"].toBool()||!op.active()))return fail("motion plan lacks recorded confirmation or matched active session");
            run=std::make_unique<PlanRun>(*snapshot,row["motion_confirmed"].toBool());ended=clear=taskConsumed=stopRequested=stopWritten=false;endLine=clearLine=0;runDeviceInfo={};
        } else if(event.startsWith("plan_")) {
            if(!run||row["run_id"]!=runId)return fail("plan input references no active snapshot");
            if(event=="plan_dispatch") {
                if(ended||!planRaw.isEmpty())return fail("overlapping or post-end plan dispatch");
                Wire::Frame request;if(!Wire::request(bytes,request,parseError))return fail(parseError);
                const auto id=quint32(request.fields["request_id"].toInteger());const auto n=quint32(request.fields["nonce"].toInteger());
                planRaw=run->dispatch(id,n,inputTime,parseError);taskConsumed=false;
                if(planRaw.isEmpty()||planRaw!=bytes)return fail("transmitted operation differs from frozen plan: "+parseError);
                if(request.type==1&&request.fields["amplitude_raw"].toInt()!=0&&!op.grantedFor(task(request.fields),inputTime))return fail("motion dispatch has no unexpired exact task grant");
            } else if(event=="plan_cancel") {
                if(ended)return fail("cancel after plan end");
                if(row["source"]!="scheduling"&&row["source"]!="operator_stop")return fail("unsupported cancellation input");
                stopRequested=stopRequested||row["source"]=="operator_stop";
                run->cancel();
            } else if(event=="plan_tick") {
                if(ended)return fail("timeout input after plan end");
                run->tick(inputTime);
            } else if(event=="plan_state"||event=="plan_end") {
                if(event=="plan_end"&&ended)return fail("duplicate plan end");
                if(event=="plan_end"&&!run->terminal()&&run->snapshot().hasMotion()&&!op.active())run->uncertain("operator_session_lost_no_retry");
                if(!sameReport(run->report(),row))return fail("stored plan result differs from raw replay");
                if(event=="plan_end"){if(!run->terminal())return fail("nonterminal plan end");ended=true;endLine=lineNo;}
            } else if(event=="plan_clearance") {
                if(!ended||!current||current->raw!=bytes||!currentMatched||current->type!=4||sentLines.value(quint32(current->fields["request_id"].toInteger()))<=endLine||!row["clear"].isBool()||row["clear"].toBool()!=clearStatus(*current))return fail("final status assertion differs from a post-end matching query/reply");
            } else if(event=="plan_probe") {
                if(!current||current->raw!=bytes||!currentMatched||(current->type!=4&&current->type!=5))return fail("probe lacks matching raw request/reply");
            }
        } else if(event=="operator_tx_intent") {
            if(!operatorEvents.isEmpty()||!operatorRaw.isEmpty())return fail("operator operation overlaps unresolved write records");
            Wire::Frame request;if(!Wire::request(bytes,request,parseError)||request.type!=7)return fail("invalid operator request");
            const auto &q=request.fields;allocation=quint32(q["request_id"].toInteger());nonce=quint32(q["nonce"].toInteger());generated.clear();
            const auto operation=Wire::Operation(q["operation"].toInt());bool allowed=false;
            switch(operation){
            case Wire::Operation::Open:allowed=op.open(inputTime);break;
            case Wire::Operation::Heartbeat:op.tick(inputTime);allowed=!generated.isEmpty();break;
            case Wire::Operation::Grant:{
                if(!run||run->terminal()||run->actionIndex()>=run->snapshot().actions().size())return fail("grant outside an active plan");
                const auto &a=run->snapshot().actions()[run->actionIndex()];const auto nested=task(q["task"].toObject());QString e;
                if(!a.motion()||Wire::encodeTask(a.task(nested.requestId),e)!=Wire::encodeTask(nested,e))return fail("grant task differs from current frozen action");
                allowed=op.grant(nested,inputTime);break;}
            case Wire::Operation::Stop:allowed=op.stop(inputTime);break;
            case Wire::Operation::Clear:allowed=op.clearFault(inputTime);break;
            case Wire::Operation::Close:allowed=op.close(inputTime);break;
            }
            if(!allowed||generated!=bytes)return fail("operator session, sequence or exact command is not reproducible");
            operatorRaw=bytes;
        } else if(event=="operator_task_submitted") {
            Wire::Frame request;if(planRaw.isEmpty()||!Wire::request(planRaw,request,parseError)||request.type!=1||taskConsumed||row["task_request_id"]!=request.fields["request_id"]||!op.taskSubmitted(task(request.fields),inputTime))return fail("task permission missing, mismatched or consumed more than once");taskConsumed=true;
        } else if(event=="tx_intent") {
            if(!physicalRaw.isEmpty())return fail("physical TX has no completion record");
            Wire::Frame request;if(!Wire::request(bytes,request,parseError))return fail(parseError);
            if(request.type==7&&bytes!=operatorRaw)return fail("operator write lacks matching typed operation");
            if(request.type==1&&(bytes!=planRaw||(request.fields["amplitude_raw"].toInt()!=0&&!taskConsumed)))return fail("task write lacks plan dispatch or one-shot grant consumption");
            if(!planRaw.isEmpty()&&request.type!=7&&bytes!=planRaw)return fail("different bytes written after plan dispatch");
            physicalRaw=bytes;
        } else if(event=="tx_queued") {
            if(physicalRaw.isEmpty())return fail("TX completion without intent");
            if(row["expected_byte_count"].toInteger(-1)!=physicalRaw.size()||row["byte_count"].toInteger(-1)!=physicalRaw.size())return fail("partial/failed TX cannot prove delivery");
            Wire::Frame request;Wire::request(physicalRaw,request,parseError);const auto id=quint32(request.fields["request_id"].toInteger());
            if(sent.contains(id)||sent.size()>=20000)return fail("physical request ID reused or ledger bound exceeded");
            sent.insert(id,request);
            sentLines.insert(id,lineNo);
            if(request.type==1)++taskWrites;
            if(request.type==7){const auto operation=QString::number(request.fields["operation"].toInt());operationWrites[operation]=operationWrites[operation].toInt()+1;if(run&&stopRequested&&request.fields["operation"].toInt()==4)stopWritten=true;}
            if(physicalRaw==planRaw)planRaw.clear();
            if(physicalRaw==operatorRaw)operatorRaw.clear();
            physicalRaw.clear();
        } else if(event=="rx_chunk") {
            if(!physicalRaw.isEmpty())return fail("RX recorded before TX completion");
            if(decodedIndex!=batch.size())return fail("decoded-frame records missing from previous RX chunk");
            batch=parser.feed(bytes);decodedIndex=0;current.reset();currentMatched=currentControlled=false;
        } else if(event=="rx_frame") {
            if(decodedIndex>=batch.size()||batch[decodedIndex].raw!=bytes)return fail("decoded frame not present in original RX stream");
            current=batch[decodedIndex++];currentControlled=false;++frames;
            const auto &r=current->fields;const auto id=quint32(r["request_id"].toInteger());const auto found=sent.constFind(id);
            currentMatched=found!=sent.cend()&&(current->type==2||current->type==3?found->type==1:found->type==current->type);
            if(currentMatched&&r.contains("nonce"))currentMatched=found->fields["nonce"]==r["nonce"];
            if(currentMatched&&current->type==7)currentMatched=found->fields["operation"]==r["operation"]&&found->fields["sequence"]==r["sequence"];
            if(currentMatched&&current->type==7&&found->fields["operation"].toInt()!=1)for(const auto *field:{"boot_low","boot_high","session_id"})currentMatched=currentMatched&&found->fields[field]==r[field];
            const auto responseKey=key(current->type,id);if(responses.contains(responseKey))currentMatched=false;
            if(currentMatched)responses.insert(responseKey);else ++unmatched;
            if(currentMatched&&current->type==6)op.setCapabilities(quint16(r["feature_mask"].toInt()));
            if(ended&&currentMatched&&current->type==4&&sentLines.value(id)>endLine){clear=clearStatus(*current);clearLine=lineNo;}
        } else if(event=="control_rx") {
            if(!current||current->raw!=bytes||currentControlled)return fail("controller RX input lacks original frame proof or is duplicated");
            currentControlled=true;
            if(current->type==7)op.receive(bytes,inputTime);
            if(run&&!run->terminal()&&run->receive(bytes,inputTime)&&current->type==6){runDeviceInfo=current->fields;runDeviceInfo["source_line"]=lineNo;}
        } else if(event=="operator_uncertain") {
            if(operatorEvents.isEmpty())op.tick(inputTime);
        } else if(event=="operator_close_not_confirmed") {
            if(op.state()!="rejected"||!op.active()||op.controlPending())return fail("unconfirmed close has no matched rejection");
            op.disconnected();
        } else if(event=="session_end") {
            closed=true;op.disconnected();if(run&&!run->terminal())run->uncertain("physical_connection_lost_no_retry");
        }
        if(event=="operator_tx_intent"||event=="operator_tx_queued"||event=="operator_rx"||event=="operator_task_submitted"||event=="operator_uncertain") {
            if(operatorEvents.isEmpty()||operatorEvents.head().first!=event||!sameReport(operatorEvents.head().second,row))return fail("operator record differs from replayed raw inputs");
            operatorEvents.dequeue();
        }
    }
    if(!version2)return fail("empty journal");
    addPlan();bool complete=!plans.isEmpty();for(const auto &p:plans)complete=complete&&p.toObject()["evidence"]=="complete";
    complete=complete&&physicalRaw.isEmpty()&&planRaw.isEmpty()&&operatorRaw.isEmpty()&&operatorEvents.isEmpty()&&parser.pending.isEmpty()&&decodedIndex==batch.size()&&raw.endsWith('\n');
    result["evidence"]=plans.isEmpty()?"not_applicable":complete?"complete":"partial";result["plans"]=plans;result["session_closed"]=closed;result["operator_client_state"]=op.state();result["operator_writes_by_opcode"]=operationWrites;result["task_writes"]=taskWrites;result["valid_frames"]=qint64(frames);result["unmatched_or_duplicate"]=qint64(unmatched);result["invalid_wire_frames"]=qint64(parser.invalid);result["discarded_wire_bytes"]=qint64(parser.discarded);result["pending_wire_bytes"]=parser.pending.size();result["journal_rows"]=lineNo;
    return true;
}
