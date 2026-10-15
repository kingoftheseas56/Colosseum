#include "watchparty/WatchPartySource.h"

#include <cmath>
#include <limits>

namespace Colosseum::WatchParty {
namespace {

QString cleanedAddonId(const QVariantMap& candidate)
{
    return candidate.value(QStringLiteral("addonId")).toString().trimmed().left(128);
}

QVariantMap descriptorToVariantMap(const SourceDescriptor& source)
{
    const SourceDescriptor descriptor = source.normalized();
    if (!descriptor.isValid())
        return {};

    QVariantMap result;
    result.insert(QStringLiteral("kind"), sourceKindName(descriptor.kind));
    if (descriptor.kind == SourceKind::Torrent) {
        result.insert(QStringLiteral("infoHash"), descriptor.infoHash);
        result.insert(QStringLiteral("fileIdx"), descriptor.fileIdx);
    } else if (descriptor.kind == SourceKind::Debrid) {
        result.insert(QStringLiteral("providerId"), descriptor.providerId);
        result.insert(QStringLiteral("providerSourceId"), descriptor.providerSourceId);
    }
    return result;
}

} // namespace

QString sourceEligibilityName(SourceEligibility eligibility)
{
    switch (eligibility) {
    case SourceEligibility::Unsupported:
        return QStringLiteral("unsupported");
    case SourceEligibility::Torrent:
        return QStringLiteral("torrent");
    case SourceEligibility::Debrid:
        return QStringLiteral("debrid");
    }
    return QStringLiteral("unsupported");
}

SourceInspector::SourceInspector(QObject* parent)
    : QObject(parent)
{
}

QVariantMap SourceInspector::describeCandidate(const QVariantMap& candidate) const
{
    return toVariantMap(inspectCandidate(candidate));
}

SourceInspection SourceInspector::inspectCandidate(const QVariantMap& candidate)
{
    SourceInspection inspection;
    inspection.addonId = cleanedAddonId(candidate);

    // Mirror PlayerPage.playStreamAt(): any explicit URL wins over an infoHash,
    // and the "url:" resume prefix is also a direct path. Current direct rows do
    // not carry provider-safe debrid provenance, so neither can become eligible.
    const QString explicitUrl = candidate.value(QStringLiteral("url")).toString();
    const QString infoHash = candidate.value(QStringLiteral("infoHash")).toString();
    if (!explicitUrl.isEmpty() || infoHash.startsWith(QStringLiteral("url:"))) {
        inspection.reason = QStringLiteral("direct_source_not_verified_debrid");
        return inspection;
    }

    bool fileIdxOk = true;
    double rawFileIdx = 0.0;
    if (candidate.contains(QStringLiteral("fileIdx"))) {
        rawFileIdx = candidate.value(QStringLiteral("fileIdx")).toDouble(&fileIdxOk);
        fileIdxOk = fileIdxOk
            && std::isfinite(rawFileIdx)
            && rawFileIdx >= 0.0
            && std::floor(rawFileIdx) == rawFileIdx
            && rawFileIdx <= std::numeric_limits<int>::max();
    }
    const int fileIdx = fileIdxOk ? static_cast<int>(rawFileIdx) : 0;

    const SourceDescriptor descriptor =
        SourceDescriptor::torrent(infoHash, fileIdx);
    if (!fileIdxOk || !descriptor.isValid()) {
        inspection.reason = QStringLiteral("invalid_torrent_identity");
        return inspection;
    }

    inspection.eligibility = SourceEligibility::Torrent;
    inspection.descriptor = descriptor;
    inspection.reason = QStringLiteral("eligible_torrent");
    return inspection;
}

SourceInspection SourceInspector::verifiedDebrid(const QString& providerId,
                                                 const QString& providerSourceId,
                                                 const QString& addonId)
{
    SourceInspection inspection;
    inspection.addonId = addonId.trimmed().left(128);
    inspection.descriptor = SourceDescriptor::debrid(providerId, providerSourceId);
    if (!inspection.descriptor.isValid()) {
        inspection.reason = QStringLiteral("invalid_debrid_descriptor");
        return inspection;
    }

    inspection.eligibility = SourceEligibility::Debrid;
    inspection.reason = QStringLiteral("eligible_verified_debrid");
    return inspection;
}

QVariantMap SourceInspector::toVariantMap(const SourceInspection& inspection)
{
    QVariantMap result;
    result.insert(QStringLiteral("eligible"), inspection.eligible());
    result.insert(QStringLiteral("eligibility"),
                  sourceEligibilityName(inspection.eligibility));
    result.insert(QStringLiteral("reason"), inspection.reason);
    result.insert(QStringLiteral("addonId"), inspection.addonId);
    result.insert(QStringLiteral("descriptor"),
                  descriptorToVariantMap(inspection.descriptor));
    return result;
}

} // namespace Colosseum::WatchParty
