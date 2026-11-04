#include "CollectionShareLink.h"

namespace Colosseum::Collections {
namespace {
constexpr qsizetype MaxLinkLength = 2048;
bool validId(const QString &id) {
    if (id.size() != 43) return false;
    for (const QChar c : id) {
        const auto u = c.unicode();
        if (!((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
              || (u >= '0' && u <= '9') || u == '_' || u == '-')) return false;
    }
    return true;
}
bool safeText(const QString &text) {
    if (text.isEmpty() || text.size() > MaxLinkLength) return false;
    for (const QChar c : text) {
        if (c.isSpace() || c.category() == QChar::Other_Control || c == QLatin1Char('%')
            || c == QLatin1Char('\\') || c == QLatin1Char('\"') || c == QLatin1Char('\'')) return false;
    }
    return true;
}
bool safeUrl(const QUrl &url) {
    return url.isValid() && !url.isRelative() && !url.host().isEmpty()
        && !url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        && !url.hasQuery() && !url.hasFragment() && safeText(url.toString(QUrl::FullyEncoded));
}
bool validOrigin(const QUrl &origin) {
    return safeUrl(origin) && (origin.scheme() == QLatin1String("https") || origin.scheme() == QLatin1String("http"))
        && (origin.path().isEmpty() || origin.path() == QLatin1String("/"));
}
bool sameOrigin(const QUrl &url, const QUrl &origin) {
    return validOrigin(origin) && url.scheme() == origin.scheme()
        && url.host() == origin.host() && url.port(-1) == origin.port(-1);
}
}
std::optional<QString> shareIdFromUrl(const QString &text, const QUrl &webOrigin, const QUrl &legacyServiceOrigin) {
    if (!safeText(text)) return std::nullopt;
    return shareIdFromUrl(QUrl(text, QUrl::StrictMode), webOrigin, legacyServiceOrigin);
}
std::optional<QString> shareIdFromUrl(const QUrl &url, const QUrl &webOrigin, const QUrl &legacyServiceOrigin) {
    if (!safeUrl(url)) return std::nullopt;
    QString prefix;
    bool trailingSlash = true;
    bool publicOrigin = sameOrigin(url, webOrigin);
    // Only the production website has a registered www alias. Fixture hosts stay exact.
    if (!publicOrigin && validOrigin(webOrigin) && webOrigin.host() == QLatin1String("getcolosseum.world")
        && url.host() == QLatin1String("www.getcolosseum.world") && url.scheme() == webOrigin.scheme()
        && url.port(-1) == webOrigin.port(-1)) publicOrigin = true;
    if (url.scheme() == QLatin1String("colosseum") && url.host() == QLatin1String("collection") && url.port(-1) == -1) {
        prefix = QStringLiteral("/"); trailingSlash = false;
    } else if (publicOrigin && url.path(QUrl::FullyEncoded).startsWith(QLatin1String("/c/"))) {
        prefix = QStringLiteral("/c/");
    } else if (sameOrigin(url, legacyServiceOrigin)) {
        prefix = QStringLiteral("/collections/s/");
    } else return std::nullopt;
    QString path = url.path(QUrl::FullyEncoded);
    if (!path.startsWith(prefix)) return std::nullopt;
    QString id = path.mid(prefix.size());
    if (trailingSlash && id.endsWith(QLatin1Char('/'))) id.chop(1);
    return validId(id) ? std::optional<QString>(id) : std::nullopt;
}
QUrl publicShareLink(const QString &id, const QUrl &webOrigin) {
    if (!validId(id) || !validOrigin(webOrigin)) return {};
    QUrl link(webOrigin); link.setPath(QStringLiteral("/c/") + id); return link;
}
QUrl applicationShareLink(const QString &id) {
    return validId(id) ? QUrl(QStringLiteral("colosseum://collection/") + id) : QUrl();
}
}
