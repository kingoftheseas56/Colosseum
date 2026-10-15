#include "engine/ComicCoverId.h"

#include <QByteArray>

namespace Colosseum {

namespace {

QString encodeSegment(const QString& value)
{
    return QString::fromLatin1(value.toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString decodeSegment(const QString& value)
{
    return QString::fromUtf8(QByteArray::fromBase64(
        value.toUtf8(), QByteArray::Base64UrlEncoding));
}

} // namespace

QString buildComicCoverId(const QString& archivePath, const QString& entryName)
{
    return encodeSegment(archivePath) + QLatin1Char('/') + encodeSegment(entryName);
}

bool parseComicCoverId(const QString& id, QString* archivePath, QString* entryName)
{
    if (archivePath) archivePath->clear();
    if (entryName) entryName->clear();

    const int slash = id.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == id.size() - 1) return false;

    const QString decodedArchive = decodeSegment(id.left(slash));
    const QString decodedEntry = decodeSegment(id.mid(slash + 1));
    if (decodedArchive.isEmpty() || decodedEntry.isEmpty()) return false;

    if (archivePath) *archivePath = decodedArchive;
    if (entryName) *entryName = decodedEntry;
    return true;
}

} // namespace Colosseum
