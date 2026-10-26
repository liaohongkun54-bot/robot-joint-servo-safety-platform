#include "operator_codec.h"
#include <QtEndian>

namespace Wire {
QByteArray encodeTask(const Task &t,QString &error) {
    QByteArray raw(30,0);raw.replace(0,6,QByteArray::fromHex("4b5401011600"));
    qToLittleEndian(quint16(1),raw.data()+6);qToLittleEndian(t.requestId,raw.data()+8);
    raw[12]=char(t.node);raw[13]=char(t.action);
    qToLittleEndian(quint16(t.amplitude),raw.data()+14);qToLittleEndian(quint16(t.limit),raw.data()+16);
    qToLittleEndian(t.duration,raw.data()+18);qToLittleEndian(t.feedbackTimeout,raw.data()+22);
    raw[26]=char(t.timeoutPolicy);raw[27]=char(t.permission);qToLittleEndian(crc16(raw.first(28)),raw.data()+28);
    Frame frame;if(!request(raw,frame,error))return {};return raw;
}
QByteArray encodeOperator(const OperatorRequest &r,QString &error) {
    QByteArray raw(56,0);raw.replace(0,6,QByteArray::fromHex("4b5401073000"));
    qToLittleEndian(r.requestId,raw.data()+6);qToLittleEndian(r.nonce,raw.data()+10);
    qToLittleEndian(r.bootLow,raw.data()+14);qToLittleEndian(r.bootHigh,raw.data()+18);
    qToLittleEndian(r.session,raw.data()+22);qToLittleEndian(r.sequence,raw.data()+26);raw[30]=char(r.operation);
    if(r.operation==Operation::Grant) {
        if(!r.task) {error=QStringLiteral("许可必须绑定精确任务");return {};}
        const auto task=encodeTask(*r.task,error);if(task.isEmpty())return {};
        raw.replace(32,22,task.mid(6,22));
    } else if(r.task) {error=QStringLiteral("非许可操作不能携带任务");return {};}
    qToLittleEndian(crc16(raw.first(54)),raw.data()+54);Frame frame;
    if(!request(raw,frame,error)) return {};
    return raw;
}
bool withinCurrentGrantLimits(const Task &t) {
    QString error;
    return !encodeTask(t,error).isEmpty()&&t.limit<=1000&&t.amplitude>=-1000&&t.amplitude<=1000&&
        (t.duration<=100||(t.amplitude==1000&&t.duration==1000));
}
}
