#include "anime/AnimeOrderIndex.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <optional>

// AnimeOrderIndex parses two community datasets into one immutable identity
// index:
//   * Fribb anime-list-mini.json — cross-provider IDs and media type.
//   * Anime-Lists anime-list-master.xml — AniDB→TVDB season/episode mappings.
// Both are joined by the AniDB id, which is the one primary key shared by the
// sources. Identity resolution never uses titles and only ever collapses to a
// single AniDB work when the supplied candidates are unambiguous.
//
// Task 1 implements identity parsing + resolution and preserves provider rows
// verbatim. Task 2 layers TVDB-map inversion and completeness onto the same
// resolve() without disturbing the identity contract.

namespace {

// A single explicit body pair "a-b" from a <mapping> element body, or a range.
// Parsed and stored now; the inversion that consumes them lands in Task 2.
struct MappingRule {
    int anidbSeason = 0;
    int tvdbSeason = 0;
    bool hasRange = false;
    int start = 0;
    int end = 0;
    bool hasEnd = false;
    int offset = 0;
    // Explicit body pairs keyed by TVDB episode → list of AniDB episodes.
    QHash<int, QList<int>> bodyTvdbToAnidb;
    bool bodyOneToMany = false; // any AniDB episode spanning multiple TVDB episodes
};

struct Entry {
    int anidb = 0;
    QString type;
    QList<int> mal;
    QList<int> anilist;
    QList<int> kitsu;
    QStringList imdb;
    int tmdbTv = 0;              // 0 == absent
    QVariantList tmdbMovies;     // list of int QVariants
    // From the Anime-Lists XML (joined by AniDB id):
    int tvdb = 0;               // 0 == absent / non-numeric (e.g. "movie")
    QString defaultTvdbSeason;  // "a", a season number, or ""
    int episodeOffset = 0;
    QList<MappingRule> mappings;
    bool hasXml = false;
};

QVariantList toVariantList(const QList<int>& ids)
{
    QVariantList out;
    out.reserve(ids.size());
    for (int id : ids)
        out.append(id);
    return out;
}

// Provider rows carry TVDB-style season/episode; tolerate the common aliases.
int seasonOf(const QVariantMap& row)
{
    if (row.contains(QStringLiteral("season")))
        return row.value(QStringLiteral("season")).toInt();
    if (row.contains(QStringLiteral("seasonNumber")))
        return row.value(QStringLiteral("seasonNumber")).toInt();
    return 0;
}

int episodeOf(const QVariantMap& row)
{
    if (row.contains(QStringLiteral("episode")))
        return row.value(QStringLiteral("episode")).toInt();
    if (row.contains(QStringLiteral("number")))
        return row.value(QStringLiteral("number")).toInt();
    return 0;
}

// Invert one TVDB-style (season, episode) into an AniDB absolute number.
// Precedence: explicit body pair > matching range > default season+offset. A
// one-to-many or many-to-one body sets `ambiguous` so completeness can refuse
// to guess which playable row owns the slot. Returns nullopt when nothing maps.
std::optional<int> resolveAbsolute(const Entry& e, int providerSeason, int providerEpisode,
                                   bool& ambiguous)
{
    ambiguous = false;

    // 1. Explicit body pair — highest precedence.
    for (const MappingRule& m : e.mappings) {
        if (m.tvdbSeason != providerSeason)
            continue;
        const auto it = m.bodyTvdbToAnidb.constFind(providerEpisode);
        if (it == m.bodyTvdbToAnidb.constEnd())
            continue;
        if (m.anidbSeason == 0)
            return std::nullopt; // a special body is never a regular absolute
        const QList<int>& anidbEps = it.value();
        if (anidbEps.isEmpty())
            return std::nullopt;
        if (anidbEps.size() != 1 || m.bodyOneToMany)
            ambiguous = true; // many-to-one / one-to-many: retained but not trusted
        return anidbEps.first();
    }

    // 2. Matching range: anidbEpisode = providerEpisode - offset, bounds-checked.
    for (const MappingRule& m : e.mappings) {
        if (m.tvdbSeason != providerSeason || !m.hasRange || m.anidbSeason == 0)
            continue;
        const int absolute = providerEpisode - m.offset;
        const bool inside = absolute >= m.start && (!m.hasEnd || absolute <= m.end);
        if (inside && absolute > 0)
            return absolute;
    }

    // 3. Default season + episode offset, only when no explicit mapping applies.
    if (!e.defaultTvdbSeason.isEmpty()) {
        bool numeric = false;
        const int defSeason = e.defaultTvdbSeason.toInt(&numeric);
        if (numeric && defSeason == providerSeason) {
            const int absolute = providerEpisode - e.episodeOffset;
            if (absolute > 0)
                return absolute;
        } else if (e.defaultTvdbSeason == QLatin1String("a") && e.mappings.isEmpty()
                   && providerSeason >= 1) {
            const int absolute = providerEpisode - e.episodeOffset;
            if (absolute > 0)
                return absolute;
        }
    }

    return std::nullopt;
}

} // namespace

