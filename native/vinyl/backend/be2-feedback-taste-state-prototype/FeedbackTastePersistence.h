#pragma once

#include "FeedbackTasteState.h"

#include <QByteArray>
#include <optional>
#include <utility>

namespace vinyl {

enum class TastePersistenceErrorCode {
    None,
    EmptySnapshot,
    MalformedSnapshot,
    UnsupportedSchema,
    UnsupportedVersion,
    ProfileMismatch,
    InvalidState,
};

struct TastePersistenceError final {
    TastePersistenceErrorCode code = TastePersistenceErrorCode::None;
    QString field;
    QString detail;
};

QString tastePersistenceErrorCodeName(TastePersistenceErrorCode code);

template <typename T>
struct TastePersistenceResult final {
    std::optional<T> value;
    TastePersistenceError error;

    bool ok() const noexcept { return value.has_value(); }

    static TastePersistenceResult success(T value)
    {
        TastePersistenceResult out;
        out.value = std::move(value);
        return out;
    }

    static TastePersistenceResult failure(
        TastePersistenceErrorCode code,
        QString field,
        QString detail = QString())
    {
        TastePersistenceResult out;
        out.error = TastePersistenceError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

class FeedbackTastePersistence final
{
public:
    static constexpr int MinimumSupportedVersion = 1;
    static constexpr int SchemaVersion = 3;

    static QString schemaName();

    static TastePersistenceResult<QByteArray> capture(
        const TasteProfileState &state);

    static TastePersistenceResult<TasteProfileState> restore(
        const ProfileScopeId &expectedProfileId,
        const QByteArray &snapshot);
};

} // namespace vinyl
