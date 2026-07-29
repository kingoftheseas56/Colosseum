// native/comicreader/ComicReaderImageResponse.h
//
// One in-flight image://comicreader/ request (Agent 1, overhaul plan
// 2026-07-28, Task 1). ComicReaderProvider hands one of these to QML per page
// hit; it does the cache lookup and the SmoothTransformation downscale on a
// worker thread instead of on Qt's image thread, and it can be CANCELLED — the
// case that matters, because a fast scroll walks past pages whose scale is
// still queued and every one of those is pure waste.
//
// Read-only, exactly like the synchronous provider it replaces: it never
// decodes, never mutates core state, never touches the decode coordinator. It
// reads the mutex-guarded page cache and the ONE live-generation atomic
// ComicReaderCore publishes. A request tagged with a superseded generation
// resolves to NOTHING, so a QML Image still bound to a retired volume can never
// repaint the old chapter's pixels.
//
// Threading + lifetime:
//  - Constructed on the caller's thread (Qt's image thread / the GUI thread);
//    that thread is the response's thread for the rest of its life.
//  - run() is the QRunnable body, executed on the provider's pool. It does the
//    cache read and the scale, then QUEUES publication back to the response's
//    own thread.
//  - Because publication is queued, a cancel() arriving from the response's
//    thread is strictly ORDERED AHEAD of it — cancel never races publication,
//    it simply wins. That is what makes the cancel path deterministic rather
//    than best-effort.
//  - autoDelete is off: the pool never deletes the response. Whoever asked for
//    it owns it — the QML engine deletes it after finished(), which can only be
//    emitted from the publication that run() has already queued.
#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QRunnable>
#include <QSize>
#include <QString>

#include <atomic>

#include <QtGlobal>

namespace comicreader {

class ComicReaderPageCache;

class ComicReaderImageResponse final : public QQuickImageResponse, public QRunnable {
    Q_OBJECT

public:
    // `cache` and `liveGeneration` are owned by ComicReaderCore and outlive the
    // QML engine; the response keeps observer pointers only. `id` is
    // "<generation>/<page>" plus any query the QML side appends purely to bust
    // its own image cache (e.g. "?rev=3") — everything from the first '?' on is
    // ignored. `requestedSize` is honoured on width only, and only downward.
    ComicReaderImageResponse(ComicReaderPageCache* cache,
                             const std::atomic<quint64>* liveGeneration,
                             const QString& id,
                             const QSize& requestedSize);

    // Null until this response has published; null forever if it was cancelled,
    // if its generation was retired, or if the page was not in the cache.
    // Ownership of the returned factory passes to the caller (Qt's contract).
    QQuickTextureFactory* textureFactory() const override;

    // Callable from the response's thread at any point before publication.
    // Cheap and non-blocking: it only raises the flag the worker and the
    // publication step consult. finished() still arrives exactly once, because
    // the queued publication is what emits it.
    void cancel() override;

    bool wasCancelled() const;

    // QRunnable body — provider's pool thread. Never call directly except from
    // a test that wants the work done inline.
    void run() override;

private:
    // Response thread only. Adopts `image` unless a cancel got here first, then
    // emits finished() — the single place finished() is ever emitted.
    void publish(const QImage& image);

    ComicReaderPageCache* m_cache;                 // not owned
    const std::atomic<quint64>* m_liveGeneration;  // not owned
    quint64 m_generation = 0;
    int m_page = -1;
    bool m_idParsed = false;
    int m_requestedWidth = 0;
    std::atomic<bool> m_cancelled{false};
    QImage m_result;
};

} // namespace comicreader
