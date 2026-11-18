#pragma once

#include <QRegularExpression>
#include <QString>
#include <QUrl>

namespace LibriVoxMedia {

constexpr qsizetype maxChapters = 500;

inline bool validProjectId(const QString& id)
{
    static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]{0,9}$"));
    return digits.match(id).hasMatch();
}

inline bool validAudioUrl(const QUrl& url)
{
    const QString host = url.host().toLower();
    const QString path = url.path();
    static const QRegularExpression storagePath(QStringLiteral("^/[0-9]+/items/"));
    return url.isValid() && url.scheme() == QLatin1String("https")
        && (host == QLatin1String("archive.org") || host.endsWith(QLatin1String(".archive.org")))
        && url.userInfo().isEmpty() && url.port(-1) == -1 && !url.hasFragment()
        && url.toEncoded().size() <= 2048 && path.endsWith(QLatin1String(".mp3"), Qt::CaseInsensitive)
        && (path.startsWith(QLatin1String("/download/")) || storagePath.match(path).hasMatch());
}

} // namespace LibriVoxMedia
