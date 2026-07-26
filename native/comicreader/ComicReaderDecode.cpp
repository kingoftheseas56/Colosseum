// native/comicreader/ComicReaderDecode.cpp
#include "comicreader/ComicReaderDecode.h"

#include "comicreader/ComicReaderPageCache.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QRunnable>

#include <utility>

namespace comicreader {

namespace {

// Does the leading bytes carry a recognizable image-container signature? Used
// ONLY to split a null decode: QImageReader reports UnsupportedFormatError for
// BOTH a real image format this build has no handler for AND arbitrary garbage,
// so error() alone can't tell them apart. If the bytes look like a known image
// container, the format is genuinely unsupported (-> UnsupportedImage); if they
// look like nothing, they are just undecodable bytes (-> DecodeFailed).
// Deliberately narrow and binary-magic only: text/garbage matches nothing.
bool looksLikeKnownImageContainer(const QByteArray& bytes) {
    const int n = bytes.size();
    const auto* u = reinterpret_cast<const unsigned char*>(bytes.constData());
    if (n >= 8 && u[0] == 0x89 && u[1] == 'P' && u[2] == 'N' && u[3] == 'G' &&
        u[4] == 0x0D && u[5] == 0x0A && u[6] == 0x1A && u[7] == 0x0A)
        return true; // PNG
    if (n >= 3 && u[0] == 0xFF && u[1] == 0xD8 && u[2] == 0xFF)
        return true; // JPEG
    if (n >= 6 && u[0] == 'G' && u[1] == 'I' && u[2] == 'F' && u[3] == '8' &&
        (u[4] == '7' || u[4] == '9') && u[5] == 'a')
        return true; // GIF
    if (n >= 2 && u[0] == 'B' && u[1] == 'M')
        return true; // BMP
    if (n >= 4 && ((u[0] == 0x49 && u[1] == 0x49 && u[2] == 0x2A && u[3] == 0x00) ||
                   (u[0] == 0x4D && u[1] == 0x4D && u[2] == 0x00 && u[3] == 0x2A)))
        return true; // TIFF (little/big endian)
    if (n >= 12 && u[0] == 'R' && u[1] == 'I' && u[2] == 'F' && u[3] == 'F' &&
        u[8] == 'W' && u[9] == 'E' && u[10] == 'B' && u[11] == 'P')
        return true; // WEBP
    if (n >= 12 && u[4] == 'f' && u[5] == 't' && u[6] == 'y' && u[7] == 'p')
        return true; // ISO-BMFF: HEIF / HEIC / AVIF ...
    if (n >= 4 && u[0] == 0x00 && u[1] == 0x00 && u[2] == 0x01 && u[3] == 0x00)
        return true; // ICO
    return false;
}

// The pool worker: PURE over its captured (gen, page, localPath). It reads the
// file, decodes it, and posts the result back to the coordinator's thread via a
// queued invocation. It never touches the cache, the inflight set, or any
// coordinator state — the only thing it does with the coordinator pointer is use
// it as the receiver/context of the queued post, which Qt marshals safely and
// discards if the coordinator is destroyed first.
class DecodeRunnable final : public QRunnable {
public:
    DecodeRunnable(ComicReaderDecode* coordinator, quint64 gen, int page,
                   QString localPath,
                   std::function<void(quint64, int)> onEnter,
                   std::function<void(quint64, int)> onExit)
        : m_coordinator(coordinator),
          m_gen(gen),
          m_page(page),
          m_localPath(std::move(localPath)),
          m_onEnter(std::move(onEnter)),
          m_onExit(std::move(onExit)) {}

