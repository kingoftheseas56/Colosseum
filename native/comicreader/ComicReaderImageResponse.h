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
//
// ── Task 2: two tiers, and where the work actually goes ──────────────────────
// The worker now asks the SCALED tier first (ComicReaderScaleCache) and only
// falls through to the full-resolution page on a miss. That is the whole point
// of Task 2: scrolling back one page used to recompute an identical
// SmoothTransformation downscale of a 2400px page; now it is a memory read.
//
// Reading and writing the scaled tier does NOT make this a writer of reader
// state. The scaled tier is a delivery-side store: nothing the reader DECIDES
// (decode priority, pairing, coupling, the strip window) ever reads it. The
// same test passes the metrics counters — they are observation, and no decision
// consults them.
//
// The three tiers, and what each is FOR:
//   preview   — a FastTransformation at the requested size. Cheap, and queued
//               ahead of hq work in the provider's pool, so the frame gets
//               pixels first. That is a strong bias, not a guarantee: with two
//               lanes an hq already running keeps running.
//   hq        — the reader's real page: the default, and what every existing
//               caller gets. Task 7's quality dial governs THIS tier and only
//               this tier — fast/balanced/best pick its resampler and decide
//               whether the tonal maths runs before or after the downscale.
//   thumbnail — SmoothTransformation, but clamped to kThumbnailMaxWidth. A
//               filmstrip entry has no business holding a viewport-sized scale;
//               a hundred of those is the memory bug this task exists to avoid
//               causing.
#pragma once

#include "comicreader/ComicReaderScaleCache.h"

#include <QElapsedTimer>
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
    // A filmstrip thumbnail's ceiling in device pixels. Chosen against the
    // overlay the plan describes (a centred strip of page thumbnails), where a
    // wider scale buys nothing anybody can see and costs a scaled-tier slot.
    static constexpr int kThumbnailMaxWidth = 240;

    // `ctx` bundles the stores this response reads; ComicReaderCore owns all of
    // them and outlives the QML engine, so the response keeps observer pointers
    // only. `id` is "<generation>/<page>" plus a query the QML side appends:
    // "?rev=N" busts QML's own image cache, "&tier=" picks preview/hq/thumbnail,
    // "&dpr=" carries the device pixel ratio into the scale key. Unknown query
    // keys are ignored, and an absent tier means hq — which is what keeps the
    // pre-Task-2 url grammar valid.
    //
    // `requestedSize` is honoured on width only, and only downward (Task 1's
    // rule, unchanged): a page is fitted to a column and its height follows, and
    // upscaling a decoded page is pure waste.
    ComicReaderImageResponse(const DeliveryContext& ctx,
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

    // Which tier this request asked for, read from the id at construction. The
    // provider consults it to queue previews ahead of hq work.
    ScaleTier tier() const { return m_tier; }

    // QRunnable body — provider's pool thread. Never call directly except from
    // a test that wants the work done inline.
    void run() override;

private:
    // Response thread only. Adopts `image` unless a cancel got here first, then
    // emits finished() — the single place finished() is ever emitted.
    void publish(const QImage& image);

    DeliveryContext m_ctx;                         // observer pointers, nothing owned
    quint64 m_generation = 0;
    int m_page = -1;
    bool m_idParsed = false;
    int m_requestedWidth = 0;
    int m_dpr100 = 100;
    ScaleTier m_tier = ScaleTier::Hq;
    // Construction -> published image. Started in the constructor, on the
    // requesting thread, so it measures what QML actually waits for: the queue
    // wait as well as the work.
    QElapsedTimer m_age;
    // Guards only itself — no companion data rides across threads on it, and
    // m_result is touched on the response thread alone. The acquire/release
    // tags are conventional, not load-bearing: the cancel path's determinism
    // comes from the queued publication hop, not from this flag's ordering.
    std::atomic<bool> m_cancelled{false};
    std::atomic<QThread*> m_servedOn{nullptr};
    QImage m_result;
};

} // namespace comicreader
