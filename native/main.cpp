// Colosseum native launcher. Runs the live qml/ tree with an on-disk HTTP cache
// and the same Metahub IPv4 pin Tankoban-3 uses for instant poster loading.

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QHostAddress>
#include <QIcon>
#include <QImageReader>
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
#include <QThread>
#include <QTimer>

#include "ClipboardHelper.h"
#include "MangaEngine.h"
#include "ProgressStore.h"
#include "CollectionStore.h"
#include "SearchHistoryStore.h"
#include "SessionStore.h"
#include "AudioPairingStore.h"
#include "work/BackgroundActivityRegistry.h"
#include "work/BackgroundWorkCoordinator.h"
#include "third_party/miniz/miniz.h"  // gunzip for the Jikan Accept-Encoding workaround
#include "engine/MangaDownloader.h"
#include "engine/BookDownloader.h"
#include "engine/AudiobookDownloader.h"
#include "engine/ComicDownloader.h"
#include "engine/ComicsCatalog.h"
#include "engine/MalCatalog.h"
#include "engine/LocalDownloads.h"
#include "engine/ExtensionsStore.h"
#include "engine/MangaTankobanService.h"
#include "net/LoopbackPinProxy.h"
#include "net/PinProxyFactory.h"
#include "net/PosterScoreboard.h"
#include <QNetworkProxyFactory>
#include <QSet>
#include <algorithm>
#include "anime/AnimeOrderService.h"
#include "reader/BookBridge.h"
#include "reader2/Reader2Bridge.h"
#include "player/caststore.h"
#include "player/downloadstore.h"
#include "player/livestore.h"
#include "player/mpvitem.h"
#include "player/seekthumbnailer.h"
#include "player/powerstore.h"
#include "player/roomstore.h"
#include "player/streamserver.h"
#include "player/windowmodestore.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/BookTorrentDownloader.h"
#include "torrent/BookTorrents.h"
#include "torrent/engine/TorrentEngine.h"

// gzip = 10-byte header (+ optional fields) + raw DEFLATE + 8-byte trailer.
// Strip the header, raw-inflate with miniz's tinfl. Empty on any malformation.
static QByteArray gunzip(const QByteArray &in) {
    if (in.size() < 18 || static_cast<quint8>(in[0]) != 0x1f
        || static_cast<quint8>(in[1]) != 0x8b)
        return {};
    int idx = 10;
    const quint8 flg = static_cast<quint8>(in[3]);
    if (flg & 0x04) { // FEXTRA
        const int xlen = static_cast<quint8>(in[idx]) | (static_cast<quint8>(in[idx + 1]) << 8);
        idx += 2 + xlen;
    }
    if (flg & 0x08) { while (idx < in.size() && in[idx] != 0) ++idx; ++idx; } // FNAME
    if (flg & 0x10) { while (idx < in.size() && in[idx] != 0) ++idx; ++idx; } // FCOMMENT
    if (flg & 0x02) idx += 2;                                                 // FHCRC
    if (idx >= in.size() - 8)
        return {};
    size_t outLen = 0;
    void *out = tinfl_decompress_mem_to_heap(in.constData() + idx,
                                             static_cast<size_t>(in.size() - idx - 8), &outLen, 0);
    if (!out)
        return {};
    QByteArray result(static_cast<const char *>(out), static_cast<int>(outLen));
    mz_free(out);
    return result;
}

