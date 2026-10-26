#include "plan_run.h"
#include "strict_json.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <cmath>

namespace {
QString sha(const QByteArray &raw) {return QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex());}
bool integer(const QJsonValue &v,int low,int high) {
    return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()==std::floor(v.toDouble())&&v.toDouble()>=low&&v.toDouble()<=high;
}
bool identifier(const QJsonValue &v) {
    static const QRegularExpression re(QStringLiteral("^[a-z][a-z0-9_]{2,63}$"));
    return v.isString()&&re.match(v.toString()).hasMatch();
}
bool exact(const QJsonObject &o,const QStringList &keys) {const auto actual=o.keys();return QSet<QString>(actual.begin(),actual.end())==QSet<QString>(keys.begin(),keys.end());}

}

bool PlanSnapshot::Action::motion() const {return testId=="limited_motion"||testId=="deadline_stop";}
Wire::Task PlanSnapshot::Action::task(quint32 id) const {
    Wire::Task t;t.requestId=id;t.amplitude=qint16(command.value_or(0));t.limit=qint16(qMax(100,qAbs(command.value_or(0))));t.duration=duration;return t;
}
QString PlanSnapshot::draftSha256() const {return sha(draft);}
QString PlanSnapshot::profileSha256() const {return sha(profile);}
bool PlanSnapshot::hasMotion() const {for(const auto &a:entries)if(a.motion())return true;return false;}
quint16 PlanSnapshot::requiredFeatures() const {
    quint16 features=5;
    for(const auto &a:entries)features|=a.command?8:2;
    // Unlike the old standalone scheduler, production motion also needs operator sessions.
    if(hasMotion())features|=0x30;
    return features;
}
std::optional<PlanSnapshot> PlanSnapshot::compile(const QByteArray &draftRaw,const QByteArray &profileRaw,QString &error) {
    error.clear();
    auto fail=[&](const QString &why)->std::optional<PlanSnapshot>{error=why;return {};};
    if(draftRaw.size()>65536||profileRaw.size()>131072)return fail("draft or profile exceeds size bound");
    QJsonObject d,p;if(!StrictJson::object(draftRaw,d,error)||!StrictJson::object(profileRaw,p,error))return {};
    if(!exact(d,{"schema_version","status","plan_id","profile_id","profile_sha256","purpose","steps"}))return fail("draft fields differ from schema");
    if(!integer(d["schema_version"],1,1)||d["status"]!="draft_not_executable"||!identifier(d["plan_id"])||!identifier(d["profile_id"]))return fail("invalid draft version, status or identifier");
    if(!integer(p["schema_version"],1,1)||p["adapter_id"]!="c620_id1_v1"||p["protocol_family"]!="vendor_can")return fail("no executable adapter for capability profile");
    if(!identifier(p["profile_id"])||p["profile_id"]!=d["profile_id"]||d["profile_sha256"].toString().toLower()!=sha(profileRaw))return fail("selected profile ID or SHA-256 mismatch");
    const auto purpose=d["purpose"].toString();
    if(!d["purpose"].isString()||purpose.isEmpty()||purpose!=purpose.trimmed()||purpose.toUcs4().size()>200)return fail("purpose requires 1..200 trimmed characters");
    QHash<QString,QJsonObject> modes;
    if(!p["control_modes"].isArray()||p["control_modes"].toArray().isEmpty()||p["control_modes"].toArray().size()>16)return fail("invalid capability control modes");
    for(const auto &v:p["control_modes"].toArray()) {
        if(!v.isObject())return fail("invalid capability mode");
        const auto m=v.toObject();const auto name=m["mode_id"].toString();
        if(!identifier(m["mode_id"])||modes.contains(name)||!m["command_unit"].isString()||m["command_unit"].toString().isEmpty()||!integer(m["min_command"],-16384,16384)||!integer(m["max_command"],-16384,16384)||m["min_command"].toInt()>m["max_command"].toInt()||!integer(m["max_duration_ms"],1,1000))return fail("invalid or duplicate capability mode limits");
        modes.insert(name,m);
    }
    if(!d["steps"].isArray()||d["steps"].toArray().isEmpty()||d["steps"].toArray().size()>16)return fail("steps requires 1..16 entries");
    PlanSnapshot result;result.id=d["plan_id"].toString();result.deviceId=d["profile_id"].toString();
    result.draft=draftRaw;result.profile=profileRaw;QSet<QString> ids;int stepIndex=0;
    const QSet<QString> tests{"feedback_readonly","zero_output","unarmed_rejection","limited_motion","deadline_stop"};
    for(const auto &v:d["steps"].toArray()) {
        if(!v.isObject())return fail("step must be object");
        const auto s=v.toObject();
        if(!exact(s,{"step_id","test_id","mode_id","command_value","duration_ms","attempts"})||!identifier(s["step_id"])||ids.contains(s["step_id"].toString())||!tests.contains(s["test_id"].toString())||!integer(s["attempts"],1,16))return fail("invalid step fields, identifier, type or attempts");
        ids.insert(s["step_id"].toString());Action a;a.stepIndex=stepIndex++;a.stepId=s["step_id"].toString();a.testId=s["test_id"].toString();
        if(a.testId=="feedback_readonly") {
            if(!s["mode_id"].isNull()||!s["command_value"].isNull()||!integer(s["duration_ms"],0,0))return fail("readonly feedback has no command or duration");
        } else {
            a.modeId=s["mode_id"].toString();const auto mode=modes.value(a.modeId);
            if(mode.isEmpty()||!integer(s["command_value"],mode["min_command"].toInt(),mode["max_command"].toInt())||!integer(s["duration_ms"],1,mode["max_duration_ms"].toInt()))return fail("step exceeds selected capability limits");
            a.command=s["command_value"].toInt();a.duration=quint32(s["duration_ms"].toInt());
            if(a.motion()==(*a.command==0))return fail("motion requires nonzero command; zero/rejection tests require zero");
            const bool supported=(a.modeId=="bounded_current_test"&&qAbs(*a.command)<=1000&&a.duration<=100)||(a.modeId=="fixed_positive_run"&&*a.command==1000&&a.duration==1000);
            if(!supported)return fail("step not supported by current MCU gate");
            QString codecError;if(Wire::encodeTask(a.task(1),codecError).isEmpty())return fail(codecError);
        }
        for(int attempt=1;attempt<=s["attempts"].toInt();++attempt) {
            a.attempt=attempt;result.entries.append(a);
            if(result.reservedRecords()>64)return fail("worst-case record reservation exceeds 64");
        }
    }
    return result;
}