    void run() override {
        if (m_onEnter)
            m_onEnter(m_gen, m_page);

        QImage image;
        PageError error = PageError::None;

        if (m_localPath.isEmpty() || !QFileInfo::exists(m_localPath)) {
            error = PageError::MissingFile;
            qWarning() << "ComicReaderDecode: missing file for page" << m_page
                       << "-" << m_localPath;
        } else {
            QFile file(m_localPath);
            if (!file.open(QIODevice::ReadOnly)) {
                // Present on disk but unreadable (permissions, vanished mid-read):
                // treat as missing rather than a decode fault.
                error = PageError::MissingFile;
                qWarning() << "ComicReaderDecode: unreadable file for page" << m_page
                           << "-" << m_localPath << "-" << file.errorString();
            } else {
                QByteArray bytes = file.readAll();
                file.close();

                // EARLY DIMENSION HINT (TB2 DecodeTask parity). The header names
                // the page's true size for a fraction of the decode's cost, so
                // the strip column can snap to real geometry — and pairing can
                // learn a spread — before the pixels exist. Without it every page
                // holds the flat 1600x2400 estimate until its FULL decode lands,
                // and the column pays a stream of anti-jump corrections during a
                // fast scroll instead of settling once. Reported on the SAME
                // queued path as the finished result, so it rides the same stale
                // guard and the same ordering.
                {
                    QBuffer probeBuf(&bytes);
                    probeBuf.open(QIODevice::ReadOnly);
                    QImageReader probe(&probeBuf);
                    probe.setDecideFormatFromContent(true);
                    probe.setAutoTransform(true);
                    const QSize dims = probe.size();   // header-only for png/jpeg/webp
                    if (dims.isValid() && dims.width() > 0 && dims.height() > 0) {
                        ComicReaderDecode* c = m_coordinator;
                        const quint64 g = m_gen;
                        const int p = m_page;
                        QMetaObject::invokeMethod(
                            c,
                            [c, g, p, dims]() { c->onWorkerDimensions(g, p, dims); },
                            Qt::QueuedConnection);
                    }
                }

                QBuffer buffer(&bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                reader.setAutoTransform(true);           // honor EXIF orientation
                reader.setDecideFormatFromContent(true); // sniff bytes, not extension
                const QImage decoded = reader.read();
                if (decoded.isNull()) {
                    // UnsupportedImage only when Qt found no handler AND the bytes
                    // are a recognizable image container (a real format this build
                    // can't decode). Everything else — garbage, truncated, corrupt
                    // (InvalidDataError) — folds to DecodeFailed.
                    const bool unsupported =
                        reader.error() == QImageReader::UnsupportedFormatError &&
                        looksLikeKnownImageContainer(bytes);
                    error = unsupported ? PageError::UnsupportedImage
                                        : PageError::DecodeFailed;
                    qWarning() << "ComicReaderDecode: decode failed for page" << m_page
                               << "-" << m_localPath << "-" << reader.errorString();
                } else {
                    image = decoded;
                    error = PageError::None;
                }
            }
        }

        if (m_onExit)
            m_onExit(m_gen, m_page);

        // Report back to the owning thread. `m_coordinator` as the context object
        // guarantees Qt runs the functor on the coordinator's thread and drops it
        // if the coordinator has been destroyed.
        ComicReaderDecode* coordinator = m_coordinator;
        const quint64 gen = m_gen;
        const int page = m_page;
        const QImage result = image;
        const PageError err = error;
        QMetaObject::invokeMethod(
            coordinator,
            [coordinator, gen, page, result, err]() {
                coordinator->onWorkerResult(gen, page, result, err);
            },
            Qt::QueuedConnection);
    }

private:
    ComicReaderDecode* m_coordinator;
    quint64 m_gen;
    int m_page;
    QString m_localPath;
    std::function<void(quint64, int)> m_onEnter;
    std::function<void(quint64, int)> m_onExit;
};

} // namespace

ComicReaderDecode::ComicReaderDecode(ComicReaderPageCache* cache, QObject* parent)
    : QObject(parent), m_cache(cache) {
    // Two decode lanes: enough to keep the visible page + one neighbor warm
    // without flooding I/O. The stale guard, not the thread count, is what makes
    // a fast entry switch safe.
    m_pool.setMaxThreadCount(2);
}

ComicReaderDecode::~ComicReaderDecode() {
    // Block until every in-flight worker finishes so no worker is mid-run when
    // members are torn down. Any report-backs still queued after teardown are
    // discarded by ~QObject (the receiver is gone) — never run against a dead
    // object. Relies on the affinity invariant: destroyed on the owning thread.
    m_pool.waitForDone();
}

void ComicReaderDecode::openGeneration(quint64 gen, const QVector<PageMeta>& pages) {
    // Flush the pool queue FIRST: gen-A runnables that were start()ed but have
    // not begun running yet must not execute ahead of gen-B's priority-100
    // visible page (equal priority is FIFO). Cleared runnables never post a
    // report-back; the up-to-2 already-running workers still finish and are
    // dropped by the stale guard in onWorkerResult.
    m_pool.clear();

    const quint64 oldGen = m_currentGen;
    m_currentGen = gen;

    m_pageByIndex.clear();
    m_pageByIndex.reserve(pages.size());
    for (const PageMeta& page : pages)
        m_pageByIndex.insert(page.index, page);

    // Stale in-flight workers are NOT interrupted; they are dropped when they
    // report back (onWorkerResult's stale guard). Clearing the set just means a
    // fresh request for the same page in the new generation is no longer
    // considered a duplicate of the old generation's in-flight decode. The
    // failed set is per-generation, so it resets here too — a page that failed
    // under the old generation gets one fresh chance under the new one.
    m_inflight.clear();
    m_failed.clear();
    m_missingRetryAt.clear();

    if (oldGen != gen)
        m_cache->clearGeneration(oldGen);
}

void ComicReaderDecode::request(int page, int priority) {
    const auto it = m_pageByIndex.constFind(page);
    if (it == m_pageByIndex.constEnd())
        return;                              // unknown page in this generation → ignore
    if (m_failed.contains(page))
        return;                              // already failed this generation → don't retry
    // A MissingFile page is cooling down, not condemned (C6): once the window passes it gets one
    // fresh attempt, so a page that was still being written to disk heals itself.
    const auto retryIt = m_missingRetryAt.constFind(page);
    if (retryIt != m_missingRetryAt.constEnd()) {
        if (QDateTime::currentMSecsSinceEpoch() < retryIt.value())
            return;                          // still cooling down — no per-frame stat storm
        m_missingRetryAt.erase(retryIt);     // cooldown over: allow exactly one fresh attempt
    }
    if (m_inflight.contains(page))
        return;                              // already decoding (currentGen, page)
    if (m_cache->get(m_currentGen, page).has_value())
        return;                              // already decoded and cached (get bumps LRU recency)

    m_inflight.insert(page);
    auto* runnable = new DecodeRunnable(this, m_currentGen, page, it->localPath,
                                        m_testOnWorkerEnter, m_testOnWorkerExit);
    m_pool.start(runnable, priority);
}

// The header-only size hint, landing ahead of the same worker's finished result
// (or ahead of its failure — a truncated file still names its own dimensions).
// Same stale guard as onWorkerResult: a hint tagged with a superseded generation
// is dropped before it can reach any client. Deliberately does NOT touch
// m_inflight or m_failed — the decode is still running and still owns those.
void ComicReaderDecode::onWorkerDimensions(quint64 gen, int page, const QSize& dims) {
    if (gen != m_currentGen)
        return;

    PageMeta meta;
    meta.index = page;
    const auto it = m_pageByIndex.constFind(page);
    if (it != m_pageByIndex.constEnd())
        meta.localPath = it->localPath;
    meta.sourceSize = dims;
    // A hint carries geometry, never pixels: `decoded` stays false so every
    // consumer of "is this page ready to paint" keeps telling the truth.
    meta.decoded = false;
    meta.detectedSpread = spreadRatioExceeded(dims);

    emit metaReady(gen, meta);
}

void ComicReaderDecode::onWorkerResult(quint64 gen, int page, const QImage& image,
                                       PageError error) {
    // THE STALE GUARD: a result tagged with a superseded generation is dropped
    // before it can touch the cache or reach any client. This is why a fast
    // A -> B switch can never paint A's page into B.
    if (gen != m_currentGen)
        return;

    m_inflight.remove(page);

    if (error != PageError::None) {
        // Memoize the failure for this generation so a re-request (e.g. Task 7's
        // strip re-request storm) doesn't re-decode a page that will just fail
        // again. Reset happens on the next openGeneration.
        //
        // EXCEPT MissingFile (C6), which is a timing condition rather than a verdict about the
        // bytes: the page is typically still being written. It cools down instead of latching, so
        // a later request can succeed on its own.
        if (error == PageError::MissingFile)
            m_missingRetryAt.insert(page, QDateTime::currentMSecsSinceEpoch() + kMissingRetryMs);
        else
            m_failed.insert(page);
        emit pageFailed(gen, page, error);
        return;
    }

    m_cache->insert(gen, page, image);

    PageMeta meta;
    meta.index = page;
    const auto it = m_pageByIndex.constFind(page);
    if (it != m_pageByIndex.constEnd())
        meta.localPath = it->localPath;
    meta.sourceSize = image.size();
    meta.decoded = true;
    // Raw geometry verdict only. Index-0 pairing-cover handling and manual
    // spreadOverride belong to the pairing pass (Task 2), not here.
    meta.detectedSpread = spreadRatioExceeded(image.size());

    emit metaReady(gen, meta);
    emit pageReady(gen, page);
}

void ComicReaderDecode::setWorkerHooksForTest(std::function<void(quint64, int)> onEnter,
                                              std::function<void(quint64, int)> onExit) {
    m_testOnWorkerEnter = std::move(onEnter);
    m_testOnWorkerExit = std::move(onExit);
}

} // namespace comicreader
