#include "monitor_link.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUuid>

MonitorLink::MonitorLink(QObject *parent):QObject(parent),
    owned(std::make_unique<QtSerialEndpoint>()),transport(owned.get()) {initialize();}
MonitorLink::MonitorLink(SerialEndpoint &endpoint,const QString &directory,QObject *parent):
    QObject(parent),transport(&endpoint),journalDirectory(directory) {initialize();}
void MonitorLink::initialize() {
    requestId=QRandomGenerator::global()->bounded(quint32(1),quint32(0x7fffffff));
    poll.setInterval(100);timeout.setSingleShot(true);
    connect(&poll,&QTimer::timeout,this,[this]{if(!expectedType) query(captured%10==9?4:5);});
    connect(&timeout,&QTimer::timeout,this,[this]{
        if(!expectedType) return;
        expectedType=0;setCapture(false);
        if(!append("query_timeout",{{"request_id",qint64(requestId)}})) {close();return;}
        emit message(QStringLiteral("查询超时，已停止采集；串口保持连接"));
    });
    connect(transport,&SerialEndpoint::readyRead,this,[this]{
        if(sending) {deferredRead=true;return;}
        receive();
    });
    connect(transport,&SerialEndpoint::failed,this,[this](const QString &error){
        if(closing) return;
        close();emit message(error);
    });
}
MonitorLink::~MonitorLink() {close();}
bool MonitorLink::append(const QString &event,QJsonObject fields) {
    if(!journal.isOpen()) return false;
    fields["event"]=event;fields["host_timestamp_utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    fields["journal_ms"]=journalClock.isValid()?journalClock.elapsed():0;
    const auto line=QJsonDocument(fields).toJson(QJsonDocument::Compact)+'\n';
    if(journal.size()+line.size()>64*1024*1024||journal.write(line)!=line.size()||!journal.flush()) {
        journalHealthy=false;
        emit message(QStringLiteral("原始记录写入失败或达到64 MiB上限，连接关闭"));return false;
    }
    return true;
}
bool MonitorLink::open(const QString &name,qint32 baud,QString &error) {
    if(isOpen()) {error=QStringLiteral("端口已连接");return false;}
    journalHealthy=true;
    const auto dir=journalDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/sessions":journalDirectory;
    if(!QDir().mkpath(dir)) {error=QStringLiteral("无法创建日志目录");return false;}
    const auto protocol=connectionPurpose==Purpose::Platform?QStringLiteral("native-monitor-v2"):QStringLiteral("native-serial-v1");
    journal.setFileName(dir+"/"+protocol+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".jsonl");
    if(!journal.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {error=journal.errorString();return false;}
    journalClock.start();
    if(!append("session_start",{{"port",name},{"baud",baud},{"protocol",protocol},{"device_identity","unverified"}})) {
        journal.close();error=QStringLiteral("日志创建失败");return false;
    }
    ++generation;const auto epoch=generation;
    if(!transport->open(name,baud,error)||epoch!=generation) {
        if(transport->isOpen()) transport->close();
        if(journal.isOpen()) {append("session_end",{{"reason","port_open_failed"}});journal.close();}
        return false;
    }
    parser.reset();expectedType=0;capabilities=0;captured=0;deferredRead=false;connected=true;
    emit connectionChanged(true);emit message(QStringLiteral("串口已连接；等待读取设备与状态"));return true;
}
void MonitorLink::close() {
    if(closing) return;
    const bool wasConnected=connected;connected=false;closing=true;++generation;
    poll.stop();timeout.stop();setCapture(false);expectedType=0;capabilities=0;deferredRead=false;exclusive=false;
    transport->close();
    if(journal.isOpen()) {append("session_end",{{"reason","monitor_closed"}});journal.close();}
    parser.reset();closing=false;if(wasConnected) emit connectionChanged(false);
}
void MonitorLink::query(quint8 type) {
    if(connectionPurpose!=Purpose::Platform||!isOpen()||exclusive||expectedType||(type!=4&&type!=5&&type!=6)) return;
    if(type==5&&!(capabilities&2)) {emit message(QStringLiteral("请先读取支持反馈查询的设备信息"));return;}
    if(requestId==0xffffffffu) {close();emit message(QStringLiteral("请求号已耗尽，请重新启动程序"));return;}
    ++requestId;nonce=QRandomGenerator::global()->generate();const auto raw=Wire::query(type,requestId,nonce);
    if(!append("tx_intent",{{"frame_hex",QString::fromLatin1(raw.toHex(' '))}})) {close();return;}
    expectedType=type;const auto epoch=generation;sending=true;
    const auto written=transport->write(raw);sending=false;
    // A synchronous transport failure can already have closed this generation.
    if(epoch!=generation||!isOpen()) return;
    if(!append("tx_queued",{{"byte_count",written},{"expected_byte_count",raw.size()}})||written!=raw.size()) {
        close();emit message(QStringLiteral("发送不完整，未重试"));return;
    }
    timeout.start(1000);
    emit rawTransmitted(raw);
    if(deferredRead) {deferredRead=false;receive();}
}
void MonitorLink::setCapture(bool on) {
    if(on&&(connectionPurpose!=Purpose::Platform||!isOpen()||exclusive||!(capabilities&2))) {emit message(QStringLiteral("请使用平台连接并读取设备信息；计划或操作会话期间由执行器管理通信"));return;}
    if(capturing!=on) {capturing=on;emit captureChanged(on);}
    if(on) {captured=0;poll.start();} else poll.stop();
}
void MonitorLink::receive() {
    if(!isOpen()) return;
    const auto epoch=generation;const auto raw=transport->readAll();if(raw.isEmpty()) return;
    if(raw.size()>8192) {
        append("rx_oversized",{{"bytes",raw.size()},{"frame_hex",QString::fromLatin1(raw.first(8192).toHex(' '))},{"truncated",true}});
        close();emit message(QStringLiteral("串口输入超过有界缓冲，连接关闭"));return;
    }
    if(!append("rx_chunk",{{"frame_hex",QString::fromLatin1(raw.toHex(' '))}})) {close();return;}
    emit rawReceived(raw);
    if(connectionPurpose==Purpose::SerialTool||epoch!=generation||!isOpen())return;
    const auto oldInvalid=parser.invalid,oldDiscarded=parser.discarded;const auto frames=parser.feed(raw);
    for(const auto &f:frames) {
        if(epoch!=generation||!isOpen()) return;
        const bool matched=f.type==expectedType&&f.fields["request_id"].toInteger()==requestId&&f.fields["nonce"].toInteger()==nonce;
        if(!append("rx_frame",{{"kind",Wire::kind(f.type)},{"frame_hex",QString::fromLatin1(f.raw.toHex(' '))},{"matched",matched}})) {close();return;}
        emit observedFrame(f);
        if(epoch!=generation||!isOpen())return;
        if(!matched) continue;
        timeout.stop();expectedType=0;
        if(f.type==6) capabilities=quint16(f.fields["feature_mask"].toInt());
        if(f.type==5||(f.type==4&&capturing)) ++captured;
        emit frameReceived(f);
    }
    if(parser.invalid!=oldInvalid||parser.discarded!=oldDiscarded) {
        setCapture(false);emit message(QStringLiteral("收到损坏或非协议数据，采集已停止；原始字节已保存"));
    }
}
bool MonitorLink::acquireControl() {
    if(connectionPurpose!=Purpose::Platform||!isOpen()||expectedType||exclusive)return false;
    setCapture(false);exclusive=true;return true;
}
void MonitorLink::releaseControl() {exclusive=false;}
quint32 MonitorLink::allocateRequest() {
    if(requestId==0xffffffffu){close();emit message(QStringLiteral("请求号已耗尽"));return 0;}
    return ++requestId;
}
qint64 MonitorLink::controlledWrite(const QByteArray &raw) {
    if(connectionPurpose!=Purpose::Platform||!isOpen()||!exclusive||sending)return -1;
    Wire::Frame request;QString error;
    if(!Wire::request(raw,request,error))return -1;
    return writeRecorded(raw);
}
bool MonitorLink::setPurpose(Purpose purpose) {
    if(isOpen()||connected||sending||exclusive)return false;
    connectionPurpose=purpose;return true;
}
qint64 MonitorLink::sendRaw(const QByteArray &raw) {
    if(connectionPurpose!=Purpose::SerialTool||!isOpen()||exclusive||sending||raw.isEmpty()||raw.size()>4096)return -1;
    return writeRecorded(raw);
}
qint64 MonitorLink::writeRecorded(const QByteArray &raw) {
    if(!append("tx_intent",{{"frame_hex",QString::fromLatin1(raw.toHex(' '))}})){close();return -1;}
    const auto epoch=generation;sending=true;const auto written=transport->write(raw);sending=false;
    if(epoch!=generation||!isOpen())return -1;
    if(!append("tx_queued",{{"byte_count",written},{"expected_byte_count",raw.size()}})||written!=raw.size()){close();return -1;}
    emit rawTransmitted(raw);
    // Defer decoded callbacks until the caller has committed its pending state.
    if(deferredRead){deferredRead=false;QTimer::singleShot(0,this,[this,epoch]{if(epoch==generation)receive();});}
    return written;
}