struct AnimeOrderIndex::Data {
    QHash<int, Entry> entries; // AniDB id → identity entry (Fribb-backed)
    QHash<int, QSet<int>> byMal;
    QHash<int, QSet<int>> byAnilist;
    QHash<int, QSet<int>> byKitsu;
    QHash<int, QSet<int>> byTvdb;
    QHash<int, QSet<int>> byTmdbTv;
    QHash<int, QSet<int>> byTmdbMovie;
    QHash<QString, QSet<int>> byImdb;

    void collect(const QString& raw, QSet<int>& out) const;
    QVariantMap buildIds(const Entry& e) const;
};

// ── Identity candidate parsing ───────────────────────────────────────────────
// Prefixed ids are matched with anchored expressions; raw IMDb is ^tt[0-9]+$.
// An unmatched candidate simply contributes nothing (callers pass every id they
// honestly possess); only genuinely conflicting matches produce ambiguity.
void AnimeOrderIndex::Data::collect(const QString& raw, QSet<int>& out) const
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return;

    const auto uniteFrom = [&](const QHash<int, QSet<int>>& map, int key) {
        const auto it = map.constFind(key);
        if (it != map.constEnd())
            out.unite(*it);
    };
    const auto uniteImdb = [&](const QString& id) {
        const auto it = byImdb.constFind(id);
        if (it != byImdb.constEnd())
            out.unite(*it);
    };

    static const QRegularExpression rePrefixed(QStringLiteral("^([a-z]+):(.+)$"));
    const QRegularExpressionMatch pm = rePrefixed.match(s);
    if (pm.hasMatch()) {
        const QString prefix = pm.captured(1);
        const QString value = pm.captured(2);
        if (prefix == QLatin1String("imdb")) {
            static const QRegularExpression reImdb(QStringLiteral("^tt[0-9]+$"));
            if (reImdb.match(value).hasMatch())
                uniteImdb(value);
            return;
        }
        bool ok = false;
        const int n = value.toInt(&ok);
        if (!ok || n <= 0)
            return;
        if (prefix == QLatin1String("mal"))
            uniteFrom(byMal, n);
        else if (prefix == QLatin1String("anilist"))
            uniteFrom(byAnilist, n);
        else if (prefix == QLatin1String("kitsu"))
            uniteFrom(byKitsu, n);
        else if (prefix == QLatin1String("tvdb"))
            uniteFrom(byTvdb, n);
        else if (prefix == QLatin1String("tmdbtv"))
            uniteFrom(byTmdbTv, n);
        else if (prefix == QLatin1String("tmdbmovie"))
            uniteFrom(byTmdbMovie, n);
        else if (prefix == QLatin1String("anidb")) {
            if (entries.contains(n))
                out.insert(n);
        }
        return;
    }

    static const QRegularExpression reRawImdb(QStringLiteral("^tt[0-9]+$"));
    if (reRawImdb.match(s).hasMatch())
        uniteImdb(s);
}

