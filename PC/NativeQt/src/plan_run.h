#pragma once
#include "operator_codec.h"
#include <QJsonArray>
#include <QSet>
#include <optional>

// Immutable, bounded copy of the draft and capability bytes used by this run.
// Compilation confers no MCU permission and performs no I/O.
class PlanSnapshot {
public:
    struct Action {
        int stepIndex=0,attempt=1;
        QString stepId,testId,modeId;
        std::optional<int> command;
        quint32 duration=0;
        bool motion() const;
        Wire::Task task(quint32 id) const;
    };
    static std::optional<PlanSnapshot> compile(const QByteArray &draft,const QByteArray &profile,QString &error);
    const QVector<Action> &actions() const {return entries;}
    const QByteArray &draftBytes() const {return draft;}
    const QByteArray &profileBytes() const {return profile;}
    QString planId() const {return id;}
    QString profileId() const {return deviceId;}
    QString draftSha256() const;
    QString profileSha256() const;
    int reservedRecords() const {return 2*int(entries.size());}
    bool hasMotion() const;
    quint16 requiredFeatures() const;
private:
    PlanSnapshot()=default;
    QByteArray draft,profile;
    QString id,deviceId;
    QVector<Action> entries;
};

// One outstanding plan operation. The caller owns transmission, one-shot GRANT,
// heartbeats, physical STOP and the shared request-ID allocator. No retries.
class PlanRun {
public:
    enum class Kind : quint8 {Task=1,Stop=3,Status=4,Feedback=5,Info=6};
    struct Operation {Kind kind;int actionIndex=-1;bool action=false;};
    explicit PlanRun(PlanSnapshot snapshot,bool motionConfirmed=false);
    std::optional<Operation> next() const;
    QByteArray dispatch(quint32 id,quint32 nonce,qint64 now,QString &error);
    bool receive(const QByteArray &raw,qint64 now);
    void tick(qint64 now);
    void cancel();
    void uncertain(const QString &reason);
    bool terminal() const;
    bool waiting() const {return pending.has_value();}
    QString state() const {return status;}
    QString detail() const {return reason;}
    int actionIndex() const {return index;}
    QJsonArray outcomes() const {return results;}
    const PlanSnapshot &snapshot() const {return plan;}
    QJsonObject report() const;
private:
    struct Pending {Operation operation;quint32 id,nonce;qint64 sent,deadline;};
    PlanSnapshot plan;
    QString status="ready",reason,phase="info";
    int index=0,checkedIndex=-1;
    bool cancelled=false;
    qint64 lastTime=-1;
    std::optional<quint32> admittedTick;
    std::optional<Pending> pending;
    QSet<quint32> usedIds;
    QJsonArray results;
    bool clock(qint64 now);
    void finish(const QString &state,const QString &detail);
    void complete(const QString &verdict,const QString &detail,quint32 tick,std::optional<quint32> elapsed={});
};
