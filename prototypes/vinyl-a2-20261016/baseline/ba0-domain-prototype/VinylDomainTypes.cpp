#include "VinylDomainTypes.h"

#include <QChar>

namespace vinyl {

QString domainErrorCodeName(DomainErrorCode code)
{
    switch (code) {
    case DomainErrorCode::None:
        return QStringLiteral("none");
    case DomainErrorCode::MissingCanonicalId:
        return QStringLiteral("missing_canonical_id");
    case DomainErrorCode::InvalidMbid:
        return QStringLiteral("invalid_mbid");
    case DomainErrorCode::InvalidProviderId:
        return QStringLiteral("invalid_provider_id");
    case DomainErrorCode::InvalidProviderItemId:
        return QStringLiteral("invalid_provider_item_id");
    case DomainErrorCode::InvalidTrackCoordinate:
        return QStringLiteral("invalid_track_coordinate");
    case DomainErrorCode::InvalidProfileScopeId:
        return QStringLiteral("invalid_profile_scope_id");
    case DomainErrorCode::InvalidPlayInstanceId:
        return QStringLiteral("invalid_play_instance_id");
    }
    return QStringLiteral("unknown");
}

namespace detail {

DomainResult<QUuid> parseMbidUuid(const QString &text, const QString &field)
{
    if (text.isEmpty()) {
        return DomainResult<QUuid>::failure(
            DomainErrorCode::MissingCanonicalId,
            field,
            QStringLiteral("MusicBrainz identity is required."));
    }

    if (text.trimmed() != text) {
        return DomainResult<QUuid>::failure(
            DomainErrorCode::InvalidMbid,
            field,
            QStringLiteral("Surrounding whitespace is not part of an MBID."));
    }

    const QUuid uuid = QUuid::fromString(text);
    if (uuid.isNull()) {
        return DomainResult<QUuid>::failure(
            DomainErrorCode::InvalidMbid,
            field,
            QStringLiteral("Expected a non-nil MusicBrainz UUID."));
    }

    return DomainResult<QUuid>::success(uuid);
}

} // namespace detail

namespace {

bool isValidOpaqueHostId(const QString &text)
{
    if (text.isEmpty() || text.size() > 128 || text.trimmed() != text)
        return false;

    for (const QChar ch : text) {
        if (ch.isNull() || ch.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

} // namespace

DomainResult<ProfileScopeId> ProfileScopeId::parse(const QString &text)
{
    if (!isValidOpaqueHostId(text)) {
        return DomainResult<ProfileScopeId>::failure(
            DomainErrorCode::InvalidProfileScopeId,
            QStringLiteral("profileScopeId"),
            QStringLiteral("Profile scope ID must be a non-empty bounded opaque host token."));
    }
    return DomainResult<ProfileScopeId>::success(ProfileScopeId(text));
}

DomainResult<PlayInstanceId> PlayInstanceId::parse(const QString &text)
{
    if (!isValidOpaqueHostId(text)) {
        return DomainResult<PlayInstanceId>::failure(
            DomainErrorCode::InvalidPlayInstanceId,
            QStringLiteral("playInstanceId"),
            QStringLiteral("Play instance ID must be a non-empty bounded opaque host token."));
    }
    return DomainResult<PlayInstanceId>::success(PlayInstanceId(text));
}

QString canonicalEntityKindName(CanonicalEntityKind kind)
{
    switch (kind) {
    case CanonicalEntityKind::Artist:
        return QStringLiteral("artist");
    case CanonicalEntityKind::ReleaseGroup:
        return QStringLiteral("release_group");
    case CanonicalEntityKind::Release:
        return QStringLiteral("release");
    case CanonicalEntityKind::ReleaseTrack:
        return QStringLiteral("release_track");
    case CanonicalEntityKind::Recording:
        return QStringLiteral("recording");
    case CanonicalEntityKind::Work:
        return QStringLiteral("work");
    }
    return QStringLiteral("unknown");
}

CanonicalEntityRef CanonicalEntityRef::from(const ArtistMbid &id)
{
    return CanonicalEntityRef(CanonicalEntityKind::Artist, id.uuid());
}

CanonicalEntityRef CanonicalEntityRef::from(const ReleaseGroupMbid &id)
{
    return CanonicalEntityRef(CanonicalEntityKind::ReleaseGroup, id.uuid());
}

CanonicalEntityRef CanonicalEntityRef::from(const ReleaseMbid &id)
{
    return CanonicalEntityRef(CanonicalEntityKind::Release, id.uuid());
}

CanonicalEntityRef CanonicalEntityRef::from(const ReleaseTrackMbid &id)
{
    return CanonicalEntityRef(CanonicalEntityKind::ReleaseTrack, id.uuid());
}

CanonicalEntityRef CanonicalEntityRef::from(const RecordingMbid &id)
{
    return CanonicalEntityRef(CanonicalEntityKind::Recording, id.uuid());
}

CanonicalEntityRef CanonicalEntityRef::from(const WorkMbid &id)
{
    return CanonicalEntityRef(CanonicalEntityKind::Work, id.uuid());
}

DomainResult<ProviderId> ProviderId::parse(const QString &text)
{
    if (text.isEmpty() || text.size() > 128) {
        return DomainResult<ProviderId>::failure(
            DomainErrorCode::InvalidProviderId,
            QStringLiteral("providerId"),
            QStringLiteral("Provider ID must contain 1..128 characters."));
    }

    const QString normalized = text.toLower();
    for (qsizetype i = 0; i < normalized.size(); ++i) {
        const QChar ch = normalized.at(i);
        const bool alpha = ch >= QLatin1Char('a') && ch <= QLatin1Char('z');
        const bool digit = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        const bool punctuation = ch == QLatin1Char('.')
            || ch == QLatin1Char('_')
            || ch == QLatin1Char(':')
            || ch == QLatin1Char('-');

        if ((!alpha && !digit && !punctuation)
            || (i == 0 && !alpha && !digit)) {
            return DomainResult<ProviderId>::failure(
                DomainErrorCode::InvalidProviderId,
                QStringLiteral("providerId"),
                QStringLiteral("Provider ID is not a safe machine token."));
        }
    }

    return DomainResult<ProviderId>::success(ProviderId(normalized));
}

DomainResult<ProviderItemId> ProviderItemId::parse(const QString &text)
{
    if (text.isEmpty() || text.size() > 1024 || text.trimmed() != text) {
        return DomainResult<ProviderItemId>::failure(
            DomainErrorCode::InvalidProviderItemId,
            QStringLiteral("providerItemId"),
            QStringLiteral("Provider item ID must be non-empty, bounded, and have no surrounding whitespace."));
    }

    for (const QChar ch : text) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) {
            return DomainResult<ProviderItemId>::failure(
                DomainErrorCode::InvalidProviderItemId,
                QStringLiteral("providerItemId"),
                QStringLiteral("Provider item ID must not contain control characters."));
        }
    }

    return DomainResult<ProviderItemId>::success(ProviderItemId(text));
}

DomainResult<ReleaseTrackCoordinate> ReleaseTrackCoordinate::create(
    ReleaseGroupMbid releaseGroup,
    ReleaseMbid release,
    std::optional<ReleaseTrackMbid> releaseTrack,
    RecordingMbid recording,
    int mediumPosition,
    int trackPosition)
{
    if (mediumPosition <= 0) {
        return DomainResult<ReleaseTrackCoordinate>::failure(
            DomainErrorCode::InvalidTrackCoordinate,
            QStringLiteral("mediumPosition"),
            QStringLiteral("MusicBrainz medium positions are one-based."));
    }

    if (trackPosition <= 0) {
        return DomainResult<ReleaseTrackCoordinate>::failure(
            DomainErrorCode::InvalidTrackCoordinate,
            QStringLiteral("trackPosition"),
            QStringLiteral("MusicBrainz track positions are one-based."));
    }

    return DomainResult<ReleaseTrackCoordinate>::success(
        ReleaseTrackCoordinate(std::move(releaseGroup),
                               std::move(release),
                               std::move(releaseTrack),
                               std::move(recording),
                               mediumPosition,
                               trackPosition));
}

} // namespace vinyl
