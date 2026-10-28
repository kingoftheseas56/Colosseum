#include "VinylBoundaryValidation.h"
#include "VinylPersonalStatePersistence.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace vinyl {

namespace {

using ErrorCode = PersonalStatePersistenceErrorCode;

template <typename T>
PersonalStatePersistenceResult<T> malformed(QString field, QString detail)
{
    return PersonalStatePersistenceResult<T>::failure(
        ErrorCode::MalformedSnapshot, std::move(field), std::move(detail));
}

QString firstUnknownKey(const QJsonObject &object,
                        std::initializer_list<const char *> allowed)
{
    QSet<QString> allowedKeys;
    for (const char *key : allowed)
        allowedKeys.insert(QString::fromLatin1(key));
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key()))
            return it.key();
    }
    return QString();
}
PersonalStatePersistenceResult<qint64> positiveInteger(
    const QJsonValue &value, const QString &field)
{
    if (!value.isDouble())
        return malformed<qint64>(field, QStringLiteral("Expected an integer."));
    const qint64 number = value.toInteger(-1);
    if (number <= 0 || !std::isfinite(value.toDouble()) || std::floor(value.toDouble()) != value.toDouble()) {
        return malformed<qint64>(
            field, QStringLiteral("Expected a positive integer."));
    }
    return PersonalStatePersistenceResult<qint64>::success(
        number);
}

QJsonObject releaseContextToJson(const ReleaseTrackCoordinate &context)
{
    QJsonObject out;
    out.insert(QStringLiteral("releaseGroupMbid"),
               context.releaseGroupMbid().toString());
    out.insert(QStringLiteral("releaseMbid"), context.releaseMbid().toString());
    out.insert(QStringLiteral("releaseTrackMbid"),
               context.releaseTrackMbid().has_value()
                   ? QJsonValue(context.releaseTrackMbid()->toString())
                   : QJsonValue(QJsonValue::Null));
    out.insert(QStringLiteral("recordingMbid"), context.recordingMbid().toString());
    out.insert(QStringLiteral("mediumPosition"), context.mediumPosition());
    out.insert(QStringLiteral("trackPosition"), context.trackPosition());
    return out;
}
template <typename MbidType>
PersonalStatePersistenceResult<MbidType> requiredMbid(
    const QJsonObject &object, const QString &key, const QString &field)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
        return malformed<MbidType>(field, QStringLiteral("Expected an MBID string."));
    auto parsed = MbidType::parse(value.toString(), field);
    if (!parsed.ok())
        return malformed<MbidType>(field, QStringLiteral("Invalid persisted MBID."));
    return PersonalStatePersistenceResult<MbidType>::success(*parsed.value);
}

