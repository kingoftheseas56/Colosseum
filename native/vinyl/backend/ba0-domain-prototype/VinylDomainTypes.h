#pragma once

#include <QString>
#include <QUuid>

#include <optional>
#include <type_traits>
#include <utility>

namespace vinyl {

enum class DomainErrorCode {
    None,
    MissingCanonicalId,
    InvalidMbid,
    InvalidProviderId,
    InvalidProviderItemId,
    InvalidTrackCoordinate,
    InvalidProfileScopeId,
    InvalidPlayInstanceId,
};

struct DomainError final {
    DomainErrorCode code = DomainErrorCode::None;
    QString field;
    QString detail;
};

QString domainErrorCodeName(DomainErrorCode code);

template <typename T>
struct DomainResult final {
    std::optional<T> value;
    DomainError error;

    bool ok() const noexcept { return value.has_value(); }

    static DomainResult success(T value)
    {
        DomainResult out;
        out.value = std::move(value);
        return out;
    }

    static DomainResult failure(DomainErrorCode code,
                                QString field,
                                QString detail = QString())
    {
        DomainResult out;
        out.error = DomainError{code, std::move(field), std::move(detail)};
        return out;
    }
};

namespace detail {
DomainResult<QUuid> parseMbidUuid(const QString &text, const QString &field);
}

template <typename Tag>
class Mbid final
{
public:
    static DomainResult<Mbid> parse(const QString &text)
    {
        return parse(text, QStringLiteral("mbid"));
    }

    static DomainResult<Mbid> parse(const QString &text, const QString &field)
    {
        auto parsed = detail::parseMbidUuid(text, field);
        if (!parsed.ok()) {
            return DomainResult<Mbid>::failure(parsed.error.code,
                                               parsed.error.field,
                                               parsed.error.detail);
        }
        return DomainResult<Mbid>::success(Mbid(*parsed.value));
    }

    const QUuid &uuid() const noexcept { return m_uuid; }

    QString toString() const
    {
        return m_uuid.toString(QUuid::WithoutBraces).toLower();
    }

    bool operator==(const Mbid &other) const noexcept
    {
        return m_uuid == other.m_uuid;
    }

