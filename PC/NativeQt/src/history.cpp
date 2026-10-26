#include "history.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {
QString key(quint8 type, qint64 request) { return QString::number(type)+":"+QString::number(request); }
}
bool History::addRequest(const QByteArray &raw) {
    Wire::Frame f;QString error;
    if(!Wire::request(raw,f,error)) return false;
    const auto requestKey=key(f.type,f.fields["request_id"].toInteger());
    if(requests.size()>=20000&&!requests.contains(requestKey)) return false;
    requests.insert(requestKey,f);
    return true;
}
bool History::accept(const Wire::Frame &f) {
    const auto lookup=key((f.type==2||f.type==3)?1:f.type,f.fields["request_id"].toInteger());
    const auto req=requests.constFind(lookup);
    bool matched=req!=requests.cend();
    if(matched && f.fields.contains("nonce")) matched=req->fields["nonce"]==f.fields["nonce"];
    if(matched && f.type==7) {
        matched=req->fields["operation"]==f.fields["operation"]&&req->fields["sequence"]==f.fields["sequence"];
        if(matched&&req->fields["operation"].toInt()!=1) for(const auto *k:{"boot_low","boot_high","session_id"}) matched=matched&&req->fields[k]==f.fields[k];
    }
    const auto responseKey=key(f.type,f.fields["request_id"].toInteger());
    if(!matched||seen.contains(responseKey)) {++unmatched;return false;}
    seen.insert(responseKey,true);
    frames.push_back(f);
    if(f.type==5) samples.push_back(Wire::Sample::fromFrame(f));
    if(frames.size()>20000) frames.removeFirst();
    if(samples.size()>20000) samples.removeFirst();
    return true;
}
bool History::load(const QString &path, QString &error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>32*1024*1024) {error=QStringLiteral("文件无法读取或超过32 MiB");return false;}
    const auto raw=file.readAll();
    History result;result.source=path;result.sha256=QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex());
    Wire::Stream parser;int lines=0;
    for(const auto &line:raw.split('\n')) {
        if(line.trimmed().isEmpty()) continue;
        if(++lines>100000||line.size()>128*1024) {error=QStringLiteral("日志行数或行长度超过限制");return false;}
        QJsonParseError parse;const auto doc=QJsonDocument::fromJson(line,&parse);
        if(parse.error!=QJsonParseError::NoError||!doc.isObject()) {error=QStringLiteral("第%1行JSON无效").arg(lines);return false;}
        const auto row=doc.object();const auto event=row["event"].toString();
        if((event.startsWith("tx_")&&event.endsWith("_intent"))||event=="tx_intent") {
            const auto hex=row["frame_hex"].toString().toLatin1();
            if(!hex.isEmpty()&&!result.addRequest(QByteArray::fromHex(hex))) ++result.invalid;
        }
        if(event=="rx_chunk") {
            const auto chunk=QByteArray::fromHex(row["frame_hex"].toString().toLatin1());
            for(const auto &f:parser.feed(chunk)) result.accept(f);
        }
        if(event=="session_end"||event=="plan_end"||event=="probe_end") result.closed=true;
    }
    result.invalid+=parser.invalid;result.discarded=parser.discarded+quint64(parser.pending.size());
    if(result.frames.isEmpty()) {error=QStringLiteral("没有可关联的有效收发帧；保留原文件");return false;}
    *this=std::move(result);return true;
}
QJsonObject History::summary() const {
    int fresh=0;for(const auto &s:samples) if(s.fresh) ++fresh;
    return {{"source",source},{"sha256",sha256},{"closed",closed},{"matched_frames",frames.size()},
            {"samples",samples.size()},{"fresh_samples",fresh},{"invalid",qint64(invalid)},
            {"discarded_bytes",qint64(discarded)},{"unmatched_or_duplicate",qint64(unmatched)}};
}
