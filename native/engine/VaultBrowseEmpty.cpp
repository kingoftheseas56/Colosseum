#include "VaultBrowseEmpty.h"

namespace VaultBrowseEmpty {

Cause classify(bool hasAnyRoots, bool levelHasRows, bool levelAway)
{
    if (!hasAnyRoots)
        return Cause::NoRoots;
    if (levelHasRows)
        return Cause::None;
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
