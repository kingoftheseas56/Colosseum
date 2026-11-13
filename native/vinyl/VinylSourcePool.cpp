#include "VinylSourcePool.h"
#include "VinylRecommendationIdentity.h"
#include "RecommendationEntityRef.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
QByteArray wire(const QVariant &value) {
    return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
}
bool boundedText(const QVariant &value, int maximum = 1024, bool empty = false) {
    return value.metaType().id() == QMetaType::QString && value.toString().size() <= maximum
        && (empty || !value.toString().trimmed().isEmpty())
        && !value.toString().contains(QRegularExpression("[\\x00-\\x1f\\x7f]"));
}
bool positiveRank(const QVariant &value, int maximum, int *out) {
    const auto type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::LongLong && type != QMetaType::Double) return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 1 || number > maximum || std::floor(number) != number) return false;
    *out = int(number); return true;
}
bool webUrl(const QString &text, const QStringList &hosts = {}) {
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http")
        && url.userInfo().isEmpty() && !url.host().isEmpty()
        && (hosts.isEmpty() || hosts.contains(url.host().toLower()));
}
QVariantMap normalized(QVariantMap item) {
    if (item.value("catalogueCard").toBool()) item = VinylSourcePool::catalogueCandidate(item);
    if (item.isEmpty()) return {};
    const auto identity = VinylRecommendationIdentity::fromCatalogue(item);
    if (!identity.value("ok").toBool()) return {};
    for (const auto &field : {"ref", "key", "destination"}) item[field] = identity.value(field);
    return item;
}
bool usableFeed(const QVariantMap &feed) {
    return !feed.value("key").toString().isEmpty() && !feed.value("items").toList().isEmpty()
        && QStringList{"ready", "partial"}.contains(feed.value("status").toString());
}
bool nonnegativeInteger(const QVariant &value, int *out) {
    const auto type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::LongLong && type != QMetaType::Double) return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > 2147483647.0 || std::floor(number) != number) return false;
    *out = int(number); return true;
}
}

bool VinylSourcePool::metadataOperation(const QString &operation) {
    return QStringList{"chartCandidates","similarArtists","recommendationDiscography"}.contains(operation);
}

QString VinylSourcePool::metadataSource(const QString &operation, const QString &value, const QVariantMap &options) {
    if (operation == "recommendationDiscography") return "musicbrainz";
    if (operation == "chartCandidates") return value.startsWith("apple:") ? "apple-music" : "lastfm";
    return options.value("source").toString();
}

bool VinylSourcePool::validMetadataRequest(const QString &operation, const QString &value, const QVariantMap &options) {
    if (!metadataOperation(operation) || wire(options).size() > 239 * 1024
        || options.value("seed").metaType().id() != QMetaType::QVariantMap) return false;
    QSet<QString> allowed{"seed","refresh","fetchedAt"};
    if (operation == "similarArtists") allowed.unite({"source","artistName"});
    if (operation == "recommendationDiscography") allowed.unite({"since","until"});
    for (auto it = options.cbegin(); it != options.cend(); ++it) if (!allowed.contains(it.key())) return false;
    if (options.contains("refresh") && options.value("refresh").metaType().id() != QMetaType::Bool) return false;
    if (options.contains("fetchedAt") && !boundedText(options.value("fetchedAt"),128,true)) return false;
    const auto seed = options.value("seed").toMap();
    if (operation == "chartCandidates") {
        return seed.isEmpty() && (QStringList{"lastfm:tracks","lastfm:artists"}.contains(value)
            || QRegularExpression("^apple:(us|gb|jp|kr|in|br|ng|de|fr|mx):(albums|songs)$").match(value).hasMatch());
    }
    const auto ref = vinyl::RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(seed));
    if (!ref.ok() || !ref.value->isCanonical() || ref.value->kind() != vinyl::CanonicalEntityKind::Artist
        || ref.value->mbidString() != value || seed.value("mbid").toString() != value) return false;
    if (operation == "similarArtists") {
        const auto source = options.value("source").toString();
        return source == "lastfm" ? boundedText(options.value("artistName"),300)
            : source == "listenbrainz" && !options.contains("artistName");
    }
    if (options.contains("since") != options.contains("until")) return false;
    if (options.contains("since")) {
        const auto since = options.value("since").toString(), until = options.value("until").toString();
        const QRegularExpression format("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
        const auto from = QDate::fromString(since,Qt::ISODate), to = QDate::fromString(until,Qt::ISODate);
        if (!boundedText(options.value("since"),10) || !boundedText(options.value("until"),10)
            || !format.match(since).hasMatch() || !format.match(until).hasMatch()
            || !from.isValid() || !to.isValid() || from > to) return false;
    }
    return true;
}

