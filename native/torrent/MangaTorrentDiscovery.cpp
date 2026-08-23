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

    // Canonical title first, verbatim through the existing query family so the
    // no-alias path stays byte-identical to pre-Arc-18 behavior.
    const QStringList canonical = queryVariants(series.title, volumeNumber);
    for (const QString& q : canonical)
        add(q);

    // Then aliases — discovery inputs now, not just validation needles.
    for (const QString& alias : series.aliases) {
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
