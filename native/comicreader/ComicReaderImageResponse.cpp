// native/comicreader/ComicReaderImageResponse.cpp
#include "comicreader/ComicReaderImageResponse.h"

#include "comicreader/ComicReaderPageCache.h"

#include <QMetaObject>
#include <QStringView>

#include <optional>

namespace comicreader {

ComicReaderImageResponse::ComicReaderImageResponse(ComicReaderPageCache* cache,
                                                   const std::atomic<quint64>* liveGeneration,
                                                   const QString& id,
                                                   const QSize& requestedSize)
    : m_cache(cache),
      m_liveGeneration(liveGeneration),
      m_requestedWidth(requestedSize.width()) {
    // The pool must not delete this — the owner does, after finished(). It does
    // still read autoDelete() back off the object once run() returns; see the
    // header's lifetime note for why that sharp edge is accepted, not absent.
    setAutoDelete(false);

    // Strip any "?rev=N" cache-buster before parsing "<generation>/<page>".
    QString key = id;
    const int q = key.indexOf(QLatin1Char('?'));
    if (q >= 0)
        key.truncate(q);

    const int slash = key.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return;

    bool okGen = false;
    bool okPage = false;
    const quint64 gen = QStringView(key).left(slash).toULongLong(&okGen);
    const int page = QStringView(key).mid(slash + 1).toInt(&okPage);
    if (!okGen || !okPage)
        return;

    m_generation = gen;
    m_page = page;
    m_idParsed = true;
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

void ComicReaderImageResponse::run() {
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
