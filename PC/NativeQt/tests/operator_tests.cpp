#include "operator_client.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTest>
#include <QtEndian>

class OperatorTests : public QObject {
    Q_OBJECT
    QJsonObject vectors;
    Wire::Task task() const {Wire::Task t;t.requestId=900;t.amplitude=t.limit=1000;t.duration=1000;return t;}
    QByteArray response(const QByteArray &raw,int result=-1,int flags=-1) const {
        Wire::Frame request;QString error;if(!Wire::request(raw,request,error))return {};
        QByteArray reply;
        const int op=request.fields["operation"].toInt();
        for(const auto &item:vectors["replies"].toArray())if(item.toObject()["fields"].toObject()["operation"].toInt()==op){reply=QByteArray::fromHex(item.toObject()["hex"].toString().toLatin1());break;}
        if(reply.isEmpty())return {};
        reply.replace(6,8,raw.mid(6,8));reply.replace(26,4,raw.mid(26,4));
        if(op==3)qToLittleEndian(quint32(request.fields["task"].toObject()["request_id"].toInteger()),reply.data()+38);
        if(result>=0)reply[35]=char(result);
        if(flags>=0) {
            qToLittleEndian(quint16(flags),reply.data()+36);
            if(flags&2) {qToLittleEndian(quint32(900),reply.data()+38);qToLittleEndian(quint32(9000),reply.data()+46);}
            else qToLittleEndian(quint32(0),reply.data()+46);
            if(!(flags&1))qToLittleEndian(quint32(0),reply.data()+50);
        }
        qToLittleEndian(Wire::crc16(reply.first(54)),reply.data()+54);return reply;
    }
    struct Fixture {
        QVector<QByteArray> writes;
        quint32 id=1;
        bool logOk=true;
        qint64 writeSize=56;
        OperatorClient client;
        Fixture():client([this](const QByteArray &raw){writes.push_back(raw);return writeSize;},[this]{return id++;},[]{return 99u;},[this](const QString &,const QJsonObject &){return logOk;}){}
    };
    void open(Fixture &f) const {f.client.setCapabilities(0x3f);QVERIFY(f.client.open(0));QVERIFY(f.client.receive(response(f.writes.last()),0));QVERIFY(f.client.active());}
private slots:
    void initTestCase() {
        QFile file(QStringLiteral(NATIVE_OPERATOR_VECTORS));QVERIFY(file.open(QIODevice::ReadOnly));vectors=QJsonDocument::fromJson(file.readAll()).object();QVERIFY(!vectors.isEmpty());
    }
    void exactTaskAndSixOperationsMatchPython() {
        QString error;QCOMPARE(Wire::encodeTask(task(),error).toHex(),vectors["task"].toObject()["hex"].toString().toLatin1());
        for(const auto &item:vectors["requests"].toArray()) {
            const auto row=item.toObject(),o=row["fields"].toObject();Wire::OperatorRequest r;
            r.requestId=quint32(o["request_id"].toInteger());r.nonce=quint32(o["nonce"].toInteger());r.bootLow=quint32(o["boot_low"].toInteger());r.bootHigh=quint32(o["boot_high"].toInteger());r.session=quint32(o["session_id"].toInteger());r.sequence=quint32(o["sequence"].toInteger());r.operation=Wire::Operation(o["operation"].toInt());if(r.operation==Wire::Operation::Grant)r.task=task();
            QCOMPARE(Wire::encodeOperator(r,error).toHex(),row["hex"].toString().toLatin1());
        }
    }
    void capabilityAndGrantLimitsCannotBeBypassed() {
        Fixture f;f.client.tick(0);QVERIFY(!f.client.open(0));QVERIFY(f.writes.isEmpty());open(f);
        auto t=task();t.amplitude=1001;QVERIFY(!f.client.grant(t,10));t=task();t.duration=999;QVERIFY(!f.client.grant(t,10));t=task();t.amplitude=-1000;QVERIFY(!f.client.grant(t,10));QCOMPARE(f.writes.size(),1);
        t.duration=100;QVERIFY(f.client.grant(t,10));
    }
    void exactGrantIsConsumedOnlyOnceAndNeverSendsTask() {
        Fixture f;open(f);const auto t=task();QVERIFY(f.client.grant(t,10));QVERIFY(f.client.receive(response(f.writes.last()),20));QVERIFY(f.client.grantedFor(t,20));
        auto changed=t;changed.requestId++;QVERIFY(!f.client.grantedFor(changed,20));QVERIFY(!f.client.taskSubmitted(changed,20));
        QVERIFY(f.client.taskSubmitted(t,30));QVERIFY(!f.client.taskSubmitted(t,30));QVERIFY(!f.client.grantedFor(t,30));
        QCOMPARE(f.writes.size(),2);for(const auto &raw:f.writes)QCOMPARE(quint8(raw[3]),quint8(7));
    }
    void stopPreemptsGrantAndOlderHeartbeatCannotRestoreIt() {
        Fixture f;open(f);QVERIFY(f.client.grant(task(),10));const auto grant=f.writes.last();
        f.client.tick(250);QVERIFY(f.client.heartbeatPending());const auto hb=f.writes.last();
        QVERIFY(f.client.stop(260));const auto stop=f.writes.last();
        QVERIFY(!f.client.receive(response(grant),270));QVERIFY(f.client.receive(response(stop),280));QVERIFY(f.client.receive(response(hb,0,3),290));
        QVERIFY(!f.client.grantedFor(task(),290));QVERIFY(!f.client.controlPending());QVERIFY(!f.client.heartbeatPending());QVERIFY(f.client.active());
    }
    void lateGrantCannotBecomePermissionEvenBeforeTick() {
        Fixture f;open(f);QVERIFY(f.client.grant(task(),10));const auto grant=f.writes.last();
        QVERIFY(!f.client.receive(response(grant),510));QCOMPARE(f.client.state(),QString("uncertain"));QVERIFY(!f.client.active());
        f.client.tick(900);QCOMPARE(f.writes.size(),2);QVERIFY(!f.client.grantedFor(task(),900));
    }
    void heartbeatAndControlHaveIndependentPendingSlots() {
        Fixture f;open(f);f.client.tick(250);QVERIFY(f.client.heartbeatPending());QVERIFY(f.client.stop(300));QVERIFY(f.client.controlPending());
        QCOMPARE(f.writes.size(),3);f.client.tick(750);QVERIFY(!f.client.active());QCOMPARE(f.client.state(),QString("uncertain"));f.client.tick(1000);QCOMPARE(f.writes.size(),3);
    }
    void shortWriteAndLogFailureDoNotRetry() {
        Fixture f;f.client.setCapabilities(0x20);f.writeSize=55;QVERIFY(!f.client.open(0));QCOMPARE(f.client.state(),QString("uncertain"));f.client.tick(1000);QCOMPARE(f.writes.size(),1);
        Fixture logging;logging.client.setCapabilities(0x20);logging.logOk=false;QVERIFY(!logging.client.open(0));QVERIFY(logging.writes.isEmpty());QCOMPARE(logging.client.state(),QString("uncertain"));
    }
    void closeAndRejectedExpiredSessionRemainInactive() {
        Fixture f;open(f);QVERIFY(f.client.close(10));QVERIFY(f.client.receive(response(f.writes.last()),20));QCOMPARE(f.client.state(),QString("closed"));QVERIFY(!f.client.active());f.client.tick(1000);QCOMPARE(f.writes.size(),2);
        Fixture expired;open(expired);expired.client.tick(250);QVERIFY(expired.client.receive(response(expired.writes.last(),2,0),260));QVERIFY(!expired.client.active());expired.client.tick(1000);QCOMPARE(expired.writes.size(),2);
    }
    void wrongNonceAndChangedBootAreNotAccepted() {
        Fixture f;open(f);QVERIFY(f.client.grant(task(),10));auto reply=response(f.writes.last());reply[10]^=1;qToLittleEndian(Wire::crc16(reply.first(54)),reply.data()+54);QVERIFY(!f.client.receive(reply,20));QVERIFY(f.client.controlPending());
        reply=response(f.writes.last());reply[14]^=1;qToLittleEndian(Wire::crc16(reply.first(54)),reply.data()+54);QVERIFY(f.client.receive(reply,30));QCOMPARE(f.client.state(),QString("uncertain"));QVERIFY(!f.client.active());
    }
    void synchronousReplyThenShortWriteStillBecomesUncertain() {
        QVector<QByteArray> writes;quint32 id=1;OperatorClient *client=nullptr;
        OperatorClient c([&](const QByteArray &raw){writes.push_back(raw);client->receive(response(raw),0);return qint64(55);},[&]{return id++;},[]{return 7u;},[](const QString &,const QJsonObject &){return true;});client=&c;
        c.setCapabilities(0x20);QVERIFY(!c.open(0));QVERIFY(!c.active());QCOMPARE(c.state(),QString("uncertain"));QCOMPARE(writes.size(),1);
    }
    void sessionExpiryAndClockReversalDoNotRenew() {
        Fixture f;open(f);f.client.tick(1000);QCOMPARE(f.client.state(),QString("uncertain"));QCOMPARE(f.writes.size(),1);
        Fixture reversed;open(reversed);reversed.client.tick(100);reversed.client.tick(99);QCOMPARE(reversed.client.state(),QString("uncertain"));QVERIFY(!reversed.client.active());
    }
};
QTEST_GUILESS_MAIN(OperatorTests)
#include "operator_tests.moc"
