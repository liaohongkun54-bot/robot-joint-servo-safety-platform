#include "serial_text.h"
#include <QStringList>

namespace SerialText {
bool encode(const QString &input,Format format,Encoding encoding,Ending ending,QByteArray &bytes,QString &error) {
    bytes.clear();error.clear();QByteArray candidate;
    if(input.size()>16384){error=QStringLiteral("输入过长；单次发送最多4096字节（含结束符）");return false;}
    if(format==Format::Text) {
        if(encoding==Encoding::Latin1)for(const auto c:input)if(c.unicode()>255){error=QStringLiteral("输入包含Latin-1无法表示的字符，请选择UTF-8");return false;}
        if(encoding==Encoding::Utf8&&!input.isValidUtf16()){error=QStringLiteral("输入包含无效Unicode字符");return false;}
        candidate=encoding==Encoding::Utf8?input.toUtf8():input.toLatin1();
    }else {
        const int base=format==Format::Hex?16:format==Format::Decimal?10:format==Format::Binary?2:8;
        QString token;
        auto consume=[&]() {
            if(token.isEmpty())return true;
            if(format==Format::Hex){
                if(token.size()%2){error=QStringLiteral("HEX每个字节需要两位，例如01 0A FF；不使用0x前缀");return false;}
                for(qsizetype i=0;i<token.size();i+=2)candidate.append(char(token.mid(i,2).toUInt(nullptr,16)));
            }else {
                bool ok=false;const auto value=token.toUInt(&ok,base);
                if(!ok||value>255){error=QStringLiteral("每个数必须在0到255之间，字节之间用空白分隔");return false;}
                candidate.append(char(value));
            }
            token.clear();return true;
        };
        for(const auto c:input) {
            if(c.isSpace()){if(!consume())return false;continue;}
            const auto u=c.unicode();const int digit=u>='0'&&u<='9'?u-'0':u>='a'&&u<='f'?u-'a'+10:u>='A'&&u<='F'?u-'A'+10:-1;
            if(digit<0||digit>=base){error=QStringLiteral("当前进制包含无效字符；不接受符号、前缀或逗号");return false;}
            token+=c;
        }
        if(!consume())return false;
    }
    if(ending==Ending::LF)candidate+='\n';else if(ending==Ending::CR)candidate+='\r';else if(ending==Ending::CRLF)candidate+="\r\n";
    if(candidate.isEmpty()){error=QStringLiteral("请输入内容或选择结束符");return false;}
    if(candidate.size()>4096){error=QStringLiteral("单次发送最多4096字节（含结束符）");return false;}
    bytes=candidate;return true;
}
QString display(const QByteArray &bytes,Format format,Encoding encoding) {
    if(format==Format::Text)return encoding==Encoding::Utf8?QString::fromUtf8(bytes):QString::fromLatin1(bytes);
    if(format==Format::Hex)return QString::fromLatin1(bytes.toHex(' ')).toUpper();
    const int base=format==Format::Decimal?10:format==Format::Binary?2:8;QStringList parts;parts.reserve(bytes.size());
    for(const auto c:bytes)parts.push_back(QString::number(quint8(c),base).rightJustified(base==2?8:base==8?3:1,'0'));
    return parts.join(' ');
}
}
