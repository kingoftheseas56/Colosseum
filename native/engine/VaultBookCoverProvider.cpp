#include "engine/VaultBookCoverProvider.h"

#include "engine/ComicCoverId.h"
#include "third_party/miniz/miniz.h"

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QRegularExpression>

#include <limits>

namespace Colosseum {
namespace {

constexpr mz_uint64 kMaxCoverBytes = 24 * 1024 * 1024;
constexpr int kDefaultMaxWidth = 240;
constexpr int kDefaultMaxHeight = 360;

bool safeEntryName(const QString& raw)
{
    const QString name = QDir::fromNativeSeparators(raw.trimmed());
    if (name.isEmpty() || name.startsWith(QLatin1Char('/'))
        || QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(name).hasMatch())
        return false;
    for (const QString& part : name.split(QLatin1Char('/'), Qt::KeepEmptyParts))
        if (part == QLatin1String("..") || part == QLatin1String("."))
            return false;
    return true;
}

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

QImage VaultBookCoverProvider::requestImage(const QString& id, QSize* size,
                                            const QSize& requestedSize)
{
    if (size)
        *size = QSize();

    QString archivePath;
    QString entryName;
    if (!parseComicCoverId(id, &archivePath, &entryName) || !safeEntryName(entryName))
        return QImage();

    mz_zip_archive zip{};
    const QByteArray path = QDir::toNativeSeparators(archivePath).toUtf8();
    if (!mz_zip_reader_init_file(&zip, path.constData(), 0))
        return QImage();
    const int index = mz_zip_reader_locate_file(&zip, entryName.toUtf8().constData(), nullptr, 0);
    if (index < 0) {
        mz_zip_reader_end(&zip);
        return QImage();
    }
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat)
        || stat.m_is_directory || stat.m_uncomp_size > kMaxCoverBytes) {
        mz_zip_reader_end(&zip);
        return QImage();
    }
    size_t extractedSize = 0;
    void* memory = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index),
                                                 &extractedSize, 0);
    if (!memory || extractedSize > kMaxCoverBytes) {
        if (memory) mz_free(memory);
        mz_zip_reader_end(&zip);
        return QImage();
    }
    const QByteArray bytes(static_cast<const char*>(memory), static_cast<qsizetype>(extractedSize));
    mz_free(memory);
    mz_zip_reader_end(&zip);

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return QImage();
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    const QSize nativeSize = reader.size();
    if (nativeSize.isValid() && nativeSize.width() > 0 && nativeSize.height() > 0) {
        const QSize fitted = nativeSize.scaled(targetBoxFor(requestedSize), Qt::KeepAspectRatio);
        if (fitted.width() < nativeSize.width() || fitted.height() < nativeSize.height())
            reader.setScaledSize(fitted);
    }
    const QImage image = reader.read();
    if (image.isNull())
        return QImage();
    if (size)
        *size = image.size();
    return image;
}

} // namespace Colosseum
