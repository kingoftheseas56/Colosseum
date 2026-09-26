#include "WallpaperSchemeHandler.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrl>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

WallpaperSchemeHandler::WallpaperSchemeHandler(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

void WallpaperSchemeHandler::registerScheme()
{
    QWebEngineUrlScheme scheme(QByteArrayLiteral("colosseum-wallpaper"));
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

QString WallpaperSchemeHandler::publicUrl(const QString &source)
{
    const QUrl url(source);
    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")
        || url.scheme() == QLatin1String("qrc")) {
        m_localPath.clear();
        return source;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : source;
    const QFileInfo info(path);
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(info);
    if (!info.isFile() || !info.isReadable()
        || !mime.name().startsWith(QLatin1String("image/"))) {
        m_localPath.clear();
        return {};
    }
    m_localPath = info.canonicalFilePath();
    return QStringLiteral("colosseum-wallpaper:/current?rev=%1")
        .arg(++m_revision);
}

void WallpaperSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    if (job->requestMethod() != QByteArrayLiteral("GET")
        || job->requestUrl().path() != QLatin1String("/current")
        || m_localPath.isEmpty()) {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }
    auto *file = new QFile(m_localPath, job);
    if (!file->open(QIODevice::ReadOnly)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    const QByteArray mime = QMimeDatabase().mimeTypeForFile(m_localPath).name().toLatin1();
    job->reply(mime, file);
}
