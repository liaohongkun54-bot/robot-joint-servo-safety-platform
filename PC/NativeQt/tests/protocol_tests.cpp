#include "history.h"
#include "wire_codec.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTest>
#include <QTemporaryFile>
#include <QtEndian>

class ProtocolTests : public QObject {
    Q_OBJECT
    QJsonObject vectors;
private slots:
    void initTestCase() {
        QFile file(qEnvironmentVariable("NATIVE_VECTORS",QStringLiteral(NATIVE_VECTOR_FILE)));QVERIFY2(file.open(QIODevice::ReadOnly),"Cannot open cross-language oracle JSON");
        vectors=QJsonDocument::fromJson(file.readAll()).object();QVERIFY(!vectors.isEmpty());
    }
    void exactPythonQueries() {
        for(const auto &item:vectors["queries"].toArray()) {const auto o=item.toObject();const auto raw=Wire::query(quint8(o["type"].toInt()),quint32(o["request"].toInteger()),quint32(o["nonce"].toInteger()));QCOMPARE(raw.toHex(),o["hex"].toString().toLatin1());Wire::Frame request;QString error;QVERIFY(Wire::request(raw,request,error));}
        for(quint8 type:{quint8(1),quint8(2),quint8(3),quint8(7)})QVERIFY(Wire::query(type,1,2).isEmpty());
        QVERIFY(Wire::query(4,0,1).isEmpty());
    }
    void decodeSyntheticFramesAgainstPython() {
        for(const auto &item:vectors["responses"].toArray()) {const auto o=item.toObject();Wire::Frame f;QString error;QVERIFY2(Wire::decode(QByteArray::fromHex(o["hex"].toString().toLatin1()),f,error),qPrintable(error));QCOMPARE(f.fields,o["fields"].toObject());}
    }
    void everyByteCrcAndChunking() {
        for(const auto &item:vectors["responses"].toArray()) {
            const auto raw=QByteArray::fromHex(item.toObject()["hex"].toString().toLatin1());
            for(int i=0;i<raw.size();++i) {auto bad=raw;bad[i]=char(quint8(bad[i])^1);Wire::Frame f;QString error;QVERIFY(!Wire::decode(bad,f,error));}
            Wire::Stream parser;QVector<Wire::Frame> found;for(char byte:raw) found+=parser.feed(QByteArray(1,byte));QCOMPARE(found.size(),1);QCOMPARE(found.first().raw,raw);QCOMPARE(parser.invalid,0);QVERIFY(parser.pending.isEmpty());
            Wire::Stream resync;auto bad=raw;bad[bad.size()-1]^=1;const auto recovered=resync.feed(QByteArray("noise")+bad+raw+raw);QCOMPARE(recovered.size(),2);QVERIFY(resync.invalid>0);QVERIFY(resync.discarded>0);
        }
    }
    void semanticValidationAfterValidCrc() {
        for(const auto &item:vectors["responses"].toArray()) {
            auto raw=QByteArray::fromHex(item.toObject()["hex"].toString().toLatin1());const auto type=quint8(raw[3]);
            const int offset=type==2?18:type==3?23:type==4?23:type==5?35:type==6?30:35;
            raw[offset]=char(0xff);qToLittleEndian(Wire::crc16(raw.first(raw.size()-2)),raw.data()+raw.size()-2);
            Wire::Frame f;QString error;QVERIFY(!Wire::decode(raw,f,error));
        }
    }
    void correlationDuplicatesAndWrap() {
        const auto responses=vectors["responses"].toArray();Wire::Frame telemetry;QString error;
        for(const auto &item:responses)if(item.toObject()["type"].toInt()==5){QVERIFY(Wire::decode(QByteArray::fromHex(item.toObject()["hex"].toString().toLatin1()),telemetry,error));break;}
        QVERIFY(!telemetry.raw.isEmpty());History history;QVERIFY(!history.accept(telemetry));
        const auto id=quint32(telemetry.fields["request_id"].toInteger()),nonce=quint32(telemetry.fields["nonce"].toInteger());
        QVERIFY(history.addRequest(Wire::query(5,id,nonce+1)));QVERIFY(!history.accept(telemetry));QVERIFY(history.samples.isEmpty());
        QVERIFY(history.addRequest(Wire::query(5,id,nonce)));QVERIFY(history.accept(telemetry));QVERIFY(!history.accept(telemetry));QCOMPARE(history.samples.size(),1);
        telemetry.fields["sample_tick_ms"]=16;telemetry.fields["last_rx_tick_ms"]=qint64(0xfffffff0u);telemetry.fields["link_state"]=1;telemetry.fields["has_valid_feedback"]=1;QVERIFY(Wire::Sample::fromFrame(telemetry).fresh);
        telemetry.fields["sample_tick_ms"]=150;QVERIFY(!Wire::Sample::fromFrame(telemetry).fresh);
    }
    void syntheticHistoryWithoutSerial() {
        History history;QString error;QVERIFY2(history.load(qEnvironmentVariable("NATIVE_SYNTHETIC_LOG",QStringLiteral(NATIVE_HISTORY_FILE)),error),qPrintable(error));
        QCOMPARE(history.samples.size(),44);for(const auto &sample:history.samples){QVERIFY(sample.fresh);QCOMPARE(sample.speed,0);}
        QVERIFY(!history.closed);QCOMPARE(history.invalid,0);QVERIFY(!history.sha256.isEmpty());
        QTemporaryFile malformed;QVERIFY(malformed.open());malformed.write("{broken}\n");malformed.flush();History bad;QVERIFY(!bad.load(malformed.fileName(),error));QVERIFY(bad.samples.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProtocolTests)
#include "protocol_tests.moc"