    bool operator!=(const Mbid &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit Mbid(QUuid uuid)
        : m_uuid(std::move(uuid))
    {
    }

    QUuid m_uuid;
};

struct ArtistMbidTag;
struct ReleaseGroupMbidTag;
struct ReleaseMbidTag;
struct ReleaseTrackMbidTag;
struct RecordingMbidTag;
struct WorkMbidTag;

using ArtistMbid = Mbid<ArtistMbidTag>;
using ReleaseGroupMbid = Mbid<ReleaseGroupMbidTag>;
using ReleaseMbid = Mbid<ReleaseMbidTag>;
using ReleaseTrackMbid = Mbid<ReleaseTrackMbidTag>;
using RecordingMbid = Mbid<RecordingMbidTag>;
using WorkMbid = Mbid<WorkMbidTag>;

static_assert(!std::is_same<ArtistMbid, RecordingMbid>::value,
              "Artist and Recording MBIDs must remain distinct types");
static_assert(!std::is_same<ReleaseGroupMbid, ReleaseMbid>::value,
              "Release Group and Release MBIDs must remain distinct types");
static_assert(!std::is_same<ReleaseTrackMbid, RecordingMbid>::value,
              "Release Track and Recording MBIDs must remain distinct types");

class ProfileScopeId final
{
public:
    static DomainResult<ProfileScopeId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const ProfileScopeId &other) const noexcept
    {
        return m_value == other.m_value;
    }
    bool operator!=(const ProfileScopeId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit ProfileScopeId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class PlayInstanceId final
{
public:
    static DomainResult<PlayInstanceId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const PlayInstanceId &other) const noexcept
    {
        return m_value == other.m_value;
    }
    bool operator!=(const PlayInstanceId &other) const noexcept
    {
        return !(*this == other);
    }

private:
    explicit PlayInstanceId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

static_assert(!std::is_same<ProfileScopeId, PlayInstanceId>::value,
              "Profile and play-journey identity must remain distinct types");

enum class CanonicalEntityKind {
    Artist,
    ReleaseGroup,
    Release,
    ReleaseTrack,
    Recording,
    Work,
};

QString canonicalEntityKindName(CanonicalEntityKind kind);

class CanonicalEntityRef final
{
public:
    static CanonicalEntityRef from(const ArtistMbid &id);
    static CanonicalEntityRef from(const ReleaseGroupMbid &id);
    static CanonicalEntityRef from(const ReleaseMbid &id);
    static CanonicalEntityRef from(const ReleaseTrackMbid &id);
    static CanonicalEntityRef from(const RecordingMbid &id);
    static CanonicalEntityRef from(const WorkMbid &id);

    CanonicalEntityKind kind() const noexcept { return m_kind; }
    const QUuid &uuid() const noexcept { return m_uuid; }

    QString mbidString() const
    {
        return m_uuid.toString(QUuid::WithoutBraces).toLower();
    }

    bool operator==(const CanonicalEntityRef &other) const noexcept
    {
        return m_kind == other.m_kind && m_uuid == other.m_uuid;
    }

    bool operator!=(const CanonicalEntityRef &other) const noexcept
    {
        return !(*this == other);
    }

private:
    CanonicalEntityRef(CanonicalEntityKind kind, QUuid uuid)
        : m_kind(kind)
        , m_uuid(std::move(uuid))
    {
    }

    CanonicalEntityKind m_kind;
    QUuid m_uuid;
};

class ProviderId final
{
public:
    static DomainResult<ProviderId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const ProviderId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit ProviderId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class ProviderItemId final
{
public:
    static DomainResult<ProviderItemId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const ProviderItemId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit ProviderItemId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

class ProviderItemRef final
{
public:
    ProviderItemRef(ProviderId providerId, ProviderItemId itemId)
        : m_providerId(std::move(providerId))
        , m_itemId(std::move(itemId))
    {
    }

    const ProviderId &providerId() const noexcept { return m_providerId; }
    const ProviderItemId &itemId() const noexcept { return m_itemId; }

    bool operator==(const ProviderItemRef &other) const noexcept
    {
        return m_providerId == other.m_providerId && m_itemId == other.m_itemId;
    }

private:
    ProviderId m_providerId;
    ProviderItemId m_itemId;
};

class ProviderMapping final
{
public:
    ProviderMapping(CanonicalEntityRef canonical, ProviderItemRef providerItem)
        : m_canonical(std::move(canonical))
        , m_providerItem(std::move(providerItem))
    {
    }

    const CanonicalEntityRef &canonical() const noexcept { return m_canonical; }
    const ProviderItemRef &providerItem() const noexcept { return m_providerItem; }

private:
    CanonicalEntityRef m_canonical;
    ProviderItemRef m_providerItem;
};

class ReleaseTrackCoordinate final
{
public:
    static DomainResult<ReleaseTrackCoordinate> create(
        ReleaseGroupMbid releaseGroup,
        ReleaseMbid release,
        std::optional<ReleaseTrackMbid> releaseTrack,
        RecordingMbid recording,
        int mediumPosition,
        int trackPosition);

    const ReleaseGroupMbid &releaseGroupMbid() const noexcept { return m_releaseGroup; }
    const ReleaseMbid &releaseMbid() const noexcept { return m_release; }
    const std::optional<ReleaseTrackMbid> &releaseTrackMbid() const noexcept { return m_releaseTrack; }
    const RecordingMbid &recordingMbid() const noexcept { return m_recording; }
    int mediumPosition() const noexcept { return m_mediumPosition; }
    int trackPosition() const noexcept { return m_trackPosition; }

private:
    ReleaseTrackCoordinate(ReleaseGroupMbid releaseGroup,
                           ReleaseMbid release,
                           std::optional<ReleaseTrackMbid> releaseTrack,
                           RecordingMbid recording,
                           int mediumPosition,
                           int trackPosition)
        : m_releaseGroup(std::move(releaseGroup))
        , m_release(std::move(release))
        , m_releaseTrack(std::move(releaseTrack))
        , m_recording(std::move(recording))
        , m_mediumPosition(mediumPosition)
        , m_trackPosition(trackPosition)
    {
    }

    ReleaseGroupMbid m_releaseGroup;
    ReleaseMbid m_release;
    std::optional<ReleaseTrackMbid> m_releaseTrack;
    RecordingMbid m_recording;
    int m_mediumPosition = 0;
    int m_trackPosition = 0;
};

} // namespace vinyl
