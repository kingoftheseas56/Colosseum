#pragma once
#include <QVariantMap>

// Pure conversion/validation seam. Mapping evidence is supplied by the existing
// catalogue owner; this class neither performs network work nor stores identity.
class VinylRecommendationIdentity final {
public:
    static QVariantMap fromCatalogue(const QVariantMap &item);
    // Direct catalogue selection, not a match against another recording/version.
    static QVariantMap fromProviderSong(const QVariantMap &item);
};
