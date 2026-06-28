// Colosseum native launcher. Runs the live qml/ tree with an on-disk HTTP cache
// and the same Metahub IPv4 pin Tankoban-3 uses for instant poster loading.

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QQmlContext>
#include <QQuickWindow>
#include <qqml.h>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QDebug>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include "MangaEngine.h"
#include "ProgressStore.h"
#include "SessionStore.h"
#include "engine/MangaDownloader.h"
#include "engine/BookDownloader.h"
#include "reader/BookBridge.h"
#include "player/mpvitem.h"
#include "player/streamserver.h"

class CachingNam : public QNetworkAccessManager {
public:
    CachingNam(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost, QObject *parent = nullptr)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)) {
        auto *cache = new QNetworkDiskCache(this);
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/colosseum-images");
        QDir().mkpath(dir);
        cache->setCacheDirectory(dir);
        cache->setMaximumCacheSize(qint64(1024) * 1024 * 1024);
        setCache(cache);
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoing) override {
        QNetworkRequest r(req);
        QUrl u = r.url();
        const QString host = u.host();

        if (m_pinnedHosts.contains(host)) {
            r.setRawHeader("Host", host.toUtf8());
            r.setPeerVerifyName(host);
            r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

            const QString ipv4 = m_ipv4ByHost.value(host);
            if (!ipv4.isEmpty()) {
                u.setHost(ipv4);
                r.setUrl(u);
            }
        }

        // Respect a User-Agent the caller already set (the QML XHR sets a browser UA for sources
        // like Fandom / MediaWiki that 403 a bot UA); only stamp our own when none was provided.
        if (r.header(QNetworkRequest::UserAgentHeader).isNull())
            r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum/0.1"));
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
        r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(op, r, outgoing);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
};

class CachingNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    CachingNamFactory(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost)
        : m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)) {}

    QNetworkAccessManager *create(QObject *parent) override {
        return new CachingNam(m_pinnedHosts, m_ipv4ByHost, parent);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
};

static QString resolveIpv4(const QString &host) {
    const QHostInfo info = QHostInfo::fromName(host);
    for (const QHostAddress &address : info.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol)
            return address.toString();
    }
    return {};
}

// Dev-only QML live-reloader: watches the qml/ tree and reloads the root window
// on save, so editing QML feels like Electron's `npm run dev`. Constructed ONLY
// when COLOSSEUM_DEV is set (dev.bat sets it); the normal launcher never makes one.
class QmlReloader : public QObject {
public:
    QmlReloader(QQmlApplicationEngine *engine, const QString &qmlPath, QObject *parent = nullptr)
        : QObject(parent), m_engine(engine) {
        m_qmlPath = QFileInfo(qmlPath).absoluteFilePath();
        m_watchDir = QFileInfo(m_qmlPath).absolutePath();

        m_debounce.setSingleShot(true);
        m_debounce.setInterval(150);  // coalesce an editor's save-burst into one reload
        QObject::connect(&m_debounce, &QTimer::timeout, this, [this] { reload(); });
        QObject::connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
                         [this](const QString &) { m_debounce.start(); });
        QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
                         [this](const QString &) { rescan(); m_debounce.start(); });
        rescan();
        qInfo("[dev] live-reload watching %s", qUtf8Printable(m_watchDir));
    }