// Transparent gzip-decompressing reply. api.jikan.moe's origin returns 504 for
// Qt's default multi-codec Accept-Encoding but 200 for a plain "gzip" request —
// and Qt does NOT auto-decompress a manually-set encoding, so we buffer the inner
// reply and gunzip it before QML's XMLHttpRequest reads responseText. No Q_OBJECT:
// only inherited QNetworkReply signals are emitted (matches this file's style).
class GunzipReply : public QNetworkReply {
public:
    explicit GunzipReply(QNetworkReply *inner) : m_inner(inner) {
        m_inner->setParent(this);
        setOperation(m_inner->operation());
        setRequest(m_inner->request());
        setUrl(m_inner->url());
        setOpenMode(QIODevice::ReadOnly);
        QObject::connect(m_inner, &QNetworkReply::finished, this, [this] { finalize(); });
    }
    void abort() override { m_inner->abort(); }
    qint64 bytesAvailable() const override {
        return QNetworkReply::bytesAvailable() + (m_buffer.size() - m_pos);
    }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxlen) override {
        const qint64 avail = m_buffer.size() - m_pos;
        if (avail <= 0)
            return m_done ? -1 : 0;
        const qint64 n = qMin(maxlen, avail);
        memcpy(data, m_buffer.constData() + m_pos, n);
        m_pos += n;
        return n;
    }

private:
    void finalize() {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,
                     m_inner->attribute(QNetworkRequest::HttpStatusCodeAttribute));
        setAttribute(QNetworkRequest::HttpReasonPhraseAttribute,
                     m_inner->attribute(QNetworkRequest::HttpReasonPhraseAttribute));
        const auto pairs = m_inner->rawHeaderPairs();
        for (const auto &h : pairs) {
            if (qstricmp(h.first.constData(), "Content-Encoding") == 0
                || qstricmp(h.first.constData(), "Content-Length") == 0)
                continue;
            setRawHeader(h.first, h.second);
        }
        const QByteArray raw = m_inner->readAll();
        const QByteArray enc = m_inner->rawHeader("Content-Encoding");
        if (qstricmp(enc.constData(), "gzip") == 0) {
            const QByteArray plain = gunzip(raw);
            m_buffer = plain.isEmpty() ? raw : plain; // fall back to raw if gunzip fails
        } else {
            m_buffer = raw;
        }
        setHeader(QNetworkRequest::ContentLengthHeader, m_buffer.size());
        if (m_inner->error() != QNetworkReply::NoError)
            setError(m_inner->error(), m_inner->errorString());
        m_done = true;
        emit metaDataChanged();
        if (m_inner->error() != QNetworkReply::NoError)
            emit errorOccurred(m_inner->error());
        emit readyRead();
        setFinished(true);
        emit finished();
    }

    QNetworkReply *m_inner;
    QByteArray m_buffer;
    qint64 m_pos = 0;
    bool m_done = false;
};

class CachingNam : public QNetworkAccessManager {
public:
    // useCache=false gives a pin+UA NAM with NO disk cache / no PreferCache — for live
    // lanes (torrent search) where a stale cached response would freeze seeder counts.
    CachingNam(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
               QObject *parent = nullptr, bool useCache = true,
               PosterScoreboard *scoreboard = nullptr)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_useCache(useCache),
          m_scoreboard(scoreboard) {
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
            // HTTP/2 MUST stay OFF for URL-rewrite-pinned hosts: rewriting the URL to
            // the IPv4 literal below makes h2's :authority the IP, which servers like
            // metahub reject ("HTTP/2 protocol error"), forcing a slower failed-h2→h1
            // retry (confirmed 2026-07-23). metahub itself no longer reaches this path
            // — it's pinned at the CONNECTION layer by the loopback concierge (spec
            // 2026-07-23), keeping its hostname so h2 works. This branch now governs
            // the JSON hosts and the concierge-unavailable fallback only.
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

        // Jikan's origin proxy returns 504 for Qt's default multi-codec Accept-Encoding
        // (br/zstd) but 200 for a plain "gzip" — this silently emptied every Jikan-fed
        // surface (genre index / Jump registry / Theatre anime). Force gzip and hand the
        // reply through GunzipReply, which decompresses it since Qt won't for a manual
        // encoding. Scoped to api.jikan.moe so nothing else changes shape.
        if (host == QLatin1String("api.jikan.moe")) {
            r.setRawHeader("Accept-Encoding", "gzip");
            QNetworkReply *inner = QNetworkAccessManager::createRequest(op, r, outgoing);
            watch(host, inner);   // watch the INNER reply: GunzipReply doesn't forward attributes
            return new GunzipReply(inner);
        }
        QNetworkReply *reply = QNetworkAccessManager::createRequest(op, r, outgoing);
        watch(host, reply);
        return reply;
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
    bool m_useCache = true;
    PosterScoreboard *m_scoreboard = nullptr;

    void watch(const QString &host, QNetworkReply *reply) {
        if (!m_scoreboard)
            return;
        PosterScoreboard *scoreboard = m_scoreboard;
        // No receiver context on purpose: the lambda runs on the reply's own thread and
        // record() is mutex-guarded. `host` is the ORIGINAL hostname — reply->url() may
        // carry the rewritten IPv4 literal for URL-pinned hosts.
        QObject::connect(reply, &QNetworkReply::finished, [scoreboard, host, reply] {
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const QVariant clen = reply->header(QNetworkRequest::ContentLengthHeader);
            const qint64 bytes = clen.isValid() ? clen.toLongLong() : reply->bytesAvailable();
            scoreboard->record(host, status, ct, bytes,
                               reply->error() != QNetworkReply::NoError);
        });
    }
};

class CachingNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    CachingNamFactory(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
                      PosterScoreboard *scoreboard)
        : m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_scoreboard(scoreboard) {}