QVariantMap AnimeOrderIndex::Data::buildIds(const Entry& e) const
{
    QVariantMap ids;
    ids.insert(QStringLiteral("anidb"), e.anidb);
    if (e.tvdb > 0)
        ids.insert(QStringLiteral("tvdb"), e.tvdb);
    ids.insert(QStringLiteral("mal"), toVariantList(e.mal));
    ids.insert(QStringLiteral("anilist"), toVariantList(e.anilist));
    ids.insert(QStringLiteral("kitsu"), toVariantList(e.kitsu));
    ids.insert(QStringLiteral("imdb"), e.imdb);

    QVariantMap tmdb;
    if (e.tmdbTv > 0)
        tmdb.insert(QStringLiteral("tv"), e.tmdbTv);
    tmdb.insert(QStringLiteral("movies"), e.tmdbMovies);
    ids.insert(QStringLiteral("tmdb"), tmdb);
    return ids;
}

namespace {

// Parse a <mapping> body such as ";1-3;2-0;" or ";1-1+2;". Each ';'-separated
// token is "<anidbEp>-<tvdbSpec>", where tvdbSpec may list several TVDB episodes
// (a+b+c) — that one-to-many shape is recorded so V1 can refuse to guess.
void parseMappingBody(const QString& body, MappingRule& rule)
{
    const QStringList tokens = body.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        const int dash = token.indexOf(QLatin1Char('-'));
        if (dash <= 0)
            continue;
        bool anidbOk = false;
        const int anidbEp = token.left(dash).toInt(&anidbOk);
        if (!anidbOk)
            continue;
        const QString tvdbSpec = token.mid(dash + 1);
        const QStringList tvdbParts = tvdbSpec.split(QLatin1Char('+'), Qt::SkipEmptyParts);
        if (tvdbParts.size() > 1)
            rule.bodyOneToMany = true;
        for (const QString& part : tvdbParts) {
            bool tvdbOk = false;
            const int tvdbEp = part.toInt(&tvdbOk);
            if (!tvdbOk)
                continue;
            rule.bodyTvdbToAnidb[tvdbEp].append(anidbEp);
        }
    }
}

// Parse one <anime> element (attributes + mapping-list). Advances the reader
// past the element's end tag. Returns false only for a non-positive AniDB id
// (the element is skipped, not fatal).
bool parseAnimeElement(QXmlStreamReader& xml, Entry& xmlEntry)
{
    const QXmlStreamAttributes attrs = xml.attributes();
    bool anidbOk = false;
    const int anidb = attrs.value(QLatin1String("anidbid")).toInt(&anidbOk);
    if (!anidbOk || anidb <= 0) {
        xml.skipCurrentElement();
        return false;
    }
    xmlEntry.anidb = anidb;
    xmlEntry.hasXml = true;
    xmlEntry.tvdb = attrs.value(QLatin1String("tvdbid")).toInt(); // 0 if non-numeric
    xmlEntry.defaultTvdbSeason = attrs.value(QLatin1String("defaulttvdbseason")).toString();
    xmlEntry.episodeOffset = attrs.value(QLatin1String("episodeoffset")).toInt(); // 0 if empty

    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("mapping-list")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("mapping")) {
                    const QXmlStreamAttributes m = xml.attributes();
                    MappingRule rule;
                    rule.anidbSeason = m.value(QLatin1String("anidbseason")).toInt();
                    rule.tvdbSeason = m.value(QLatin1String("tvdbseason")).toInt();
                    if (m.hasAttribute(QLatin1String("start"))) {
                        rule.hasRange = true;
                        rule.start = m.value(QLatin1String("start")).toInt();
                        rule.offset = m.value(QLatin1String("offset")).toInt();
                        if (m.hasAttribute(QLatin1String("end"))) {
                            rule.hasEnd = true;
                            rule.end = m.value(QLatin1String("end")).toInt();
                        }
                    }
                    const QString body = xml.readElementText(); // consumes to </mapping>
                    if (!body.trimmed().isEmpty())
                        parseMappingBody(body, rule);
                    xmlEntry.mappings.append(rule);
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            xml.skipCurrentElement(); // <name>, <before>, etc. (not needed in V1)
        }
    }
    return true;
}

} // namespace

