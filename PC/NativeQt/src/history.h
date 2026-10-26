#pragma once
#include "wire_codec.h"
#include <QHash>

class History {
public:
    bool load(const QString &path, QString &error);
    bool addRequest(const QByteArray &raw);
    bool accept(const Wire::Frame &frame);
    QVector<Wire::Sample> samples;
    QVector<Wire::Frame> frames;
    QString source, sha256;
    bool closed = false;
    quint64 invalid = 0, discarded = 0, unmatched = 0;
    QJsonObject summary() const;
private:
    QHash<QString,Wire::Frame> requests;
    QHash<QString,bool> seen;
};
