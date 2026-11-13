#pragma once
#include <QFile>
#include <QJsonObject>
#include <QStringList>
namespace server1::local {
// Native M1079 append-only records. Explicit paths only. Metadata cache uses the
// same class with validateFiles=false; source names that file <database>Meta.
class LocalAddonLog final {
public:
    explicit LocalAddonLog(bool validateFiles=true):validateFiles_(validateFiles){}
    bool open(QString path,QString *error=nullptr);
    bool append(QString key,QJsonObject entry,QString *error=nullptr);
    void close();
    QStringList keys() const{return order_;}
    QJsonObject entry(const QString &key) const{return records_.value(key).toObject();}
private:
    bool writeRecord(const QString &key,const QJsonObject &entry,QString *error);
    bool validateFiles_;QFile file_;QJsonObject records_;QStringList order_;
};
}
