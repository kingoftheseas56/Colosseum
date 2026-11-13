#include "HomeServerService.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
struct HeldTitle {
    QJsonObject root;
    QList<QJsonObject> episodes;
};
bool playable(const QJsonObject &item) {
    for(const auto &value:item.value("versions").toArray()) {
        const auto id=value.toObject().value("id").toString();
        if(!id.trimmed().isEmpty() && !id.contains("://")) return true;
    }
    return false;
}
bool exactEpisode(const QJsonObject &item) {
    if(item.value("kind")!="episode" || HomeServerCatalog::canonicalVideoId(item).isEmpty()) return false;
    const auto identity=item.value("identity").toObject();
    for(auto key:{"season","episode"}) {
        const auto value=identity.value(key);
        const auto number=value.toInteger(-1);
        if(!value.isDouble() || number<0 || number>2147483647 || value.toDouble()!=double(number)) return false;
    }
    return true;
}
QList<HeldTitle> heldTitles(const QJsonArray &index,const HomeServerProgress::IdentityAliases &aliases) {
    const auto inherited=HomeServerCatalog::inheritSeriesIdentity(index);
    QList<QJsonObject> items;
    QHash<QString,QJsonObject> series;
    for(const auto &value:inherited) {
        const auto item=HomeServerProgress::withAliases(value.toObject(),aliases);
        items.append(item);
        if(item.value("kind")=="series") series.insert(item.value("id").toString(),item);
    }
    QHash<QString,QList<QJsonObject>> episodes;
    for(const auto &item:items) {
        if(!playable(item) || !exactEpisode(item)) continue;
        const auto parent=item.value("parentId").toString();
        const auto root=series.value(parent);
        auto identity=item.value("identity").toObject();
        identity.remove("season");identity.remove("episode");
        if(!root.isEmpty() && HomeServerCatalog::identitiesMatch(root,{{"kind","series"},{"identity",identity}}))
            episodes[parent].append(item);
    }
    QList<HeldTitle> result;
    for(const auto &item:items) {
        const auto kind=item.value("kind").toString();
        if(HomeServerCatalog::canonicalVideoId(item).isEmpty()) continue;
        if(kind=="movie" && playable(item)) result.append({item,{}});
        else if(kind=="series" && !episodes.value(item.value("id").toString()).isEmpty())
            result.append({item,episodes.value(item.value("id").toString())});
    }
    return result;
}
QString readableTitle(const QJsonObject &item,const QString &fallback) {
    const auto title=item.value("title").toString();
    return title.trimmed().isEmpty()?fallback:title;
}
void publicIds(const QJsonObject &identity,QVariantMap &out) {
    static const QRegularExpression validImdb("^tt[0-9]+$");
    const auto imdb=identity.value("imdbId").toString();
    if(validImdb.match(imdb).hasMatch()) out.insert("imdbId",imdb);
    for(auto key:{"tmdbId","tvdbId"}) {
        const auto value=identity.value(key);
        const auto id=value.toInteger(0);
        if(value.isDouble() && id>0 && value.toDouble()==double(id)) out.insert(key,id);
    }
}
bool readableLess(const QVariant &a,const QVariant &b,const QString &field) {
    const auto left=a.toMap(),right=b.toMap();
    const int comparison=QString::localeAwareCompare(left.value(field).toString().toCaseFolded(),right.value(field).toString().toCaseFolded());
    return comparison?comparison<0:left.value("id").toString()<right.value("id").toString();
}
QVariantList publicTitles(const QList<HeldTitle> &titles) {
    QVariantList out;
    QSet<QString> seen;
    for(const auto &held:titles) {
        const auto &item=held.root;
        const auto id=HomeServerCatalog::canonicalVideoId(item);
        if(seen.contains(id)) continue;
        seen.insert(id);
        const auto kind=item.value("kind").toString();
        const auto title=readableTitle(item,id);
        QVariantMap identity{{"world","Theatre"},{"identityId",id},{"type",kind},{"title",title},{"coverUrl",QString()}};
        publicIds(item.value("identity").toObject(),identity);
        out.append(QVariantMap{{"id",id},{"kind",kind},{"title",title},{"year",item.value("year").toInt()},{"coverUrl",QString()},{"identity",identity}});
    }
    std::sort(out.begin(),out.end(),[](const auto &a,const auto &b){return readableLess(a,b,"title");});
    return out;
}
HomeServerProgress::IdentityAliases aliasesFor(const std::shared_ptr<HomeServerProgressOwner> &progress) {
    return progress?HomeServerProgress::identityAliases(progress->entries()):HomeServerProgress::IdentityAliases{};
}
}

