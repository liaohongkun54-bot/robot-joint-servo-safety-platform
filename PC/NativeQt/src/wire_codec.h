#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace Wire {
struct Frame { quint8 type = 0; QByteArray raw; QJsonObject fields; };
quint16 crc16(const QByteArray &bytes);
QByteArray query(quint8 type, quint32 request, quint32 nonce);
bool decode(const QByteArray &raw, Frame &frame, QString &error);
bool request(const QByteArray &raw, Frame &frame, QString &error);
class Stream {
public:
    QVector<Frame> feed(const QByteArray &data);
    void reset();
    quint64 invalid = 0, discarded = 0;
    QByteArray pending;
};
struct Sample {
    quint32 request = 0, tick = 0, lastRx = 0, count = 0;
    int encoder = 0, speed = 0, current = 0, temperature = 0;
    bool fresh = false;
    static Sample fromFrame(const Frame &frame);
};
QString kind(quint8 type);
}
