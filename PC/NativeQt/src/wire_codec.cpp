#include "wire_codec.h"
#include <QtEndian>
#include <array>
#include <QDate>
#include <QTime>

namespace {
quint8 u8(const QByteArray &b, int p) { return quint8(b.at(p)); }
quint16 u16(const QByteArray &b, int p) { return quint16(u8(b,p)) | quint16(quint16(u8(b,p+1))<<8); }
qint16 i16(const QByteArray &b, int p) { return qint16(u16(b,p)); }
quint32 u32(const QByteArray &b, int p) { return quint32(u16(b,p)) | (quint32(u16(b,p+2))<<16); }
void put16(QByteArray &b, int p, quint16 value) { qToLittleEndian(value,b.data()+p); }
void put32(QByteArray &b, int p, quint32 value) { qToLittleEndian(value,b.data()+p); }
bool envelope(const QByteArray &b, bool response, QString &error) {
    if(b.size()<8 || b.size()>56 || b.left(2)!=(response?QByteArray("TR"):QByteArray("KT")) || u8(b,2)!=1 || u16(b,4)!=b.size()-8) {
        error=QStringLiteral("帧长度、标识或版本错误"); return false;
    }
    if(Wire::crc16(b.first(b.size()-2))!=u16(b,b.size()-2)) { error=QStringLiteral("CRC校验失败"); return false; }
    return true;
}
}

