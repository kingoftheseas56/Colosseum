#include "VaultBrowseEmpty.h"

namespace VaultBrowseEmpty {

Cause classify(bool hasAnyRoots, bool levelHasRows, bool levelAway, bool filteredOut)
{
    if (!hasAnyRoots)
        return Cause::NoRoots;
    if (levelHasRows)
        return Cause::None;
    // Before AllAway on purpose: a filter the user set (and can clear) is the more actionable
    // explanation for an empty grid than the drive being away — the copy's own next step is
    // "clear the filter", and a filtered-empty level on an away root is still filtered.
    if (filteredOut)
        return Cause::Filtered;
    return levelAway ? Cause::AllAway : Cause::EmptyFolder;
}

QString causeName(Cause cause)
{
    switch (cause) {
    case Cause::NoRoots:
        return QStringLiteral("noRoots");
    case Cause::EmptyFolder:
        return QStringLiteral("emptyFolder");
    case Cause::AllAway:
        return QStringLiteral("allAway");
    case Cause::Filtered:
        return QStringLiteral("filtered");
    case Cause::None:
    default:
        return QStringLiteral("none");
    }
}

bool isLevelAway(bool indexSaysAway, bool hasOwnerRoot, bool ownerDirectoryExists)
{
    return indexSaysAway || (hasOwnerRoot && !ownerDirectoryExists);
}

} // namespace VaultBrowseEmpty