PersonalStatePersistenceResult<ReleaseTrackCoordinate> releaseContextFromJson(
    const QJsonObject &object, const QString &field)
{
    const QString unknown = firstUnknownKey(
        object, {"releaseGroupMbid", "releaseMbid", "releaseTrackMbid",
                 "recordingMbid", "mediumPosition", "trackPosition"});
    if (!unknown.isEmpty())
        return malformed<ReleaseTrackCoordinate>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown release-context field."));

    auto group = requiredMbid<ReleaseGroupMbid>(
        object, QStringLiteral("releaseGroupMbid"),
        field + QStringLiteral(".releaseGroupMbid"));
    if (!group.ok())
        return PersonalStatePersistenceResult<ReleaseTrackCoordinate>::failure(
            group.error.code, group.error.field, group.error.detail);
    auto release = requiredMbid<ReleaseMbid>(
        object, QStringLiteral("releaseMbid"),
        field + QStringLiteral(".releaseMbid"));
    if (!release.ok())
        return PersonalStatePersistenceResult<ReleaseTrackCoordinate>::failure(
            release.error.code, release.error.field, release.error.detail);
    std::optional<ReleaseTrackMbid> releaseTrack;
    const QJsonValue releaseTrackValue =
        object.value(QStringLiteral("releaseTrackMbid"));
    if (!releaseTrackValue.isNull()) {
        if (!releaseTrackValue.isString())
            return malformed<ReleaseTrackCoordinate>(
                field + QStringLiteral(".releaseTrackMbid"),
                QStringLiteral("Expected MBID string or null."));
        auto parsed = ReleaseTrackMbid::parse(
            releaseTrackValue.toString(),
            field + QStringLiteral(".releaseTrackMbid"));
        if (!parsed.ok())
            return malformed<ReleaseTrackCoordinate>(
                field + QStringLiteral(".releaseTrackMbid"),
                QStringLiteral("Invalid Release Track MBID."));
        releaseTrack = *parsed.value;
    }

    auto recording = requiredMbid<RecordingMbid>(
        object, QStringLiteral("recordingMbid"),
        field + QStringLiteral(".recordingMbid"));
    if (!recording.ok())
        return PersonalStatePersistenceResult<ReleaseTrackCoordinate>::failure(
            recording.error.code, recording.error.field, recording.error.detail);

    auto medium = positiveInteger(
        object.value(QStringLiteral("mediumPosition")),
        field + QStringLiteral(".mediumPosition"));
    auto track = positiveInteger(
        object.value(QStringLiteral("trackPosition")),
        field + QStringLiteral(".trackPosition"));
    if (!medium.ok() || !track.ok()
        || *medium.value > std::numeric_limits<int>::max()
        || *track.value > std::numeric_limits<int>::max())
        return malformed<ReleaseTrackCoordinate>(
            field, QStringLiteral("Invalid release track coordinates."));

    auto created = ReleaseTrackCoordinate::create(
        *group.value, *release.value, releaseTrack, *recording.value,
        static_cast<int>(*medium.value), static_cast<int>(*track.value));
    if (!created.ok())
        return malformed<ReleaseTrackCoordinate>(
            field, QStringLiteral("Release context violates BA0 invariants."));
    return PersonalStatePersistenceResult<ReleaseTrackCoordinate>::success(
        *created.value);
}
QJsonValue displaySnapshotToJson(
    const std::optional<PersonalDisplaySnapshot> &snapshot)
{
    if (!snapshot.has_value())
        return QJsonValue::Null;

    QJsonObject out;
    out.insert(QStringLiteral("title"), snapshot->title);
    out.insert(QStringLiteral("artistCredit"), snapshot->artistCredit);
    return out;
}

PersonalStatePersistenceResult<std::optional<PersonalDisplaySnapshot>>
displaySnapshotFromJson(const QJsonValue &value, const QString &field)
{
    if (value.isNull()) {
        return PersonalStatePersistenceResult<
            std::optional<PersonalDisplaySnapshot>>::success(std::nullopt);
    }
    if (!value.isObject()) {
        return malformed<std::optional<PersonalDisplaySnapshot>>(
            field, QStringLiteral("Display snapshot must be object or null."));
    }

    const QJsonObject object = value.toObject();
    const QString unknown =
        firstUnknownKey(object, {"title", "artistCredit"});
    if (!unknown.isEmpty()) {
        return malformed<std::optional<PersonalDisplaySnapshot>>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown display-snapshot field."));
    }

    const QJsonValue title = object.value(QStringLiteral("title"));
    const QJsonValue credit = object.value(QStringLiteral("artistCredit"));
    if (!title.isString() || !credit.isString()
        || !boundary::display(title.toString(), credit.toString())) {
        return malformed<std::optional<PersonalDisplaySnapshot>>(
            field,
            QStringLiteral("Display snapshot requires title and artistCredit strings."));
    }

    return PersonalStatePersistenceResult<
        std::optional<PersonalDisplaySnapshot>>::success(
            PersonalDisplaySnapshot{title.toString(), credit.toString()});
}

