// native/comicreader/ComicReaderProvider.h
//
// Read-only image://comicreader/ provider for the Comic Reader (Agent 1, plan
// 2026-07-23, Task 7). QML asks for a decoded page by URL —
//   image://comicreader/<generation>/<page>?rev=<n>
// — and this provider answers straight from the backend's pinned LRU cache. It
// NEVER decodes, NEVER mutates any core state, and NEVER touches the coordinator:
// it only reads the cache and consults the ONE live-generation value the backend
// publishes. A request tagged with a superseded generation (a stale QML Image
// still bound to the previous entry) returns a null QImage, so a fast entry
// switch can never repaint the old chapter's pixels.
//
// Threading: Qt may call requestImage() off the GUI thread. That is safe here —
// ComicReaderPageCache is mutex-guarded and the live generation is read through a
// std::atomic. Both are owned by ComicReaderCore and outlive the QML engine that
// owns this provider (the engine takes ownership via addImageProvider()).
#pragma once

#include <QQuickImageProvider>

#include <atomic>

#include <QtGlobal>

namespace comicreader {

class ComicReaderPageCache;

class ComicReaderProvider final : public QQuickImageProvider {
public:
    // `cache` and `liveGeneration` are owned by ComicReaderCore; the provider
    // keeps raw/observer pointers only (never owns, never deletes).
    ComicReaderProvider(ComicReaderPageCache* cache,
                        const std::atomic<quint64>* liveGeneration);

    // id == "<generation>/<page>" (any "?rev=..." query is ignored — it exists
    // solely to bust QML's own image cache when a page re-decodes). Returns the
    // cached image for (generation, page) when generation is live, scaled to
    // requestedSize.width() when a width is asked; otherwise a null QImage.
    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

private:
    ComicReaderPageCache* m_cache;                 // not owned
    const std::atomic<quint64>* m_liveGeneration;  // not owned
};

} // namespace comicreader
