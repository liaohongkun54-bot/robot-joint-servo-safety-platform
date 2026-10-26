#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Offline only. Replays raw request/reply evidence; never owns a serial port.
// A complete local trace is not cryptographic device authentication or proof of
// physical motor motion/standstill. Old untimed v1 logs remain unsupported.
class NativeAudit {
public:
    bool load(const QString &path,QString &error);
    bool audit(const QByteArray &raw,QString &error);
    QJsonObject report() const {return result;}
private:
    QJsonObject result;
};
