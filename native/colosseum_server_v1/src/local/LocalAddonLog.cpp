#include "server1/local/LocalAddonLog.h"
#include <QFileInfo>
#include <QJsonDocument>
namespace server1::local {
namespace {constexpr qint64 maxLine=16*1024*1024;}
void LocalAddonLog::close(){file_.close();}
bool LocalAddonLog::open(QString path,QString *error){close();records_={};order_.clear();file_.setFileName(path);if(!file_.open(QIODevice::ReadWrite|QIODevice::Append)){if(error)*error=file_.errorString();return false;}bool truncate=false;file_.seek(0);while(!file_.atEnd()){auto line=file_.readLine(maxLine+1);if(line.size()>maxLine){if(error)*error="append log record exceeds 16 MiB";close();return false;}auto record=QJsonDocument::fromJson(line).object();auto key=record["id"].toString();if(record["v"]!="1.10.0"||!record["id"].isString()||!record["entry"].isObject()||(validateFiles_&&!QFileInfo(key).isReadable())){truncate=true;continue;}if(!records_.contains(key))order_.append(key);records_[key]=record["entry"];}
    if(truncate){if(!file_.resize(0)){if(error)*error=file_.errorString();close();return false;}file_.seek(0);for(auto key:order_)if(!writeRecord(key,records_[key].toObject(),error)){close();return false;}}
    file_.seek(file_.size());return true;
}
bool LocalAddonLog::writeRecord(const QString &key,const QJsonObject &entry,QString *error){auto bytes=QJsonDocument(QJsonObject{{"id",key},{"entry",entry},{"v","1.10.0"}}).toJson(QJsonDocument::Compact)+'\n';if(bytes.size()>maxLine){if(error)*error="append log record exceeds 16 MiB";return false;}if(file_.write(bytes)!=bytes.size()||!file_.flush()){if(error)*error=file_.errorString();return false;}return true;}
bool LocalAddonLog::append(QString key,QJsonObject entry,QString *error){if(records_.contains(key))return true;records_[key]=entry;order_.append(key);if(!file_.isOpen()){if(error)*error="unable to persist, no fd";return false;}return writeRecord(key,entry,error);}
}