namespace Wire {
quint16 crc16(const QByteArray &bytes) {
    quint16 crc=0xffff;
    for(char c:bytes) { crc^=quint16(quint8(c))<<8; for(int i=0;i<8;++i) crc=crc&0x8000?quint16((crc<<1)^0x1021):quint16(crc<<1); }
    return crc;
}
QString kind(quint8 type) {
    switch(type) {case 1:return "task";case 2:return "result";case 3:return "stop";case 4:return "status";case 5:return "telemetry";case 6:return "info";case 7:return "operator";default:return "unknown";}
}
QByteArray query(quint8 type, quint32 requestId, quint32 nonce) {
    if((type!=4 && type!=5 && type!=6)||!requestId) return {};
    QByteArray b(30,0); b.replace(0,2,"KT"); b[2]=1; b[3]=char(type);
    put16(b,4,22); put32(b,6,requestId); put32(b,10,nonce); put16(b,14,1);
    put16(b,28,crc16(b.first(28))); return b;
}
bool request(const QByteArray &b, Frame &f, QString &error) {
    if(!envelope(b,false,error)) return false;
    const auto t=u8(b,3); QJsonObject o;
    if(t==4||t==5||t==6) {
        if(b.size()!=30||u16(b,14)!=1||b.mid(16,12)!=QByteArray(12,0)||!u32(b,6)) {error="invalid query";return false;}
        o={{"request_id",qint64(u32(b,6))},{"nonce",qint64(u32(b,10))}};
    } else if(t==1) {
        if(b.size()!=30||u16(b,6)!=1||!u32(b,8)||u8(b,12)!=1||u8(b,13)!=1||u8(b,26)!=1||u8(b,27)!=1||i16(b,16)<1||i16(b,16)>16384||qAbs(i16(b,14))>i16(b,16)||u32(b,18)<1||u32(b,18)>1000||u32(b,22)<100||u32(b,22)>1000) {error="invalid task";return false;}
        o={{"schema_version",u16(b,6)},{"request_id",qint64(u32(b,8))},{"target_node_id",u8(b,12)},{"action",u8(b,13)},{"amplitude_raw",i16(b,14)},{"amplitude_limit_raw",i16(b,16)},{"duration_ms",qint64(u32(b,18))},{"feedback_timeout_ms",qint64(u32(b,22))},{"timeout_policy",u8(b,26)},{"permission",u8(b,27)}};
    } else if(t==7) {
        if(b.size()!=56||u8(b,30)<1||u8(b,30)>6||u8(b,31)!=0||!u32(b,6)) {error="invalid operator";return false;}
        o={{"request_id",qint64(u32(b,6))},{"nonce",qint64(u32(b,10))},{"boot_low",qint64(u32(b,14))},{"boot_high",qint64(u32(b,18))},{"session_id",qint64(u32(b,22))},{"sequence",qint64(u32(b,26))},{"operation",u8(b,30)}};
        if(u8(b,30)==1) {
            if(b.mid(14,16)!=QByteArray(16,0)) {error="invalid OPEN";return false;}
        } else if(!(u32(b,14)||u32(b,18))||!u32(b,22)||!u32(b,26)) {error="invalid session";return false;}
        if(u8(b,30)==3) {
            QByteArray nested=QByteArray::fromHex("4b5401011600")+b.mid(32,22);nested.resize(30);put16(nested,28,crc16(nested.first(28)));
            Frame task;
            if(!request(nested,task,error)) return false;
            o["task"]=task.fields;
        } else if(b.mid(32,22)!=QByteArray(22,0)) {error="unexpected task payload";return false;}
    } else {error="unsupported request";return false;}
    f={t,b,o}; return true;
}
bool decode(const QByteArray &b, Frame &f, QString &error) {
    if(!envelope(b,true,error)) return false;
    const auto t=u8(b,3); const std::array<int,8> lengths{0,0,24,42,38,42,48,56};
    if(t<2||t>7||b.size()!=lengths[t]) {error="unsupported response length";return false;}
    QJsonObject o{{"request_id",qint64(u32(b,6))}};
    auto n32=[&](const char *k,int p){o[k]=qint64(u32(b,p));};
    if(t==2) {
        n32("timestamp_ms",10);o["error_code"]=u16(b,14);o["record_count"]=u16(b,16);
        o["status"]=u8(b,18);o["stage"]=u8(b,19);o["safety_state"]=u8(b,20);o["flags"]=u8(b,21);
        if(u8(b,18)<1||u8(b,18)>4||u8(b,19)<1||u8(b,19)>6||u8(b,21)>3||(!u32(b,6)&&u8(b,19)!=1)||(u8(b,18)==2&&(u8(b,19)!=6||u16(b,14)))||(u8(b,18)==3&&!u16(b,14))) {error="invalid result semantics";return false;}
    } else if(t==3) {
        n32("timestamp_ms",10);o["command_amplitude_raw"]=i16(b,14);o["feedback_speed_rpm"]=i16(b,16);o["feedback_current_raw"]=i16(b,18);o["feedback_encoder_raw"]=u16(b,20);o["feedback_temperature_c"]=u8(b,22);o["stop_reason"]=u8(b,23);o["safety_state"]=u8(b,24);o["link_state"]=u8(b,25);n32("safety_fault_code",26);n32("observation_abs_travel_raw",30);o["observation_peak_abs_speed_rpm"]=u16(b,34);n32("observation_sample_count",36);
        if(!u32(b,6)||u8(b,23)<1||u8(b,23)>5) {error="invalid stop";return false;}
    } else if(t==4) {
        n32("nonce",10);n32("timestamp_ms",14);o["feature_mask"]=u16(b,20);o["safety_state"]=u8(b,22);o["output_permitted"]=u8(b,23);o["zero_output_required"]=u8(b,24);o["fault_latched"]=u8(b,25);o["motion_active"]=u8(b,26);o["record_count"]=u16(b,28);n32("safety_fault_code",32);
        if(!u32(b,6)||u16(b,18)!=1||u16(b,20)!=1||u8(b,22)>6||u8(b,27)||u16(b,30)!=64||u16(b,28)>64||u8(b,23)>1||u8(b,24)>1||u8(b,25)>1||u8(b,26)>1) {error="invalid status";return false;}
    } else if(t==5) {
        n32("nonce",10);n32("sample_tick_ms",14);n32("last_rx_tick_ms",18);n32("valid_frame_count",22);o["encoder_raw"]=u16(b,26);o["speed_rpm"]=i16(b,28);o["current_raw"]=i16(b,30);o["temperature_c"]=u8(b,32);o["link_state"]=u8(b,33);o["has_valid_feedback"]=u8(b,34);o["source_id"]=u8(b,35);o["feedback_std_id"]=u16(b,36);
        if(!u32(b,6)||u16(b,38)||u16(b,26)>8191||u8(b,33)>2||u8(b,34)>1||u8(b,35)!=1||u16(b,36)!=0x201) {error="invalid telemetry";return false;}
    } else if(t==6) {
        n32("nonce",10);n32("timestamp_ms",14);n32("uid_word0",18);n32("uid_word1",22);n32("uid_word2",26);o["feature_mask"]=u16(b,32);n32("firmware_version",34);n32("build_date",38);n32("build_time",42);
        if(!u32(b,6)||u16(b,30)!=1) {error="invalid info revision";return false;}
        const auto date=u32(b,38),time=u32(b,42);
        if(!QDate(int(date/10000),int(date/100%100),int(date%100)).isValid()||!QTime(int(time/10000),int(time/100%100),int(time%100)).isValid()) {error="invalid build date/time";return false;}
    } else {
        n32("nonce",10);n32("boot_low",14);n32("boot_high",18);n32("session_id",22);n32("sequence",26);n32("timestamp_ms",30);o["operation"]=u8(b,34);o["result"]=u8(b,35);o["flags"]=u16(b,36);n32("task_request_id",38);n32("fault_code",42);n32("grant_remaining_ms",46);n32("session_remaining_ms",50);
        if(!u32(b,6)||u8(b,34)<1||u8(b,34)>6||u8(b,35)>7||u16(b,36)>15||u32(b,46)>10000||u32(b,50)>1000) {error="invalid operator reply";return false;}
        const auto flags=u16(b,36);
        if(!(u32(b,14)||u32(b,18))||((flags&2)&&(!(flags&1)||(flags&12)||!u32(b,38)))||(!(flags&2)&&u32(b,46))||(!(flags&1)&&u32(b,50))) {error="inconsistent operator flags";return false;}
    }
    f={t,b,o};return true;
}
QVector<Frame> Stream::feed(const QByteArray &data) {
    pending+=data;
    if(pending.size()>8192) {const auto excess=pending.size()-8192;discarded+=quint64(excess);pending.remove(0,excess);}
    QVector<Frame> result;
    while(pending.size()>=2) {
        if(!pending.startsWith("TR")) {pending.remove(0,1);++discarded;continue;}
        if(pending.size()<6) break;
        const auto t=u8(pending,3);const std::array<int,8> sizes{0,0,24,42,38,42,48,56};
        if(u8(pending,2)!=1||t<2||t>7||u16(pending,4)!=sizes[t]-8) {pending.remove(0,1);++invalid;continue;}
        if(pending.size()<sizes[t]) break;
        Frame f;QString error;
        if(!decode(pending.first(sizes[t]),f,error)) {pending.remove(0,1);++invalid;continue;}
        pending.remove(0,sizes[t]);result.push_back(f);
    }
    return result;
}
void Stream::reset() { pending.clear();invalid=discarded=0; }
Sample Sample::fromFrame(const Frame &f) {
    Sample s;const auto &o=f.fields;
    s.request=quint32(o["request_id"].toInteger());s.tick=quint32(o["sample_tick_ms"].toInteger());s.lastRx=quint32(o["last_rx_tick_ms"].toInteger());s.count=quint32(o["valid_frame_count"].toInteger());s.encoder=o["encoder_raw"].toInt();s.speed=o["speed_rpm"].toInt();s.current=o["current_raw"].toInt();s.temperature=o["temperature_c"].toInt();s.fresh=o["has_valid_feedback"].toInt()==1&&o["link_state"].toInt()==1&&quint32(s.tick-s.lastRx)<=100;return s;
}
}
