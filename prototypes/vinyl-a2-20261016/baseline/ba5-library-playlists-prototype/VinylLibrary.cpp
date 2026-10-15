#include "VinylLibrary.h"

#include <QChar>

namespace vinyl {

namespace {

bool isSupportedLibraryKind(CanonicalEntityKind kind)
{
    return kind == CanonicalEntityKind::Artist
        || kind == CanonicalEntityKind::ReleaseGroup
        || kind == CanonicalEntityKind::Recording;
}

} // namespace

QString libraryErrorCodeName(LibraryErrorCode code)
{
    switch (code) {
    case LibraryErrorCode::None:
        return QStringLiteral("none");
    case LibraryErrorCode::UnsupportedCanonicalEntityKind:
        return QStringLiteral("unsupported_canonical_entity_kind");
    case LibraryErrorCode::InvalidSavedAt:
        return QStringLiteral("invalid_saved_at");
    case LibraryErrorCode::EntryNotFound:
        return QStringLiteral("entry_not_found");
    }
    return QStringLiteral("unknown");
}

LibraryResult<LibraryEntry> LibraryEntry::create(
    CanonicalEntityRef canonical,
    qint64 savedAtMs,
    std::optional<PersonalDisplaySnapshot> displaySnapshot)
{
    if (!isSupportedLibraryKind(canonical.kind())) {
        return LibraryResult<LibraryEntry>::failure(
            LibraryErrorCode::UnsupportedCanonicalEntityKind,
            QStringLiteral("canonical"),
            QStringLiteral("Library supports Artist, Release Group, and Recording canonical entities."));
    }

    if (savedAtMs <= 0) {
        return LibraryResult<LibraryEntry>::failure(
            LibraryErrorCode::InvalidSavedAt,
            QStringLiteral("savedAtMs"),
            QStringLiteral("Library saved timestamp must be positive."));
    }

    return LibraryResult<LibraryEntry>::success(
        LibraryEntry(
            std::move(canonical), savedAtMs, std::move(displaySnapshot)));
}

bool VinylLibrary::contains(const CanonicalEntityRef &canonical) const
{
    return indexOf(canonical) >= 0;
}

const LibraryEntry *VinylLibrary::find(const CanonicalEntityRef &canonical) const
{
    const int index = indexOf(canonical);
    if (index < 0)
        return nullptr;
    return &m_entries.at(index);
}

LibraryMutationStatus VinylLibrary::save(LibraryEntry entry)
{
    if (contains(entry.canonical()))
        return LibraryMutationStatus::success(false);

    m_entries.append(std::move(entry));
    return LibraryMutationStatus::success(true);
}

LibraryMutationStatus VinylLibrary::remove(const CanonicalEntityRef &canonical)
{
    const int index = indexOf(canonical);
    if (index < 0) {
        return LibraryMutationStatus::failure(
            LibraryErrorCode::EntryNotFound,
            QStringLiteral("canonical"),
            QStringLiteral("Canonical entity is not saved in this profile."));
    }

    m_entries.removeAt(index);
    return LibraryMutationStatus::success(true);
}

int VinylLibrary::indexOf(const CanonicalEntityRef &canonical) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).canonical() == canonical)
            return i;
    }
    return -1;
}

} // namespace vinyl
