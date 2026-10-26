#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace StrictJson {
// Duplicate keys (including escaped aliases) and nesting beyond 32 are rejected.
bool object(const QByteArray &raw,QJsonObject &out,QString &error);
}
