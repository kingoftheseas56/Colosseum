#include "server1/archive/ArchiveSources.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
namespace server1::archive {
namespace {
bool truthy(const QJsonValue &v) { return !v.isNull()&&!v.isUndefined()&&(!v.isBool()||v.toBool())&&(!v.isDouble()||v.toDouble()!=0)&&(!v.isString()||!v.toString().isEmpty()); }
QString familyName(SourceFamily f) {switch(f){case SourceFamily::Rar:return "rar";case SourceFamily::Zip:return "zip";case SourceFamily::SevenZip:return "7zip";case SourceFamily::Tar:return "tar";case SourceFamily::Gzip:return "tgz";}return {};}
QString sourceUrl(const QJsonValue &v){return v.isObject()?v.toObject()["url"].toString():v.toString();}
}
SourceDescriptor describeSource(SourceFamily f,const QJsonValue &value,const QList<qint64>&prior,bool last){
 SourceDescriptor d;d.url=sourceUrl(value); if(d.url.isEmpty()){d.error="Invalid source URL";return d;}
 if(value.isObject()&&truthy(value.toObject()["bytes"])) {bool ok=false;auto b=value.toObject()["bytes"];qint64 n=b.isString()?b.toString().toLongLong(&ok):b.toInteger(-1);if(!b.isString())ok=n>=0;if(ok)d.length=n;else d.error="Invalid source length";}
 QString tail=d.url.section('/',-1);if(tail.contains('.')){for(int p=0;(p=tail.indexOf('%',p))>=0;p+=3)if(p+2>=tail.size()||!QRegularExpression("^[0-9a-fA-F]{2}$").match(tail.mid(p+1,2)).hasMatch()){d.error="URI malformed";return d;}d.name=QUrl::fromPercentEncoding(tail.toUtf8()).section('?',0,0);}else{QString ext=f==SourceFamily::SevenZip?"7z":f==SourceFamily::Gzip?"gz":familyName(f);d.name="archive."+ext;}
 if(!d.length&&prior.size()>1&&prior[0]==prior[1]){bool infer=false;switch(f){case SourceFamily::Rar:break;case SourceFamily::SevenZip:infer=!last;break;case SourceFamily::Zip:infer=!d.name.endsWith(".zip",Qt::CaseInsensitive);break;case SourceFamily::Tar:infer=!d.name.endsWith(".tar",Qt::CaseInsensitive);break;case SourceFamily::Gzip:infer=!QRegularExpression("\\.t?gz$",QRegularExpression::CaseInsensitiveOption).match(d.name).hasMatch();break;}if(infer)d.length=prior[0];}return d;
}
QByteArray sourceRangeHeader(qint64 length,const QJsonObject &range){if(range.isEmpty())return {};auto num=[](const QJsonValue&v){return v.isString()?v.toString().toLongLong():v.toInteger();};qint64 start=num(range["start"]),end=num(range["end"]);return "bytes="+QByteArray::number(start)+"-"+((end==0||end>length-1)?QByteArray{}:QByteArray::number(end));}
QString decompressUriQuery(QString input,QString *error){
 if(error)error->clear();if(input.isEmpty())return {};input.replace(' ','+');const QString alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-$";int index=0,position=0,value=0;bool exhausted=false;
 auto bits=[&](int n){quint32 out=0;for(int k=0;k<n;++k){if(!position){if(index>=input.size()){exhausted=true;return quint32(0);}value=alphabet.indexOf(input[index++]);if(value<0)value=0;position=32;}if(value&position)out|=quint32(1)<<k;position>>=1;}return out;};
 auto fail=[&](){if(error)*error="Invalid compressed query";return QString{};};
 QVector<QString> dict(4);auto first=bits(2);if(first==2)return {};if(first>1||exhausted)return fail();QString w(QChar(ushort(bits(first?16:8))));dict[3]=w;QString result=w;int enlarge=4,width=3;
 while(!exhausted){quint32 code=bits(width);if(exhausted)return fail();if(code==0||code==1){auto c=bits(code?16:8);dict.append(QString(QChar(ushort(c))));code=dict.size()-1;--enlarge;}else if(code==2)return result;if(!enlarge){enlarge=1<<width;++width;}QString entry;if(code<quint32(dict.size()))entry=dict[int(code)];else if(code==quint32(dict.size()))entry=w+w[0];else return fail();if(entry.isEmpty())return fail();result+=entry;if(result.size()>16*1024*1024||width>=24)return fail();dict.append(w+entry[0]);w=entry;if(!--enlarge){enlarge=1<<width;++width;}}
 return fail();
}
ArchiveKeys::ArchiveKeys(SourceFamily f):family_(f){}
KeyResult ArchiveKeys::create(const QString &method,const QJsonValue &body,const QString &requestedKey,const QString &lz){
 KeyResult r;QJsonArray urls;QJsonObject data;QString key=requestedKey;
 if(method=="POST"){if(!body.isArray()){r.status=500;r.error="Cannot parse JSON data, err 1";return r;}urls=body.toArray();}
 else {if(lz.isEmpty()){r.status=500;r.error="Cannot parse JSON data, err 2";return r;}data=QJsonDocument::fromJson(decompressUriQuery(lz).toUtf8()).object();urls=data["urls"].toArray();if(urls.isEmpty()){r.status=500;r.error="Cannot parse JSON data, err 3";return r;}key=QString::fromLatin1(QCryptographicHash::hash(lz.toUtf8(),QCryptographicHash::Sha256).toHex());}
 if(key.isEmpty())do{key=QUuid::createUuid().toString(QUuid::Id128).left(12);}while(keys_.contains(key));
 if(!keys_.contains(key)){QList<QJsonValue> values;for(auto v:urls){if(v.isArray()){auto a=v.toArray();if(a.isEmpty()||!truthy(a[0]))continue;QJsonObject o{{"url",a[0]}};if(a.size()>1&&truthy(a[1]))o["bytes"]=a[1];v=o;}if(truthy(v))values.append(v);}QString pattern;int fallback=0;switch(family_){case SourceFamily::Zip:pattern="\\.Z(\\d{1,5})$";fallback=99999;break;case SourceFamily::SevenZip:pattern="\\.7Z\\.(\\d{1,5})$";break;case SourceFamily::Tar:pattern="\\.TAR\\.(\\d{1,5})$";break;case SourceFamily::Gzip:pattern="\\.(\\d{1,5})$";break;case SourceFamily::Rar:break;}if(!pattern.isEmpty()){QRegularExpression re(pattern,QRegularExpression::CaseInsensitiveOption);auto order=[&](const QJsonValue&v){auto m=re.match(sourceUrl(v).section('?',0,0));return m.hasMatch()?m.captured(1).toInt():fallback;};std::stable_sort(values.begin(),values.end(),[&](const auto&a,const auto&b){return order(a)<order(b);});}QJsonArray normalized;for(auto v:values)normalized.append(v);keys_[key]=normalized;}
 r.key=key;if(method!="POST"){r.status=302;QJsonObject opts;if(truthy(data["fileMustInclude"]))opts["fileMustInclude"]=data["fileMustInclude"];if(data["maxFiles"].toDouble()>0)opts["maxFiles"]=data["maxFiles"];if(truthy(data["fileIdx"])&&data["fileIdx"].toDouble()>-1)opts["fileIdx"]=data["fileIdx"];r.location="/"+familyName(family_)+"/stream?key="+key;if(!opts.isEmpty())r.location+="&o="+QString::fromLatin1(QUrl::toPercentEncoding([&](){QByteArray json="{";for(const QString name:{QString("fileMustInclude"),QString("maxFiles"),QString("fileIdx")})if(opts.contains(name)){if(json.size()>1)json+=",";auto encoded=QJsonDocument(QJsonArray{opts[name]}).toJson(QJsonDocument::Compact);json+="\""+name.toUtf8()+"\":"+encoded.mid(1,encoded.size()-2);}json+="}";return QString::fromUtf8(json);}(),"!~*'()"));}return r;
}
std::optional<QJsonArray> ArchiveKeys::get(const QString &key)const{auto i=keys_.constFind(key);if(i==keys_.cend())return {};return *i;}
}
