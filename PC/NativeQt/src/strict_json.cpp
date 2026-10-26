#include "strict_json.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
namespace {
// QJsonDocument accepts duplicate object keys. Scan the already valid JSON first
// to reject duplicates, including escaped aliases, before it can lose that fact.
class UniqueKeys {
    const QByteArray &raw;qsizetype pos=0;
    void space() {while(pos<raw.size()&&QByteArray(" \r\n\t").contains(raw[pos]))++pos;}
    QByteArray string() {
        const auto start=pos++;
        while(pos<raw.size()) {const char c=raw[pos++];if(c=='\\')++pos;else if(c=='"')break;}
        return raw.mid(start,pos-start);
    }
    bool value(int depth) {
        if(depth>32)return false;
        space();if(pos>=raw.size())return false;
        if(raw[pos]=='"') {string();return true;}
        if(raw[pos]=='{'||raw[pos]=='[') {
            const bool object=raw[pos++]=='{';QSet<QString> keys;space();
            const char end=object?'}':']';if(pos<raw.size()&&raw[pos]==end){++pos;return true;}
            while(pos<raw.size()) {
                if(object) {
                    space();if(raw[pos]!='"')return false;
                    const auto encoded=string();const auto key=QJsonDocument::fromJson("["+encoded+"]").array().first().toString();
                    if(keys.contains(key))return false;
                    keys.insert(key);space();
                    if(pos>=raw.size()||raw[pos++]!=':')return false;
                }
                if(!value(depth+1))return false;
                space();if(pos>=raw.size())return false;
                if(raw[pos]==end){++pos;return true;}
                if(raw[pos++]!=',')return false;
            }
            return false;
        }
        while(pos<raw.size()&&!QByteArray(",]} \r\n\t").contains(raw[pos]))++pos;
        return true;
    }
public:
    explicit UniqueKeys(const QByteArray &r):raw(r) {}
    bool check() {if(raw.startsWith(QByteArray::fromHex("efbbbf")))pos=3;return value(0);}
};
}
bool StrictJson::object(const QByteArray &raw,QJsonObject &out,QString &error) {
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(raw,&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()) {error="invalid JSON object: "+parse.errorString();return false;}
    if(!UniqueKeys(raw).check()) {error="duplicate JSON key or nesting exceeds 32";return false;}
    out=doc.object();return true;
}