bool VinylSourcePool::normalizeMetadataEnvelope(const QString &operation, const QString &value,
    const QVariantMap &options, const QVariant &raw, QVariantMap *out) {
    if (!out || !validMetadataRequest(operation,value,options)
        || raw.metaType().id() != QMetaType::QVariantMap || wire(raw).size() > 256 * 1024) return false;
    auto data = raw.toMap();
    const QSet<QString> allowed{"version","seed","items","provenance","fetchedAt","complete","status",
        "artist","error","sourceStatus","retryAfterSeconds","_cached","_cacheError","_stale"};
    for (auto it = data.cbegin(); it != data.cend(); ++it) if (!allowed.contains(it.key())) return false;
    int version = 0;
    if (!positiveRank(data.value("version"),1,&version)
        || data.value("seed").metaType().id() != QMetaType::QVariantMap || data.value("seed") != options.value("seed")
        || data.value("items").metaType().id() != QMetaType::QVariantList
        || data.value("complete").metaType().id() != QMetaType::Bool
        || data.value("provenance").metaType().id() != QMetaType::QVariantMap
        || !boundedText(data.value("fetchedAt"),128)
        || !QDateTime::fromString(data.value("fetchedAt").toString(),Qt::ISODateWithMs).isValid()) return false;
    for (const auto &flag : {"_cached","_stale"}) if (data.contains(flag) && data.value(flag).metaType().id() != QMetaType::Bool) return false;
    for (const auto &text : {"error","_cacheError"}) if (data.contains(text) && !boundedText(data.value(text),2048,true)) return false;
    if (data.contains("retryAfterSeconds")) { int retry = 0; if (!positiveRank(data.value("retryAfterSeconds"),3600,&retry)) return false; }
    if (data.contains("sourceStatus") && !boundedText(data.value("sourceStatus"),128,true)) {
        int status = 0; if (!positiveRank(data.value("sourceStatus"),599,&status) || status < 100) return false;
    }
    const auto provenance = data.value("provenance").toMap();
    if (provenance.value("source") != metadataSource(operation,value,options)) return false;
    const auto status = data.value("status").toString(); const bool complete = data.value("complete").toBool();
    if (!QStringList{"ready","empty","partial","unavailable"}.contains(status)
        || ((status == "ready" || status == "empty") && !complete)
        || ((status == "partial" || status == "unavailable") && complete)) return false;
    const auto input = data.value("items").toList();
    if (operation == "recommendationDiscography") {
        if (!input.isEmpty()) return false;
        if (status == "unavailable" && !data.contains("artist")) { *out = data; return true; }
        if (data.value("artist").metaType().id() != QMetaType::QVariantMap) return false;
        const auto artist = data.value("artist").toMap();
        const QSet<QString> artistFields{"id","releaseGroups","releaseGroupsComplete","releaseGroupCount","releaseGroupScope","releaseGroupWindow"};
        for (auto it = artist.cbegin(); it != artist.cend(); ++it) if (!artistFields.contains(it.key())) return false;
        if (artist.value("id") != value || artist.value("releaseGroups").metaType().id() != QMetaType::QVariantList
            || artist.value("releaseGroupsComplete").metaType().id() != QMetaType::Bool
            || artist.value("releaseGroupsComplete").toBool() != complete) return false;
        if (options.contains("since")) {
            if (artist.value("releaseGroupScope") != "recent-releases"
                || artist.value("releaseGroupWindow").toMap() != QVariantMap{{"since",options.value("since")},{"until",options.value("until")}}) return false;
        } else if (artist.contains("releaseGroupScope") || artist.contains("releaseGroupWindow")) return false;
        const auto groups = artist.value("releaseGroups").toList(); if (groups.size() > 500) return false;
        int count = -1;
        if (!artist.contains("releaseGroupCount") || (!artist.value("releaseGroupCount").isNull()
            && !nonnegativeInteger(artist.value("releaseGroupCount"),&count))) return false;
        if ((count >= 0 && count < groups.size()) || (complete && count != groups.size())) return false;
        if ((status == "ready" || status == "partial") == groups.isEmpty()) return false;
        QSet<QString> seen;
        for (const auto &entry : groups) {
            if (entry.metaType().id() != QMetaType::QVariantMap) return false;
            const auto group = entry.toMap();
            for (auto it = group.cbegin(); it != group.cend(); ++it)
                if (!QSet<QString>{"id","title","first-release-date","primary-type","secondary-types"}.contains(it.key())) return false;
            const auto id = vinyl::ReleaseGroupMbid::parse(group.value("id").toString());
            if (!id.ok() || seen.contains(id.value->toString()) || !boundedText(group.value("title"))) return false;
            seen.insert(id.value->toString());
            if (group.contains("first-release-date") && !boundedText(group.value("first-release-date"),32,true)) return false;
            if (group.contains("primary-type") && !group.value("primary-type").isNull() && !boundedText(group.value("primary-type"),100,true)) return false;
            if (group.contains("secondary-types")) {
                const auto types = group.value("secondary-types");
                if (types.metaType().id() != QMetaType::QVariantList && types.metaType().id() != QMetaType::QStringList) return false;
                if (types.toList().size() > 32) return false;
                for (const auto &type : types.toList()) if (!boundedText(type,100)) return false;
            }
        }
        *out = data; return true;
    }
    if (data.contains("artist") || input.size() > (operation == "chartCandidates" ? 50 : 60)
        || ((status == "ready" || status == "partial") == input.isEmpty())) return false;
    const auto source = metadataSource(operation,value,options);
    QString kind;
    if (operation == "chartCandidates") kind = value == "lastfm:tracks" ? "songs" : value == "lastfm:artists" ? "artists" : value.section(':',2,2);
    else kind = "artists";
    if (status != "unavailable") {
        if (provenance.value("generator") != (operation == "chartCandidates" ? "chart-feed" : "artist-similarity")) return false;
        if (operation == "chartCandidates") {
            if (source == "apple-music" ? provenance.value("feed") != value
                : provenance.value("method") != (kind == "songs" ? "chart.getTopTracks" : "chart.getTopArtists")) return false;
        } else if (source == "lastfm") {
            if (provenance.value("method") != "artist.getSimilar" || provenance.value("artistName") != options.value("artistName")) return false;
        } else if (!boundedText(provenance.value("algorithm"),200)) return false;
    }
    QVariantList items; QSet<QString> seen;
    for (const auto &entry : input) {
        if (entry.metaType().id() != QMetaType::QVariantMap) return false;
        const auto supplied = entry.toMap(); const bool projected = supplied.contains("catalogueItem");
        if (projected && supplied.value("catalogueItem").metaType().id() != QMetaType::QVariantMap) return false;
        const auto card = projected ? supplied.value("catalogueItem").toMap() : supplied;
        if (card.value("source") != source || card.value("kind") != kind) return false;
        int rank = 0; if (!positiveRank(card.value("rank"),operation == "chartCandidates" ? 50 : 60,&rank)) return false;
        if (operation == "chartCandidates") {
            if (card.value("chartKind") != kind || (source == "apple-music" ? card.value("storefront").toString() != value.section(':',1,1) : card.contains("storefront"))) return false;
        } else {
            const auto similarity = card.value("similarity"); const auto type = similarity.metaType().id();
            const bool absentScore = source == "listenbrainz" && !card.contains("similarity");
            if (!absentScore && ((type != QMetaType::Double && type != QMetaType::Int && type != QMetaType::LongLong)
                || !std::isfinite(similarity.toDouble()) || similarity.toDouble() < 0
                || (source == "lastfm" && similarity.toDouble() > 1))) return false;
            const auto evidence = card.value("hints").toMap().value("mbidEvidence").toMap();
            if (source == "listenbrainz" && (evidence.value("artist") != card.value("sourceIds").toMap().value("id")
                || (evidence.contains("referenceArtist") && evidence.value("referenceArtist") != value))) return false;
        }
        const auto item = normalized(card); if (item.isEmpty()) return false;
        if (projected) for (auto it = supplied.cbegin(); it != supplied.cend(); ++it)
            if (!item.contains(it.key()) || it.value() != item.value(it.key())) return false;
        const auto key = item.value("key").toString(); if (seen.contains(key)) continue;
        seen.insert(key); items.append(item);
    }
    data["items"] = items; *out = data; return true;
}