std::shared_ptr<const AnimeOrderIndex> AnimeOrderIndex::fromSources(
    const QByteArray& fribbJson, const QByteArray& animeListXml, QString* error)
{
    const auto fail = [&](const QString& why) -> std::shared_ptr<const AnimeOrderIndex> {
        if (error)
            *error = why;
        return nullptr;
    };

    auto data = std::make_shared<Data>();

    // ── Fribb identity records ────────────────────────────────────────────────
    QJsonParseError perr {};
    const QJsonDocument doc = QJsonDocument::fromJson(fribbJson, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return fail(QStringLiteral("fribb payload is not a valid JSON array"));

    const QJsonArray records = doc.array();
    for (const QJsonValue& value : records) {
        if (!value.isObject())
            continue;
        const QJsonObject obj = value.toObject();
        if (!obj.contains(QStringLiteral("anidb_id")))
            continue;
        const int anidb = obj.value(QStringLiteral("anidb_id")).toInt(0);
        if (anidb <= 0)
            continue; // invalid record skipped, not fatal
        if (data->entries.contains(anidb))
            return fail(QStringLiteral("duplicate AniDB record %1 in fribb payload").arg(anidb));

        Entry e;
        e.anidb = anidb;
        e.type = obj.value(QStringLiteral("type")).toString();
        const auto addScalar = [&](const char* key, QList<int>& dst) {
            const QJsonValue v = obj.value(QLatin1String(key));
            if (v.isDouble()) {
                const int id = v.toInt(0);
                if (id > 0)
                    dst.append(id);
            }
        };
        addScalar("mal_id", e.mal);
        addScalar("anilist_id", e.anilist);
        addScalar("kitsu_id", e.kitsu);

        const QJsonValue imdbV = obj.value(QStringLiteral("imdb_id"));
        if (imdbV.isString()) {
            const QString s = imdbV.toString();
            if (!s.isEmpty())
                e.imdb.append(s);
        } else if (imdbV.isArray()) {
            for (const QJsonValue& iv : imdbV.toArray()) {
                const QString s = iv.toString();
                if (!s.isEmpty())
                    e.imdb.append(s);
            }
        }

        const QJsonValue tmdbV = obj.value(QStringLiteral("themoviedb_id"));
        if (tmdbV.isObject()) {
            const QJsonObject t = tmdbV.toObject();
            if (t.contains(QStringLiteral("tv")))
                e.tmdbTv = t.value(QStringLiteral("tv")).toInt(0);
            const QJsonValue movieV = t.value(QStringLiteral("movie"));
            if (movieV.isArray()) {
                for (const QJsonValue& mv : movieV.toArray())
                    e.tmdbMovies.append(mv.toInt(0));
            } else if (movieV.isDouble()) {
                e.tmdbMovies.append(movieV.toInt(0));
            }
        } else if (tmdbV.isDouble()) {
            e.tmdbTv = tmdbV.toInt(0);
        }

        data->entries.insert(anidb, e);
    }

    if (data->entries.isEmpty())
        return fail(QStringLiteral("fribb payload contains no usable identity records"));

    // ── Anime-Lists AniDB↔TVDB mapping records ───────────────────────────────
    QXmlStreamReader xml(animeListXml);
    if (!xml.readNextStartElement())
        return fail(QStringLiteral("anime-list xml is empty or unreadable"));
    if (xml.name() != QLatin1String("anime-list"))
        return fail(QStringLiteral("anime-list xml root is not <anime-list>"));

    QSet<int> seenXmlAnidb;
    int xmlCount = 0;
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("anime")) {
            xml.skipCurrentElement();
            continue;
        }
        Entry xmlEntry;
        if (!parseAnimeElement(xml, xmlEntry))
            continue;
        if (seenXmlAnidb.contains(xmlEntry.anidb))
            return fail(QStringLiteral("duplicate AniDB record %1 in anime-list xml").arg(xmlEntry.anidb));
        seenXmlAnidb.insert(xmlEntry.anidb);
        ++xmlCount;

        // Join mapping data onto the identity entry if it exists.
        const auto it = data->entries.find(xmlEntry.anidb);
        if (it != data->entries.end()) {
            it->tvdb = xmlEntry.tvdb;
            it->defaultTvdbSeason = xmlEntry.defaultTvdbSeason;
            it->episodeOffset = xmlEntry.episodeOffset;
            it->mappings = xmlEntry.mappings;
            it->hasXml = true;
        }
    }
    if (xml.hasError())
        return fail(QStringLiteral("anime-list xml parse error: %1").arg(xml.errorString()));
    if (xmlCount == 0)
        return fail(QStringLiteral("anime-list xml contains no usable <anime> records"));

    // ── Build identity lookup tables ─────────────────────────────────────────
    for (auto it = data->entries.constBegin(); it != data->entries.constEnd(); ++it) {
        const int anidb = it.key();
        const Entry& e = it.value();
        for (int id : e.mal)
            data->byMal[id].insert(anidb);
        for (int id : e.anilist)
            data->byAnilist[id].insert(anidb);
        for (int id : e.kitsu)
            data->byKitsu[id].insert(anidb);
        for (const QString& s : e.imdb)
            data->byImdb[s].insert(anidb);
        if (e.tvdb > 0)
            data->byTvdb[e.tvdb].insert(anidb);
        if (e.tmdbTv > 0)
            data->byTmdbTv[e.tmdbTv].insert(anidb);
        for (const QVariant& mv : e.tmdbMovies)
            data->byTmdbMovie[mv.toInt()].insert(anidb);
    }

    if (error)
        error->clear();
    return std::shared_ptr<const AnimeOrderIndex>(new AnimeOrderIndex(std::move(data)));
}

