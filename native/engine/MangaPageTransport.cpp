#include "MangaPageTransport.h"

#include "TankoyomiIdentity.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QVariantMap>

namespace {

QString safeHttpReferer(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('\r'))
        || trimmed.contains(QLatin1Char('\n'))) {
        return {};
    }
    const QUrl url(trimmed);
    if (!url.isValid()
        || (url.scheme() != QLatin1String("http")
            && url.scheme() != QLatin1String("https"))) {
        return {};
    }
    return url.toString(QUrl::FullyEncoded);
}


} // namespace
QList<PageInfo> MangaPageTransport::normalizeTankoyomiPages(
    const QVariantList &rows, const QString &qualifiedChapterId)
{
    QList<PageInfo> pages;
    int fallbackIndex = 0;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString imageUrl = row.value(QStringLiteral("url")).toString().trimmed();
        if (imageUrl.isEmpty()) continue;

        PageInfo page;
        page.index = row.contains(QStringLiteral("index"))
            ? row.value(QStringLiteral("index")).toInt()
            : fallbackIndex;
        page.imageUrl = imageUrl;
        page.referer = safeHttpReferer(row.value(QStringLiteral("referer")).toString());
        page.pageGroup = row.contains(QStringLiteral("group"))
            ? row.value(QStringLiteral("group")).toInt()
            : -1;
        pages.append(page);
        ++fallbackIndex;
    }
    return pages;
}

QNetworkRequest MangaPageTransport::requestForPage(const PageInfo &page,
                                                   const QString &chapterId)
{
    QNetworkRequest request(QUrl(page.imageUrl));
    request.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/152.0 Safari/537.36");
    request.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
    QString referer = safeHttpReferer(page.referer);
    if (referer.isEmpty() && !TankoyomiIdentity::isQualifiedChapter(chapterId))
        referer = QStringLiteral("https://weebcentral.com/");
    if (!referer.isEmpty())
        request.setRawHeader("Referer", referer.toUtf8());

    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(30000);
    return request;
}
