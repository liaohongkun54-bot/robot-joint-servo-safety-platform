#include "operator_client.h"
#include <QScopedValueRollback>

OperatorClient::OperatorClient(Writer w,Allocate a,Nonce n,Record r,QObject *parent):
    QObject(parent),writer(std::move(w)),allocate(std::move(a)),nonce(std::move(n)),record(std::move(r)) {}
void OperatorClient::setState(const QString &value) {status=value;emit stateChanged(value);}
bool OperatorClient::active() const {return session&&status!="disconnected"&&status!="uncertain"&&status!="closed";}
void OperatorClient::disconnected() {
    ++generation;bootLow=bootHigh=session=sequence=0;control.reset();heartbeat.reset();granted.reset();
    grantDeadline=sessionDeadline=0;observedSequence=-1;setState("disconnected");
}
void OperatorClient::setCapabilities(quint16 features) {supported=(features&0x20)!=0;if(!supported)disconnected();}
void OperatorClient::fail(qint64 now,const QString &reason) {
    disconnected();setState("uncertain");record("operator_uncertain",{{"now_ms",now},{"reason",reason}});
}
bool OperatorClient::validTime(qint64 now) {
    if(now<0||now<lastTime) {fail(now,"non_monotonic_clock");return false;}
    lastTime=now;return true;
}
bool OperatorClient::send(Wire::Operation op,qint64 now,std::optional<Wire::Task> task) {
    if(!supported||inWrite||!validTime(now))return false;
    auto &slot=op==Wire::Operation::Heartbeat?heartbeat:control;
    if(slot)return false;
    Wire::OperatorRequest req;req.operation=op;req.task=task;
    if(op==Wire::Operation::Open) {if(active())return false;}
    else {
        if(!active())return false;
        if(sequence==0xffffffffu) {fail(now,"sequence_exhausted");return false;}
        req.bootLow=bootLow;req.bootHigh=bootHigh;req.session=session;req.sequence=++sequence;
    }
    req.requestId=allocate();req.nonce=nonce();QString error;const auto raw=Wire::encodeOperator(req,error);
    if(raw.isEmpty()) {fail(now,"invalid_operator_request");return false;}
    slot=Pending{req,now};if(op==Wire::Operation::Heartbeat)lastHeartbeat=now;
    QScopedValueRollback<bool> guard(inWrite,true);
    if(!record("operator_tx_intent",{{"now_ms",now},{"request_id",qint64(req.requestId)},{"frame_hex",QString::fromLatin1(raw.toHex(' '))}})) {fail(now,"log_error");return false;}
    const auto epoch=generation;const auto written=writer(raw);
    if(epoch!=generation)return false;
    if(!record("operator_tx_queued",{{"now_ms",now},{"request_id",qint64(req.requestId)},{"byte_count",written},{"expected_byte_count",raw.size()}})||written!=raw.size()) {fail(now,"write_or_log_error");return false;}
    return true;
}
bool OperatorClient::open(qint64 now) {
    if(active()||control)return false;
    setState("opening");const bool sent=send(Wire::Operation::Open,now);
    if(!sent&&status=="opening") setState("disconnected");
    return sent;
}
bool OperatorClient::grant(const Wire::Task &task,qint64 now) {
    if(control||!active()||!Wire::withinCurrentGrantLimits(task))return false;
    granted.reset();grantDeadline=0;return send(Wire::Operation::Grant,now,task);
}
bool OperatorClient::stop(qint64 now) {
    if(!active()||inWrite)return false;
    control.reset();granted.reset();grantDeadline=0;
    return send(Wire::Operation::Stop,now);
}
bool OperatorClient::clearFault(qint64 now) {
    if(control||!active())return false;
    granted.reset();grantDeadline=0;return send(Wire::Operation::Clear,now);
}
bool OperatorClient::close(qint64 now) {
    if(control||!active())return false;
    granted.reset();grantDeadline=0;return send(Wire::Operation::Close,now);
}
void OperatorClient::tick(qint64 now) {
    if(!validTime(now))return;
    if((control&&now-control->sent>=500)||(heartbeat&&now-heartbeat->sent>=500)) {fail(now,"reply_timeout_no_retry");return;}
    if(!active())return;
    if(now>=sessionDeadline) {fail(now,"observed_session_expired");return;}
    if(now>=grantDeadline)granted.reset();
    if(!heartbeat&&now-lastHeartbeat>=250)send(Wire::Operation::Heartbeat,now);
}
bool OperatorClient::grantedFor(const Wire::Task &task,qint64 now) const {
    if(!active()||now<lastTime||now>=sessionDeadline||now>=grantDeadline||!granted)return false;
    QString error;return Wire::encodeTask(task,error)==Wire::encodeTask(*granted,error);
}
bool OperatorClient::taskSubmitted(const Wire::Task &task,qint64 now) {
    if(!validTime(now)||!grantedFor(task,now))return false;
    granted.reset();grantDeadline=0;
    if(!record("operator_task_submitted",{{"now_ms",now},{"task_request_id",qint64(task.requestId)}})) {fail(now,"log_error");return false;}
    return true;
}
bool OperatorClient::receive(const QByteArray &raw,qint64 now) {
    if(!validTime(now))return false;
    Wire::Frame frame;QString error;if(!Wire::decode(raw,frame,error)||frame.type!=7)return false;
    const auto &r=frame.fields;const auto op=Wire::Operation(r["operation"].toInt());
    auto &slot=op==Wire::Operation::Heartbeat?heartbeat:control;
    if(!slot)return false;
    const auto pending=*slot;const auto &q=pending.request;
    if(r["request_id"].toInteger()!=q.requestId||r["nonce"].toInteger()!=q.nonce||op!=q.operation||r["sequence"].toInteger()!=q.sequence)return false;
    if(now-pending.sent>=500) {fail(now,"late_reply");return false;}
    if(!record("operator_rx",{{"now_ms",now},{"frame_hex",QString::fromLatin1(raw.toHex(' '))}})) {fail(now,"log_error");return false;}
    slot.reset();
    if(op!=Wire::Operation::Open&&(r["boot_low"].toInteger()!=bootLow||r["boot_high"].toInteger()!=bootHigh||r["session_id"].toInteger()!=session)) {fail(now,"controller_session_changed");return true;}
    if(op!=Wire::Operation::Open&&q.sequence<observedSequence)return true;
    observedSequence=q.sequence;
    const int flags=r["flags"].toInt(),result=r["result"].toInt();
    if(result) {
        granted.reset();grantDeadline=0;
        if(op==Wire::Operation::Open||result==2||!(flags&1))disconnected();
        setState("rejected");return true;
    }
    if(!(flags&1)||!r["session_id"].toInteger()) {
        if(op==Wire::Operation::Close) {disconnected();setState("closed");}
        else fail(now,"controller_session_not_active");
        return true;
    }
    if(op==Wire::Operation::Open) {
        if(flags!=1) {fail(now,"open_must_not_grant");return true;}
        bootLow=quint32(r["boot_low"].toInteger());bootHigh=quint32(r["boot_high"].toInteger());session=quint32(r["session_id"].toInteger());sequence=0;lastHeartbeat=now;
    }
    sessionDeadline=pending.sent+r["session_remaining_ms"].toInteger();
    if(op==Wire::Operation::Grant) {
        if(flags!=3||!q.task||r["task_request_id"].toInteger()!=q.task->requestId) {fail(now,"grant_reply_task_mismatch");return true;}
        granted=q.task;grantDeadline=pending.sent+r["grant_remaining_ms"].toInteger();
    } else if(!(flags&2)) {granted.reset();grantDeadline=0;}
    setState(flags&8?"stop_pending":"session_active");return true;
}
