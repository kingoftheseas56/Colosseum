#pragma once

#include "VinylDomainTypes.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class LibraryErrorCode {
    None,
    UnsupportedCanonicalEntityKind,
    InvalidSavedAt,
    EntryNotFound,
    InvalidDisplaySnapshot,
};

struct LibraryError final {
    LibraryErrorCode code = LibraryErrorCode::None;
    QString field;
    QString detail;
};

QString libraryErrorCodeName(LibraryErrorCode code);

template <typename T>
struct LibraryResult final {
    std::optional<T> value;
    LibraryError error;

    bool ok() const noexcept { return value.has_value(); }

    static LibraryResult success(T value)
    {
        LibraryResult out;
        out.value = std::move(value);
        return out;
    }

    static LibraryResult failure(LibraryErrorCode code,
                                 QString field,
                                 QString detail = QString())
    {
        LibraryResult out;
        out.error = LibraryError{code, std::move(field), std::move(detail)};
        return out;
    }
};

struct LibraryMutationStatus final {
    bool ok = true;
    bool changed = false;
    LibraryError error;

    static LibraryMutationStatus success(bool changed = true)
    {
        LibraryMutationStatus status;
        status.changed = changed;
        return status;
    }

    static LibraryMutationStatus failure(LibraryErrorCode code,
                                         QString field,
                                         QString detail = QString())
    {
        LibraryMutationStatus status;
        status.ok = false;
        status.error = LibraryError{code, std::move(field), std::move(detail)};
        return status;
    }
};

struct PersonalDisplaySnapshot final {
    QString title;
    QString artistCredit;

    bool operator==(const PersonalDisplaySnapshot &other) const noexcept
    {
        return title == other.title && artistCredit == other.artistCredit;
    }
};

class LibraryEntry final
{
public:
    static LibraryResult<LibraryEntry> create(
        CanonicalEntityRef canonical,
        qint64 savedAtMs,
        std::optional<PersonalDisplaySnapshot> displaySnapshot = std::nullopt);

    const CanonicalEntityRef &canonical() const noexcept { return m_canonical; }
    qint64 savedAtMs() const noexcept { return m_savedAtMs; }
    const std::optional<PersonalDisplaySnapshot> &displaySnapshot() const noexcept
    {
        return m_displaySnapshot;
    }

private:
    LibraryEntry(CanonicalEntityRef canonical,
                 qint64 savedAtMs,
                 std::optional<PersonalDisplaySnapshot> displaySnapshot)
        : m_canonical(std::move(canonical))
        , m_savedAtMs(savedAtMs)
        , m_displaySnapshot(std::move(displaySnapshot))
    {
    }

    CanonicalEntityRef m_canonical;
    qint64 m_savedAtMs = 0;
    std::optional<PersonalDisplaySnapshot> m_displaySnapshot;
};

class VinylLibrary final
{
public:
    explicit VinylLibrary(ProfileScopeId profileId)
        : m_profileId(std::move(profileId))
    {
    }

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }

    int size() const noexcept { return m_entries.size(); }
    bool empty() const noexcept { return m_entries.isEmpty(); }
    const QVector<LibraryEntry> &items() const noexcept { return m_entries; }

    bool contains(const CanonicalEntityRef &canonical) const;
    const LibraryEntry *find(const CanonicalEntityRef &canonical) const;

    LibraryMutationStatus save(LibraryEntry entry);
    LibraryMutationStatus remove(const CanonicalEntityRef &canonical);

private:
    int indexOf(const CanonicalEntityRef &canonical) const;

    ProfileScopeId m_profileId;
    QVector<LibraryEntry> m_entries;
};

} // namespace vinyl
