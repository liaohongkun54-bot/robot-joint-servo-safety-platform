#pragma once
#include "wire_codec.h"
#include "serial_endpoint.h"
#include <QFile>
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>

class PlanController;
// Sole physical port owner. Control writes are private to the typed plan/session
// coordinator. Manual bytes require a distinct, immutable connection purpose.
class MonitorLink : public QObject {
    Q_OBJECT
public:
    enum class Purpose {Platform,SerialTool};
    explicit MonitorLink(QObject *parent=nullptr);
    MonitorLink(SerialEndpoint &endpoint,const QString &journalDirectory,QObject *parent=nullptr);
    ~MonitorLink() override;
    bool open(const QString &name, qint32 baud, QString &error);
    bool setPurpose(Purpose purpose);
    Purpose purpose() const {return connectionPurpose;}
    qint64 sendRaw(const QByteArray &raw);
    void close();
    void setCapture(bool on);
    void query(quint8 type);
    bool isOpen() const {return transport->isOpen();}
    bool isCapturing() const {return capturing;}
    bool isWaiting() const {return expectedType!=0;}
    QString journalPath() const {return journal.fileName();}
signals:
    void frameReceived(const Wire::Frame &frame);
    void message(const QString &text);
    void connectionChanged(bool connected);
    void captureChanged(bool capturing);
    void rawReceived(const QByteArray &raw);
    void rawTransmitted(const QByteArray &raw);
private:
    friend class PlanController;
    bool acquireControl();
    void releaseControl();
    quint32 allocateRequest();
    qint64 controlledWrite(const QByteArray &raw);
signals:
    void observedFrame(const Wire::Frame &frame);
private:
    std::unique_ptr<SerialEndpoint> owned;
    SerialEndpoint *transport;
    QString journalDirectory;
    QTimer poll,timeout;
    Wire::Stream parser;
    QFile journal;
    QElapsedTimer journalClock;
    quint32 requestId=0,nonce=0;
    quint8 expectedType=0;
    bool connected=false,capturing=false,closing=false,sending=false,deferredRead=false,exclusive=false,journalHealthy=true;
    quint64 generation=0;
    quint16 capabilities=0;
    int captured=0;
    Purpose connectionPurpose=Purpose::Platform;
    qint64 writeRecorded(const QByteArray &raw);
    void initialize();
    bool append(const QString &event,QJsonObject fields={});
    void receive();
};