private:
    // (Re)watch every .qml/.js under the tree. Many editors save via temp-file +
    // rename, which silently drops that file's watch — so we re-add on every pass.
    void rescan() {
        QStringList found;
        QDirIterator it(m_watchDir, {QStringLiteral("*.qml"), QStringLiteral("*.js")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) found << it.next();
        const QStringList watched = m_watcher.files();
        QStringList toAdd;
        for (const QString &p : found)
            if (!watched.contains(p)) toAdd << p;
        if (!toAdd.isEmpty()) m_watcher.addPaths(toAdd);
        if (!m_watcher.directories().contains(m_watchDir)) m_watcher.addPath(m_watchDir);
    }

    void reload() {
        rescan();  // re-arm any watches dropped by atomic saves
        const QList<QObject *> oldRoots = m_engine->rootObjects();
        m_engine->clearComponentCache();
        m_engine->load(QUrl::fromLocalFile(m_qmlPath));
        for (QObject *o : oldRoots) o->deleteLater();  // drop the previous window
        qInfo("[dev] reloaded");
    }

    QQmlApplicationEngine *m_engine;
    QString m_qmlPath;
    QString m_watchDir;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

int main(int argc, char *argv[]) {
    // mpvqt renders through OpenGL, so the whole Quick scene must use the OpenGL RHI
    // backend (set process-wide, before the QGuiApplication). Proven 2026-06-27 that
    // Colosseum's frosted glass survives this — the player path's one prerequisite.
    // WebEngine (the foliate EPUB reader) also rides OpenGL: share contexts + init it
    // before the QGuiApplication, alongside the RHI pick. All three must precede app.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Colosseum"));

    // The video player surface (mpv), reached from QML as `import Colosseum.Player`.
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QQmlApplicationEngine engine;
    const QStringList pinnedHosts = {
        QStringLiteral("live.metahub.space"),
        QStringLiteral("images.metahub.space")
    };
    QHash<QString, QString> ipv4ByHost;
    for (const QString &host : pinnedHosts) {
        const QString ipv4 = resolveIpv4(host);
        if (!ipv4.isEmpty())
            ipv4ByHost.insert(host, ipv4);
    }
    engine.setNetworkAccessManagerFactory(new CachingNamFactory(pinnedHosts, ipv4ByHost));

    // Native manga engine (WeebCentral) exposed to QML as `Manga`.
    auto *manga = new MangaEngine(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Manga"), manga);

    // Download-fed reading backbone exposed to QML as `Downloads`. Reading is never
    // a live stream: a chapter is downloaded to loose local files once, then the
    // reader reads those offline. Own plain NAM (no cache) — it persists to disk itself.
    auto *dlNam = new QNetworkAccessManager(&app);
    auto *downloads = new MangaDownloader(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Downloads"), downloads);
    if (qEnvironmentVariableIsSet("COLOSSEUM_DL_SELFTEST"))
        downloads->selfTest(qEnvironmentVariable("COLOSSEUM_DL_SELFTEST"));

    // Book download backbone (LibGen → local .epub) exposed to QML as `Books`.
    // Same download-fed law as manga: a book is fetched to disk once, then the
    // reader opens the local file (never a stream). Shares the plain uncached NAM.
    auto *books = new BookDownloader(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Books"), books);
    if (qEnvironmentVariableIsSet("COLOSSEUM_BOOK_DLTEST"))
        books->selfTest(qEnvironmentVariable("COLOSSEUM_BOOK_DLTEST"));

    // Foliate EPUB reader bridge exposed to the WebEngine reader's QWebChannel as
    // `BookBridge` (a JS shim maps it to window.electronAPI). Ported from TB2.
    auto *bookBridge = new BookBridge(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("BookBridge"), bookBridge);

    // Torrent stream engine (Stremio sidecar) exposed to QML as `Stream`. Lazy: the
    // runtime only spawns on the first Stream.play() call.
    auto *stream = new StreamServer(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Stream"), stream);

    // Continue / resume backbone exposed to QML as `Progress`. The player and the
    // manga reader write watch/read progress; every Continue row reads it back.
    // QSettings-backed, so it survives a restart.
    auto *progress = new ProgressStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Progress"), progress);

    // Open-sessions model exposed to QML as `Sessions` - the OS-shell's switcher state
    // (which surfaces are open, which is active, each one's saved-state blob).
    auto *sessions = new SessionStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Sessions"), sessions);
    if (qEnvironmentVariableIsSet("COLOSSEUM_SESSION_SELFTEST"))
        sessions->selfTest();

    const QString qmlPath = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("qml/Main.qml");
    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty())
        return -1;

    // Live-reload only in dev (dev.bat sets COLOSSEUM_DEV). Production is untouched.
    if (qEnvironmentVariableIsSet("COLOSSEUM_DEV")) {
        new QmlReloader(&engine, qmlPath, &app);
        manga->selfTest(QStringLiteral("Berserk"));  // log WeebCentral chapter count at startup
        manga->volumes(QStringLiteral("One Piece"));  // DEBUG: log MangaDex volume resolution
    }

    return app.exec();
}
