#pragma once

#include "../ba0-domain-prototype/VinylDomainTypes.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl::catalogue {

enum class CatalogueErrorCode {
    None,
    NotFound,
    Unavailable,
    InvalidQuery,
    InvalidPage,
};

struct CatalogueError final {
    CatalogueErrorCode code = CatalogueErrorCode::None;
    QString field;
    QString detail;
};

QString catalogueErrorCodeName(CatalogueErrorCode code);

template <typename T>
struct CatalogueResult final {
    std::optional<T> value;
    CatalogueError error;

    bool ok() const noexcept { return value.has_value(); }

    static CatalogueResult success(T value)
    {
        CatalogueResult out;
        out.value = std::move(value);
        return out;
    }

    static CatalogueResult failure(CatalogueErrorCode code,
                                   QString field = QString(),
                                   QString detail = QString())
    {
        CatalogueResult out;
        out.error = CatalogueError{code, std::move(field), std::move(detail)};
        return out;
    }
};

template <typename T, typename Id>
struct CatalogueLookupResult final {
    std::optional<T> value;
    std::optional<Id> redirectedTo;
    CatalogueError error;

    bool ok() const noexcept { return value.has_value(); }
    bool redirected() const noexcept { return redirectedTo.has_value(); }
    bool failed() const noexcept { return !ok() && !redirected(); }

    static CatalogueLookupResult found(T value)
    {
        CatalogueLookupResult out;
        out.value = std::move(value);
        return out;
    }

    static CatalogueLookupResult redirect(Id to)
    {
        CatalogueLookupResult out;
        out.redirectedTo = std::move(to);
        return out;
    }

    static CatalogueLookupResult failure(CatalogueErrorCode code,
                                         QString field = QString(),
                                         QString detail = QString())
    {
        CatalogueLookupResult out;
        out.error = CatalogueError{code, std::move(field), std::move(detail)};
        return out;
    }
};

struct Alias final {
    QString name;
    QString sortName;
    QString locale;
    QString script;
    bool primary = false;
};

struct ArtistCredit final {
    ArtistMbid artistMbid;
    QString creditedName;
};

struct LastKnownDisplaySnapshot final {
    QString primaryText;
    QStringList artistCredit;
};

enum class CanonicalBatchLookupState {
    Found,
    Redirected,
    NotFound,
};

QString canonicalBatchLookupStateName(CanonicalBatchLookupState state);

struct CanonicalBatchLookupItem final {
    CanonicalEntityRef requested;
    CanonicalBatchLookupState state = CanonicalBatchLookupState::NotFound;
    std::optional<CanonicalEntityRef> redirectedTo;
    std::optional<LastKnownDisplaySnapshot> displaySnapshot;
};

struct Artist final {
    ArtistMbid mbid;
    QString name;
    QString sortName;
    QString disambiguation;
    QVector<Alias> aliases;
};

enum class ReleaseGroupPrimaryType {
    Album,
    Single,
    EP,
    Broadcast,
    Other,
};

QString releaseGroupPrimaryTypeName(ReleaseGroupPrimaryType type);

struct ReleaseGroup final {
    ReleaseGroupMbid mbid;
    QString title;
    QVector<Alias> aliases;
    QVector<ArtistCredit> artistCredit;
    ReleaseGroupPrimaryType primaryType = ReleaseGroupPrimaryType::Other;
    QStringList secondaryTypes;
    std::optional<int> firstReleaseYear;
};

struct Release final {
    ReleaseMbid mbid;
    ReleaseGroupMbid releaseGroupMbid;
    QString title;
    QVector<ArtistCredit> artistCredit;
    QString status;
    QString country;
    QDate releaseDate;
    QString barcode;
};

struct Recording final {
    RecordingMbid mbid;
    QString title;
    QVector<ArtistCredit> artistCredit;
    int lengthMs = 0;
    QVector<Alias> aliases;
};

struct ReleaseTrack final {
    std::optional<ReleaseTrackMbid> releaseTrackMbid;
    RecordingMbid recordingMbid;
    int position = 0;
    QString title;
    QVector<ArtistCredit> artistCredit;
    int lengthMs = 0;
};

struct ReleaseMedium final {
    int position = 0;
    QString format;
    QString title;
    QVector<ReleaseTrack> tracks;
};

struct ReleaseTracklist final {
    ReleaseMbid releaseMbid;
    QVector<ReleaseMedium> media;
};

struct RecordingAppearance final {
    ReleaseGroupMbid releaseGroupMbid;
    ReleaseMbid releaseMbid;
    std::optional<ReleaseTrackMbid> releaseTrackMbid;
    int mediumPosition = 0;
    int trackPosition = 0;
    QString releaseTitle;
};

struct EntityRelationship final {
    CanonicalEntityRef target;
    QString relationType;
};

class PageCursor final
{
public:
    explicit PageCursor(QString token)
        : m_token(std::move(token))
    {
    }

    const QString &token() const noexcept { return m_token; }

    bool operator==(const PageCursor &other) const noexcept
    {
        return m_token == other.m_token;
    }

private:
    QString m_token;
};

struct PageRequest final {
    int limit = 50;
    std::optional<PageCursor> cursor;
};

template <typename T>
struct Page final {
    QVector<T> items;
    std::optional<PageCursor> nextCursor;
};

struct ReleaseGroupFilter final {
    QVector<ReleaseGroupPrimaryType> primaryTypes;
    QStringList secondaryTypes;
    std::optional<int> firstReleaseYearFrom;
    std::optional<int> firstReleaseYearTo;
};

struct ArtistDiscographyQuery final {
    ReleaseGroupFilter releaseGroups;
};

struct ReleaseVersionQuery final {
    QStringList statuses;
    QStringList countries;
};

enum class SearchEntityKind {
    Artist,
    ReleaseGroup,
    Recording,
};

QString searchEntityKindName(SearchEntityKind kind);

struct SearchQuery final {
    QString text;
    QVector<SearchEntityKind> kinds;
    ReleaseGroupFilter releaseGroups;
};

struct SearchHit final {
    SearchEntityKind kind;
    CanonicalEntityRef entity;
    QString canonicalText;
    QString matchedText;
};

} // namespace vinyl::catalogue
