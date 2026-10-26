#pragma once
#include "plan_controller.h"
#include <QtEndian>
namespace {
void u16(QByteArray &b,int p,quint16 v){qToLittleEndian(v,b.data()+p);}
void u32(QByteArray &b,int p,quint32 v){qToLittleEndian(v,b.data()+p);}
quint32 read32(const QByteArray &b,int p){return qFromLittleEndian<quint32>(b.constData()+p);}
QByteArray seal(QByteArray b){u16(b,int(b.size())-2,Wire::crc16(b.first(b.size()-2)));return b;}
QByteArray envelope(int type,int size,quint32 id){QByteArray b(size,0);b.replace(0,2,"TR");b[2]=1;b[3]=char(type);u16(b,4,quint16(size-8));u32(b,6,id);return b;}
// An entirely in-memory MCU model. It has no QSerialPort or device names.
class MemoryEndpoint : public SerialEndpoint {
public:
    bool online=false,permit=false,holdTask=false,dropQuery=false,dropHeartbeat=false,dropClose=false,rejectClose=false,shortTask=false,failTask=false;
    int opens=0;quint32 task=0;QByteArray input;QVector<QByteArray> writes;
    bool open(const QString &name,qint32 baud,QString &) override {if(name!="MEMORY:"||baud!=115200)return false;online=true;++opens;return true;}
    void close() override {online=false;input.clear();}
    bool isOpen() const override {return online;}
    QByteArray readAll() override {QByteArray b;input.swap(b);return b;}
    void feed(const QByteArray &b){input+=b;emit readyRead();}
    QByteArray status(quint32 id,quint32 nonce) {
        auto b=envelope(4,38,id);u32(b,10,nonce);u16(b,18,1);u16(b,20,1);b[22]=2;b[23]=permit;b[24]=!permit;b[26]=task!=0;u16(b,30,64);return seal(b);
    }
    QByteArray stopped(int reason=1) {
        auto b=envelope(3,42,task);u32(b,10,2000);u16(b,14,1000);b[23]=char(reason);b[24]=2;b[25]=1;task=0;permit=false;return seal(b);
    }
    qint64 write(const QByteArray &raw) override {
        writes.append(raw);const int type=quint8(raw[3]);const auto id=read32(raw,6);
        if(type==6){auto b=envelope(6,48,id);u32(b,10,read32(raw,10));u16(b,30,1);u16(b,32,63);u32(b,38,20261022);u32(b,42,120000);feed(seal(b));}
        if(type==4&&!dropQuery)feed(status(id,read32(raw,10)));
        if(type==5&&!dropQuery){auto b=envelope(5,42,id);u32(b,10,read32(raw,10));u32(b,14,1000);u32(b,18,990);b[33]=1;b[34]=1;b[35]=1;u16(b,36,0x201);feed(seal(b));}
        if(type==7){const auto op=quint8(raw[30]);if((op==2&&dropHeartbeat)||(op==6&&dropClose))return raw.size();auto b=envelope(7,56,id);b.replace(10,4,raw.mid(10,4));u32(b,14,1);u32(b,22,3);b.replace(26,4,raw.mid(26,4));b[34]=char(op);int flags=1;
            if(op==3){permit=true;flags=3;u32(b,38,read32(raw,34));u32(b,46,10000);}
            if(op==2&&permit&&!task){flags=3;u32(b,38,1);u32(b,46,9000);}
            if(op==4){permit=false;if(task)feed(stopped(4));}
            if(op==6){if(rejectClose){b[35]=3;}else{permit=false;flags=0;}}
            u16(b,36,quint16(flags));u32(b,50,flags?1000:0);feed(seal(b));
        }
        if(type==1){const auto taskId=read32(raw,8);if(failTask){online=false;emit failed("injected drop during task write");return -1;}if(shortTask)return 2;
            auto b=envelope(2,24,taskId);u32(b,10,1000);u16(b,16,2);const bool nonzero=qFromLittleEndian<qint16>(raw.constData()+14)!=0;
            const bool rejected=!permit;u16(b,14,rejected?0xe2:0);b[18]=rejected?3:2;b[19]=rejected?3:6;b[20]=2;feed(seal(b));
            if(!rejected&&nonzero){task=taskId;if(!holdTask)feed(stopped());}
        }
        return raw.size();
    }
    int count(int type,int op=0) const {int n=0;for(const auto &b:writes)if(quint8(b[3])==type&&(!op||quint8(b[30])==op))++n;return n;}
};
}
