#pragma once

#include "watchparty/WatchPartyTypes.h"

#include <QObject>
#include <QVariantMap>

namespace Colosseum::WatchParty {

enum class SourceEligibility {
    Unsupported,
    Torrent,
    Debrid
};

struct SourceInspection {
    SourceEligibility eligibility = SourceEligibility::Unsupported;
    SourceDescriptor descriptor;
    QString addonId;
    QString reason;

    bool eligible() const
    {
        return eligibility != SourceEligibility::Unsupported && descriptor.isValid();
    }
};

QString sourceEligibilityName(SourceEligibility eligibility);

// Player 1's narrow Watch Party source seam.
//
// The current app can positively prove torrent identity from infoHash + fileIdx.
// Generic direct URLs are deliberately unsupported here: current extension rows
// do not carry provider-safe debrid provenance, and URL/host/header/name guessing
// is forbidden by the approved Watch Party design.
//
// A future source/provider owner may call verifiedDebrid() only after it has
// verified provider portability/credential semantics and can supply a non-secret
// providerSourceId. There is intentionally no QML/direct-row inference path to it.
class SourceInspector final : public QObject
{
    Q_OBJECT

public:
    explicit SourceInspector(QObject* parent = nullptr);

    Q_INVOKABLE QVariantMap describeCandidate(const QVariantMap& candidate) const;

    static SourceInspection inspectCandidate(const QVariantMap& candidate);
    static SourceInspection verifiedDebrid(const QString& providerId,
                                           const QString& providerSourceId,
                                           const QString& addonId = QString());
    static QVariantMap toVariantMap(const SourceInspection& inspection);
};

} // namespace Colosseum::WatchParty
