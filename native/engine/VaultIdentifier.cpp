#include "VaultIdentifier.h"

#include "ComicsCatalog.h"
#include "ImdbCatalog.h"
#include "MalCatalog.h"
#include "VaultIndex.h"
#include "VaultKit.h"

#include <QVariantList>
#include <QVariantMap>
#include <QRegularExpression>
#include <QStringList>

namespace {

QString firstNonEmpty(const QString& first, const QString& fallback)
{
    return first.isEmpty() ? fallback : first;
}

void clearIdentity(VaultIndex::FileRow& row)
{
    row.identityId.clear();
    row.identityTitle.clear();
    row.identitySource.clear();
    row.identitySynopsis.clear();
    row.identityCoverUrl.clear();
    row.identityWorld.clear();
    row.identityYear = 0;
    row.identityRating = 0;   // phase-4 G1: the adopted IMDb facts die with the identity
    row.identityGenres.clear();
    row.identityState.clear();
    row.identityCandidateCount = 0;
}

struct LookupTitle {
    QString normalized;
    int year = 0;
};

LookupTitle lookupTitle(const QString& title)
{
    QString withoutYear = title;
    int year = 0;
    const QRegularExpressionMatch yearMatch =
        QRegularExpression(QStringLiteral("\\b((?:19|20)\\d{2})\\b")).match(withoutYear);
    if (yearMatch.hasMatch()) {
        year = yearMatch.captured(1).toInt();
        withoutYear.remove(yearMatch.capturedStart(1), yearMatch.capturedLength(1));
    }
    return {VaultKit::normalizedTitle(withoutYear), year};
}

// Vault ux uplift S16: the cleaner already found the year and stripped it; the identifier's
// own re-grep then sees none and matchByTitle skips its year filter, so every remake reads
// ambiguous. The census's parsedYear (the RAW name's first 19xx/20xx token) is the exact
// find the cleaner made — it wins whenever the name had one; the re-grep is the fallback
// for rows that predate the column or were assembled by a path that never stamped it.
void applyLookupYear(LookupTitle& lookup, const VaultIndex::FileRow& row)
{
    if (row.parsedYear > 0)
        lookup.year = row.parsedYear;
}

} // namespace

VaultIdentifier::VaultIdentifier(VaultIndex* index, ComicsCatalog* comics,
                                 MalCatalog* mal, ImdbCatalog* imdb,
                                 QObject* parent)
    : QObject(parent), m_index(index), m_comics(comics), m_mal(mal), m_imdb(imdb)
{
}

