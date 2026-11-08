#pragma once

#include <QJsonObject>
#include <QUrl>

namespace NuvioManifest {

inline bool isPreviewCandidate(const QJsonObject &manifest)
{
    return manifest.value(QStringLiteral("scrapers")).isArray()
        && !manifest.value(QStringLiteral("resources")).isArray();
}

inline bool usableUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && !url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        && !url.hasQuery() && !url.hasFragment() && url.toEncoded().size() <= 2048;
}

} // namespace NuvioManifest