AnimeOrderIndex::AnimeOrderIndex(std::shared_ptr<const Data> data)
    : m_data(std::move(data)) {}

int AnimeOrderIndex::entryCount() const
{
    return m_data ? m_data->entries.size() : 0;
}

QVariantMap AnimeOrderIndex::resolve(const QVariantMap& identities,
                                     const QVariantList& providerEpisodes) const
{
    QVariantMap result;
    result.insert(QStringLiteral("seasons"), QVariantList{});
    result.insert(QStringLiteral("absoluteComplete"), false);
    result.insert(QStringLiteral("defaultOrder"), QStringLiteral("seasons"));

    if (!m_data) {
        result.insert(QStringLiteral("status"), QStringLiteral("unmapped"));
        result.insert(QStringLiteral("ids"), QVariantMap{});
        result.insert(QStringLiteral("episodes"), providerEpisodes);
        result.insert(QStringLiteral("diagnostic"), QStringLiteral("index unavailable"));
        return result;
    }

    // Gather every AniDB work implied by the supplied identities. Titles ignored.
    QSet<int> matches;
    m_data->collect(identities.value(QStringLiteral("sourceId")).toString(), matches);
    m_data->collect(identities.value(QStringLiteral("resolvedId")).toString(), matches);
    for (const QVariant& v : identities.value(QStringLiteral("imdbIds")).toList())
        m_data->collect(v.toString(), matches);

    QString status;
    if (matches.isEmpty())
        status = QStringLiteral("unmapped");
    else if (matches.size() != 1)
        status = QStringLiteral("ambiguous");
    else
        status = QStringLiteral("mapped");

    result.insert(QStringLiteral("status"), status);
    if (status != QLatin1String("mapped")) {
        // Unmapped/ambiguous: leave the original provider array untouched so the
        // UI simply keeps today's season behavior.
        result.insert(QStringLiteral("ids"), QVariantMap{});
        result.insert(QStringLiteral("episodes"), providerEpisodes);
        result.insert(QStringLiteral("diagnostic"), QStringLiteral("identity %1").arg(status));
        return result;
    }

    const Entry& entry = m_data->entries.value(*matches.constBegin());
    result.insert(QStringLiteral("ids"), m_data->buildIds(entry));

    // Annotate every provider row without dropping or renaming one. Regular rows
    // resolve to an absolute number; specials are transported but stay out of the
    // absolute queue and never affect regular completeness.
    QVariantList regularMatched, regularUnmatched, specials;
    bool anyAmbiguous = false;
    for (const QVariant& rowVar : providerEpisodes) {
        QVariantMap out = rowVar.toMap();
        const int season = seasonOf(out);
        const int episode = episodeOf(out);
        out.insert(QStringLiteral("streamId"), out.value(QStringLiteral("id")).toString());
        out.insert(QStringLiteral("sourceSeason"), season);
        out.insert(QStringLiteral("sourceEpisode"), episode);
        if (season == 0) {
            out.insert(QStringLiteral("kind"), QStringLiteral("special"));
            out.insert(QStringLiteral("mapped"), false);
            out.insert(QStringLiteral("absoluteNumber"), QVariant());
            specials.append(out);
            continue;
        }
        out.insert(QStringLiteral("kind"), QStringLiteral("episode"));
        bool ambiguous = false;
        const std::optional<int> absolute = resolveAbsolute(entry, season, episode, ambiguous);
        if (absolute && *absolute > 0) {
            out.insert(QStringLiteral("absoluteNumber"), *absolute);
            out.insert(QStringLiteral("mapped"), true);
            if (ambiguous)
                anyAmbiguous = true;
            regularMatched.append(out);
        } else {
            out.insert(QStringLiteral("absoluteNumber"), QVariant());
            out.insert(QStringLiteral("mapped"), false);
            regularUnmatched.append(out);
        }
    }

    std::sort(regularMatched.begin(), regularMatched.end(),
              [](const QVariant& a, const QVariant& b) {
                  return a.toMap().value(QStringLiteral("absoluteNumber")).toInt()
                       < b.toMap().value(QStringLiteral("absoluteNumber")).toInt();
              });

    // Completeness is judged only across the regular rows the provider supplied:
    // unique, positive, and contiguous (a truthful window is allowed; holes and
    // ambiguity are not).
    QList<int> absNums;
    absNums.reserve(regularMatched.size());
    for (const QVariant& v : regularMatched)
        absNums.append(v.toMap().value(QStringLiteral("absoluteNumber")).toInt());
    bool contiguous = true;
    for (int i = 1; i < absNums.size(); ++i) {
        if (absNums.at(i) != absNums.at(i - 1) + 1) {
            contiguous = false;
            break;
        }
    }
    const int regularCount = regularMatched.size() + regularUnmatched.size();
    const bool complete = regularCount > 0 && !anyAmbiguous && regularUnmatched.isEmpty()
        && absNums.size() == regularCount && contiguous;

    QVariantList episodes = regularMatched;
    episodes.append(regularUnmatched);
    episodes.append(specials);

    // Season summary in ascending order (0 == Specials).
    QMap<int, int> seasonCounts;
    for (const QVariant& v : episodes)
        seasonCounts[v.toMap().value(QStringLiteral("sourceSeason")).toInt()]++;
    QVariantList seasons;
    for (auto it = seasonCounts.constBegin(); it != seasonCounts.constEnd(); ++it) {
        QVariantMap s;
        s.insert(QStringLiteral("number"), it.key());
        s.insert(QStringLiteral("label"),
                 it.key() == 0 ? QStringLiteral("Specials") : QStringLiteral("Season %1").arg(it.key()));
        s.insert(QStringLiteral("count"), it.value());
        seasons.append(s);
    }

    const bool defaultAbsolute = complete && entry.defaultTvdbSeason == QLatin1String("a");
    result.insert(QStringLiteral("episodes"), episodes);
    result.insert(QStringLiteral("seasons"), seasons);
    result.insert(QStringLiteral("absoluteComplete"), complete);
    result.insert(QStringLiteral("defaultOrder"),
                  defaultAbsolute ? QStringLiteral("absolute") : QStringLiteral("seasons"));
    result.insert(QStringLiteral("diagnostic"),
                  complete ? QString()
                           : (anyAmbiguous ? QStringLiteral("ambiguous episode mapping")
                                           : QStringLiteral("incomplete absolute mapping")));
    return result;
}
