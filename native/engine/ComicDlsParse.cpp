#include "ComicDlsParse.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>

#include <algorithm>

QStringList parseDlsLinks(const QByteArray& html)
{
    const QString text = QString::fromUtf8(html);
    // Every /dls/ anchor with its inner text, scored: the signed DOWNLOAD NOW
    // (aio-red) button leads, named mirrors follow. Requiring ":" in the payload
    // keeps the bare ad-gate /dls/<token>/ out (the TB2 scar).
    static const QRegularExpression kAnchorRe(
        QStringLiteral(R"RX(<a\b([^>]*)href="(https://getcomics\.org/dls/[^"]*:[^"]+)"([^>]*)>(.*?)</a>)RX"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QList<QPair<int, QString>> scored;
    QSet<QString> seen;
    auto it = kAnchorRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        QString url = m.captured(2);
        url.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        if (seen.contains(url)) continue;
        seen.insert(url);
        const QString attrs = m.captured(1) + m.captured(3);
        const QString inner = m.captured(4);
        // pixeldrain is BLOCKED from this ISP — drop, never fall back to it
        if (attrs.contains(QStringLiteral("pixeldrain"), Qt::CaseInsensitive)
            || inner.contains(QStringLiteral("pixeldrain"), Qt::CaseInsensitive))
            continue;
        int score = 0;
        if (attrs.contains(QStringLiteral("DOWNLOAD NOW"), Qt::CaseInsensitive)
            || inner.contains(QStringLiteral("DOWNLOAD NOW"), Qt::CaseInsensitive)) score += 4;
        if (attrs.contains(QStringLiteral("MAIN SERVER"), Qt::CaseInsensitive))  score += 3;
        if (attrs.contains(QStringLiteral("aio-red"), Qt::CaseInsensitive))      score += 2;
        scored.append({ score, url });
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    QStringList urls;
    for (const auto& p : scored) urls.append(p.second);
    return urls;
}