PlanRun::PlanRun(PlanSnapshot snapshot,bool motionConfirmed):plan(std::move(snapshot)) {
    if(plan.hasMotion()&&!motionConfirmed)finish("blocked","motion not explicitly confirmed for this run");
}
bool PlanRun::terminal() const {return status=="succeeded"||status=="failed"||status=="cancelled"||status=="uncertain"||status=="blocked";}
void PlanRun::finish(const QString &value,const QString &why) {status=value;reason=why;pending.reset();}
bool PlanRun::clock(qint64 now) {
    if(now<0||now<lastTime){uncertain("non_monotonic_clock");return false;}lastTime=now;return true;
}
std::optional<PlanRun::Operation> PlanRun::next() const {
    if(terminal()||pending||cancelled)return {};
    if(phase=="info")return Operation{Kind::Info};
    if(phase=="status")return Operation{Kind::Status};
    const auto &a=plan.actions().at(index);
    if(a.command&&checkedIndex!=index)return Operation{Kind::Status,index,false};
    return Operation{a.command?Kind::Task:Kind::Feedback,index,true};
}
QByteArray PlanRun::dispatch(quint32 id,quint32 nonce,qint64 now,QString &error) {
    error.clear();if(!clock(now)) {error=reason;return {};}
    const auto op=next();
    if(!op||!id||usedIds.contains(id)){error="operation unavailable or request ID invalid/reused";return {};}
    QByteArray raw;
    if(op->kind==Kind::Task)raw=Wire::encodeTask(plan.actions()[index].task(id),error);
    else raw=Wire::query(quint8(op->kind),id,nonce);
    if(raw.isEmpty())return {};
    usedIds.insert(id);pending=Pending{*op,id,nonce,now,now+(op->kind==Kind::Feedback?300:1000)};
    status=op->kind==Kind::Info?"waiting_info":op->kind==Kind::Status?"waiting_status":op->kind==Kind::Feedback?"waiting_feedback":"waiting_result";
    return raw;
}
void PlanRun::uncertain(const QString &why) {
    if(terminal())return;
    if(pending&&pending->operation.action) {
        const auto &a=plan.actions()[index];results.append(QJsonObject{{"action_index",index},{"step_id",a.stepId},{"attempt",a.attempt},{"request_id",qint64(pending->id)},{"outcome","uncertain"},{"detail",why},{"mcu_tick_ms",QJsonValue::Null},{"elapsed_ms",QJsonValue::Null}});
    }
    finish("uncertain",why);
}
void PlanRun::tick(qint64 now) {if(terminal()||!clock(now))return;if(pending&&now>=pending->deadline)uncertain("receipt_timeout_no_retry");}
void PlanRun::cancel() {if(terminal())return;cancelled=true;if(!pending)finish("cancelled","future scheduling cancelled");}
void PlanRun::complete(const QString &verdict,const QString &why,quint32 tick,std::optional<quint32> elapsed) {
    const auto &a=plan.actions()[index];
    results.append(QJsonObject{{"action_index",index},{"step_id",a.stepId},{"attempt",a.attempt},{"request_id",qint64(pending->id)},{"outcome",verdict},{"detail",why},{"mcu_tick_ms",qint64(tick)},{"elapsed_ms",elapsed?QJsonValue(qint64(*elapsed)):QJsonValue::Null}});
    pending.reset();admittedTick.reset();
    if(cancelled)finish("cancelled","scheduling cancelled; in-flight outcome retained");
    else if(verdict!="passed")finish("failed",why);
    else if(++index==plan.actions().size())finish("succeeded","all planned protocol checks passed");
    else status="ready";
}
bool PlanRun::receive(const QByteArray &raw,qint64 now) {
    if(terminal()||!pending||!clock(now))return false;
    if(now>=pending->deadline){uncertain("receipt_timeout_no_retry");return false;}
    Wire::Frame f;QString error;if(!Wire::decode(raw,f,error))return false;
    const auto p=*pending;const auto expected=p.operation.kind==Kind::Task?2:quint8(p.operation.kind);const auto &r=f.fields;
    if(f.type!=expected||r["request_id"].toInteger()!=p.id)return false;
    if(f.type>=4&&r["nonce"].toInteger()!=p.nonce)return false;
    if(f.type==6) {
        if((r["feature_mask"].toInt()&plan.requiredFeatures())!=plan.requiredFeatures())finish("failed","device lacks required protocol features");
        else {pending.reset();phase="status";status="ready";}
    } else if(f.type==4) {
        bool tasksRemain=false;for(int i=index;i<plan.actions().size();++i)tasksRemain|=plan.actions()[i].command.has_value();
        const int required=plan.reservedRecords()-2*int(results.size());
        const bool actionCheck=p.operation.actionIndex>=0;
        if(r["motion_active"].toInt()||r["record_count"].toInt()+required>64)finish("failed","controller busy or insufficient remaining record capacity");
        else if(tasksRemain&&(r["safety_state"].toInt()!=2||r["fault_latched"].toInt()||r["safety_fault_code"].toInteger()))finish("failed","local Safety is not ready for this task plan");
        else if(actionCheck&&plan.actions()[index].testId=="unarmed_rejection"&&(r["output_permitted"].toInt()!=0||r["zero_output_required"].toInt()!=1))finish("failed","expected-rejection check requires unarmed status");
        else if(actionCheck&&plan.actions()[index].command&&plan.actions()[index].testId!="unarmed_rejection"&&(r["output_permitted"].toInt()!=1||r["zero_output_required"].toInt()!=0))finish("failed","MCU has not permitted task output");
        else {pending.reset();phase="actions";checkedIndex=p.operation.actionIndex;status="ready";}
    } else if(f.type==5) {
        const bool fresh=Wire::Sample::fromFrame(f).fresh;complete(fresh?"passed":"failed",fresh?"fresh_feedback":"stale_or_offline_feedback",quint32(r["sample_tick_ms"].toInteger()));
    } else if(f.type==2) {
        const auto &a=plan.actions()[index];const auto tick=quint32(r["timestamp_ms"].toInteger());
        if(r["record_count"].toInt()>64){uncertain("task receipt exceeds known record capacity");return true;}
        if(a.testId=="unarmed_rejection") {
            const bool expectedReject=r["status"].toInt()==3&&r["stage"].toInt()==3&&r["error_code"].toInt()==0xe2;
            complete(expectedReject?"passed":"failed",expectedReject?"expected_safety_rejection":"unexpected_task_admission_or_rejection",tick);
        } else if(r["status"].toInt()==2&&r["stage"].toInt()==6&&r["error_code"].toInt()==0) {
            if(a.command.value_or(0)==0)complete("passed","zero_output_ack",tick);
            else {admittedTick=tick;pending->operation.kind=Kind::Stop;pending->deadline=now+a.duration+3000;status="waiting_stop";}
        } else complete("failed",QString("task_rejected_stage_%1_error_0x%2").arg(r["stage"].toInt()).arg(r["error_code"].toInt(),4,16,QLatin1Char('0')),tick);
    } else if(f.type==3) {
        const auto &a=plan.actions()[index];const auto tick=quint32(r["timestamp_ms"].toInteger());const quint32 elapsed=tick-*admittedTick;
        if(r["command_amplitude_raw"].toInt()!=a.command.value_or(0)||elapsed>=0x80000000u)uncertain("stop amplitude or MCU timestamp differs from admitted task");
        else {const bool correct=r["stop_reason"].toInt()==1&&r["safety_fault_code"].toInteger()==0&&elapsed>=a.duration;complete(correct?"passed":"failed",correct?"deadline_stop_receipt":"early_or_fault_stop",tick,elapsed);}
    }
    if(cancelled&&!terminal()&&!pending)finish("cancelled","future scheduling cancelled");
    return true;
}
QJsonObject PlanRun::report() const {
    QJsonArray actions;
    for(int i=0;i<plan.actions().size();++i) {
        const auto &a=plan.actions()[i];QJsonObject row{{"action_index",i},{"step_id",a.stepId},{"attempt",a.attempt},{"test_id",a.testId},{"command_raw",a.command?QJsonValue(*a.command):QJsonValue::Null},{"duration_ms",qint64(a.duration)},{"outcome","not_executed"}};
        if(i<results.size()){const auto result=results[i].toObject();for(auto it=result.begin();it!=result.end();++it)row[it.key()]=it.value();}
        else if(pending&&pending->operation.action&&i==index)row["outcome"]=status;
        actions.append(row);
    }
    return {{"schema_version",1},{"plan_id",plan.planId()},{"profile_id",plan.profileId()},{"profile_sha256",plan.profileSha256()},{"draft_sha256",plan.draftSha256()},{"state",status},{"detail",reason},{"reserved_records",plan.reservedRecords()},{"actions",actions},{"mechanical_standstill","not_inferred_from_receipts"}};
}