QJsonObject libraryEntryToJson(const LibraryEntry &entry)
{
    QJsonObject out;
    out.insert(QStringLiteral("kind"),
               canonicalEntityKindName(entry.canonical().kind()));
    out.insert(QStringLiteral("mbid"), entry.canonical().mbidString());
    out.insert(QStringLiteral("savedAtMs"), entry.savedAtMs());
    out.insert(QStringLiteral("displaySnapshot"),
               displaySnapshotToJson(entry.displaySnapshot()));
    return out;
}

PersonalStatePersistenceResult<CanonicalEntityRef> libraryCanonicalFromJson(
    const QJsonObject &object, const QString &field)
{
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    const QJsonValue mbidValue = object.value(QStringLiteral("mbid"));
    if (!kindValue.isString() || !mbidValue.isString())
        return malformed<CanonicalEntityRef>(
            field, QStringLiteral("Library entry requires kind and MBID strings."));

    const QString kind = kindValue.toString();
    const QString mbid = mbidValue.toString();
    if (kind == QStringLiteral("artist")) {
        auto id = ArtistMbid::parse(mbid, field + QStringLiteral(".mbid"));
        if (id.ok())
            return PersonalStatePersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*id.value));
    } else if (kind == QStringLiteral("release_group")) {
        auto id = ReleaseGroupMbid::parse(mbid, field + QStringLiteral(".mbid"));
        if (id.ok())
            return PersonalStatePersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*id.value));
    } else if (kind == QStringLiteral("recording")) {
        auto id = RecordingMbid::parse(mbid, field + QStringLiteral(".mbid"));
        if (id.ok())
            return PersonalStatePersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*id.value));
    }
    return malformed<CanonicalEntityRef>(
        field, QStringLiteral("Unsupported or invalid Library canonical identity."));
}
QJsonObject playlistItemToJson(const PlaylistItem &item)
{
    QJsonObject out;
    out.insert(QStringLiteral("playlistItemId"), item.itemId().value());
    out.insert(QStringLiteral("recordingMbid"), item.recordingMbid().toString());
    out.insert(QStringLiteral("addedAtMs"), item.addedAtMs());
    if (item.releaseContext().has_value())
        out.insert(QStringLiteral("releaseContext"),
                   releaseContextToJson(*item.releaseContext()));
    else
        out.insert(QStringLiteral("releaseContext"), QJsonValue::Null);
    out.insert(QStringLiteral("displaySnapshot"),
               displaySnapshotToJson(item.displaySnapshot()));
    return out;
}

QJsonObject playlistToJson(const Playlist &playlist)
{
    QJsonArray items;
    for (const PlaylistItem &item : playlist.items())
        items.append(playlistItemToJson(item));

    QJsonObject out;
    out.insert(QStringLiteral("playlistId"), playlist.playlistId().value());
    out.insert(QStringLiteral("name"), playlist.name());
    out.insert(QStringLiteral("createdAtMs"), playlist.createdAtMs());
    out.insert(QStringLiteral("provenance"),
               playlistProvenanceKindName(playlist.provenance()));
    out.insert(QStringLiteral("items"), items);
    return out;
}

std::optional<PlaylistProvenanceKind> provenanceFromName(const QString &name)
{
    if (name == QStringLiteral("manual"))
        return PlaylistProvenanceKind::Manual;
    if (name == QStringLiteral("generated"))
        return PlaylistProvenanceKind::Generated;
    return std::nullopt;
}