QVariantList HomeServerService::shelves() const {
    QVariantList rows;
    if(!available()) return rows;
    const auto aliases=aliasesFor(m_progress);
    for(const auto &value:m_store.connections()) {
        const auto connection=value.toObject();
        const auto id=connection.value("id").toString();
        if(!connection.value("enabled").toBool() || m_expired.contains(id) || m_store.token(id).isEmpty()) continue;
        const auto titles=publicTitles(heldTitles(m_index.value(id),aliases));
        rows.append(QVariantMap{{"id",id},{"name",connection.value("name").toString()},{"titleCount",int(titles.size())}});
    }
    std::sort(rows.begin(),rows.end(),[](const auto &a,const auto &b){return readableLess(a,b,"name");});
    return rows;
}
QVariantList HomeServerService::shelfTitles(const QString &id) const {
    if(!available()) return {};
    const auto connection=m_store.connection(id);
    if(connection.isEmpty() || !connection.value("enabled").toBool() || m_expired.contains(id) || m_store.token(id).isEmpty()) return {};
    return publicTitles(heldTitles(m_index.value(id),aliasesFor(m_progress)));
}
QVariantMap HomeServerService::titleMetadata(const QVariantMap &requested) const {
    if(!available()) return {};
    auto type=requested.value("type").toString();
    if(type=="tv") type="series";
    if(type!="movie" && type!="series") return {};
    const auto input=QJsonObject::fromVariantMap(requested);
    QJsonObject query{{"type",type}};
    for(auto key:{"id","imdbId","tmdbId","tvdbId","season","episode"}) if(input.contains(key)) query.insert(key,input.value(key));
    auto synthetic=query;synthetic.insert("kind",type);
    const auto queriedId=HomeServerCatalog::canonicalVideoId(synthetic);
    if(queriedId.isEmpty()) return {};
    const auto aliases=aliasesFor(m_progress);
    QList<HeldTitle> matches;
    for(const auto &value:m_store.connections()) {
        const auto connection=value.toObject();
        const auto id=connection.value("id").toString();
        if(!connection.value("enabled").toBool() || m_expired.contains(id) || m_store.token(id).isEmpty()) continue;
        for(const auto &title:heldTitles(m_index.value(id),aliases))
            if(HomeServerCatalog::identitiesMatch(title.root,query)) matches.append(title);
    }
    if(matches.isEmpty()) return {};
    qsizetype representative=0;
    for(qsizetype index=0;index<matches.size();++index)
        if(HomeServerCatalog::canonicalVideoId(matches.at(index).root).startsWith("tt")) {representative=index;break;}
    auto root=matches.at(representative).root;
    // A queried IMDb is admitted only after the held item matched its known
    // identifiers. Keep known progress aliases and provider identities intact.
    if(queriedId.startsWith("tt") && !queriedId.contains(':')) {
        auto identity=root.value("identity").toObject();identity.insert("imdbId",queriedId);root.insert("identity",identity);
    }
    const auto canonical=HomeServerCatalog::canonicalVideoId(root);
    QVariantMap metadata{{"id",canonical},{"type",type},{"name",readableTitle(root,canonical)},{"year",root.value("year").toInt()},{"videos",QVariantList{}}};
    QVariantMap external;publicIds(root.value("identity").toObject(),external);
    if(external.contains("tmdbId")) {metadata.insert("tmdbId",external.value("tmdbId"));metadata.insert("moviedb_id",external.value("tmdbId"));}
    if(external.contains("tvdbId")) metadata.insert("tvdbId",external.value("tvdbId"));
    if(type=="series") {
        QVariantList videos;QSet<QString> seen;
        for(const auto &held:matches) {
            if(!HomeServerCatalog::identitiesMatch(root,held.root)) continue;
            for(const auto &episode:held.episodes) {
                const auto identity=episode.value("identity").toObject();
                const auto season=identity.value("season").toInt(),number=identity.value("episode").toInt();
                const auto id=canonical+':'+QString::number(season)+':'+QString::number(number);
                if(seen.contains(id)) continue;
                seen.insert(id);
                videos.append(QVariantMap{{"id",id},{"title",readableTitle(episode,id)},{"season",season},{"episode",number}});
            }
        }
        std::sort(videos.begin(),videos.end(),[](const QVariant &a,const QVariant &b) {
            const auto left=a.toMap(),right=b.toMap();
            if(left.value("season").toInt()!=right.value("season").toInt()) return left.value("season").toInt()<right.value("season").toInt();
            return left.value("episode").toInt()<right.value("episode").toInt();
        });
        metadata.insert("videos",videos);
    }
    return metadata;
}
