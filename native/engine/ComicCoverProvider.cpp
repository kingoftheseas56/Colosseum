#include "engine/ComicCoverProvider.h"
#include "engine/CbzArchive.h"
#include "engine/ComicCoverId.h"

#include <QBuffer>
#include <QByteArray>
#include <QImageReader>

#include <limits>

namespace Colosseum {

namespace {

// A filmstrip/library-grid tile has no business holding a full-resolution
// decode -- matches the order of magnitude ComicReaderImageResponse's own
// thumbnail cap uses for the same reason (see kThumbnailMaxWidth).
constexpr int kDefaultMaxWidth = 240;
constexpr int kDefaultMaxHeight = 360;

// requestedSize may constrain only ONE dimension (QML's `sourceSize.width`
// alone produces e.g. QSize(296, 0)) -- treat a non-positive dimension as
// unconstrained rather than requiring BOTH to be positive, which silently
// dropped the caller's request and fell back to the default box entirely.
QSize targetBoxFor(const QSize& requestedSize)
{
    const int w = requestedSize.width();
    const int h = requestedSize.height();
    if (w > 0 && h > 0) return QSize(w, h);
    if (w > 0) return QSize(w, std::numeric_limits<int>::max());
    if (h > 0) return QSize(std::numeric_limits<int>::max(), h);
    return QSize(kDefaultMaxWidth, kDefaultMaxHeight);
}

} // namespace

QImage ComicCoverProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    if (size) *size = QSize();

    QString archivePath;
    QString entryName;
    if (!parseComicCoverId(id, &archivePath, &entryName)) return QImage();

    // readEntry() returns an empty QByteArray on any failure -- missing
    // archive, missing entry, or a read error -- which folds cleanly into
    // this provider's own "resolve null" contract for anything unreadable.
    const QByteArray bytes = MangaTankoban::CbzArchive::readEntry(archivePath, entryName);
    if (bytes.isEmpty()) return QImage();

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return QImage();

    QImageReader reader(&buffer);
    reader.setAutoTransform(true);           // honor EXIF orientation
    reader.setDecideFormatFromContent(true);  // sniff bytes, not the entry's suffix

    // Scale DOWN to the requested tile (or a bounded default when the caller
    // doesn't specify one, or specifies only one dimension) before decoding
    // pixels -- setScaledSize() makes QImageReader do the downscale as part
    // of the decode itself, so a 200px grid tile never pays for allocating/
    // decoding the full page first. Never scales UP: a source already
    // smaller than the target box is served at its native size untouched.
    const QSize nativeSize = reader.size();   // header-only for png/jpeg/webp
    if (nativeSize.isValid() && nativeSize.width() > 0 && nativeSize.height() > 0) {
        const QSize targetBox = targetBoxFor(requestedSize);
        const QSize fitted = nativeSize.scaled(targetBox, Qt::KeepAspectRatio);
        if (fitted.width() < nativeSize.width() || fitted.height() < nativeSize.height())
            reader.setScaledSize(fitted);
    }

    const QImage image = reader.read();
    if (image.isNull()) return QImage();

    if (size) *size = image.size();
    return image;
}

} // namespace Colosseum