PersonalStatePersistenceResult<PlaylistItem> playlistItemFromJson(
    const QJsonObject &object, const QString &field, int version)
{
    const QString unknown = version >= 3
        ? firstUnknownKey(
              object,
              {"playlistItemId", "recordingMbid", "addedAtMs",
               "releaseContext", "displaySnapshot"})
        : firstUnknownKey(
              object,
              {"playlistItemId", "recordingMbid", "addedAtMs",
               "releaseContext"});
    if (!unknown.isEmpty())
        return malformed<PlaylistItem>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown playlist-item field."));

    const QJsonValue idValue = object.value(QStringLiteral("playlistItemId"));
    if (!idValue.isString())
        return malformed<PlaylistItem>(
            field + QStringLiteral(".playlistItemId"),
            QStringLiteral("Playlist item ID must be a string."));
    auto id = PlaylistItemId::parse(idValue.toString());
    if (!id.ok())
        return malformed<PlaylistItem>(
            field + QStringLiteral(".playlistItemId"),
            QStringLiteral("Invalid playlist item ID."));

    auto recording = requiredMbid<RecordingMbid>(
        object, QStringLiteral("recordingMbid"),
        field + QStringLiteral(".recordingMbid"));
    if (!recording.ok())
        return PersonalStatePersistenceResult<PlaylistItem>::failure(
            recording.error.code, recording.error.field, recording.error.detail);

    auto added = positiveInteger(
        object.value(QStringLiteral("addedAtMs")),
        field + QStringLiteral(".addedAtMs"));
    if (!added.ok())
        return PersonalStatePersistenceResult<PlaylistItem>::failure(
            added.error.code, added.error.field, added.error.detail);

    std::optional<ReleaseTrackCoordinate> releaseContext;
    const QJsonValue releaseValue = object.value(QStringLiteral("releaseContext"));
    if (!releaseValue.isNull()) {
        if (!releaseValue.isObject())
            return malformed<PlaylistItem>(
                field + QStringLiteral(".releaseContext"),
                QStringLiteral("Release context must be object or null."));
        auto parsed = releaseContextFromJson(
            releaseValue.toObject(), field + QStringLiteral(".releaseContext"));
        if (!parsed.ok())
            return PersonalStatePersistenceResult<PlaylistItem>::failure(
                parsed.error.code, parsed.error.field, parsed.error.detail);
        releaseContext = *parsed.value;
    }

    std::optional<PersonalDisplaySnapshot> displaySnapshot;
    if (version >= 3) {
        auto parsedDisplay = displaySnapshotFromJson(
            object.value(QStringLiteral("displaySnapshot")),
            field + QStringLiteral(".displaySnapshot"));
        if (!parsedDisplay.ok()) {
            return PersonalStatePersistenceResult<PlaylistItem>::failure(
                parsedDisplay.error.code,
                parsedDisplay.error.field,
                parsedDisplay.error.detail);
        }
        displaySnapshot = std::move(*parsedDisplay.value);
    }

    auto item = PlaylistItem::create(
        *id.value,
        *recording.value,
        *added.value,
        std::move(releaseContext),
        std::move(displaySnapshot));
    if (!item.ok())
        return malformed<PlaylistItem>(
            field, QStringLiteral("Playlist item violates BA5 invariants."));
    return PersonalStatePersistenceResult<PlaylistItem>::success(*item.value);
}