VaultIdentifier::Match VaultIdentifier::matchGroup(const QString& groupKey) const
{
    Match match;
    if (!m_index || groupKey.isEmpty())
        return match;

    const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return match;
    const VaultIndex::FileRow& row = rows.constFirst();
    const QString groupTitle = row.groupTitle.trimmed();
    for (const VaultIndex::FileRow& candidate : rows) {
        // Coherence is a group-level contract. A real series keeps one shared
        // group title while its files may carry different display titles.
        if (candidate.groupKey != groupKey || candidate.kind != row.kind
            || candidate.groupTitle.trimmed() != groupTitle)
            return match;
    }
    // identitySuppressed records the user's last Un-identify choice so a future
    // rescan stays filename-honest. It must not block this explicit Identify
    // gesture: a deliberate re-identification is allowed to clear that marker.
    if (row.away || !row.errorState.isEmpty())
        return match;

    const QString title = row.groupTitle.trimmed();
    // Vault ux uplift S16: the census's parsed year is the cleaner's own find, and it wins
    // the lookup over the cleaned title (whose year the cleaner already removed), so a
    // remade title matches the catalogue's year filter instead of reading ambiguous.
    LookupTitle lookup = lookupTitle(title);
    applyLookupYear(lookup, row);
    const QString normalizedTitle = lookup.normalized;
    if (title.isEmpty() || normalizedTitle.isEmpty())
        return match;

    if (row.kind == QLatin1String("book")) {
        // EPUB metadata is already local and provenance-tagged by VaultEnricher;
        // it is the only book identity source in this slice.
        if (row.metadataSource != QLatin1String("EPUB"))
            return match;
        match.adopted = true;
        match.source = QStringLiteral("EPUB");
        match.sourceId = QStringLiteral("epub:") + row.id;
        match.title = firstNonEmpty(row.displayTitle, row.groupTitle);
        match.synopsis = row.synopsis;
        match.world = QStringLiteral("Biblio");
        return match;
    }

    if (row.kind == QLatin1String("comic")) {
        QVariantList comics;
        if (m_comics && m_comics->ready())
            for (const QVariant& value : m_comics->exactMatches(title)) {
                const QVariantMap hit = value.toMap();
                if (VaultKit::normalizedTitle(hit.value(QStringLiteral("title")).toString())
                    == normalizedTitle)
                    comics.append(value);
            }

        QVariantList mal;
        // ComicsCatalog owns the western-comics lane. Only fall back to MAL when
        // it has no exact western candidate, so a title present in both corpora
        // never becomes an artificial cross-source ambiguity.
        if (comics.isEmpty() && m_mal && m_mal->ready())
            mal = m_mal->matchByTitle(normalizedTitle, lookup.year, {});

        // One exact candidate across the allowed offline catalogues is the
        // certainty threshold. An ambiguity stays filename-honest.
        const int candidateCount = comics.size() + mal.size();
        match.candidateCount = candidateCount;
        if (candidateCount != 1)
            return match;

        if (comics.size() == 1) {
            const QVariantMap hit = comics.constFirst().toMap();
            const int gcdId = hit.value(QStringLiteral("gcdId")).toInt();
            if (gcdId <= 0)
                return Match();
            const QVariantMap series = m_comics->series(gcdId);
            match.adopted = true;
            match.source = QStringLiteral("COMICS");
            match.sourceId = QStringLiteral("comics:") + QString::number(gcdId);
            match.title = firstNonEmpty(series.value(QStringLiteral("title")).toString(),
                                        hit.value(QStringLiteral("title")).toString());
            match.synopsis = series.value(QStringLiteral("synopsis")).toString();
            match.coverUrl = firstNonEmpty(series.value(QStringLiteral("cover")).toString(),
                                           hit.value(QStringLiteral("cover")).toString());
            match.world = QStringLiteral("Tankoban");
            match.year = firstNonEmpty(QString::number(series.value(QStringLiteral("year")).toInt()),
                                       QString::number(hit.value(QStringLiteral("year")).toInt()))
                             .toInt();
            return match;
        }

        const QVariantMap hit = mal.constFirst().toMap();
        const int malId = hit.value(QStringLiteral("mal_id")).toInt();
        if (malId <= 0)
            return Match();
        match.adopted = true;
        match.source = QStringLiteral("MAL");
        match.sourceId = QStringLiteral("mal:") + QString::number(malId);
        match.title = firstNonEmpty(hit.value(QStringLiteral("title_english")).toString(),
                                   hit.value(QStringLiteral("title")).toString());
        match.synopsis = hit.value(QStringLiteral("synopsis")).toString();
        match.coverUrl = hit.value(QStringLiteral("coverUrl")).toString();
        match.world = QStringLiteral("Tankoban");
        match.year = hit.value(QStringLiteral("year")).toInt();
        return match;
    }

    if (row.kind == QLatin1String("video")) {
        if (!m_imdb || !m_imdb->ready())
            return match;
        const QVariantList hits = m_imdb->matchByTitle(normalizedTitle, lookup.year);
        match.candidateCount = hits.size();
        if (hits.size() != 1)
            return match;
        const QVariantMap hit = hits.constFirst().toMap();
        const QString tt = hit.value(QStringLiteral("tt")).toString();
        if (tt.isEmpty())
            return match;
        match.adopted = true;
        match.source = QStringLiteral("IMDB");
        match.sourceId = QStringLiteral("imdb:") + tt;
        match.title = hit.value(QStringLiteral("title")).toString();
        match.synopsis = hit.value(QStringLiteral("synopsis")).toString();
        match.coverUrl = QStringLiteral("https://live.metahub.space/poster/medium/%1/img").arg(tt);
        match.world = QStringLiteral("Theatre");
        match.year = hit.value(QStringLiteral("year")).toInt();
        // Phase-4 G1 ruling (2026-08-25): the adopted identity's rating + genres ride the
        // Match. The bake already returns both; ImdbCatalog folds the baked db's JSON genre
        // array to a QVariantList, so the join reducer here goes straight to the list. Votes
        // deliberately not carried (the ruling excluded them — a popularity count ages).
        match.rating = hit.value(QStringLiteral("rating")).toDouble();
        {
            QStringList genres;
            const QVariantList genreList =
                hit.value(QStringLiteral("genres")).toList();
            for (const QVariant& g : genreList) {
                const QString gr = g.toString().trimmed();
                if (!gr.isEmpty())
                    genres << gr;
            }
            match.genres = genres.join(QStringLiteral(" · "));
        }
        return match;
    }

    return match;
}

