#include "monitor_link.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QtEndian>
#include <functional>

class FakeEndpoint final : public SerialEndpoint {
public:
    bool active=false,denyOpen=false;
    int opens=0,closes=0;
    qint64 writeResult=-2;
    QByteArray incoming;
    QVector<QByteArray> writes;
    std::function<void(const QByteArray &)> onWrite;
    bool open(const QString &name,qint32 baud,QString &error) override {
        ++opens;
        if(!name.startsWith("FAKE:")||baud!=115200||denyOpen) {error="fake open rejected";return false;}
        active=true;return true;
    }
    void close() override {if(active)++closes;active=false;incoming.clear();}
    bool isOpen() const override {return active;}
    qint64 write(const QByteArray &raw) override {
        writes.push_back(raw);
        if(onWrite)onWrite(raw);
        return writeResult==-2?raw.size():writeResult;
    }
    QByteArray readAll() override {QByteArray raw;raw.swap(incoming);return raw;}
    void feed(const QByteArray &raw) {incoming+=raw;emit readyRead();}
    void fail() {active=false;emit failed("injected resource error");}
};

class MonitorTests : public QObject {
    Q_OBJECT
    QJsonArray vectors;
    QByteArray reply(const QByteArray &request,int nonceOffset=0) const {
        QByteArray raw;
        for(const auto &item:vectors)if(item.toObject()["type"].toInt()==quint8(request[3])){raw=QByteArray::fromHex(item.toObject()["hex"].toString().toLatin1());break;}
        if(raw.isEmpty())return {};
        raw.replace(6,8,request.mid(6,8));
        if(nonceOffset)qToLittleEndian(qFromLittleEndian<quint32>(raw.constData()+10)+quint32(nonceOffset),raw.data()+10);
        qToLittleEndian(Wire::crc16(raw.first(raw.size()-2)),raw.data()+raw.size()-2);
        return raw;
    }
    void identify(MonitorLink &link,FakeEndpoint &fake) const {
        link.query(6);fake.feed(reply(fake.writes.last()));
    }
private slots:
    void initTestCase() {
        QFile file(QStringLiteral(NATIVE_VECTOR_FILE));QVERIFY(file.open(QIODevice::ReadOnly));
        vectors=QJsonDocument::fromJson(file.readAll()).object()["responses"].toArray();QVERIFY(!vectors.isEmpty());
    }
    void whitelistCapabilityAndCapture() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;
        QVERIFY(link.open("FAKE:monitor",115200,error));QTest::qWait(120);QVERIFY(fake.writes.isEmpty());
        link.query(1);link.query(7);link.query(5);link.setCapture(true);QVERIFY(fake.writes.isEmpty());QVERIFY(!link.isCapturing());
        int samples=0,status=0;connect(&link,&MonitorLink::frameReceived,this,[&](const Wire::Frame &f){samples+=f.type==5;status+=f.type==4;});
        identify(link,fake);
        fake.onWrite=[&](const QByteArray &raw){fake.feed(reply(raw));};
        link.setCapture(true);QTRY_VERIFY_WITH_TIMEOUT(samples>=10&&status>=1,1800);
        link.setCapture(false);const auto count=fake.writes.size();QTest::qWait(130);QCOMPARE(fake.writes.size(),count);QVERIFY(link.isOpen());
        for(const auto &raw:fake.writes)QVERIFY(quint8(raw[3])==4||quint8(raw[3])==5||quint8(raw[3])==6);
    }
    void synchronousReplyIsLoggedAfterQueueRecord() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:sync",115200,error));
        int received=0;connect(&link,&MonitorLink::frameReceived,this,[&](const Wire::Frame &){++received;});
        fake.onWrite=[&](const QByteArray &raw){fake.feed(reply(raw));};link.query(6);
        QCOMPARE(received,1);QVERIFY(!link.isWaiting());
        QFile file(link.journalPath());QVERIFY(file.open(QIODevice::ReadOnly));const auto data=file.readAll();
        QVERIFY(data.indexOf("tx_queued")<data.indexOf("rx_chunk"));
        link.close();QVERIFY(!link.isOpen());
    }
    void wrongNonceBusyTimeoutAndLateReply() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:timeout",115200,error));
        int received=0;connect(&link,&MonitorLink::frameReceived,this,[&](const Wire::Frame &){++received;});
        link.query(4);const auto first=fake.writes.last();link.query(6);QCOMPARE(fake.writes.size(),1);
        fake.feed(reply(first,1));QCOMPARE(received,0);QVERIFY(link.isWaiting());
        QTRY_VERIFY_WITH_TIMEOUT(!link.isWaiting(),1500);QVERIFY(link.isOpen());QCOMPARE(fake.writes.size(),1);
        fake.feed(reply(first));QCOMPARE(received,0);
        link.query(4);QVERIFY(fake.writes.last()!=first);fake.feed(reply(fake.writes.last()));QCOMPARE(received,1);
    }
    void shortWriteAndSynchronousTransportFailure() {
        QTemporaryDir dir;
        for(bool fail:{false,true}) {
            FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:write",115200,error));
            QSignalSpy changed(&link,&MonitorLink::connectionChanged);
            if(fail)fake.onWrite=[&](const QByteArray &){fake.fail();};else fake.writeResult=29;
            link.query(4);QVERIFY(!link.isOpen());QVERIFY(!link.isWaiting());QVERIFY(!link.isCapturing());QCOMPARE(fake.writes.size(),1);QCOMPARE(changed.size(),1);
            link.query(4);QCOMPARE(fake.writes.size(),1);link.close();QCOMPARE(changed.size(),1);
        }
    }
    void reconnectDropsOldPartialAndOldReplies() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:old",115200,error));
        link.query(4);const auto old=reply(fake.writes.last());fake.feed(old.first(10));const auto path=link.journalPath();link.close();
        QVERIFY(link.open("FAKE:new",115200,error));QVERIFY(link.journalPath()!=path);
        int received=0;connect(&link,&MonitorLink::frameReceived,this,[&](const Wire::Frame &){++received;});
        link.query(4);const auto current=reply(fake.writes.last());fake.feed(old.mid(10)+old+current);QCOMPARE(received,1);QVERIFY(!link.isWaiting());
    }
    void crcStopsCaptureAndManualRestartRecovers() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:crc",115200,error));identify(link,fake);
        link.setCapture(true);link.query(5);auto raw=reply(fake.writes.last());raw[raw.size()-1]^=1;fake.feed(raw);QVERIFY(!link.isCapturing());
        fake.feed(reply(fake.writes.last()));QVERIFY(!link.isWaiting());
        int received=0;connect(&link,&MonitorLink::frameReceived,this,[&](const Wire::Frame &f){received+=f.type==5;});
        fake.onWrite=[&](const QByteArray &query){fake.feed(reply(query));};link.setCapture(true);
        QTRY_VERIFY_WITH_TIMEOUT(received>=2,600);QVERIFY(link.isCapturing());link.setCapture(false);
        fake.feed(QByteArray(8193,'x'));QVERIFY(!link.isOpen());
    }
    void journalCannotOpenDoesNotOpenTransport() {
        QTemporaryFile occupied;QVERIFY(occupied.open());FakeEndpoint fake;MonitorLink link(fake,occupied.fileName());QString error;
        QVERIFY(!link.open("FAKE:log",115200,error));QCOMPARE(fake.opens,0);QVERIFY(fake.writes.isEmpty());
    }
    void recordLimitStopsBeforeTransmission() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:limit",115200,error));
        QFile record(link.journalPath());QVERIFY(record.open(QIODevice::ReadWrite));QVERIFY(record.resize(64*1024*1024));record.close();
        link.query(4);QVERIFY(fake.writes.isEmpty());QVERIFY(!link.isOpen());
    }
    void physicalFailureClearsCaptureAndDoesNotReconnect() {
        QTemporaryDir dir;FakeEndpoint fake;MonitorLink link(fake,dir.path());QString error;QVERIFY(link.open("FAKE:drop",115200,error));identify(link,fake);link.setCapture(true);
        QSignalSpy changed(&link,&MonitorLink::connectionChanged);fake.fail();QVERIFY(!link.isOpen());QVERIFY(!link.isCapturing());QVERIFY(!link.isWaiting());QCOMPARE(changed.size(),1);
        QTest::qWait(150);QCOMPARE(fake.opens,1);
    }
};
QTEST_GUILESS_MAIN(MonitorTests)
#include "monitor_tests.moc"
