// native/comicreader/ComicReaderProvider.cpp
#include "comicreader/ComicReaderProvider.h"

#include "comicreader/ComicReaderPageCache.h"

#include <QImage>
#include <QStringView>

namespace comicreader {

ComicReaderProvider::ComicReaderProvider(ComicReaderPageCache* cache,
                                         const std::atomic<quint64>* liveGeneration)
    : QQuickImageProvider(QQuickImageProvider::Image),
      m_cache(cache),
      m_liveGeneration(liveGeneration) {}

QImage ComicReaderProvider::requestImage(const QString& id, QSize* size,
                                         const QSize& requestedSize) {
    if (!m_cache || !m_liveGeneration)
        return {};

    // Strip any "?rev=N" cache-buster before parsing "<generation>/<page>".
    QString key = id;
    const int q = key.indexOf(QLatin1Char('?'));
    if (q >= 0)
        key.truncate(q);

    const int slash = key.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return {};

    bool okGen = false;
    bool okPage = false;
    const quint64 gen = QStringView(key).left(slash).toULongLong(&okGen);
    const int page = QStringView(key).mid(slash + 1).toInt(&okPage);
    if (!okGen || !okPage)
        return {};

    // Stale guard: a request for anything but the live generation returns null,
    // so a QML Image still bound to a retired entry never repaints old pixels.
    if (gen != m_liveGeneration->load())
        return {};

    const std::optional<QImage> img = m_cache->get(gen, page);
    if (!img.has_value() || img->isNull())
        return {};

    QImage out = *img;
    if (requestedSize.width() > 0 && requestedSize.width() < out.width())
        out = out.scaledToWidth(requestedSize.width(), Qt::SmoothTransformation);

    if (size)
        *size = out.size();
    return out;
}

} // namespace comicreader
