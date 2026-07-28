// native/comicreader/ComicReaderTypes.cpp
#include "comicreader/ComicReaderTypes.h"

namespace comicreader {

bool spreadRatioExceeded(const QSize& size) {
    if (size.height() <= 0)
        return false;
    return static_cast<double>(size.width()) >= 1.08 * static_cast<double>(size.height());
}

QString pageErrorToString(PageError error) {
    switch (error) {
    case PageError::None:             return QStringLiteral("none");
    case PageError::MissingFile:      return QStringLiteral("missing_file");
    case PageError::DecodeFailed:     return QStringLiteral("decode_failed");
    case PageError::UnsupportedImage: return QStringLiteral("unsupported_image");
    }
    return QStringLiteral("none");
}

PageError pageErrorFromString(const QString& code) {
    if (code == QLatin1String("missing_file"))      return PageError::MissingFile;
    if (code == QLatin1String("decode_failed"))     return PageError::DecodeFailed;
    if (code == QLatin1String("unsupported_image")) return PageError::UnsupportedImage;
    return PageError::None;
}

// PageMeta and PairUnit round-trip through a flat QVariantMap so the QML backend
// (later tasks) can hand these across the C++/QML boundary as plain maps. QSize is
// split into two ints because a bare QSize does not survive a JSON/QML hop cleanly.

QVariantMap PageMeta::toVariantMap() const {
    QVariantMap m;
    m.insert(QStringLiteral("index"), index);
    m.insert(QStringLiteral("sourceKind"),
             sourceKind == PageSourceKind::CbzEntry
                 ? QStringLiteral("cbz_entry")
                 : QStringLiteral("local_file"));
    m.insert(QStringLiteral("localPath"), localPath);
    m.insert(QStringLiteral("archivePath"), archivePath);
    m.insert(QStringLiteral("archiveEntry"), archiveEntry);
    m.insert(QStringLiteral("sourceWidth"), sourceSize.width());
    m.insert(QStringLiteral("sourceHeight"), sourceSize.height());
    m.insert(QStringLiteral("decoded"), decoded);
    m.insert(QStringLiteral("detectedSpread"), detectedSpread);
    // Only present when the user has actually marked the page — absence is the
    // "no override, defer to detection" state and must round-trip as absence.
    if (spreadOverride.has_value())
        m.insert(QStringLiteral("spreadOverride"), *spreadOverride);
    m.insert(QStringLiteral("error"), pageErrorToString(error));
    return m;
}

PageMeta PageMeta::fromVariantMap(const QVariantMap& map) {
    PageMeta p;
    p.index = map.value(QStringLiteral("index"), -1).toInt();
    p.sourceKind =
        map.value(QStringLiteral("sourceKind")).toString() == QLatin1String("cbz_entry")
            ? PageSourceKind::CbzEntry
            : PageSourceKind::LocalFile;
    p.localPath = map.value(QStringLiteral("localPath")).toString();
    p.archivePath = map.value(QStringLiteral("archivePath")).toString();
    p.archiveEntry = map.value(QStringLiteral("archiveEntry")).toString();
    p.sourceSize = QSize(map.value(QStringLiteral("sourceWidth"), 0).toInt(),
                         map.value(QStringLiteral("sourceHeight"), 0).toInt());
    p.decoded = map.value(QStringLiteral("decoded"), false).toBool();
    p.detectedSpread = map.value(QStringLiteral("detectedSpread"), false).toBool();
    if (map.contains(QStringLiteral("spreadOverride")))
        p.spreadOverride = map.value(QStringLiteral("spreadOverride")).toBool();
    p.error = pageErrorFromString(map.value(QStringLiteral("error")).toString());
    return p;
}

QVariantMap PairUnit::toVariantMap() const {
    QVariantMap m;
    m.insert(QStringLiteral("rightIndex"), rightIndex);
    m.insert(QStringLiteral("leftIndex"), leftIndex);
    m.insert(QStringLiteral("spread"), spread);
    m.insert(QStringLiteral("coverAlone"), coverAlone);
    return m;
}

PairUnit PairUnit::fromVariantMap(const QVariantMap& map) {
    PairUnit u;
    u.rightIndex = map.value(QStringLiteral("rightIndex"), -1).toInt();
    u.leftIndex = map.value(QStringLiteral("leftIndex"), -1).toInt();
    u.spread = map.value(QStringLiteral("spread"), false).toBool();
    u.coverAlone = map.value(QStringLiteral("coverAlone"), false).toBool();
    return u;
}

} // namespace comicreader
