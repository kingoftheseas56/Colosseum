#include "server1/local/LocalFilename.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
namespace server1::local {
namespace {
QRegularExpression rx(const char *pattern,bool insensitive=false){return QRegularExpression(QString::fromLatin1(pattern),insensitive?QRegularExpression::CaseInsensitiveOption:QRegularExpression::NoPatternOption);}
bool numeric(const QString &s){if(s.trimmed().isEmpty())return true;bool ok=false;s.toDouble(&ok);if(!ok&&QRegularExpression("^[+-]?0[xX][0-9a-fA-F]+$").match(s).hasMatch())ok=true;return ok;}
QJsonValue prefixInteger(const QString &s){auto m=QRegularExpression("^[+-]?[0-9]+").match(s);return m.hasMatch()?QJsonValue(m.captured().toInt()):QJsonValue(QJsonValue::Null);}
QString simplify(QString s){s=s.toLower().trimmed();s.remove(rx("\\([^\\(]+\\)$"));s.replace('&',"and");s.replace(rx("[^0-9a-z ]+")," ");return s.split(' ',Qt::SkipEmptyParts).join(' ');}
}
QJsonObject parseLocalFilename(QString path,QJsonObject options){
    const QStringList keywords{"1080p","720p","480p","blurayrip","brrip","divx","dvdrip","hdrip","hdtv","tvrip","xvid","camrip"};
    QString normalized=path;normalized.replace('\\','/');auto segments=normalized.split('/',Qt::SkipEmptyParts);std::reverse(segments.begin(),segments.end());segments=segments.mid(0,3);if(segments.isEmpty())return {{"type","other"},{"tag",QJsonArray{}}};
    QJsonObject meta;auto saneSeason=[&]{return meta.value("season").isDouble();};auto saneEpisode=[&]{return !meta.value("episode").toArray().isEmpty();};
    auto firstSplit=segments[0].split(rx("\\.| |_"));
    for(int i=0;i<std::min(2,int(segments.size()));++i){auto matches=rx("\\b[0-9]{4}\\b").globalMatch(segments[i]);while(matches.hasNext()){auto y=matches.next().captured().toInt();if(y>=1900&&y<=2030)meta["year"]=y;}}
    for(int i=0;i<std::min(2,int(segments.size()));++i){auto match=rx("([0-9]{4})(\\.|-| )([0-9]{2})(\\.|-| )([0-9]{2})").match(segments[i]);auto y=match.captured(1).toInt();if(match.hasMatch()&&y>=1900&&y<=2030)meta["aired"]=QString::number(y)+"-"+match.captured(3)+"-"+match.captured(5);}
    for(int i=0;i<std::min(2,int(segments.size()));++i){auto seg=segments[i];for(auto x:seg.split(rx("\\.| |_"))){auto season=rx("S([0-9]{1,2})",true).match(x);if(season.hasMatch())meta["season"]=season.captured(1).toInt();auto matches=rx("(?<=\\W|[0-9])E([0-9]{2})",true).globalMatch(x);QJsonArray episodes;while(matches.hasNext())episodes.append(matches.next().captured(1).toInt());if(!episodes.isEmpty())meta["episode"]=episodes;auto stamp=rx("([0-9][0-9]?)x([0-9][0-9]?)",true).match(x);if(stamp.hasMatch()){meta["season"]=stamp.captured(1).toInt();meta["episode"]=QJsonArray{stamp.captured(2).toInt()};}}
        auto full=rx("^([a-zA-Z0-9,\\-?!'& ]*) S([0-9]{1,2})E([0-9]{2})",true).match(seg.replace(rx("\\.| |;|_")," "));if(meta.value("name").toString().isEmpty()&&meta.value("season").toInt()&&saneEpisode()&&full.hasMatch()&&!full.captured(1).isEmpty())meta["name"]=full.captured(1);
    }
    auto dot=rx("[^0-9a-zA-Z]([0-9][0-9]?)\\.([0-9][0-9]?)[^0-9a-zA-Z]",true).match(segments[0]);if(!(saneSeason()&&saneEpisode())&&dot.hasMatch()&&!meta.contains("year")){meta["season"]=dot.captured(1).toInt();meta["episode"]=QJsonArray{dot.captured(2).toInt()};}
    if(!(saneSeason()&&saneEpisode())&&!options["strict"].toBool()){
        std::reverse(firstSplit.begin(),firstSplit.end());QString stamp;for(auto x:firstSplit){if(rx("[0-9]{3}[0-9]?e").match(x).hasMatch())x.chop(1);if(rx("s[0-9]{3}[0-9]?").match(x).hasMatch())x=x.mid(1);if(numeric(x)&&(x.size()==3||(!meta.contains("year")&&x.size()==4))){stamp=x;break;}}
        if(!stamp.isEmpty()&&(!meta.contains("year")||firstSplit.indexOf(stamp)<firstSplit.indexOf(QString::number(meta.value("year").toInt())))){meta["episode"]=QJsonArray{prefixInteger(stamp.right(2))};meta["season"]=prefixInteger(stamp.left(stamp.size()-2));}
    }
    auto joined=segments.join('/');
    if(!saneSeason()){auto season=rx("season(\\.| )?([0-9]{1,2})",true).match(joined);if(season.hasMatch())meta["season"]=season.captured(2).toInt();auto both=rx("Season ([0-9]{1,2}) - ([0-9]{1,2})",true).match(joined);if(both.hasMatch()){meta["season"]=both.captured(1).toInt();meta["episode"]=QJsonArray{both.captured(2).toInt()};}}
    if(!saneEpisode()){auto episode=rx("ep(isode)?(\\.| )?([0-9]+)",true).match(joined);if(episode.hasMatch())meta["episode"]=QJsonArray{episode.captured(3).toInt()};}
    auto disk=rx("[ _.-]*(?:cd|dvd|p(?:ar)?t|dis[ck]|d)[ _.-]*([0-9])[^0-9]").match(segments[0]);if(disk.hasMatch())meta["diskNumber"]=disk.captured(1).toInt();
    auto ordered=segments;if(!options["fromInside"].toBool())std::reverse(ordered.begin(),ordered.end());bool sample=false;
    for(auto seg:ordered){if(seg==segments[0]){auto parts=seg.split('.');parts.removeLast();seg=parts.join('.');auto prefix=rx("^\\[(.*?)\\]").match(seg);if(prefix.hasMatch())seg=seg.mid(prefix.capturedLength());}auto bracket=seg.indexOf('[');if(bracket>=0)seg=seg.left(bracket);auto parts=seg.split(rx("\\.| |-|;|_"));sample=sample||rx("^sample|^etrg",true).match(seg).hasMatch();if(!meta.value("name").toString().isEmpty())continue;
        QStringList name;for(int i=0;i<parts.size();++i){auto word=parts[i];auto lower=word.toLower();bool valid=rx("^[a-zA-Z,?!'&]*$").match(word).hasMatch()||(numeric(word)&&(word.size()<=2||i==0));if(!valid||keywords.contains(lower)||(QStringList{"ep","episode","season"}.contains(lower)&&i+1<parts.size()&&numeric(parts[i+1])))break;name.append(word);}
        if(name.size()!=1||!numeric(name[0])){QStringList out;for(auto word:name)if(!word.isEmpty())out.append(word.left(1).toUpper()+word.mid(1).toLower());meta["name"]=out.join(' ');}
    }
    sample=sample||(segments.size()>1&&segments[1].toLower()=="sample");bool movie=meta.contains("year");if(!options["strict"].toBool()){movie=movie||meta.contains("diskNumber");for(auto keyword:keywords)movie=movie||joined.contains(keyword,Qt::CaseInsensitive);}
    QString type="other";auto name=meta.value("name").toString();if(!name.isEmpty()&&saneSeason()&&saneEpisode())type="series";else if(!name.isEmpty()&&movie)type="movie";else if(!name.isEmpty()&&saneSeason())type="extras";
    auto size=options["fileLength"].toDouble();if(size&&size<1024*(type=="movie"?80:50)*1024&&(type=="movie"||type=="series")&&!sample)type="other";
    if(type!="series"||meta.contains("aired")){meta.remove("episode");meta.remove("season");}
    if(type=="series"&&meta.contains("year")){if(!meta.contains("aired"))meta["aired"]=meta.value("year");meta.remove("year");}
    meta["type"]=type+(sample?"-sample":"");if(meta.contains("name"))meta["name"]=simplify(name);auto hints=options["hints"].toObject();if(!hints["imdb_id"].toString().isEmpty())meta["imdb_id"]=hints["imdb_id"];
    QJsonArray tags;if(path.contains("1080p",Qt::CaseInsensitive)){tags.append("hd");tags.append("1080p");}for(auto tag:{"720p","480p"})if(path.contains(tag,Qt::CaseInsensitive))tags.append(tag);if(sample)tags.append("sample");meta["tag"]=tags;return meta;
}
}
