#include "monitor_link.h"
#include "plan_controller.h"
#include "serial_panel.h"
#include "serial_text.h"
#include "main_window.h"
#include <QApplication>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QAction>
#include <QTextBrowser>
#include <QtEndian>
#include <functional>

class ManualEndpoint final : public SerialEndpoint {
public:
    bool active=false,shortWrite=false,drop=false;
    QByteArray incoming;QVector<QByteArray> writes;int opens=0;
    std::function<void(const QByteArray &)> onWrite;
    bool open(const QString &name,qint32,QString &error) override {if(!name.startsWith("MEMORY:")){error="memory only";return false;}active=true;++opens;return true;}
    void close() override {active=false;incoming.clear();}
    bool isOpen() const override {return active;}
    qint64 write(const QByteArray &raw) override {writes.append(raw);if(drop){active=false;emit failed("injected failure");return -1;}if(onWrite)onWrite(raw);else{incoming=raw;emit readyRead();}return shortWrite?raw.size()-1:raw.size();}
    QByteArray readAll() override {QByteArray r;r.swap(incoming);return r;}
    void feed(const QByteArray &raw){incoming+=raw;emit readyRead();}
};
class SerialToolTests : public QObject {
    Q_OBJECT
private slots:
    void strictBytesAndEndings() {
        using namespace SerialText;QByteArray bytes;QString error;
        for(const auto &pair:QVector<QPair<Format,QString>>{{Format::Hex,"00 7f FF"},{Format::Hex,"007fff"},{Format::Decimal,"0 127 255"},{Format::Binary,"0 01111111 11111111"},{Format::Octal,"0 177 377"}}){QVERIFY(encode(pair.second,pair.first,Encoding::Utf8,Ending::None,bytes,error));QCOMPARE(bytes,QByteArray::fromHex("007fff"));QVERIFY(encode(display(bytes,pair.first,Encoding::Utf8),pair.first,Encoding::Utf8,Ending::None,bytes,error));QCOMPARE(bytes,QByteArray::fromHex("007fff"));}
        QVERIFY(encode(QStringLiteral("你好"),Format::Text,Encoding::Utf8,Ending::CRLF,bytes,error));QCOMPARE(bytes,QStringLiteral("你好\r\n").toUtf8());
        QVERIFY(encode({},Format::Text,Encoding::Utf8,Ending::LF,bytes,error));QCOMPARE(bytes,QByteArray("\n"));
        QVERIFY(encode(QString(QChar(255)),Format::Text,Encoding::Latin1,Ending::CR,bytes,error));QCOMPARE(bytes,QByteArray::fromHex("ff0d"));
    }
    void malformedAndOversizeNeverReturnPartialBytes() {
        using namespace SerialText;QByteArray bytes;QString error;
        for(const auto &pair:QVector<QPair<Format,QString>>{{Format::Hex,"1"},{Format::Hex,"01 0x02"},{Format::Hex,"01 2"},{Format::Decimal,"1 256"},{Format::Decimal,"1 -1"},{Format::Decimal,"9999999999999"},{Format::Binary,"1 2"},{Format::Octal,"1 8"},{Format::Hex,"０１"},{Format::Hex,"01,02"}}){bytes="old";QVERIFY(!encode(pair.second,pair.first,Encoding::Utf8,Ending::None,bytes,error));QVERIFY(bytes.isEmpty());QVERIFY(!error.isEmpty());}
        QVERIFY(!encode(QStringLiteral("中文"),Format::Text,Encoding::Latin1,Ending::None,bytes,error));QVERIFY(bytes.isEmpty());
        QVERIFY(!encode({},Format::Text,Encoding::Utf8,Ending::None,bytes,error));
        QVERIFY(encode(QString(4096,'a'),Format::Text,Encoding::Utf8,Ending::None,bytes,error));QCOMPARE(bytes.size(),4096);
        QVERIFY(!encode(QString(4096,'a'),Format::Text,Encoding::Utf8,Ending::LF,bytes,error));QVERIFY(bytes.isEmpty());
        QVERIFY(!encode(QString(16385,'0'),Format::Hex,Encoding::Utf8,Ending::None,bytes,error));
    }
    void connectionPurposeIsImmutableAndExclusive() {
        QTemporaryDir dir;ManualEndpoint endpoint;MonitorLink link(endpoint,dir.path());PlanController controller(link);QString error;
        QVERIFY(link.open("MEMORY:platform",115200,error));QCOMPARE(link.sendRaw("hello"),-1);QVERIFY(!link.setPurpose(MonitorLink::Purpose::SerialTool));QVERIFY(endpoint.writes.isEmpty());link.close();
        QVERIFY(link.setPurpose(MonitorLink::Purpose::SerialTool));QVERIFY(link.open("MEMORY:manual",115200,error));
        QSignalSpy typed(&link,&MonitorLink::observedFrame);link.query(4);link.query(5);link.query(6);link.setCapture(true);QVERIFY(!controller.openSession());QVERIFY(!controller.refreshStatus());QVERIFY(!link.isCapturing());QVERIFY(!link.isWaiting());QVERIFY(endpoint.writes.isEmpty());
        QSignalSpy bytes(&link,&MonitorLink::rawReceived);const auto raw=Wire::query(4,123,456);QCOMPARE(link.sendRaw(raw),raw.size());QTRY_COMPARE(bytes.size(),1);QCOMPARE(typed.size(),0);QCOMPARE(endpoint.writes.size(),1);
        link.close();QVERIFY(link.setPurpose(MonitorLink::Purpose::Platform));QVERIFY(link.open("MEMORY:new",115200,error));QVERIFY(!controller.openSession());QCOMPARE(endpoint.writes.size(),1); // No remembered MCU capabilities or auto retry.
    }
    void queueOrderingBoundsAndNoRetry() {
        QTemporaryDir dir;ManualEndpoint endpoint;MonitorLink link(endpoint,dir.path());QString error;link.setPurpose(MonitorLink::Purpose::SerialTool);QVERIFY(link.open("MEMORY:order",115200,error));
        QSignalSpy received(&link,&MonitorLink::rawReceived);QCOMPARE(link.sendRaw("abc"),3);QTRY_COMPARE(received.size(),1);
        QFile record(link.journalPath());QVERIFY(record.open(QIODevice::ReadOnly));const auto raw=record.readAll();record.close();QVERIFY(raw.contains("native-serial-v1"));QVERIFY(raw.indexOf("tx_queued")<raw.indexOf("rx_chunk"));QVERIFY(!raw.contains("rx_frame"));
        QCOMPARE(link.sendRaw({}),-1);QCOMPARE(link.sendRaw(QByteArray(4097,'x')),-1);QCOMPARE(endpoint.writes.size(),1);
        endpoint.shortWrite=true;QCOMPARE(link.sendRaw("xyz"),-1);QVERIFY(!link.isOpen());QTest::qWait(60);QCOMPARE(endpoint.writes.size(),2);
        endpoint.shortWrite=false;endpoint.drop=true;QVERIFY(link.open("MEMORY:drop",115200,error));QCOMPARE(link.sendRaw("abc"),-1);QVERIFY(!link.isOpen());QTest::qWait(60);QCOMPARE(endpoint.opens,2);QCOMPARE(endpoint.writes.size(),3);
    }
    void panelIsReadOnlyInPlatformAndClearPreservesJournal() {
        QTemporaryDir dir;ManualEndpoint endpoint;MonitorLink link(endpoint,dir.path());SerialPanel panel(link);QString error;
        auto *input=panel.findChild<QPlainTextEdit*>("serialInput");auto *send=panel.findChild<QPushButton*>("serialSend");auto *rx=panel.findChild<QPlainTextEdit*>("serialReceived");QVERIFY(input&&send&&rx);input->setPlainText("01 FF");QVERIFY(!send->isEnabled());
        QVERIFY(link.open("MEMORY:view",115200,error));QVERIFY(!send->isEnabled());link.close();link.setPurpose(MonitorLink::Purpose::SerialTool);panel.refreshConnection();QVERIFY(link.open("MEMORY:send",115200,error));QVERIFY(send->isEnabled());send->click();QTRY_VERIFY(rx->toPlainText().contains("01 FF"));QCOMPARE(endpoint.writes.size(),1);
        input->setPlainText("01 XX");QVERIFY(!send->isEnabled());
        auto *clear=static_cast<QPushButton*>(nullptr);for(auto *b:panel.findChildren<QPushButton*>())if(b->text()==QStringLiteral("清空显示"))clear=b;QVERIFY(clear);
        QFile file(link.journalPath());QVERIFY(file.open(QIODevice::ReadOnly));const auto before=file.readAll();file.close();clear->click();QVERIFY(rx->toPlainText().isEmpty());QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),before);file.close();
        endpoint.feed(QByteArray(8193,'x'));QVERIFY(!link.isOpen());QVERIFY(!send->isEnabled());
    }
    void applicationConnectOnlyQueriesAndMaintenanceDoesNotQuery() {
        QTemporaryDir dir;ManualEndpoint endpoint;MainWindow window(endpoint,dir.path());
        QFile vectors(QStringLiteral(NATIVE_VECTOR_FILE));QVERIFY(vectors.open(QIODevice::ReadOnly));const auto responses=QJsonDocument::fromJson(vectors.readAll()).object()["responses"].toArray();
        endpoint.onWrite=[&](const QByteArray &request){QByteArray response;for(const auto &item:responses)if(item.toObject()["type"].toInt()==quint8(request[3])){response=QByteArray::fromHex(item.toObject()["hex"].toString().toLatin1());break;}if(response.isEmpty())return;response.replace(6,8,request.mid(6,8));qToLittleEndian(Wire::crc16(response.first(response.size()-2)),response.data()+response.size()-2);endpoint.feed(response);};
        auto *ports=window.findChild<QComboBox*>("serialPorts");auto *button=window.findChild<QPushButton*>("connectPort");auto *purpose=window.findChild<QComboBox*>("connectionPurpose");QVERIFY(ports&&button&&purpose);ports->clear();ports->addItem("Memory","MEMORY:application");button->click();
        QTRY_VERIFY_WITH_TIMEOUT(endpoint.writes.size()>=3,1400);QVERIFY(!purpose->isEnabled());for(const auto &raw:endpoint.writes)QVERIFY(quint8(raw[3])==4||quint8(raw[3])==6);
        button->click();QVERIFY(!endpoint.active);purpose->setCurrentIndex(1);const auto count=endpoint.writes.size();button->click();QVERIFY(endpoint.active);QTest::qWait(650);QCOMPARE(endpoint.writes.size(),count);QVERIFY(!window.findChild<QPushButton*>("serialSend")->isEnabled());button->click();
        QAction *guide=nullptr;for(auto *action:window.findChildren<QAction*>())if(action->text()==QStringLiteral("使用说明书"))guide=action;QVERIFY(guide);guide->trigger();auto *browser=window.findChild<QTextBrowser*>();QVERIFY(browser);QVERIFY(browser->toPlainText().contains(QStringLiteral("串口维护工具")));QVERIFY(browser->toPlainText().contains("1.0.0"));QVERIFY(!endpoint.active);
    }
    void shippedPlansMatchActualCapabilityAndStayDrafts() {
        QFile file(QStringLiteral(NATIVE_PROFILE));QVERIFY(file.open(QIODevice::ReadOnly));const auto profile=file.readAll();
        for(const auto &name:{"readonly_unarmed.json","single_deadline.json"}){QFile plan(QStringLiteral(NATIVE_RELEASE_PLANS)+"/"+name);QVERIFY(plan.open(QIODevice::ReadOnly));QString error;const auto raw=plan.readAll();const auto snapshot=PlanSnapshot::compile(raw,profile,error);QVERIFY2(snapshot,qPrintable(error));QCOMPARE(snapshot->actions().size(),3);QCOMPARE(snapshot->reservedRecords(),6);QCOMPARE(snapshot->hasMotion(),QString::fromLatin1(name).startsWith("single"));QCOMPARE(QJsonDocument::fromJson(raw).object()["status"].toString(),QString("draft_not_executable"));}
    }
};
QTEST_MAIN(SerialToolTests)
#include "serial_tool_tests.moc"