QStringList VinylSourcePool::artistNames(const QVariantMap &identity) {
    const auto artist = identity.value("artist").toMap();
    const auto mbid = vinyl::ArtistMbid::parse(identity.value("mbid").toString());
    if (identity.value("status") != "verified" || !mbid.ok()
        || artist.value("id").toString().toLower() != mbid.value->toString()) return {};
    QStringList result; QSet<QString> seen;
    auto append = [&](const QVariant &value) {
        if (!boundedText(value, 300)) return false;
        const auto name = value.toString().trimmed(), key = name.normalized(QString::NormalizationForm_C);
        if (!seen.contains(key)) { seen.insert(key); result.append(name); }
        return true;
    };
    if (!append(artist.value("name"))) return {};
    const auto aliases = artist.value("aliases");
    if (aliases.isValid()) {
        if (aliases.metaType().id() != QMetaType::QVariantList && aliases.metaType().id() != QMetaType::QStringList) return {};
        for (const auto &value : aliases.toList()) if (!append(value)) return {};
    }
    return result;
}

QVariantMap VinylSourcePool::catalogueCandidate(const QVariantMap &card) {
    const QSet<QString> allowed{"id","kind","title","artist","subtitle","cover","year","catalogueCard",
        "source","sourceIds","hints","rank","chartKind","storefront","similarity","score"};
    for (auto it = card.cbegin(); it != card.cend(); ++it) if (!allowed.contains(it.key())) return {};
    const auto source = card.value("source").toString(), kind = card.value("kind").toString();
    if (card.value("catalogueCard").metaType().id() != QMetaType::Bool || !card.value("catalogueCard").toBool()
        || !QStringList{"apple-music","lastfm","listenbrainz","jiosaavn"}.contains(source)
        || !QStringList{"songs","albums","artists"}.contains(kind)
        || !boundedText(card.value("title")) || !boundedText(card.value("artist"))
        || card.value("sourceIds").metaType().id() != QMetaType::QVariantMap
        || card.value("hints").metaType().id() != QMetaType::QVariantMap
        || !QRegularExpression("^" + QRegularExpression::escape(source + ":" + kind + ":") + "[0-9a-f]{24}$")
            .match(card.value("id").toString()).hasMatch() || wire(card).size() > 239 * 1024) return {};
    const auto ids = card.value("sourceIds").toMap();
    if (!boundedText(ids.value("id"), 2048)) return {};
    const auto id = ids.value("id").toString();
    if (source == "apple-music" && !QRegularExpression("^[1-9][0-9]{0,19}$").match(id).hasMatch()) return {};
    if (source == "listenbrainz" && !vinyl::ArtistMbid::parse(id).ok()) return {};
    if (source == "lastfm") {
        if (id.startsWith("mbid:")) {
            if (kind == "artists" ? !vinyl::ArtistMbid::parse(id.mid(5)).ok()
                : kind == "songs" ? !vinyl::RecordingMbid::parse(id.mid(5)).ok()
                : !vinyl::ReleaseMbid::parse(id.mid(5)).ok()) return {};
        } else if (!webUrl(id, {"last.fm","www.last.fm"}) || !QUrl(id).path().startsWith("/music/")) return {};
    }
    const auto cover = card.value("cover", "");
    if (!boundedText(cover, 4096, true) || (!cover.toString().isEmpty() && !webUrl(cover.toString()))) return {};
    QVariantMap item{{"id",card.value("id")},{"provider",source},{"providerId",id},
        {"kind",kind == "songs" ? "song" : kind == "albums" ? "album" : "artist"},
        {"title",card.value("title")},{"name",card.value("title")},{"cover",cover},
        {"subtitle",card.value("subtitle",card.value("artist"))},{"catalogueItem",card},{"requiresResolution",true}};
    for (const auto &field : {"rank","chartKind","storefront","similarity"})
        if (card.contains(field)) item[field] = card.value(field);
    QVariantList credits;
    if (kind == "artists") {
        credits.append(QVariantMap{{"kind","artist"},{"provider",source},{"providerId",id},{"name",card.value("title")}});
    } else if (source == "apple-music" && QRegularExpression("^[1-9][0-9]{0,19}$").match(ids.value("artistId").toString()).hasMatch()) {
        credits.append(QVariantMap{{"kind","artist"},{"provider",source},{"providerId",ids.value("artistId")},{"name",card.value("artist")}});
    }
    item["artistRefs"] = credits;
    // Source MBIDs and dates remain evidence inside catalogueItem.hints. They do
    // not attest a canonical mapping, a MusicBrainz release date or playable media.
    return item;
}

