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
// That stale guard is NOT byte-for-byte the old behaviour, and the difference
// is worth stating: the synchronous provider read the atomic inline, at request
// time; this reads it on the pool thread at some later moment. So a generation
// that retires between the request and the worker now nulls a request the old
// code would have served. It can never go the other way — a retired generation
// can never start serving again — so the delta is strictly MORE conservative,
// which is the safe direction and is inherent to going async.
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
//  - autoDelete is off, so the pool never deletes the response — whoever asked
//    for it owns it, and the QML engine deletes it after finished(). The pool
//    captures that flag BEFORE dispatch (qtbase 6.11's QThreadPoolThread::run()
//    reads autoDelete() into a local, then calls run(), then acts on the local,
//    precisely so an autoDelete(false) runnable may be destroyed during or
//    after run()), so it never revisits the object once run() has returned.
//    Qt's image reader then disposes of the response with deleteLater() on the
//    reader thread. Net guarantee: an owner may destroy the response the
//    instant finished() arrives, with NO window — which is exactly what both
//    harnesses do with a bare delete.
#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QRunnable>
#include <QSize>
#include <QString>
#include <QThread>

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

    // The thread that ran the work; nullptr until run() starts. Getting the
    // cache copy and the SmoothTransformation scale OFF the requesting thread
    // is the entire reason this class exists, so which thread actually did the
    // work is part of what a response reports about itself — not a test-only
    // detail. Read it after finished(); the queued publication is what
    // synchronises it with the response's thread.
    QThread* servedOn() const;

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
    // Guards only itself — no companion data rides across threads on it, and
    // m_result is touched on the response thread alone. The acquire/release
    // tags are conventional, not load-bearing: the cancel path's determinism
    // comes from the queued publication hop, not from this flag's ordering.
    std::atomic<bool> m_cancelled{false};
    std::atomic<QThread*> m_servedOn{nullptr};
    QImage m_result;
};

} // namespace comicreader