int VaultIdentifier::autoIdentifyExisting()
{
    if (!m_index)
        return 0;

    int adopted = 0;
    const QStringList kinds = {QStringLiteral("comic"), QStringLiteral("book"),
                               QStringLiteral("video")};
    for (const QString& kind : kinds) {
        const QVariantList groups = m_index->groupsForKind(kind);
        for (const QVariant& value : groups) {
            const QString groupKey = value.toMap().value(QStringLiteral("groupKey")).toString();
            if (groupKey.isEmpty())
                continue;
            const QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
            if (rows.isEmpty())
                continue;

            bool eligible = true;
            for (const VaultIndex::FileRow& row : rows) {
                // Suppression is a user decision: automatic passes honour it, while the
                // explicit Identify action deliberately bypasses it and clears the marker.
                if (!row.identityId.isEmpty() || row.identitySuppressed || row.away
                    || !row.errorState.isEmpty()) {
                    eligible = false;
                    break;
                }
            }
            if (!eligible)
                continue;

            const Match match = matchGroup(groupKey);
            if (match.adopted && applyGroup(groupKey, match))
                ++adopted;
        }
    }
    return adopted;
}

bool VaultIdentifier::applyGroup(const QString& groupKey, const Match& match)
{
    if (!m_index || groupKey.isEmpty() || !match.adopted || match.source.isEmpty()
        || match.sourceId.isEmpty() || match.title.isEmpty())
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    for (VaultIndex::FileRow& row : rows) {
        row.identityId = match.sourceId;
        row.identityTitle = match.title;
        row.identitySource = match.source;
        row.identitySynopsis = match.synopsis;
        row.identityCoverUrl = match.coverUrl;
        row.identityWorld = match.world;
        row.identityYear = match.year;
        row.identityRating = match.rating;   // phase-4 G1: the adopted IMDb facts persist
        row.identityGenres = match.genres;
        row.identitySuppressed = false;
        // Adoption clears any prior ambiguity — identify-in-place settles the tile.
        row.identityState = QStringLiteral("adopted");
        row.identityCandidateCount = 0;
    }
    return m_index->upsertMany(rows);
}

bool VaultIdentifier::identifyGroupWith(const QString& groupKey, const Match& match)
{
    return applyGroup(groupKey, match);
}

bool VaultIdentifier::unidentifyGroup(const QString& groupKey)
{
    if (!m_index || groupKey.isEmpty())
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    for (VaultIndex::FileRow& row : rows) {
        clearIdentity(row);
        row.identitySuppressed = true;
        row.identityState = QStringLiteral("suppressed");
        if (row.kind == QLatin1String("book")) {
            row.displayTitle = VaultKit::cleanMediaFolderTitle(row.realName);
            row.author.clear();
            row.format.clear();
            row.coverRef.clear();
            row.synopsis.clear();
            row.metadataSource.clear();
        }
    }
    return m_index->upsertMany(rows);
}

bool VaultIdentifier::reshelveGroup(const QString& groupKey, const QString& kind)
{
    if (!m_index || groupKey.isEmpty()
        || !(kind == QLatin1String("comic") || kind == QLatin1String("book")
             || kind == QLatin1String("video")))
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    for (VaultIndex::FileRow& row : rows) {
        row.kind = kind;
        clearIdentity(row);
        row.identitySuppressed = false;
    }
    return m_index->upsertMany(rows);
}

bool VaultIdentifier::recordAmbiguous(const QString& groupKey, int candidateCount)
{
    if (!m_index || groupKey.isEmpty() || candidateCount <= 1)
        return false;
    QList<VaultIndex::FileRow> rows = m_index->rowsForGroup(groupKey);
    if (rows.isEmpty())
        return false;
    bool dirty = false;
    for (VaultIndex::FileRow& row : rows) {
        // Never overwrite a group that has already resolved either way (adopted, or a user's
        // explicit Un-identify) — ambiguity recording only applies to a group that has never
        // resolved, so a later re-check of an already-settled group is a harmless no-op.
        if (!row.identityId.isEmpty() || row.identitySuppressed)
            continue;
        if (row.identityState != QLatin1String("ambiguous")
            || row.identityCandidateCount != candidateCount) {
            row.identityState = QStringLiteral("ambiguous");
            row.identityCandidateCount = candidateCount;
            dirty = true;
        }
    }
    return dirty && m_index->upsertMany(rows);
}
