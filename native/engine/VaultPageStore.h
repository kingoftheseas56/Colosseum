#pragma once
// VaultPageStore — the comic-reader adapter for local archives (Slice 7). It
// satisfies ComicReaderShell's injected-store contract (`store.localPages(id)`)
// so a Vault CBZ opens in ComicReader 2 with ZERO reader edits: it returns the
// SAME "direct archive descriptors" shape the Tankoban volume lane
// (MangaVolumeIndex) returns —
//     [{index, archive, entry, group}]
// — which the reader decodes in place, without extraction. The `id` passed in is
// the archive path.

#include <QObject>
#include <QString>
#include <QVariantList>

class VaultPageStore : public QObject
{
    Q_OBJECT

public:
    explicit VaultPageStore(QObject* parent = nullptr);

    // Reader contract: entries in natural reading order as archive descriptors.
    Q_INVOKABLE QVariantList localPages(const QString& archivePath) const;
};
