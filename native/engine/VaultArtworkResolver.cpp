#include "VaultArtworkResolver.h"

#include "VaultCacheKey.h"
#include "VaultPosterFetcher.h"
#include "VaultThumbnailer.h"

VaultArtworkResolver::VaultArtworkResolver(VaultThumbnailer* thumbnailer, VaultPosterFetcher* posterFetcher,
                                            QObject* parent)
    : QObject(parent), m_thumbnailer(thumbnailer), m_posterFetcher(posterFetcher)
{
    if (m_thumbnailer)
        connect(m_thumbnailer, &VaultThumbnailer::thumbReady, this, &VaultArtworkResolver::onThumbReady);
    if (m_posterFetcher)
        connect(m_posterFetcher, &VaultPosterFetcher::posterReady, this, &VaultArtworkResolver::onPosterReady);
}

QString VaultArtworkResolver::resolve(const RowFacts& facts)
{
    // Rung 1: locked pick — reserved. No source exists yet for a user-locked image
    // ref, so this rung is an explicit, permanent no-op this slice; a later slice
    // fills it in without disturbing the rungs below.

    // Rung 2: local artwork the row already carries (adopted local poster for a
    // video group, or the comic/book cover ref VaultLibrary already derived). This
    // class receives it as-is — it never re-derives cover refs itself.
    if (!facts.localRef.isEmpty())
        return facts.localRef;

    // Rung 3: canonical poster. Only meaningful once the row is identified (an
    // identityId) AND that identity carries a poster URL to fetch.
    if (m_posterFetcher && !facts.identityId.isEmpty() && !facts.posterUrl.isEmpty()) {
        const QString cached = m_posterFetcher->cachedPosterPath(facts.identityId);
        if (!cached.isEmpty())
            return cached;
        m_pendingPosterRowKeys[facts.identityId].insert(facts.rowKey);
        m_posterFetcher->requestPoster(facts.identityId, facts.posterUrl);
        // Miss: fall through to rung 4 rather than block on the async fetch.
    }

    // Rung 4: frame-grab. Video-only — a Folder/Show/Season/comic/book row has no
    // own video file to grab a frame from and simply never matches this gate.
    if (m_thumbnailer && facts.kind == QStringLiteral("video") && !facts.path.isEmpty()) {
        const QString cached = m_thumbnailer->cachedThumbPath(facts.path, facts.size, facts.mtimeMs);
        if (!cached.isEmpty())
            return cached;
        const QString key = VaultCacheKey::make(facts.path, facts.size, facts.mtimeMs);
        m_pendingThumbRowKeys[key].insert(facts.rowKey);
        m_thumbnailer->requestThumb(facts.path, facts.size, facts.mtimeMs, facts.durationSec);
    }

    // Rung 5: typographic — the caller paints its own fallback for "".
    return QString();
}

void VaultArtworkResolver::onPosterReady(const QString& identityId, const QString& /*filePath*/)
{
    const QSet<QString> rowKeys = m_pendingPosterRowKeys.take(identityId);
    for (const QString& rowKey : rowKeys)
        emit artResolved(rowKey);
}

void VaultArtworkResolver::onThumbReady(const QString& key, const QString& /*filePath*/)
{
    const QSet<QString> rowKeys = m_pendingThumbRowKeys.take(key);
    for (const QString& rowKey : rowKeys)
        emit artResolved(rowKey);
}
