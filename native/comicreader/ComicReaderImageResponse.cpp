// native/comicreader/ComicReaderImageResponse.cpp
#include "comicreader/ComicReaderImageResponse.h"

#include "comicreader/ComicReaderPageCache.h"

#include <QMetaObject>
#include <QStringView>

#include <optional>

namespace comicreader {
namespace {

// "<generation>/<page>", ignoring any query the QML side appended purely to
// bust its own image cache ("?rev=3"). Leaves the outputs untouched and returns
// false for anything that is not a well-formed, non-negative pair.
bool parseKey(const QString& id, quint64& generation, int& page) {
    QStringView key(id);
    const qsizetype query = key.indexOf(QLatin1Char('?'));
    if (query >= 0)
        key = key.left(query);

    const qsizetype slash = key.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return false;

    bool okGeneration = false;
    bool okPage = false;
    const quint64 parsedGeneration = key.left(slash).toULongLong(&okGeneration);
    const int parsedPage = key.mid(slash + 1).toInt(&okPage);
    // toInt() accepts "-1" happily. A negative page is never a real request, so
    // reject it here rather than letting a later cache miss hide it.
    if (!okGeneration || !okPage || parsedPage < 0)
        return false;

    generation = parsedGeneration;
    page = parsedPage;
    return true;
}

} // namespace

ComicReaderImageResponse::ComicReaderImageResponse(ComicReaderPageCache* cache,
                                                   const std::atomic<quint64>* liveGeneration,
                                                   const QString& id,
                                                   const QSize& requestedSize)
    : m_cache(cache),
      m_liveGeneration(liveGeneration),
      m_requestedWidth(requestedSize.width()) {
    // The pool must not delete this — the owner does, after finished(). Safe to
    // set here: the pool reads the flag before dispatch and never revisits the
    // object once run() returns (see the header's lifetime note).
    setAutoDelete(false);

    m_idParsed = parseKey(id, m_generation, m_page);
}

void ComicReaderImageResponse::cancel() {
    m_cancelled.store(true, std::memory_order_release);
}

bool ComicReaderImageResponse::wasCancelled() const {
    return m_cancelled.load(std::memory_order_acquire);
}

QQuickTextureFactory* ComicReaderImageResponse::textureFactory() const {
    return m_result.isNull() ? nullptr
                             : QQuickTextureFactory::textureFactoryForImage(m_result);
}

QThread* ComicReaderImageResponse::servedOn() const {
    return m_servedOn.load(std::memory_order_relaxed);
}

void ComicReaderImageResponse::run() {
    m_servedOn.store(QThread::currentThread(), std::memory_order_relaxed);

    QImage served;

    // An unparseable id, a provider wired to nothing, or a cancel that beat the
    // worker to the start: all exit without ever touching the cache.
    if (m_idParsed && m_cache && m_liveGeneration && !wasCancelled()) {
        // Stale guard: anything but the live generation resolves to nothing, so
        // a QML Image still bound to a retired entry never repaints old pixels.
        if (m_generation == m_liveGeneration->load()) {
            const std::optional<QImage> cached = m_cache->get(m_generation, m_page);

            // Cancelled while the cache lookup ran — skip the scale, which is
            // the expensive half and the whole reason this is off-thread.
            if (cached.has_value() && !cached->isNull() && !wasCancelled()) {
                served = *cached;
                if (m_requestedWidth > 0 && m_requestedWidth < served.width())
                    served = served.scaledToWidth(m_requestedWidth, Qt::SmoothTransformation);
            }
        }
    }

    // Publish on the response's own thread. Queued, so a cancel() issued from
    // that thread is ordered ahead of this and simply wins.
    QMetaObject::invokeMethod(
        this, [this, served]() { publish(served); }, Qt::QueuedConnection);
}

void ComicReaderImageResponse::publish(const QImage& image) {
    if (!wasCancelled())
        m_result = image;
    emit finished();
}

} // namespace comicreader
