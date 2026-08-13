#pragma once
// VaultCacheKey — the one (normalizedPath, size, mtimeMs) -> QString key derivation
// shared by every Vault on-disk cache that is keyed by file identity: VaultEnricher's
// video duration cache (durations.json, VaultEnricher.cpp) and VaultThumbnailer's
// frame-grab cache (Vault browse-artwork execution plan, Slice 1, 2026-08-13). A file
// replaced or re-encoded at the same path changes size and/or mtime, so the triple —
// never the bare path — is what makes a stale cache entry impossible to alias onto a
// different file. Extracted out of VaultEnricher::durationKey so the two caches never
// drift onto two silently-different derivations.
//
// Header-only inline free function — no extra translation unit (VaultStoreIo.h idiom).

#include <QDir>
#include <QString>

namespace VaultCacheKey {

inline QString make(const QString& path, qint64 size, qint64 mtimeMs)
{
    QString n = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n + QStringLiteral("::") + QString::number(size)
        + QStringLiteral("::") + QString::number(mtimeMs);
}

} // namespace VaultCacheKey
