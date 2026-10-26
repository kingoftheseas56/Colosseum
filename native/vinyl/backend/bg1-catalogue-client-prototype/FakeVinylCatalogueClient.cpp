#include "FakeVinylCatalogueClient.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cstdlib>

namespace vinyl::catalogue {
namespace {

template <typename Id>
Id fixtureId(const char *text)
{
    const auto parsed = Id::parse(QString::fromLatin1(text));
    if (!parsed.ok())
        std::abort();
    return *parsed.value;
}

template <typename T>
CatalogueResult<T> unavailable()
{
    return CatalogueResult<T>::failure(
        CatalogueErrorCode::Unavailable,
        QStringLiteral("catalogue"),
        QStringLiteral("Catalogue service is temporarily unavailable."));
}

template <typename T>
CatalogueResult<T> notFound(const QString &field)
{
    return CatalogueResult<T>::failure(
        CatalogueErrorCode::NotFound,
        field,
        QStringLiteral("Canonical entity was not found."));
}

template <typename T, typename Id>
CatalogueLookupResult<T, Id> lookupUnavailable()
{
    return CatalogueLookupResult<T, Id>::failure(
        CatalogueErrorCode::Unavailable,
        QStringLiteral("catalogue"),
        QStringLiteral("Catalogue service is temporarily unavailable."));
}

template <typename T, typename Id>
CatalogueLookupResult<T, Id> lookupNotFound(const QString &field)
{
    return CatalogueLookupResult<T, Id>::failure(
        CatalogueErrorCode::NotFound,
        field,
        QStringLiteral("Canonical entity was not found."));
}

template <typename Id>
std::optional<Id> redirectTarget(
    const QVector<std::pair<CanonicalEntityRef, CanonicalEntityRef>> &redirects,
    const CanonicalEntityRef &from,
    CanonicalEntityKind expectedKind)
{
    for (const auto &redirect : redirects) {
        if (redirect.first != from)
            continue;
        if (redirect.second.kind() != expectedKind)
            std::abort();
        const auto parsed = Id::parse(redirect.second.mbidString());
        if (!parsed.ok())
            std::abort();
        return *parsed.value;
    }
    return std::nullopt;
}

QStringList displayCredit(const QVector<ArtistCredit> &credit)
{
    QStringList out;
    out.reserve(credit.size());
    for (const ArtistCredit &entry : credit)
        out.push_back(entry.creditedName);
    return out;
}

LastKnownDisplaySnapshot displaySnapshot(const Artist &artist)
{
    return LastKnownDisplaySnapshot{artist.name, {}};
}

LastKnownDisplaySnapshot displaySnapshot(const ReleaseGroup &group)
{
    return LastKnownDisplaySnapshot{group.title, displayCredit(group.artistCredit)};
}

LastKnownDisplaySnapshot displaySnapshot(const Release &release)
{
    return LastKnownDisplaySnapshot{release.title, displayCredit(release.artistCredit)};
}

LastKnownDisplaySnapshot displaySnapshot(const Recording &recording)
{
    return LastKnownDisplaySnapshot{recording.title, displayCredit(recording.artistCredit)};
}

QString cursorSignature(const QString &queryKey)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(queryKey.toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .left(16));
}

template <typename T>
CatalogueResult<Page<T>> paginate(const QVector<T> &items,
                                  const QString &queryKey,
                                  const PageRequest &request)
{
    if (request.limit < 1 || request.limit > 100) {
        return CatalogueResult<Page<T>>::failure(
            CatalogueErrorCode::InvalidQuery,
            QStringLiteral("page.limit"),
            QStringLiteral("Page limit must be in the inclusive range 1..100."));
    }

    int offset = 0;
    const QString signature = cursorSignature(queryKey);
    if (request.cursor.has_value()) {
        const QStringList parts = request.cursor->token().split(QLatin1Char(':'));
        bool offsetOk = false;
        const int parsedOffset = parts.size() == 3 ? parts.at(2).toInt(&offsetOk) : -1;
        if (parts.size() != 3
            || parts.at(0) != QStringLiteral("bg1p1")
            || parts.at(1) != signature
            || !offsetOk
            || parsedOffset < 0
            || parsedOffset > items.size()) {
            return CatalogueResult<Page<T>>::failure(
                CatalogueErrorCode::InvalidPage,
                QStringLiteral("page.cursor"),
                QStringLiteral("Cursor is malformed, stale, or belongs to another query."));
        }
        offset = parsedOffset;
    }

    const QVector<T> slice = items.mid(offset, request.limit);
    const int nextOffset = offset + slice.size();

    std::optional<PageCursor> next;
    if (nextOffset < items.size()) {
        next.emplace(QStringLiteral("bg1p1:%1:%2")
                         .arg(signature)
                         .arg(nextOffset));
    }

    return CatalogueResult<Page<T>>::success(Page<T>{slice, std::move(next)});
}

bool containsInsensitive(const QStringList &values, const QString &value)
{
    for (const QString &candidate : values) {
        if (candidate.compare(value, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool releaseGroupMatches(const ReleaseGroup &group, const ReleaseGroupFilter &filter)
{
    if (!filter.primaryTypes.isEmpty() && !filter.primaryTypes.contains(group.primaryType))
        return false;

    if (!filter.secondaryTypes.isEmpty()) {
        bool matched = false;
        for (const QString &wanted : filter.secondaryTypes) {
            if (containsInsensitive(group.secondaryTypes, wanted)) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }

    if (filter.firstReleaseYearFrom.has_value()
        && (!group.firstReleaseYear.has_value()
            || *group.firstReleaseYear < *filter.firstReleaseYearFrom)) {
        return false;
    }

    if (filter.firstReleaseYearTo.has_value()
        && (!group.firstReleaseYear.has_value()
            || *group.firstReleaseYear > *filter.firstReleaseYearTo)) {
        return false;
    }

    return true;
}

bool validYearRange(const ReleaseGroupFilter &filter)
{
    return !filter.firstReleaseYearFrom.has_value()
        || !filter.firstReleaseYearTo.has_value()
        || *filter.firstReleaseYearFrom <= *filter.firstReleaseYearTo;
}

QString releaseGroupFilterKey(const ReleaseGroupFilter &filter)
{
    QStringList primary;
    for (ReleaseGroupPrimaryType type : filter.primaryTypes)
        primary.push_back(releaseGroupPrimaryTypeName(type));
    primary.sort(Qt::CaseInsensitive);

    QStringList secondary;
    for (const QString &value : filter.secondaryTypes)
        secondary.push_back(value.toCaseFolded());
    secondary.sort(Qt::CaseInsensitive);

    return QStringLiteral("p=%1;s=%2;yf=%3;yt=%4")
        .arg(primary.join(QLatin1Char(',')))
        .arg(secondary.join(QLatin1Char(',')))
        .arg(filter.firstReleaseYearFrom.value_or(-1))
        .arg(filter.firstReleaseYearTo.value_or(-1));
}

QString normalizedListKey(const QStringList &values)
{
    QStringList copy;
    for (const QString &value : values)
        copy.push_back(value.toCaseFolded());
    copy.sort(Qt::CaseInsensitive);
    return copy.join(QLatin1Char(','));
}

bool textMatches(const QString &needle, const QString &candidate)
{
    return candidate.contains(needle, Qt::CaseInsensitive);
}

} // namespace

FakeCatalogueClient::FakeCatalogueClient()
{
    const auto artistA = fixtureId<ArtistMbid>("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    const auto artistB = fixtureId<ArtistMbid>("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");

    m_artists = {
        Artist{
            artistA,
            QStringLiteral("宇多田ヒカル"),
            QStringLiteral("うただ ひかる"),
            QStringLiteral("Japanese singer-songwriter"),
            QVector<Alias>{
                Alias{QStringLiteral("Hikaru Utada"), QStringLiteral("Utada, Hikaru"),
                      QStringLiteral("en"), QStringLiteral("Latn"), true},
                Alias{QStringLiteral("Utada Hikaru"), QStringLiteral("Utada Hikaru"),
                      QStringLiteral("en"), QStringLiteral("Latn"), false},
            }},
        Artist{
            artistB,
            QStringLiteral("Blue Ensemble"),
            QStringLiteral("Blue Ensemble"),
            QString(),
            {}},
    };

    const QVector<ArtistCredit> creditA{
        ArtistCredit{artistA, QStringLiteral("宇多田ヒカル")}};
    const QVector<ArtistCredit> creditB{
        ArtistCredit{artistB, QStringLiteral("Blue Ensemble")}};

    const auto rg1 = fixtureId<ReleaseGroupMbid>("11111111-1111-4111-8111-111111111111");
    const auto rg2 = fixtureId<ReleaseGroupMbid>("22222222-2222-4222-8222-222222222222");
    const auto rg3 = fixtureId<ReleaseGroupMbid>("33333333-3333-4333-8333-333333333333");
    const auto rg4 = fixtureId<ReleaseGroupMbid>("44444444-4444-4444-8444-444444444444");

    m_releaseGroups = {
        ReleaseGroup{rg1, QStringLiteral("First Light"), {}, creditA,
                     ReleaseGroupPrimaryType::Album, {}, 1999},
        ReleaseGroup{rg2, QStringLiteral("青いアルバム"),
                     QVector<Alias>{Alias{QStringLiteral("Blue Album"),
                                          QStringLiteral("Blue Album"),
                                          QStringLiteral("en"),
                                          QStringLiteral("Latn"),
                                          true}},
                     creditA, ReleaseGroupPrimaryType::Album, {}, 2000},
        ReleaseGroup{rg3, QStringLiteral("Live Orbit"), {}, creditA,
                     ReleaseGroupPrimaryType::Album, {QStringLiteral("Live")}, 2001},
        ReleaseGroup{rg4, QStringLiteral("Future Echo"), {}, creditA,
                     ReleaseGroupPrimaryType::EP, {}, 2002},
    };

    const auto rel1 = fixtureId<ReleaseMbid>("51111111-1111-4111-8111-111111111111");
    const auto rel2 = fixtureId<ReleaseMbid>("52222222-2222-4222-8222-222222222222");

    m_releases = {
        Release{rel1, rg1, QStringLiteral("First Light"), creditA,
                QStringLiteral("official"), QStringLiteral("JP"),
                QDate(1999, 3, 10), QStringLiteral("4988000000001")},
        Release{rel2, rg1, QStringLiteral("First Light"), creditA,
                QStringLiteral("official"), QStringLiteral("US"),
                QDate(1999, 5, 4), QStringLiteral("000000000002")},
    };

    const auto rec1 = fixtureId<RecordingMbid>("61111111-1111-4111-8111-111111111111");
    const auto rec2 = fixtureId<RecordingMbid>("62222222-2222-4222-8222-222222222222");
    const auto rec3 = fixtureId<RecordingMbid>("63333333-3333-4333-8333-333333333333");

    m_recordings = {
        Recording{rec1, QStringLiteral("Blue Song"), creditB, 241000, {}},
        Recording{rec2, QStringLiteral("光"), creditA, 302000,
                  QVector<Alias>{Alias{QStringLiteral("Hikari"), QStringLiteral("Hikari"),
                                       QStringLiteral("en"), QStringLiteral("Latn"), true}}},
        Recording{rec3, QStringLiteral("Bonus Orbit"), creditA, 198000, {}},
    };

    const auto rt1 = fixtureId<ReleaseTrackMbid>("71111111-1111-4111-8111-111111111111");
    const auto rt2 = fixtureId<ReleaseTrackMbid>("72222222-2222-4222-8222-222222222222");
    const auto rt3 = fixtureId<ReleaseTrackMbid>("73333333-3333-4333-8333-333333333333");

    m_tracklists = {
        ReleaseTracklist{
            rel1,
            QVector<ReleaseMedium>{
                ReleaseMedium{
                    1, QStringLiteral("CD"), QStringLiteral("Main Disc"),
                    QVector<ReleaseTrack>{
                        ReleaseTrack{rt1, rec1, 1, QStringLiteral("Blue Song"), creditB, 241000},
                        ReleaseTrack{std::nullopt, rec2, 2, QStringLiteral("光"), creditA, 302000},
                    }},
                ReleaseMedium{
                    2, QStringLiteral("CD"), QStringLiteral("Bonus Disc"),
                    QVector<ReleaseTrack>{
                        ReleaseTrack{rt3, rec3, 1, QStringLiteral("Bonus Orbit"), creditA, 198000},
                    }},
            }},
        ReleaseTracklist{
            rel2,
            QVector<ReleaseMedium>{
                ReleaseMedium{
                    1, QStringLiteral("CD"), QStringLiteral("US Disc"),
                    QVector<ReleaseTrack>{
                        ReleaseTrack{rt2, rec2, 1, QStringLiteral("Hikari"), creditA, 302000},
                    }},
            }},
    };

    m_appearanceSets = {
        AppearanceSet{
            rec2,
            QVector<RecordingAppearance>{
                RecordingAppearance{rg1, rel1, std::nullopt, 1, 2, QStringLiteral("First Light")},
                RecordingAppearance{rg1, rel2, rt2, 1, 1, QStringLiteral("First Light")},
            }},
    };

    const auto work = fixtureId<WorkMbid>("81111111-1111-4111-8111-111111111111");
    m_relationshipSets = {
        RelationshipSet{
            rec2,
            QVector<EntityRelationship>{
                EntityRelationship{CanonicalEntityRef::from(work),
                                   QStringLiteral("performance_of")},
            }},
    };

    const auto oldRec2 = fixtureId<RecordingMbid>("69999999-9999-4999-8999-999999999999");
    const auto oldRg1 = fixtureId<ReleaseGroupMbid>("19999999-9999-4999-8999-999999999999");
    m_redirects = {
        {CanonicalEntityRef::from(oldRec2), CanonicalEntityRef::from(rec2)},
        {CanonicalEntityRef::from(oldRg1), CanonicalEntityRef::from(rg1)},
    };

    m_representativeReleases = {
        {rg1, rel1},
    };
}

CatalogueLookupResult<Artist, ArtistMbid> FakeCatalogueClient::artistById(
    const ArtistMbid &id) const
{
    if (!m_available)
        return lookupUnavailable<Artist, ArtistMbid>();
    for (const Artist &artist : m_artists) {
        if (artist.mbid == id)
            return CatalogueLookupResult<Artist, ArtistMbid>::found(artist);
    }
    const auto redirect = redirectTarget<ArtistMbid>(
        m_redirects, CanonicalEntityRef::from(id), CanonicalEntityKind::Artist);
    if (redirect.has_value())
        return CatalogueLookupResult<Artist, ArtistMbid>::redirect(*redirect);
    return lookupNotFound<Artist, ArtistMbid>(QStringLiteral("artistMbid"));
}

CatalogueLookupResult<ReleaseGroup, ReleaseGroupMbid>
FakeCatalogueClient::releaseGroupById(const ReleaseGroupMbid &id) const
{
    if (!m_available)
        return lookupUnavailable<ReleaseGroup, ReleaseGroupMbid>();
    for (const ReleaseGroup &group : m_releaseGroups) {
        if (group.mbid == id)
            return CatalogueLookupResult<ReleaseGroup, ReleaseGroupMbid>::found(group);
    }
    const auto redirect = redirectTarget<ReleaseGroupMbid>(
        m_redirects, CanonicalEntityRef::from(id), CanonicalEntityKind::ReleaseGroup);
    if (redirect.has_value())
        return CatalogueLookupResult<ReleaseGroup, ReleaseGroupMbid>::redirect(*redirect);
    return lookupNotFound<ReleaseGroup, ReleaseGroupMbid>(
        QStringLiteral("releaseGroupMbid"));
}

CatalogueLookupResult<Release, ReleaseMbid> FakeCatalogueClient::releaseById(
    const ReleaseMbid &id) const
{
    if (!m_available)
        return lookupUnavailable<Release, ReleaseMbid>();
    for (const Release &release : m_releases) {
        if (release.mbid == id)
            return CatalogueLookupResult<Release, ReleaseMbid>::found(release);
    }
    const auto redirect = redirectTarget<ReleaseMbid>(
        m_redirects, CanonicalEntityRef::from(id), CanonicalEntityKind::Release);
    if (redirect.has_value())
        return CatalogueLookupResult<Release, ReleaseMbid>::redirect(*redirect);
    return lookupNotFound<Release, ReleaseMbid>(QStringLiteral("releaseMbid"));
}

CatalogueLookupResult<Recording, RecordingMbid> FakeCatalogueClient::recordingById(
    const RecordingMbid &id) const
{
    if (!m_available)
        return lookupUnavailable<Recording, RecordingMbid>();
    for (const Recording &recording : m_recordings) {
        if (recording.mbid == id)
            return CatalogueLookupResult<Recording, RecordingMbid>::found(recording);
    }
    const auto redirect = redirectTarget<RecordingMbid>(
        m_redirects, CanonicalEntityRef::from(id), CanonicalEntityKind::Recording);
    if (redirect.has_value())
        return CatalogueLookupResult<Recording, RecordingMbid>::redirect(*redirect);
    return lookupNotFound<Recording, RecordingMbid>(QStringLiteral("recordingMbid"));
}

CatalogueResult<QVector<CanonicalBatchLookupItem>>
FakeCatalogueClient::lookupCanonicalBatch(
    const QVector<CanonicalEntityRef> &ids) const
{
    if (!m_available)
        return unavailable<QVector<CanonicalBatchLookupItem>>();

    QVector<CanonicalBatchLookupItem> out;
    out.reserve(ids.size());

    const auto appendFound = [&out](
                                 const CanonicalEntityRef &requested,
                                 LastKnownDisplaySnapshot snapshot) {
        out.push_back(CanonicalBatchLookupItem{
            requested,
            CanonicalBatchLookupState::Found,
            std::nullopt,
            std::optional<LastKnownDisplaySnapshot>{std::move(snapshot)}});
    };
    const auto appendRedirect = [&out](
                                    const CanonicalEntityRef &requested,
                                    CanonicalEntityRef to) {
        out.push_back(CanonicalBatchLookupItem{
            requested,
            CanonicalBatchLookupState::Redirected,
            std::optional<CanonicalEntityRef>{std::move(to)},
            std::nullopt});
    };
    const auto appendNotFound = [&out](const CanonicalEntityRef &requested) {
        out.push_back(CanonicalBatchLookupItem{
            requested,
            CanonicalBatchLookupState::NotFound,
            std::nullopt,
            std::nullopt});
    };

    for (const CanonicalEntityRef &requested : ids) {
        switch (requested.kind()) {
        case CanonicalEntityKind::Artist: {
            const auto parsed = ArtistMbid::parse(requested.mbidString());
            if (!parsed.ok())
                std::abort();
            const auto result = artistById(*parsed.value);
            if (result.ok())
                appendFound(requested, displaySnapshot(*result.value));
            else if (result.redirected())
                appendRedirect(requested, CanonicalEntityRef::from(*result.redirectedTo));
            else
                appendNotFound(requested);
            break;
        }
        case CanonicalEntityKind::ReleaseGroup: {
            const auto parsed = ReleaseGroupMbid::parse(requested.mbidString());
            if (!parsed.ok())
                std::abort();
            const auto result = releaseGroupById(*parsed.value);
            if (result.ok())
                appendFound(requested, displaySnapshot(*result.value));
            else if (result.redirected())
                appendRedirect(requested, CanonicalEntityRef::from(*result.redirectedTo));
            else
                appendNotFound(requested);
            break;
        }
        case CanonicalEntityKind::Release: {
            const auto parsed = ReleaseMbid::parse(requested.mbidString());
            if (!parsed.ok())
                std::abort();
            const auto result = releaseById(*parsed.value);
            if (result.ok())
                appendFound(requested, displaySnapshot(*result.value));
            else if (result.redirected())
                appendRedirect(requested, CanonicalEntityRef::from(*result.redirectedTo));
            else
                appendNotFound(requested);
            break;
        }
        case CanonicalEntityKind::Recording: {
            const auto parsed = RecordingMbid::parse(requested.mbidString());
            if (!parsed.ok())
                std::abort();
            const auto result = recordingById(*parsed.value);
            if (result.ok())
                appendFound(requested, displaySnapshot(*result.value));
            else if (result.redirected())
                appendRedirect(requested, CanonicalEntityRef::from(*result.redirectedTo));
            else
                appendNotFound(requested);
            break;
        }
        case CanonicalEntityKind::ReleaseTrack:
        case CanonicalEntityKind::Work:
            appendNotFound(requested);
            break;
        }
    }

    return CatalogueResult<QVector<CanonicalBatchLookupItem>>::success(
        std::move(out));
}

CatalogueLookupResult<Release, ReleaseGroupMbid>
FakeCatalogueClient::representativeRelease(
    const ReleaseGroupMbid &releaseGroup) const
{
    if (!m_available)
        return lookupUnavailable<Release, ReleaseGroupMbid>();

    const auto group = releaseGroupById(releaseGroup);
    if (group.redirected()) {
        return CatalogueLookupResult<Release, ReleaseGroupMbid>::redirect(
            *group.redirectedTo);
    }
    if (!group.ok()) {
        return CatalogueLookupResult<Release, ReleaseGroupMbid>::failure(
            group.error.code, group.error.field, group.error.detail);
    }

    for (const auto &mapping : m_representativeReleases) {
        if (mapping.first != releaseGroup)
            continue;
        for (const Release &release : m_releases) {
            if (release.mbid == mapping.second) {
                return CatalogueLookupResult<Release, ReleaseGroupMbid>::found(
                    release);
            }
        }
        break;
    }

    return CatalogueLookupResult<Release, ReleaseGroupMbid>::failure(
        CatalogueErrorCode::NotFound,
        QStringLiteral("representativeRelease"),
        QStringLiteral(
            "Catalogue has no canonical representative release for this release group."));
}

CatalogueResult<Page<ReleaseGroup>> FakeCatalogueClient::artistDiscography(
    const ArtistMbid &artist,
    const ArtistDiscographyQuery &query,
    const PageRequest &page) const
{
    if (!m_available)
        return unavailable<Page<ReleaseGroup>>();
    if (!artistById(artist).ok())
        return notFound<Page<ReleaseGroup>>(QStringLiteral("artistMbid"));
    if (!validYearRange(query.releaseGroups)) {
        return CatalogueResult<Page<ReleaseGroup>>::failure(
            CatalogueErrorCode::InvalidQuery,
            QStringLiteral("releaseGroups.firstReleaseYear"),
            QStringLiteral("Year range start must not exceed its end."));
    }

    QVector<ReleaseGroup> matches;
    for (const ReleaseGroup &group : m_releaseGroups) {
        bool credited = false;
        for (const ArtistCredit &credit : group.artistCredit) {
            if (credit.artistMbid == artist) {
                credited = true;
                break;
            }
        }
        if (credited && releaseGroupMatches(group, query.releaseGroups))
            matches.push_back(group);
    }

    std::sort(matches.begin(), matches.end(), [](const ReleaseGroup &a, const ReleaseGroup &b) {
        const int ay = a.firstReleaseYear.value_or(9999);
        const int by = b.firstReleaseYear.value_or(9999);
        if (ay != by)
            return ay < by;
        const int titleCmp = QString::compare(a.title, b.title, Qt::CaseInsensitive);
        if (titleCmp != 0)
            return titleCmp < 0;
        return a.mbid.toString() < b.mbid.toString();
    });

    const QString key = QStringLiteral("discography:%1:%2")
        .arg(artist.toString(), releaseGroupFilterKey(query.releaseGroups));
    return paginate(matches, key, page);
}

CatalogueResult<Page<Release>> FakeCatalogueClient::releaseGroupReleases(
    const ReleaseGroupMbid &releaseGroup,
    const ReleaseVersionQuery &query,
    const PageRequest &page) const
{
    if (!m_available)
        return unavailable<Page<Release>>();
    if (!releaseGroupById(releaseGroup).ok())
        return notFound<Page<Release>>(QStringLiteral("releaseGroupMbid"));

    QVector<Release> matches;
    for (const Release &release : m_releases) {
        if (release.releaseGroupMbid != releaseGroup)
            continue;
        if (!query.statuses.isEmpty() && !containsInsensitive(query.statuses, release.status))
            continue;
        if (!query.countries.isEmpty() && !containsInsensitive(query.countries, release.country))
            continue;
        matches.push_back(release);
    }

    std::sort(matches.begin(), matches.end(), [](const Release &a, const Release &b) {
        if (a.releaseDate != b.releaseDate)
            return a.releaseDate < b.releaseDate;
        return a.mbid.toString() < b.mbid.toString();
    });

    const QString key = QStringLiteral("versions:%1:s=%2:c=%3")
        .arg(releaseGroup.toString(),
             normalizedListKey(query.statuses),
             normalizedListKey(query.countries));
    return paginate(matches, key, page);
}

CatalogueResult<ReleaseTracklist> FakeCatalogueClient::releaseTracklist(
    const ReleaseMbid &release) const
{
    if (!m_available)
        return unavailable<ReleaseTracklist>();
    if (!releaseById(release).ok())
        return notFound<ReleaseTracklist>(QStringLiteral("releaseMbid"));
    for (const ReleaseTracklist &tracklist : m_tracklists) {
        if (tracklist.releaseMbid == release)
            return CatalogueResult<ReleaseTracklist>::success(tracklist);
    }
    return CatalogueResult<ReleaseTracklist>::success(ReleaseTracklist{release, {}});
}

CatalogueResult<Page<RecordingAppearance>> FakeCatalogueClient::recordingAppearances(
    const RecordingMbid &recording,
    const PageRequest &page) const
{
    if (!m_available)
        return unavailable<Page<RecordingAppearance>>();
    if (!recordingById(recording).ok())
        return notFound<Page<RecordingAppearance>>(QStringLiteral("recordingMbid"));

    QVector<RecordingAppearance> matches;
    for (const AppearanceSet &set : m_appearanceSets) {
        if (set.recordingMbid == recording) {
            matches = set.appearances;
            break;
        }
    }

    std::sort(matches.begin(), matches.end(), [](const RecordingAppearance &a,
                                                 const RecordingAppearance &b) {
        if (a.releaseMbid.toString() != b.releaseMbid.toString())
            return a.releaseMbid.toString() < b.releaseMbid.toString();
        if (a.mediumPosition != b.mediumPosition)
            return a.mediumPosition < b.mediumPosition;
        return a.trackPosition < b.trackPosition;
    });

    return paginate(matches,
                    QStringLiteral("appearances:%1").arg(recording.toString()),
                    page);
}

CatalogueResult<QVector<EntityRelationship>> FakeCatalogueClient::recordingRelationships(
    const RecordingMbid &recording) const
{
    if (!m_available)
        return unavailable<QVector<EntityRelationship>>();
    if (!recordingById(recording).ok())
        return notFound<QVector<EntityRelationship>>(QStringLiteral("recordingMbid"));

    for (const RelationshipSet &set : m_relationshipSets) {
        if (set.recordingMbid == recording)
            return CatalogueResult<QVector<EntityRelationship>>::success(set.relationships);
    }
    return CatalogueResult<QVector<EntityRelationship>>::success({});
}

CatalogueResult<Page<SearchHit>> FakeCatalogueClient::search(
    const SearchQuery &query,
    const PageRequest &page) const
{
    if (!m_available)
        return unavailable<Page<SearchHit>>();
    if (query.text.trimmed().isEmpty()) {
        return CatalogueResult<Page<SearchHit>>::failure(
            CatalogueErrorCode::InvalidQuery,
            QStringLiteral("search.text"),
            QStringLiteral("Search text must contain a non-whitespace term."));
    }
    if (!validYearRange(query.releaseGroups)) {
        return CatalogueResult<Page<SearchHit>>::failure(
            CatalogueErrorCode::InvalidQuery,
            QStringLiteral("releaseGroups.firstReleaseYear"),
            QStringLiteral("Year range start must not exceed its end."));
    }

    const QString needle = query.text.trimmed();
    const auto kindEnabled = [&query](SearchEntityKind kind) {
        return query.kinds.isEmpty() || query.kinds.contains(kind);
};
    QVector<SearchHit> hits;

    if (kindEnabled(SearchEntityKind::Artist)) {
        for (const Artist &artist : m_artists) {
            QString matched;
            if (textMatches(needle, artist.name))
                matched = artist.name;
            if (matched.isEmpty()) {
                for (const Alias &alias : artist.aliases) {
                    if (textMatches(needle, alias.name)) {
                        matched = alias.name;
                        break;
                    }
                }
            }
            if (!matched.isEmpty()) {
                hits.push_back(SearchHit{
                    SearchEntityKind::Artist,
                    CanonicalEntityRef::from(artist.mbid),
                    artist.name,
                    matched});
            }
        }
    }

    if (kindEnabled(SearchEntityKind::ReleaseGroup)) {
        for (const ReleaseGroup &group : m_releaseGroups) {
            if (!releaseGroupMatches(group, query.releaseGroups))
                continue;

            QString matched;
            if (textMatches(needle, group.title))
                matched = group.title;
            if (matched.isEmpty()) {
                for (const Alias &alias : group.aliases) {
                    if (textMatches(needle, alias.name)) {
                        matched = alias.name;
                        break;
                    }
                }
            }
            if (!matched.isEmpty()) {
                hits.push_back(SearchHit{
                    SearchEntityKind::ReleaseGroup,
                    CanonicalEntityRef::from(group.mbid),
                    group.title,
                    matched});
            }
        }
    }

    if (kindEnabled(SearchEntityKind::Recording)) {
        for (const Recording &recording : m_recordings) {
            QString matched;
            if (textMatches(needle, recording.title))
                matched = recording.title;
            if (matched.isEmpty()) {
                for (const Alias &alias : recording.aliases) {
                    if (textMatches(needle, alias.name)) {
                        matched = alias.name;
                        break;
                    }
                }
            }
            if (!matched.isEmpty()) {
                hits.push_back(SearchHit{
                    SearchEntityKind::Recording,
                    CanonicalEntityRef::from(recording.mbid),
                    recording.title,
                    matched});
            }
        }
    }

    std::sort(hits.begin(), hits.end(), [](const SearchHit &a, const SearchHit &b) {
        if (a.kind != b.kind)
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        const int textCmp = QString::compare(a.canonicalText, b.canonicalText,
                                             Qt::CaseInsensitive);
        if (textCmp != 0)
            return textCmp < 0;
        return a.entity.mbidString() < b.entity.mbidString();
    });

    QStringList kinds;
    for (SearchEntityKind kind : query.kinds)
        kinds.push_back(searchEntityKindName(kind));
    kinds.sort(Qt::CaseInsensitive);

    const QString key = QStringLiteral("search:%1:k=%2:%3")
        .arg(needle.toCaseFolded(),
             kinds.join(QLatin1Char(',')),
             releaseGroupFilterKey(query.releaseGroups));
    return paginate(hits, key, page);
}

} // namespace vinyl::catalogue