PersonalStatePersistenceResult<Playlist> playlistFromJson(
    const ProfileScopeId &profileId,
    const QJsonObject &object,
    const QString &field,
    int version)
{
    const QString unknown = firstUnknownKey(
        object, {"playlistId", "name", "createdAtMs", "provenance", "items"});
    if (!unknown.isEmpty())
        return malformed<Playlist>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown playlist field."));

    const QJsonValue idValue = object.value(QStringLiteral("playlistId"));
    const QJsonValue nameValue = object.value(QStringLiteral("name"));
    const QJsonValue provenanceValue = object.value(QStringLiteral("provenance"));
    const QJsonValue itemsValue = object.value(QStringLiteral("items"));
    if (!idValue.isString() || !nameValue.isString()
        || !provenanceValue.isString() || !itemsValue.isArray()
        || itemsValue.toArray().size() > boundary::MaxQueueItems)
        return malformed<Playlist>(
            field, QStringLiteral("Playlist has malformed required fields."));

    auto id = PlaylistId::parse(idValue.toString());
    auto created = positiveInteger(
        object.value(QStringLiteral("createdAtMs")),
        field + QStringLiteral(".createdAtMs"));
    const auto provenance = provenanceFromName(provenanceValue.toString());

    if (!id.ok() || !created.ok() || !provenance.has_value())
        return malformed<Playlist>(
            field, QStringLiteral("Playlist identity, time, or provenance is invalid."));

    auto playlist = Playlist::create(
        profileId, *id.value, nameValue.toString(), *created.value, *provenance);
    if (!playlist.ok())
        return malformed<Playlist>(
            field, QStringLiteral("Playlist metadata violates BA5 invariants."));

    int index = 0;
    for (const QJsonValue &value : itemsValue.toArray()) {
        const QString itemField = field + QStringLiteral(".items[")
            + QString::number(index) + QLatin1Char(']');
        if (!value.isObject())
            return malformed<Playlist>(
                itemField, QStringLiteral("Playlist item must be an object."));
        auto item = playlistItemFromJson(value.toObject(), itemField, version);
        if (!item.ok())
            return PersonalStatePersistenceResult<Playlist>::failure(
                item.error.code, item.error.field, item.error.detail);
        auto status = playlist.value->append(*item.value);
        if (!status.ok)
            return malformed<Playlist>(
                itemField, QStringLiteral("Playlist item violates ordered-state invariants."));
        ++index;
    }
    return PersonalStatePersistenceResult<Playlist>::success(*playlist.value);
}

} // namespace

QString personalStatePersistenceErrorCodeName(
    PersonalStatePersistenceErrorCode code)
{
    switch (code) {
    case ErrorCode::None: return QStringLiteral("none");
    case ErrorCode::EmptySnapshot: return QStringLiteral("empty_snapshot");
    case ErrorCode::MalformedSnapshot: return QStringLiteral("malformed_snapshot");
    case ErrorCode::UnsupportedSchema: return QStringLiteral("unsupported_schema");
    case ErrorCode::UnsupportedVersion: return QStringLiteral("unsupported_version");
    case ErrorCode::ProfileMismatch: return QStringLiteral("profile_mismatch");
    case ErrorCode::InvalidState: return QStringLiteral("invalid_state");
    }
    return QStringLiteral("unknown");
}

QString VinylPersonalStatePersistence::schemaName()
{
    return QStringLiteral("colosseum.vinyl.personal-state");
}

PersonalStatePersistenceResult<QByteArray> VinylPersonalStatePersistence::capture(
    const ProfileScopeId &profileId,
    const VinylLibrary &library,
    const QVector<Playlist> &playlists)
{

    if (library.profileId() != profileId)
        return PersonalStatePersistenceResult<QByteArray>::failure(
            ErrorCode::ProfileMismatch, QStringLiteral("library.profileScopeId"));

    if (library.size() > boundary::MaxQueueItems || playlists.size() > boundary::MaxQueueItems)
        return PersonalStatePersistenceResult<QByteArray>::failure(ErrorCode::InvalidState, QStringLiteral("snapshot"));
    QVector<LibraryEntry> canonicalLibrary = library.items();
    std::sort(canonicalLibrary.begin(), canonicalLibrary.end(),
              [](const LibraryEntry &left, const LibraryEntry &right) {
        const QString leftKey = canonicalEntityKindName(left.canonical().kind())
            + QLatin1Char(':') + left.canonical().mbidString();
        const QString rightKey = canonicalEntityKindName(right.canonical().kind())
            + QLatin1Char(':') + right.canonical().mbidString();
        return leftKey < rightKey;
    });

    QJsonArray libraryArray;
    for (const LibraryEntry &entry : canonicalLibrary)
        libraryArray.append(libraryEntryToJson(entry));

    QVector<Playlist> canonicalPlaylists = playlists;
    std::sort(canonicalPlaylists.begin(), canonicalPlaylists.end(),
              [](const Playlist &left, const Playlist &right) {
        return left.playlistId().value() < right.playlistId().value();
    });

    QSet<QString> playlistIds;
    QJsonArray playlistArray;
    for (const Playlist &playlist : canonicalPlaylists) {
        if (playlist.size() > boundary::MaxQueueItems)
            return PersonalStatePersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidState, QStringLiteral("playlists.items"));
        if (playlist.profileId() != profileId)
            return PersonalStatePersistenceResult<QByteArray>::failure(
                ErrorCode::ProfileMismatch, QStringLiteral("playlists.profileScopeId"));
        const QString id = playlist.playlistId().value();
        if (playlistIds.contains(id))
            return PersonalStatePersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidState, QStringLiteral("playlists.playlistId"),
                QStringLiteral("Playlist IDs must be unique inside one profile snapshot."));
        playlistIds.insert(id);
        playlistArray.append(playlistToJson(playlist));
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("version"), SchemaVersion);
    root.insert(QStringLiteral("profileScopeId"), profileId.value());
    root.insert(QStringLiteral("library"), libraryArray);
    root.insert(QStringLiteral("playlists"), playlistArray);

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > boundary::MaxSnapshotBytes)
        return PersonalStatePersistenceResult<QByteArray>::failure(ErrorCode::InvalidState, QStringLiteral("snapshot"));
    return PersonalStatePersistenceResult<QByteArray>::success(bytes);
}

