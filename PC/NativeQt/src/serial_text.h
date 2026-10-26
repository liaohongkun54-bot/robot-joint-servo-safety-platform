#pragma once
#include <QByteArray>
#include <QString>

namespace SerialText {
enum class Format {Text,Hex,Decimal,Binary,Octal};
enum class Encoding {Utf8,Latin1};
enum class Ending {None,LF,CR,CRLF};
// Exactly one bounded message; failures never return partially encoded bytes.
bool encode(const QString &input,Format format,Encoding encoding,Ending ending,QByteArray &bytes,QString &error);
QString display(const QByteArray &bytes,Format format,Encoding encoding);
}