QVariantList VinylSourcePool::mergeCandidates(const QVariantList &items) {
    QMap<QString,QVariantMap> merged; QMap<QString,QSet<QString>> sources;
    for (const auto &value : items) {
        const auto item = normalized(value.toMap()); if (item.isEmpty()) continue;
        const auto key = item.value("key").toString(); sources[key].insert(item.value("provider").toString());
        if (!merged.contains(key) || wire(item) < wire(merged.value(key))) merged[key] = item;
    }
    QVariantList result;
    for (auto it = merged.begin(); it != merged.end(); ++it) {
        auto names = sources.value(it.key()).values(); std::sort(names.begin(),names.end());
        it.value()["poolSources"] = names; result.append(it.value());
    }
    return result;
}

QVariantList VinylSourcePool::mergeCharts(const QVariantList &feeds, const QString &kind) {
    struct Entry { QVariantMap item; QMap<QString,int> ranks; int score = 0, primaryRank = 0; };
    QMap<QString,Entry> entries;
    for (const auto &value : feeds) {
        const auto feed = value.toMap(); if (feed.value("kind") != kind || !usableFeed(feed)) continue;
        const auto storefront = feed.value("source").toString() + ":" + feed.value("storefront",feed.value("key")).toString();
        const auto items = feed.value("items").toList();
        for (int index = 0; index < items.size() && index < 100; ++index) {
            const auto raw = items[index].toMap(), item = normalized(raw); if (item.isEmpty()) continue;
            int rank = index + 1; if (raw.contains("rank") && !positiveRank(raw.value("rank"),100,&rank)) continue;
            auto &entry = entries[item.value("key").toString()];
            if (entry.item.isEmpty() || wire(item) < wire(entry.item)) entry.item = item;
            if (!entry.ranks.contains(storefront) || rank < entry.ranks.value(storefront)) entry.ranks[storefront] = rank;
            if (kind == "songs" && feed.value("source") == "lastfm")
                entry.primaryRank = entry.primaryRank ? qMin(entry.primaryRank,rank) : rank;
        }
    }
    QList<Entry> sorted;
    for (auto entry : entries) {
        for (auto it = entry.ranks.cbegin(); it != entry.ranks.cend(); ++it) {
            entry.score += 101 - it.value();
        }
        entry.item["chartScore"] = entry.score; entry.item["chartCoverage"] = entry.ranks.size();
        if (entry.primaryRank) entry.item["chartPrimaryRank"] = entry.primaryRank;
        sorted.append(entry);
    }
    std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b) {
        // Last.fm owns the global song order; Apple supplies corroboration and
        // the fallback when the global list has no usable cached response.
        if (bool(a.primaryRank) != bool(b.primaryRank)) return a.primaryRank > 0;
        if (a.primaryRank && a.primaryRank != b.primaryRank) return a.primaryRank < b.primaryRank;
        if (a.score != b.score) return a.score > b.score;
        if (a.ranks.size() != b.ranks.size()) return a.ranks.size() > b.ranks.size();
        return a.item.value("key").toString() < b.item.value("key").toString();
    });
    QVariantList result; for (const auto &entry : sorted) { if (result.size() == 60) break; result.append(entry.item); }
    return result;
}

