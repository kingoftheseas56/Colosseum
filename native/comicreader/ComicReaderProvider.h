// native/comicreader/ComicReaderProvider.h
//
// Read-only image://comicreader/ provider for the Comic Reader (Agent 1, plan
// 2026-07-23 Task 7; made ASYNC by the overhaul plan 2026-07-28 Task 1). QML
// asks for a decoded page by URL —
//   image://comicreader/<generation>/<page>?rev=<n>
// — and this provider answers from the backend's pinned LRU cache. It NEVER
// decodes, NEVER mutates any core state, and NEVER touches the coordinator: it
// only reads the cache and consults the ONE live-generation value the backend
// publishes. A request tagged with a superseded generation (a stale QML Image
// still bound to the previous entry) resolves to nothing, so a fast entry
// switch can never repaint the old chapter's pixels. That guard is now checked
// on the worker thread rather than inline, which makes it strictly more
// conservative than the synchronous version — see ComicReaderImageResponse.h.
//
// Why async: the synchronous requestImage() ran the cache copy and the
// SmoothTransformation downscale on Qt's image thread with NO way to cancel, so
// a fast scroll paid in full for every page it had already passed. Each request
// is now a ComicReaderImageResponse scheduled on this provider's own pool, and
// QML can cancel one the moment the page leaves the window.
//
// Who parses the URL: the RESPONSE does, in its constructor — not this provider,
// which only dispatches. Two reasons, and Task 2's author (adding `tier`/`dpr`
// to the grammar) should know them before moving it: the parse then happens on
// the requesting thread rather than burning pool time, and the response stays
// self-contained enough to construct and drive directly, which is what lets the
// harness hold a worker without any production test hook.
//
// Threading: Qt calls requestImageResponse() off the GUI thread. That is safe —
// ComicReaderPageCache is mutex-guarded and the live generation is read through
// a std::atomic. Both are owned by ComicReaderCore and outlive the QML engine
// that owns this provider (the engine takes ownership via addImageProvider()).
#pragma once

#include <QQuickImageProvider>
#include <QThreadPool>

#include <atomic>

#include <QtGlobal>

namespace comicreader {

class ComicReaderPageCache;

class ComicReaderProvider final : public QQuickAsyncImageProvider {
public:
    // `cache` and `liveGeneration` are owned by ComicReaderCore; the provider
    // keeps raw/observer pointers only (never owns, never deletes).
    ComicReaderProvider(ComicReaderPageCache* cache,
                        const std::atomic<quint64>* liveGeneration);

    // id == "<generation>/<page>" (any "?..." query is ignored — it exists
    // solely to bust QML's own image cache when a page re-decodes). The
    // returned response resolves, on a pool thread, to the cached image for
    // (generation, page) scaled to requestedSize.width() when a smaller width
    // is asked; to nothing when the generation is retired, the page is not
    // cached, the id does not parse, or the response was cancelled first.
    // Ownership passes to the caller (the QML engine).
    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

private:
    ComicReaderPageCache* m_cache;                 // not owned
    const std::atomic<quint64>* m_liveGeneration;  // not owned
    // Serves this provider's responses only. Destroying it drains every queued
    // and running response, so no worker survives the provider itself — nothing
    // is left running against a half-torn-down engine.
    //
    // Its thread count is set DELIBERATELY narrow in the constructor rather than
    // inherited from idealThreadCount(): this pool runs alongside the two-lane
    // decoder on the same machine whose stutter is the reason for this arc, and
    // a full-width pool can put one smooth scale per core in contention with
    // decode and the GUI — costing more than the wasted scales cancellation
    // saves. Task 2's metrics are what should actually tune this.
    QThreadPool m_pool;
};

} // namespace comicreader