    QNetworkAccessManager *create(QObject *parent) override {
        return new CachingNam(m_pinnedHosts, m_ipv4ByHost, parent, /*useCache=*/true, m_scoreboard);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
    PosterScoreboard *m_scoreboard = nullptr;   // owned by the app, outlives every NAM
};

// Resolve a host's IPv4, retrying briefly on a miss. The pin is computed ONCE at
// boot; a transient DNS race at startup would otherwise leave the host UNPINNED
// for the whole session, sending every request to it into the dead-AAAA IPv6
// stall (the 2026-07-13 Jikan scar — genre index / Jump registry / Theatre anime
// all silently emptied). A short backoff-retry makes a cold-DNS boot survivable.
static QString resolveIpv4(const QString &host) {
    for (int attempt = 1; attempt <= 4; ++attempt) {
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol)
                return address.toString();
        }
        if (attempt < 4)
            QThread::msleep(250);  // DNS can be momentarily cold at boot; give it a beat
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

    // Test-only AppData isolation (Task 11, COLOSSEUM_COMIC_PACK_DLTEST gate).
    // Qt's Windows QStandardPaths backend reads AppDataLocation from the
    // registry (SHGetKnownFolderPath), NOT the $env:APPDATA process variable —
    // verified empirically (a probe with $env:APPDATA overridden still
    // resolved the real Roaming path). QStandardPaths DOES re-resolve from the
    // CURRENT applicationName on every call, so a per-run name suffix reliably
    // redirects every AppData-backed store (comics index, the pack transport's
    // ledger/staging/torrent dirs, QSettings, ...) to a disposable sibling
    // folder — never the real Roaming/Brotherhood/Colosseum tree a brother's
    // real downloads live in. Applied AFTER the migration above so that
    // one-time logic is never confused by a suffixed name. Runs ONLY when the
    // env var is set — an ordinary launch is byte-for-byte unaffected.
    if (qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG")) {
        app.setApplicationName(QStringLiteral("Colosseum-dltest-")
            + qEnvironmentVariable("COLOSSEUM_APPDATA_TAG"));
    }