QVariantList VinylSourcePool::updateCharts(const QVariantList &cached, const QVariantList &received) {
    QMap<QString,QVariantMap> retained;
    for (const auto &value : cached) if (usableFeed(value.toMap())) retained[value.toMap().value("key").toString()] = value.toMap();
    for (const auto &value : received) {
        auto feed = value.toMap(); const auto key = feed.value("key").toString(); if (key.isEmpty()) continue;
        if (usableFeed(feed) && (feed.value("complete").toBool() || !retained.contains(key))) retained[key] = feed;
        else if (retained.contains(key)) { auto &old = retained[key]; old["_stale"] = true; old["_refreshStatus"] = feed.value("status"); }
    }
    QVariantList result; for (const auto &feed : retained) result.append(feed); return result;
}

bool VinylSourcePool::isNewRelease(const QString &date, const QDate &today) {
    if (!today.isValid() || !QRegularExpression("^[0-9]{4}-[0-9]{2}-[0-9]{2}$").match(date).hasMatch()) return false;
    const auto parsed = QDate::fromString(date,Qt::ISODate);
    return parsed.isValid() && parsed <= today && parsed >= today.addMonths(-12);
}

QVariantList VinylSourcePool::newReleaseNeighbors(const QVariantList &seeds, const QVariantMap &bySeed, int ownCount, int minimum) {
    if (ownCount >= minimum || minimum < 1) return {};
    QSet<QString> all, seedKeys;
    for (const auto &value : seeds) { const auto row = normalized(value.toMap()); if (!row.isEmpty()) seedKeys.insert(row.value("key").toString()); }
    QVariantList result;
    for (const auto &value : seeds) {
        const auto seed = value.toMap(); QSet<QString> local;
        for (const auto &candidate : bySeed.value(seed.value("key").toString()).toList()) {
            const auto row = normalized(candidate.toMap()); if (row.isEmpty() || row.value("kind") != "artist") continue;
            const auto key = row.value("key").toString(); if (seedKeys.contains(key) || local.contains(key)) continue;
            local.insert(key); if (!all.contains(key)) { all.insert(key); result.append(row); }
            if (local.size() == 10) break;
        }
    }
    return result;
}

QVariantList VinylSourcePool::homeRows(const QVariantMap &buckets, bool history) {
    const QStringList keys{"mixes-radio","new-from-your-artists","albums-for-you","because-you-played","deep-cuts","rediscover","artists-like-yours"};
    const QStringList titles{"Mixes & radio","New from your artists","Albums for you",
        history ? QString::fromUtf8("Because you played…") : QString::fromUtf8("Because you picked…"),
        "Deep cuts from your artists","Rediscover","Artists like the ones you love"};
    QVariantList result;
    for (int index = 0; index < keys.size(); ++index) {
        if (keys[index] == "rediscover" && !history) continue;
        const auto items = buckets.value(keys[index]).toList(); if (items.size() < 8) continue;
        result.append(QVariantMap{{"key",keys[index]},{"id",keys[index]},{"title",titles[index]},
            {"items",items},{"itemCount",items.size()},{"status","ready"}});
    }
    return result;
}
