#pragma once

#include "VinylLibrary.h"
#include "VinylPlaylist.h"

#include <QByteArray>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class PersonalStatePersistenceErrorCode {
    None,
    EmptySnapshot,
    MalformedSnapshot,
    UnsupportedSchema,
    UnsupportedVersion,
    ProfileMismatch,
    InvalidState,
};

struct PersonalStatePersistenceError final {
    PersonalStatePersistenceErrorCode code = PersonalStatePersistenceErrorCode::None;
    QString field;
    QString detail;
};

QString personalStatePersistenceErrorCodeName(PersonalStatePersistenceErrorCode code);
template <typename T>
struct PersonalStatePersistenceResult final {
    std::optional<T> value;
    PersonalStatePersistenceError error;

    bool ok() const noexcept { return value.has_value(); }

    static PersonalStatePersistenceResult success(T value)
    {
        PersonalStatePersistenceResult out;
        out.value = std::move(value);
        return out;
    }

    static PersonalStatePersistenceResult failure(
        PersonalStatePersistenceErrorCode code,
        QString field,
        QString detail = QString())
    {
        PersonalStatePersistenceResult out;
        out.error = PersonalStatePersistenceError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

struct RestoredVinylPersonalState final {
    VinylLibrary library;
    QVector<Playlist> playlists;

    RestoredVinylPersonalState(VinylLibrary libraryValue,
                               QVector<Playlist> playlistValues)
        : library(std::move(libraryValue))
        , playlists(std::move(playlistValues))
    {
    }
};
class VinylPersonalStatePersistence final
{
public:
    static constexpr int MinimumSupportedVersion = 1;
    static constexpr int SchemaVersion = 3;

    static QString schemaName();

    static PersonalStatePersistenceResult<QByteArray> capture(
        const ProfileScopeId &profileId,
        const VinylLibrary &library,
        const QVector<Playlist> &playlists);

    static PersonalStatePersistenceResult<RestoredVinylPersonalState> restore(
        const ProfileScopeId &expectedProfileId,
        const QByteArray &snapshot);
};

} // namespace vinyl
