// native/torrent/MangaTorrentDiscovery.cpp — see the header for the contract.
#include "torrent/MangaTorrentDiscovery.h"

#include "torrent/MangaNyaaSource.h" // queryVariants — the canonical-title family

namespace MangaTankoban {
namespace MangaTorrentDiscovery {

QStringList queryFamily(const SeriesSnapshot& series, const QString& volumeNumber, int cap)
{
    QStringList out;
    const auto add = [&out, cap](const QString& q) {
        if (out.size() >= cap)
            return;
        const QString t = q.simplified();
        if (!t.isEmpty() && !out.contains(t))
            out.append(t);
    };

    const bool hasDiscoveryProfile = !series.discoveryTitle.trimmed().isEmpty()
        || !series.discoveryAliases.isEmpty();
    const QString discoveryTitle = hasDiscoveryProfile ? series.discoveryTitle : series.title;
    const QStringList discoveryAliases = hasDiscoveryProfile ? series.discoveryAliases : series.aliases;

    // Canonical discovery title first. With no edition profile this remains
    // byte-identical to the pre-profile title family.
    const QStringList canonical = queryVariants(discoveryTitle, volumeNumber);
    for (const QString& q : canonical)
        add(q);

    // Then only the aliases belonging to the selected discovery profile.
    for (const QString& alias : discoveryAliases) {
        if (alias.trimmed().isEmpty())
            continue;
        const QStringList aliasVariants = queryVariants(alias, volumeNumber);
        for (const QString& q : aliasVariants)
            add(q);
        if (out.size() >= cap)
            break;
    }
    return out;
}

} // namespace MangaTorrentDiscovery
} // namespace MangaTankoban