    // The video player surface (mpv), reached from QML as `import Colosseum.Player`.
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");
    qmlRegisterType<SeekThumbnailer>("Colosseum.Player", 1, 0, "SeekThumbnailer");

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
        QStringLiteral("api.jikan.moe"),
        // Tankoban Mode (2026-07-15, eyes-on): per-volume MangaDex cover thumbnails ride
        // uploads.mangadex.org (QML Image via this factory); the volume catalog rides
        // api.mangadex.org (MangaEngine NAM, pins passed via setIpv4Pins); Apple Books +
        // Open Library per-volume synopsis lookups ride itunes.apple.com / openlibrary.org.
        // All publish a dead AAAA on this ISP → ~21s stall per request (covers never
        // painted; Apple synopses trickled a scattered few). Same scar, same fix. NOTE:
        // Open Library's IPv4 is ALSO ISP-firewalled — pinning only makes it fail fast so
        // the Apple fallback runs sooner; it never returns data here.
        QStringLiteral("uploads.mangadex.org"),
        QStringLiteral("api.mangadex.org"),
        QStringLiteral("itunes.apple.com"),
        QStringLiteral("openlibrary.org"),
        // Wallpaper CDN (WallpaperApi.js): unpinned, its requests rode the same dead-AAAA
        // ISP stall as the Jikan scar, so walls silently fell back to the packaged
        // captured-motion asset (humbled-current recap 2026-07-24). Same scar, same fix.
        QStringLiteral("wsrv.nl")
    };
    QHash<QString, QString> ipv4ByHost;
    for (const QString &host : pinnedHosts) {
        const QString ipv4 = resolveIpv4(host);
        if (!ipv4.isEmpty()) {
            ipv4ByHost.insert(host, ipv4);
            qInfo("[net] IPv4-pinned %s -> %s", qUtf8Printable(host), qUtf8Printable(ipv4));
        } else {
            // A pinned host with no IPv4 falls through to normal DNS -> the IPv6
            // stall. Never let that be silent again: this WARNING is the smoking
            // gun for an emptied Jikan/MangaDex/Apple surface.
            qWarning("[net] NO IPv4 for %s after retries -> its requests ride the IPv6 stall",
                     qUtf8Printable(host));
        }
    }
    // Instant posters (spec 2026-07-23): keep the hostname in metahub requests so
    // HTTP/2 negotiates (SNI / cert / :authority all correct → the whole poster wall
    // multiplexes over ONE connection), while a loopback CONNECT concierge pins the
    // CONNECTION to metahub's IPv4 — dodging the dead-IPv6 stall the URL-rewrite pin
    // exists for. If the concierge can't bind, metahub stays URL-rewrite-pinned
    // (today's slower HTTP/1.1 path); posters never break.
    const QSet<QString> metahubHosts = {
        QStringLiteral("live.metahub.space"), QStringLiteral("images.metahub.space")
    };
    auto *concierge = new LoopbackPinProxy(ipv4ByHost, &app);
    const bool conciergeOk = concierge->start();
    if (conciergeOk)
        qInfo("[net] connection concierge on 127.0.0.1:%u (metahub -> HTTP/2)", concierge->port());
    else
        qWarning("[net] concierge could not bind -> metahub falls back to URL-pin + HTTP/1.1");
    QNetworkProxyFactory::setApplicationProxyFactory(
        new PinProxyFactory(metahubHosts, conciergeOk ? concierge->port() : quint16(0), conciergeOk));

    // When the concierge is up, metahub leaves the URL-rewrite pin set (its URL keeps
    // the hostname, h2 stays on, the proxy carries the connection). Everything else —
    // and metahub in the fallback — keeps today's URL-rewrite pin.
    QStringList namPinnedHosts = pinnedHosts;
    if (conciergeOk)
        namPinnedHosts.erase(std::remove_if(namPinnedHosts.begin(), namPinnedHosts.end(),
            [&](const QString &h){ return metahubHosts.contains(h); }), namPinnedHosts.end());
    // Felt-speed Stage 0: the poster scoreboard. Counts every reply on the QML image
    // NAM; dumped at quit so a real run answers "did the posters actually arrive?"
    // with numbers instead of a shrug. The webp check is the dev-hack scar made loud:
    // the decoder must ship BESIDE the exe (deploy-runtime.bat), not live in the Qt install.
    auto *scoreboard = new PosterScoreboard(&app);
    {
        const bool webpOk =
            QImageReader::supportedImageFormats().contains(QByteArrayLiteral("webp"));
        scoreboard->setWebpDecoderPresent(webpOk);
        if (webpOk)
            qInfo("[img] webp decoder present");
        else
            qWarning("[img] webp decoder MISSING -> every image/webp poster is UNDECODABLE "
                     "(run native/deploy-runtime.bat to bundle qwebp.dll)");
    }
    engine.setNetworkAccessManagerFactory(
        new CachingNamFactory(namPinnedHosts, ipv4ByHost, scoreboard));
    engine.rootContext()->setContextProperty(QStringLiteral("NetScoreboard"), scoreboard);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, scoreboard, [scoreboard] {
        const QString text = scoreboard->summaryText();
        if (!text.isEmpty())
            qInfo("[net] poster scoreboard (arrived/failed/undecodable/bytes by host):\n%s",
                  qUtf8Printable(text));
    });

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
    // Foliate EPUB reader bridge exposed to the WebEngine reader's QWebChannel as
    // `BookBridge` (a JS shim maps it to window.electronAPI). Ported from TB2.
    auto *bookBridge = new BookBridge(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("BookBridge"), bookBridge);

    // The FRESH reader's native seam (Task 16 swap): Biblio's reader layer is
    // qml/reader2/ReaderShell over the vendored Anx foliate paper. QML sees the full
    // bridge; the paper's QWebChannel gets ONLY its filesRead/paperEvent gate
    // (Reader2Bridge.paperGate — least privilege). Shares BookStores files with
    // BookBridge byte-identically, so old-reader progress/marks resume unchanged.
    // BookBridge stays constructed above for the remaining callers (audiobook strip).
    auto *reader2Bridge = new Reader2Bridge(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Reader2Bridge"), reader2Bridge);

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

    // ── Tankorent engine (Phase 2 — books consume it) ───────────────────────
    // One engine per job (spec 2026-07-13): Stremio keeps watch-now; this
    // embedded libtorrent engine owns download-to-keep. BookTorrents is its
    // first consumer — it lazy-start()s the engine on the first book download,
    // so an idle app still touches no network (born-asleep, 4fbb1c2). Manga
    // volumes join in Phase 3. Session/resume state under Colosseum's OWN
    // appdata; download-and-stop seeding posture (pause 1 s after seeding).
    const QString torrentEngineDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/torrent-engine");
    QDir().mkpath(torrentEngineDir);
    auto *torrentEngine = new TorrentEngine(torrentEngineDir, &app);
    torrentEngine->setGlobalSeedingRules(0.f, 1);

    // Book torrents shelf (Biblio): federated indexer search + engine-fed single-file pull.
    // searchNam = pinned + UA-stamped + UNCACHED CachingNam (live seeder counts, no stale
    // cache); torrentEngine carries the download bytes. pinnedHosts/ipv4ByHost include the
    // 3 indexers.
    auto *searchNam = new CachingNam(pinnedHosts, ipv4ByHost, &app, /*useCache=*/false);

    // Comics keeps one public reader/download identity (`Comics`) while privately
    // composing torrent search + archive download with its proven extraction/index.
    auto *comics = new ComicDownloader(dlNam, searchNam, torrentEngine, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("Comics"), comics);
    if (qEnvironmentVariableIsSet("COLOSSEUM_COMIC_DLTEST"))
        comics->selfTest(qEnvironmentVariable("COLOSSEUM_COMIC_DLTEST"));

    // Availability-first SQLite catalogue (spec 2026-07-17): read-only seam; the db
    // is pipeline-deployed to data/ (cwd = repo root), dormant when absent.
    auto *comicsCatalog = new ComicsCatalog(QStringLiteral("data/comics_catalog.db"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("ComicsCatalog"), comicsCatalog);

    // Baked MyAnimeList catalog (genre-page revival 2026-07-18): same doctrine —
    // read-only seam, script-built db in data/, dormant when absent (live ladder runs).
    auto *malCatalog = new MalCatalog(QStringLiteral("data/mal_catalog.db"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("MalCatalog"), malCatalog);

    auto *bookTorrents = new BookTorrents(searchNam, torrentEngine, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("BookTorrents"), bookTorrents);

    // Tankoban "volume mode" façade exposed to QML as `TankobanVolumes` (Task 8).
    // ONE object composes the whole volume lifecycle: Nyaa search over the pinned
    // uncached searchNam, per-volume download through the shared TorrentEngine
    // (wrapped in a queued IMangaTorrentEngine adapter built inside), the
    // WeebCentral chapter-pack fallback + synopsis enrichment over the plain
    // dlNam, and ingestion into a durable local index under AppDataLocation.
    auto *tankobanVolumes = new MangaTankobanService(searchNam, dlNam, torrentEngine, QString(), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("TankobanVolumes"), tankobanVolumes);
    if (qEnvironmentVariableIsSet("COLOSSEUM_TANKOBAN_DLTEST")) {
        // Honest end-to-end self-test (Task 11). Spec:
        //   "<magnet-or-infohash>|<seriesId>|<seriesTitle>|<volumeNumber>".
        // Drives search-cache-bypassed download → ingest → index and exits 0 on
        // ready pages / 2 on any failure (240 s backstop). Mirrors the
        // COLOSSEUM_COMIC_DLTEST / COLOSSEUM_TORRENT_DLTEST self-test idiom above.
        // Never runs unless the env var is set (an idle app touches no network).
        tankobanVolumes->runDownloadSelfTest(
            qEnvironmentVariable("COLOSSEUM_TANKOBAN_DLTEST"));
    }

    if (qEnvironmentVariableIsSet("COLOSSEUM_COMIC_PACK_DLTEST")) {
        // Honest end-to-end self-test (Task 11) for the Tankorent Comic
        // shared-infohash pack-selection transport. Spec:
        //   "<scenario>|<magnet>|<fixture-id>[|<fixture-id2>]"
        // scenario in {single, issues, shared, restart}. Drives the REAL
        // production downloadTorrentEdition() path against a legal loopback
        // seeder (tests/comic_torrent_pack_seed_harness.cpp), prints
        // "COMIC_PACK_<SCENARIO>_DONE pages=<n> [groups=<n>]" and exits 0, or
        // "[comic-pack-dltest] FAIL <reason>" and exits 2. Mirrors the
        // COLOSSEUM_TANKOBAN_DLTEST self-test idiom above. Never runs unless
        // the env var is set.
        comics->runPackSelfTest(qEnvironmentVariable("COLOSSEUM_COMIC_PACK_DLTEST"));
    }

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
    if (qEnvironmentVariableIsSet("COLOSSEUM_TORRENT_DLTEST")) {
        // Book lane: "<infoHash>|<title>". Comics end-to-end lane:
        // "<infoHash>|<series title>|<edition title>" -> archive -> extract -> localPages.
        const QStringList a = qEnvironmentVariable("COLOSSEUM_TORRENT_DLTEST").split(QChar('|'));
        if (a.size() == 3) {
            comics->selfTestTorrent(a[0], a[1], a[2]);
        } else if (a.size() == 2) {
            auto* dl = new BookTorrentDownloader(torrentEngine, &app);
            dl->selfTest(a[0], a[1]);
        }
        QTimer::singleShot(240000, &app, []() {   // >= live gate's 240s DHT wait
            qWarning() << "[bt-dl] FAIL timeout — no DONE/FAIL after 240s";
            QCoreApplication::exit(2);            // honest failing verdict, never a silent exit-0 green
        });
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
    auto *localDownloads = new LocalDownloads(downloads, books, comics, download,
                                              tankobanVolumes, &app);
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
    // bakeoff harness (long-strip reader bakeoff, spec 2026-07-15): point
    // COLOSSEUM_BAKEOFF_STRIP at an extracted fixture page directory and the shell
    // boots a page-only MangaReader over those exact bytes. Adapter only — the
    // production reader component and its scroll behavior are untouched.
    {
        const QString bakeoffDir = qEnvironmentVariable("COLOSSEUM_BAKEOFF_STRIP");
        QVariantList bakeoffPages;
        if (!bakeoffDir.isEmpty()) {
            QDir dir(bakeoffDir);
            const QStringList names = dir.entryList(
                QStringList() << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg")
                              << QStringLiteral("*.png") << QStringLiteral("*.webp"),
                QDir::Files, QDir::Name);
            int idx = 0;
            for (const QString& name : names) {
                QVariantMap page;
                page[QStringLiteral("index")] = idx++;
                page[QStringLiteral("url")] =
                    QUrl::fromLocalFile(dir.absoluteFilePath(name)).toString();
                page[QStringLiteral("group")] = -1;
                bakeoffPages.append(page);
            }
            qInfo() << "[bakeoff] strip fixture pages:" << bakeoffPages.size()
                    << "from" << bakeoffDir;
        }
        engine.rootContext()->setContextProperty(
            QStringLiteral("DevBakeoffStripPages"), bakeoffPages);
    }
    // dev harness: COLOSSEUM_SUBS_SELFTEST="movie|tt0111161" logs subtitle rows
    // per source (multi-well proof) headlessly at boot.
    engine.rootContext()->setContextProperty(
        QStringLiteral("DevSubsSelfTest"),
        qEnvironmentVariable("COLOSSEUM_SUBS_SELFTEST"));

    // Live TV / DVR player state exposed to QML as `Live`.
    auto *live = new LiveStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Live"), live);

    // Shared background-work spine: ONE coordinator (one worker) for offline-analysis
    // domains. Kept as the unified activity surface QML reads via `BackgroundActivity`.
    auto *backgroundWork = new work::BackgroundWorkCoordinator(1, &app);
    Q_UNUSED(backgroundWork);
    auto *backgroundActivity = new work::BackgroundActivityRegistry(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("BackgroundActivity"),
                                             backgroundActivity);

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

    // Your Collection shelf exposed to QML as `Collection`: what the user CHOSE to
    // save via the + Library toggle, distinct from Progress's auto-tracked history.
    auto *collection = new CollectionStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Collection"), collection);

    // Read-along pairings: which audiobook is linked to which book + chapter map.
    // The reader's Audio tab writes it; opening a book reads it to auto-summon the
    // paired audiobook at the right chapter. QSettings-backed, survives a restart.
    auto *audioPairing = new AudioPairingStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioPairing"), audioPairing);

    // Hand the reader bridge the SAME audiobook library + pairing store QML uses,
    // so a pairing saved in the reader's Audio tab is what the auto-load path reads.
    bookBridge->setAudiobooks(audiobooks);
    bookBridge->setPairing(audioPairing);

    // Read-along auto-attach (Task 12): when an audiobook finishes downloading from a
    // book's page, the downloader writes the pairing under the reader's bookId itself —
    // no pairing UI. Same store instance QML/BookBridge use, so the Audio tab reads it.
    audiobooks->setPairing(audioPairing);

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

    // Keyless anime ordering (spec 2026-07-15): downloads two community mapping
    // datasets at runtime, caches immutable generations, and hands Theatre/player
    // a cheap synchronous resolver as `AnimeOrder`. Shares the plain download NAM;
    // never blocks first paint (progressive enhancement).
    auto *animeOrder = new AnimeOrderService(dlNam, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("AnimeOrder"), animeOrder);

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
