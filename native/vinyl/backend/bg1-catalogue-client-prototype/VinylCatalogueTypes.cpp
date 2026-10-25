#include "VinylCatalogueTypes.h"

namespace vinyl::catalogue {

QString catalogueErrorCodeName(CatalogueErrorCode code)
{
    switch (code) {
    case CatalogueErrorCode::None:
        return QStringLiteral("none");
    case CatalogueErrorCode::NotFound:
        return QStringLiteral("not_found");
    case CatalogueErrorCode::Unavailable:
        return QStringLiteral("unavailable");
    case CatalogueErrorCode::InvalidQuery:
        return QStringLiteral("invalid_query");
    case CatalogueErrorCode::InvalidPage:
        return QStringLiteral("invalid_page");
    }
    return QStringLiteral("unknown");
}

QString canonicalBatchLookupStateName(CanonicalBatchLookupState state)
{
    switch (state) {
    case CanonicalBatchLookupState::Found:
        return QStringLiteral("found");
    case CanonicalBatchLookupState::Redirected:
        return QStringLiteral("redirected");
    case CanonicalBatchLookupState::NotFound:
        return QStringLiteral("not_found");
    }
    return QStringLiteral("unknown");
}

QString releaseGroupPrimaryTypeName(ReleaseGroupPrimaryType type)
{
    switch (type) {
    case ReleaseGroupPrimaryType::Album:
        return QStringLiteral("album");
    case ReleaseGroupPrimaryType::Single:
        return QStringLiteral("single");
    case ReleaseGroupPrimaryType::EP:
        return QStringLiteral("ep");
    case ReleaseGroupPrimaryType::Broadcast:
        return QStringLiteral("broadcast");
    case ReleaseGroupPrimaryType::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

QString searchEntityKindName(SearchEntityKind kind)
{
    switch (kind) {
    case SearchEntityKind::Artist:
        return QStringLiteral("artist");
    case SearchEntityKind::ReleaseGroup:
        return QStringLiteral("release_group");
    case SearchEntityKind::Recording:
        return QStringLiteral("recording");
    }
    return QStringLiteral("unknown");
}

} // namespace vinyl::catalogue
