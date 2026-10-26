#pragma once
#include "operator_codec.h"
#include <functional>
#include <QObject>

// Pure client state machine. Caller owns the common link, request IDs and clock.
// No port ownership, task transmission, automatic OPEN or GRANT.
class OperatorClient : public QObject {
    Q_OBJECT
public:
    using Writer=std::function<qint64(const QByteArray &)>;
    using Allocate=std::function<quint32()>;
    using Nonce=std::function<quint32()>;
    using Record=std::function<bool(const QString &,const QJsonObject &)>;
    OperatorClient(Writer writer,Allocate allocate,Nonce nonce,Record record,QObject *parent=nullptr);
    void setCapabilities(quint16 features);
    void disconnected();
    bool open(qint64 now);
    bool grant(const Wire::Task &task,qint64 now);
    bool stop(qint64 now);
    bool clearFault(qint64 now);
    bool close(qint64 now);
    void tick(qint64 now);
    bool receive(const QByteArray &raw,qint64 now);
    bool grantedFor(const Wire::Task &task,qint64 now) const;
    bool taskSubmitted(const Wire::Task &task,qint64 now);
    bool active() const;
    QString state() const {return status;}
    quint32 sessionId() const {return session;}
    bool controlPending() const {return control.has_value();}
    bool heartbeatPending() const {return heartbeat.has_value();}
signals:
    void stateChanged(const QString &state);
private:
    struct Pending {Wire::OperatorRequest request;qint64 sent;};
    Writer writer;
    Allocate allocate;
    Nonce nonce;
    Record record;
    bool supported=false,inWrite=false;
    quint32 bootLow=0,bootHigh=0,session=0,sequence=0;
    qint64 observedSequence=-1,lastHeartbeat=0,sessionDeadline=0,grantDeadline=0,lastTime=-1;
    quint64 generation=0;
    std::optional<Pending> control,heartbeat;
    std::optional<Wire::Task> granted;
    QString status="disconnected";
    void setState(const QString &value);
    void fail(qint64 now,const QString &reason);
    bool validTime(qint64 now);
    bool send(Wire::Operation op,qint64 now,std::optional<Wire::Task> task={});
};