PersonalStatePersistenceResult<RestoredVinylPersonalState>
VinylPersonalStatePersistence::restore(
    const ProfileScopeId &expectedProfileId,
    const QByteArray &snapshot)
{
    if (snapshot.size() > boundary::MaxSnapshotBytes)
        return malformed<RestoredVinylPersonalState>(QStringLiteral("snapshot"), QStringLiteral("Snapshot exceeds byte budget."));
    if (snapshot.trimmed().isEmpty())
        return PersonalStatePersistenceResult<RestoredVinylPersonalState>::failure(
            ErrorCode::EmptySnapshot, QStringLiteral("snapshot"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(snapshot, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return malformed<RestoredVinylPersonalState>(
            QStringLiteral("snapshot"), QStringLiteral("Malformed JSON snapshot."));

    const QJsonObject root = document.object();
    const QString unknown = firstUnknownKey(
        root, {"schema", "version", "profileScopeId", "library", "playlists"});
    if (!unknown.isEmpty())
        return malformed<RestoredVinylPersonalState>(
            unknown, QStringLiteral("Unknown root field."));

    if (root.value(QStringLiteral("schema")).toString() != schemaName())
        return PersonalStatePersistenceResult<RestoredVinylPersonalState>::failure(
            ErrorCode::UnsupportedSchema, QStringLiteral("schema"));

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble()
        || std::floor(versionValue.toDouble()) != versionValue.toDouble()) {
        return PersonalStatePersistenceResult<RestoredVinylPersonalState>::failure(
            ErrorCode::UnsupportedVersion, QStringLiteral("version"));
    }
    const int version = versionValue.toInt(-1);
    if (version < MinimumSupportedVersion || version > SchemaVersion) {
        return PersonalStatePersistenceResult<RestoredVinylPersonalState>::failure(
            ErrorCode::UnsupportedVersion, QStringLiteral("version"));
    }

    const QJsonValue profileValue = root.value(QStringLiteral("profileScopeId"));
    if (!profileValue.isString())
        return malformed<RestoredVinylPersonalState>(
            QStringLiteral("profileScopeId"), QStringLiteral("Expected profile string."));
    auto profile = ProfileScopeId::parse(profileValue.toString());
    if (!profile.ok())
        return malformed<RestoredVinylPersonalState>(
            QStringLiteral("profileScopeId"), QStringLiteral("Invalid profile scope."));
    if (*profile.value != expectedProfileId)
        return PersonalStatePersistenceResult<RestoredVinylPersonalState>::failure(
            ErrorCode::ProfileMismatch, QStringLiteral("profileScopeId"));

    const QJsonValue libraryValue = root.value(QStringLiteral("library"));
    const QJsonValue playlistsValue = root.value(QStringLiteral("playlists"));
    if (!libraryValue.isArray() || !playlistsValue.isArray()
        || libraryValue.toArray().size() > boundary::MaxQueueItems
        || playlistsValue.toArray().size() > boundary::MaxQueueItems)
        return malformed<RestoredVinylPersonalState>(
            QStringLiteral("snapshot"),
            QStringLiteral("Library and playlists must be arrays."));

    VinylLibrary library(expectedProfileId);
    int libraryIndex = 0;

    for (const QJsonValue &value : libraryValue.toArray()) {
        const QString field = QStringLiteral("library[")
            + QString::number(libraryIndex) + QLatin1Char(']');
        if (!value.isObject())
            return malformed<RestoredVinylPersonalState>(
                field, QStringLiteral("Library entry must be an object."));
        const QJsonObject object = value.toObject();
        const QString entryUnknown = version >= 3
            ? firstUnknownKey(
                  object, {"kind", "mbid", "savedAtMs", "displaySnapshot"})
            : firstUnknownKey(object, {"kind", "mbid", "savedAtMs"});
        if (!entryUnknown.isEmpty())
            return malformed<RestoredVinylPersonalState>(
                field + QLatin1Char('.') + entryUnknown,
                QStringLiteral("Unknown Library entry field."));

        auto canonical = libraryCanonicalFromJson(object, field);
        auto saved = positiveInteger(
            object.value(QStringLiteral("savedAtMs")),
            field + QStringLiteral(".savedAtMs"));
        if (!canonical.ok() || !saved.ok())
            return malformed<RestoredVinylPersonalState>(
                field, QStringLiteral("Invalid Library entry."));
        std::optional<PersonalDisplaySnapshot> displaySnapshot;
        if (version >= 3) {
            auto parsedDisplay = displaySnapshotFromJson(
                object.value(QStringLiteral("displaySnapshot")),
                field + QStringLiteral(".displaySnapshot"));
            if (!parsedDisplay.ok()) {
                return PersonalStatePersistenceResult<
                    RestoredVinylPersonalState>::failure(
                        parsedDisplay.error.code,
                        parsedDisplay.error.field,
                        parsedDisplay.error.detail);
            }
            displaySnapshot = std::move(*parsedDisplay.value);
        }
        auto entry = LibraryEntry::create(
            *canonical.value, *saved.value, std::move(displaySnapshot));
        if (!entry.ok() || library.contains(*canonical.value) || !library.save(*entry.value).ok)
            return malformed<RestoredVinylPersonalState>(
                field, QStringLiteral("Library entry violates BA5 invariants."));
        ++libraryIndex;
    }

    QVector<Playlist> playlists;
    QSet<QString> ids;
    int playlistIndex = 0;
    for (const QJsonValue &value : playlistsValue.toArray()) {
        const QString field = QStringLiteral("playlists[")
            + QString::number(playlistIndex) + QLatin1Char(']');
        if (!value.isObject())
            return malformed<RestoredVinylPersonalState>(
                field, QStringLiteral("Playlist must be an object."));
        auto playlist = playlistFromJson(
            expectedProfileId, value.toObject(), field, version);
        if (!playlist.ok())
            return PersonalStatePersistenceResult<RestoredVinylPersonalState>::failure(
                playlist.error.code, playlist.error.field, playlist.error.detail);

        const QString id = playlist.value->playlistId().value();
        if (ids.contains(id))
            return malformed<RestoredVinylPersonalState>(
                field + QStringLiteral(".playlistId"),
                QStringLiteral("Duplicate playlist ID."));
        ids.insert(id);
        playlists.append(*playlist.value);
        ++playlistIndex;
    }

    return PersonalStatePersistenceResult<RestoredVinylPersonalState>::success(
        RestoredVinylPersonalState(std::move(library), std::move(playlists)));
}

} // namespace vinyl
