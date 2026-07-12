// Colosseum native launcher. Runs the live qml/ tree with an on-disk HTTP cache
// and the same Metahub IPv4 pin Tankoban-3 uses for instant poster loading.

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QHostAddress>
#include <QIcon>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
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

#include "ClipboardHelper.h"
#include "MangaEngine.h"
#include "ProgressStore.h"
#include "SearchHistoryStore.h"
#include "SessionStore.h"
#include "engine/MangaDownloader.h"
#include "engine/BookDownloader.h"
#include "engine/AudiobookDownloader.h"
#include "engine/ComicDownloader.h"
#include "engine/LocalDownloads.h"
#include "engine/ExtensionsStore.h"
#include "reader/BookBridge.h"
#include "player/caststore.h"
#include "player/downloadstore.h"
#include "player/livestore.h"
#include "player/mpvitem.h"
#include "player/powerstore.h"
#include "player/roomstore.h"
#include "player/streamserver.h"
#include "player/windowmodestore.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/BookTorrentDownloader.h"
#include "torrent/BookTorrents.h"

class CachingNam : public QNetworkAccessManager {
public:
    // useCache=false gives a pin+UA NAM with NO disk cache / no PreferCache — for live
    // lanes (torrent search) where a stale cached response would freeze seeder counts.
    CachingNam(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
               QObject *parent = nullptr, bool useCache = true)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_useCache(useCache) {
        if (m_useCache) {
            auto *cache = new QNetworkDiskCache(this);
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                + QStringLiteral("/colosseum-images");
            QDir().mkpath(dir);
            cache->setCacheDirectory(dir);
            cache->setMaximumCacheSize(qint64(1024) * 1024 * 1024);
            setCache(cache);
        }
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

        // Stamp a browser UA on every request that doesn't carry one. The old comment here
        // claimed QML XHRs set their own browser UA — they never could: User-Agent is a
        // restricted XHR header Qt silently DROPS, so every QML request actually went out as
        // "Colosseum/0.1". That identity started 403-ing at LOCG (2026-07-12, "LOCG OFFLINE")
        // and is the long-suspected Fandom-images failure. The browser UA is the fix at the
        // one layer that CAN set it. (C++ engines run their own NAMs and set their own UA.)
        if (r.header(QNetworkRequest::UserAgentHeader).isNull())
            r.setRawHeader("User-Agent",
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
        if (m_useCache)
            r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(op, r, outgoing);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
    bool m_useCache = true;
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

    // Qt Quick Controls style: the default on Windows is the NATIVE style, which refuses to
    // customize a control's contentItem/background — so HouseScrollBar's overrides were IGNORED
    // and a native white scrollbar rendered anyway (Hemanth's "ugly white bar", 2026-07-12).
    // ScrollBar is the ONLY Controls type this app instantiates (RoundButton is a local Item
    // component, not the Controls one), so forcing the fully-customizable Basic style has zero
    // blast radius beyond finally letting HouseScrollBar's gold sliver take. Set via env (pure
    // QtCore) so no QuickControls2 C++ module needs linking; must precede the QML engine.
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Colosseum"));
    // App identity on the Windows taskbar / alt-tab / title: the amphitheatre glyph on a
    // dark tile (embedded via app_resources.qrc). The .exe file icon comes from app_icon.rc.
    app.setWindowIcon(QIcon(QStringLiteral(":/colosseum.ico")));
    // QSettings (every QML `Settings` block — reader prefs, player settings) keys on the
    // organization; without it QSettings::init fails (status 1) and NOTHING persists. This
    // was silently broken forever (Hemanth's log, 2026-07-09).
    // CAVEAT: on Windows, AppDataLocation *includes* the org — so setting it moves the data
    // dir (Roaming/Colosseum -> Roaming/Brotherhood/Colosseum), which would orphan the
    // existing downloads/books/videos/extensions. Capture the old path, set the org, then
    // MOVE the old tree to the new location ONCE (guarded), so nothing is lost.
    const QString oldAppData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    app.setOrganizationName(QStringLiteral("Brotherhood"));
    app.setOrganizationDomain(QStringLiteral("colosseum.brotherhood"));
    const QString newAppData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (oldAppData != newAppData && QDir(oldAppData).exists() && !QDir(newAppData).exists()) {
        QDir().mkpath(QFileInfo(newAppData).absolutePath());
        if (QDir().rename(oldAppData, newAppData))
            qInfo("[migrate] app data %s -> %s (org-name change)",
                  qUtf8Printable(oldAppData), qUtf8Printable(newAppData));
        else
            qWarning("[migrate] FAILED to move app data %s -> %s; downloads may appear missing",
                     qUtf8Printable(oldAppData), qUtf8Printable(newAppData));
    }

    // The video player surface (mpv), reached from QML as `import Colosseum.Player`.
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QQmlApplicationEngine engine;
    // ---- the user lane (argless launch: double-click / shortcut / Colosseum.bat) ----
    // With a QML-path argument (dev.bat, test harnesses) NONE of this runs — that lane
    // is byte-for-byte the old behavior: relative path, no network, no CWD change.
    if (argc <= 1) {
        // 1) self-locate: exe lives at <repo>/native/build-msvc/colosseum.exe. Anchor the
        //    working directory on the repo root so every relative path (live qml/ tree,
        //    disk cache, assets) behaves exactly as under Colosseum.bat's `cd /d %~dp0`.
        QDir root(QCoreApplication::applicationDirPath());
        if (root.cdUp() && root.cdUp() && root.exists(QStringLiteral("qml/Main.qml"))) {
            QDir::setCurrent(root.absolutePath());
            // 2) self-update: pull the latest before the engine reads the QML tree — the
            //    tree is loaded live from disk, so whatever lands here IS this boot.
            //    --ff-only + hard timeout: offline / dirty / diverged → one honest line,
            //    boot as-is. Never stash, never force — a brother's uncommitted work is
            //    sacred. Core (native/) changes need a rebuild — log-only (ratified).
            QProcess pull;
            pull.start(QStringLiteral("git"),
                       { QStringLiteral("-C"), root.absolutePath(),
                         QStringLiteral("pull"), QStringLiteral("--ff-only") });
            if (!pull.waitForFinished(8000)) {
                pull.kill();
                qInfo("[self-update] skipped (git timed out — offline or slow remote)");
            } else if (pull.exitCode() != 0) {
                qInfo("[self-update] skipped (%s)",
                      pull.readAllStandardError().simplified().constData());
            } else {
                const QByteArray out = pull.readAllStandardOutput();
                if (out.contains("Already up to date"))
                    qInfo("[self-update] already current");
                else {
                    qInfo("[self-update] pulled:\n%s", out.trimmed().constData());
                    if (out.contains(" native/"))
                        qInfo("[self-update] core update pulled — rebuild pending "
                              "(engine changes are not live until a brother rebuilds)");
                }
            }
        }
    }
    const QString qmlPath = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("qml/Main.qml");
    const QStringList pinnedHosts = {
        QStringLiteral("live.metahub.space"),
        QStringLiteral("images.metahub.space"),
        // Theatre search + genre browse hosts: both publish AAAA records and Qt tries IPv6
        // first — on this ISP's dead IPv6 route that's a ~21s stall per connection (the TB3
        // scar). Pin them to IPv4 like the poster hosts. (2026-07-05, Theatre search triage)
        QStringLiteral("v3-cinemeta.strem.io"),
        QStringLiteral("cinemeta-catalogs.strem.io"),
        // Manga art lane: AniList (API + banner CDN) and Kitsu (fallback API + media CDN)
        // all publish AAAA records — same dead-IPv6 stall. The API hosts are called from
        // MangaEngine's own NAM (pins passed via setIpv4Pins below); the CDNs are loaded
        // by QML Image through this factory. (2026-07-06, manga art ladder)
        QStringLiteral("graphql.anilist.co"),
        QStringLiteral("s4.anilist.co"),
        QStringLiteral("kitsu.io"),
        QStringLiteral("media.kitsu.app"),
        // Universe banners (2026-07-12 expansion): the Weekly Shonen Jump cover rides
        // Wikimedia, which publishes AAAA records — same dead-IPv6 stall as the rest.
        QStringLiteral("upload.wikimedia.org"),
        // Comics lane (2026-07-12, "no top-10 series has any issues"): BOTH comics API
        // hosts publish AAAA records (getcomics is Cloudflare-fronted) and were never
        // pinned — every attach/search/releases XHR ate the dead-IPv6 stall, so series
        // pages sat empty while curl (happy-eyeballs) worked and hid the root cause.
        // Probe: tests pattern _gc_net_probe — Qt-stack reachability, exit-code verdict.
        QStringLiteral("getcomics.org"),
        QStringLiteral("leagueofcomicgeeks.com"),
        // Book-torrent indexer hosts (2026-07-13): apibay/extto/torrents-csv publish AAAA
        // records → dead-IPv6 ~21s stall unless pinned to IPv4 (same scar as the comics lane).
        QStringLiteral("apibay.org"),
        QStringLiteral("extto.org"),
        QStringLiteral("torrents-csv.com"),
        // Jikan (2026-07-13, genre-pages triage): api.jikan.moe publishes AAAA and was
        // NEVER pinned — every manga genre / Jump registry / Theatre anime call rode the
        // dead-IPv6 stall on top of whatever Jikan itself was doing. Same scar, same fix.
        QStringLiteral("api.jikan.moe")
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
    manga->setIpv4Pins(ipv4ByHost);   // art-lane hosts ride the same IPv4 pins
    engine.rootContext()->setContextProperty(QStringLiteral("Manga"), manga);

    // Download-fed reading backbone exposed to QML as `Downloads`. Reading is never
    // a live stream: a chapter is downloaded to loose local files once, then the
    // reader reads those offline. Own plain NAM (no cache) — it persists to disk itself.
    auto *dlNam = new QNetworkAccessManager(&app);
    auto *downloads = new MangaDownloader(dlNam, &app);
    downloads->setIpv4Pins(ipv4ByHost);   // WeebCentral page-image downloads ride the same pins
    engine.rootContext()->setContextProperty(QStringLiteral("Downloads"), downloads);
    if (qEnvironmentVariableIsSet("COLOSSEUM_DL_SELFTEST"))
        downloads->selfTest(qEnvironmentVariable("COLOSSEUM_DL_SELFTEST"));
    if (qEnvironmentVariableIsSet("COLOSSEUM_INDEX_SELFTEST"))
        downloads->indexSelfTest();

    // Book download backbone (LibGen → local .epub) exposed to QML as `Books`.
    // Same download-fed law as manga: a book is fetched to disk once, then the
    // reader opens the local file (never a stream). Shares the plain uncached NAM.
    auto *books = new BookDownloader(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Books"), books);
    if (qEnvironmentVariableIsSet("COLOSSEUM_BOOK_DLTEST"))
        books->selfTest(qEnvironmentVariable("COLOSSEUM_BOOK_DLTEST"));

    // Western-comics download backbone (GetComics release → archive → local page
    // dir) exposed to QML as `Comics`. BookDownloader lineage: ONE signed-link
    // file per release post, extracted so MangaReader reads it like a chapter.
    auto *comics = new ComicDownloader(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Comics"), comics);
    if (qEnvironmentVariableIsSet("COLOSSEUM_COMIC_DLTEST"))
        comics->selfTest(qEnvironmentVariable("COLOSSEUM_COMIC_DLTEST"));

    // Foliate EPUB reader bridge exposed to the WebEngine reader's QWebChannel as
    // `BookBridge` (a JS shim maps it to window.electronAPI). Ported from TB2.
    auto *bookBridge = new BookBridge(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("BookBridge"), bookBridge);

    // Torrent stream engine (Stremio sidecar) exposed to QML as `Stream`. Lazy: the
    // runtime only spawns on the first Stream.play() call.
    auto *stream = new StreamServer(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Stream"), stream);

    // Audiobook download backbone exposed to QML as `Audiobooks`. BookDownloader
    // lineage, but multi-file and Stremio-fed: a book's paired audiobook torrent
    // (from AudioBookBay) downloads its audio files to <appdata>/audiobooks, keyed
    // by pairKey so the book page flips to "Listen". Needs the Stream engine above.
    auto *audiobooks = new AudiobookDownloader(dlNam, stream, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Audiobooks"), audiobooks);

    // Book torrents shelf (Biblio): federated indexer search + Stremio-fed single-file pull.
    // searchNam = pinned + UA-stamped + UNCACHED CachingNam (live seeder counts, no stale
    // cache); dlNam carries the torrent bytes. pinnedHosts/ipv4ByHost include the 3 indexers.
    auto *searchNam = new CachingNam(pinnedHosts, ipv4ByHost, &app, /*useCache=*/false);
    auto *bookTorrents = new BookTorrents(searchNam, dlNam, stream, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("BookTorrents"), bookTorrents);
    if (qEnvironmentVariableIsSet("COLOSSEUM_ABB_DLTEST")) {
        const QString spec = qEnvironmentVariable("COLOSSEUM_ABB_DLTEST");   // "<pairKey>|<infoHash>"
        const int bar = spec.lastIndexOf(QChar('|'));
        if (bar > 0) audiobooks->selfTest(spec.left(bar), spec.mid(bar + 1));
    }

    // Dev smoke: which book-torrent indexers still work? Runs on the SAME pinned +
    // UA-stamped + UNCACHED NAM production uses, so a slow-but-alive indexer isn't
    // falsely declared dead by an unpinned IPv6 stall. Ends on searchFinished.
    if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_SEARCHTEST")) {
        auto *smokeNam = new CachingNam(pinnedHosts, ipv4ByHost, &app, /*useCache=*/false);
        auto *svc = new TankorentSearchService(smokeNam, &app);
        svc->selfTest(qEnvironmentVariable("COLOSSEUM_TORRENT_SEARCHTEST"));
        QTimer::singleShot(45000, &app, &QCoreApplication::quit);   // hard backstop only
    }
    if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_DLTEST")) {     // "<infoHash>|<title>"
        const QStringList a = qEnvironmentVariable("COLOSSEUM_TORRENT_DLTEST").split(QChar('|'));
        auto* dl = new BookTorrentDownloader(dlNam, stream, &app);
        if (a.size() == 2) dl->selfTest(a[0], a[1]);
        QTimer::singleShot(120000, &app, &QCoreApplication::quit);
    }

    // Cast session state exposed to QML as `Cast`. Network discovery/control is the
    // later backend; this slice gives the player real device/session state today.
    auto *cast = new CastStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Cast"), cast);

    // Current-player video download state exposed to QML as `Download`.
    auto *download = new DownloadStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Download"), download);
    if (qEnvironmentVariableIsSet("COLOSSEUM_VIDEOQ_SELFTEST"))
        download->selfTest(qEnvironmentVariable("COLOSSEUM_VIDEOQ_SELFTEST"));

    // Unified downloads read-model exposed to QML as `LocalDownloads` — the
    // Downloads page renders this; every mutation still routes to the owning
    // backend (Downloads / Books / Comics / Download).
    auto *localDownloads = new LocalDownloads(downloads, books, comics, download, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("LocalDownloads"), localDownloads);

    // Extension registry (Stremio-protocol addons) exposed to QML as `Extensions`.
    // Spec: Brotherhood docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md.
    // Shares the plain uncached NAM — manifests are small JSON, never cache-served.
    auto *extensions = new ExtensionsStore(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Extensions"), extensions);
    // dev harness: COLOSSEUM_OPEN_EXTENSIONS=1 boots straight into the store, so
    // smoke runs exercise the page Loader (QML errors only surface on activation).
    engine.rootContext()->setContextProperty(
        QStringLiteral("DevOpenExtensions"),
        qEnvironmentVariableIsSet("COLOSSEUM_OPEN_EXTENSIONS"));
    // dev harness: COLOSSEUM_STREAMS_SELFTEST="movie|tt0816692" runs the multi-
    // extension stream ask headlessly at boot and logs row counts per extension.
    engine.rootContext()->setContextProperty(
        QStringLiteral("DevStreamsSelfTest"),
        qEnvironmentVariable("COLOSSEUM_STREAMS_SELFTEST"));
    // dev harness: COLOSSEUM_CATALOG_SELFTEST="movies" logs a tab's rows (house +
    // extension shelves) headlessly at boot.
    engine.rootContext()->setContextProperty(
        QStringLiteral("DevCatalogSelfTest"),
        qEnvironmentVariable("COLOSSEUM_CATALOG_SELFTEST"));
    // dev harness: COLOSSEUM_OPEN_WORLD="Theatre" boots straight into a world.
    engine.rootContext()->setContextProperty(
        QStringLiteral("DevOpenWorld"),
        qEnvironmentVariable("COLOSSEUM_OPEN_WORLD"));
    // dev harness: COLOSSEUM_SUBS_SELFTEST="movie|tt0111161" logs subtitle rows
    // per source (multi-well proof) headlessly at boot.
    engine.rootContext()->setContextProperty(
        QStringLiteral("DevSubsSelfTest"),
        qEnvironmentVariable("COLOSSEUM_SUBS_SELFTEST"));

    // Live TV / DVR player state exposed to QML as `Live`.
    auto *live = new LiveStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Live"), live);

    // Watch-room / together backbone exposed to QML as `Room`. This first slice is
    // local and in-process, but it carries the participant/chat/sync model the
    // network transport will publish later.
    auto *room = new RoomStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Room"), room);

    // Native player window modes exposed to QML as `WindowMode` for PiP/fullscreen parity.
    auto *windowMode = new WindowModeStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("WindowMode"), windowMode);

    // Playback power-inhibit exposed to QML as `Power`, matching Harbor's play-only
    // OS/display sleep prevention.
    auto *power = new PowerStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Power"), power);

    // Continue / resume backbone exposed to QML as `Progress`. The player and the
    // manga reader write watch/read progress; every Continue row reads it back.
    // QSettings-backed, so it survives a restart.
    auto *progress = new ProgressStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Progress"), progress);

    // Durable, world-scoped recent searches. Search QML reloads this store when its Loader
    // is recreated, so remote provider success is irrelevant to whether intent is remembered.
    auto *history = new SearchHistoryStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("SearchHistory"), history);

    // System clipboard for QML — the sources sheet's copy-magnet button (spec 2026-07-08).
    auto *clipboard = new ClipboardHelper(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Clipboard"), clipboard);

    // Open-sessions model exposed to QML as `Sessions` - the OS-shell's switcher state
    // (which surfaces are open, which is active, each one's saved-state blob).
    auto *sessions = new SessionStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Sessions"), sessions);
    if (qEnvironmentVariableIsSet("COLOSSEUM_SESSION_SELFTEST"))
        sessions->selfTest();

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
