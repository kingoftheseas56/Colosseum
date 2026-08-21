// Colosseum native launcher. Runs the live qml/ tree with an on-disk HTTP cache
// and the same Metahub IPv4 pin Tankoban-3 uses for instant poster loading.

#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QHostAddress>
#include <QIcon>
#include <QImageReader>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QThread>
#include <QTimer>

#include <memory>
#include <functional>

#include "ClipboardHelper.h"
#include "MangaEngine.h"
#include "ProgressStore.h"
#include "CollectionStore.h"
#include "SearchHistoryStore.h"
#include "SessionStore.h"
#include "AudioPairingStore.h"
#include "account/AccountRuntime.h"
#include "account/ActivityPlaybackTracker.h"
#include "update/UpdateCache.h"
#include "update/UpdateDownload.h"
#include "update/UpdateInstallBridge.h"
#include "update/UpdateReleaseClient.h"
#include "update/UpdateService.h"
#include "update/UpdateTrust.h"
#include "work/BackgroundActivityRegistry.h"
#include "work/BackgroundWorkCoordinator.h"
#include "third_party/miniz/miniz.h"  // gunzip for the Jikan Accept-Encoding workaround
#include "engine/MangaDownloader.h"
#include "devtools/LanistaServer.h"
#include "engine/BookDownloader.h"
#include "engine/AudiobookDownloader.h"
#include "engine/ComicDownloader.h"
#include "engine/ComicsCatalog.h"
#include "engine/MalCatalog.h"
#include "engine/TankobanCatalog.h"
#include "engine/TankobanChapterMigration.h"
#include "engine/ImdbCatalog.h"
#include "engine/BiblioCatalog.h"
#include "engine/LocalDownloads.h"
#include "engine/AppLog.h"
#include "engine/ExtensionsStore.h"
#include "engine/MangaTankobanService.h"
#include "engine/LocalLaunch.h"
#include "engine/VaultPageStore.h"
#include "engine/VaultIndex.h"
#include "engine/VaultConfig.h"
#include "engine/VaultIdentity.h"
#include "engine/VaultScanner.h"
#include "engine/VaultLibrary.h"
#include "engine/VaultIdentifier.h"
#include "engine/VaultDownloadsRoot.h"
#include "engine/VaultEnricher.h"
#include "engine/VaultForensics.h"
#include "player/MediaAdmissionProbe.h"
#include "net/LoopbackPinProxy.h"
#include "net/PinProxyFactory.h"
#include "net/PosterScoreboard.h"
#include "net/BiblioImageDiag.h"
#include <QNetworkProxyFactory>
#include <QSet>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <algorithm>
#include "anime/AnimeOrderService.h"
#include "reader2/Reader2Bridge.h"
#include "comicreader/ComicReaderCore.h"
#include "comicreader/ComicReaderProvider.h"
#include "engine/ComicCoverProvider.h"
#include "engine/VaultBookCoverProvider.h"
#include "player/caststore.h"
#include "player/downloadstore.h"
#include "player/livestore.h"
#include "player/mpvitem.h"
#include "player/seekthumbnailer.h"
#include "player/powerstore.h"
#include "player/roomstore.h"
#include "watchparty/WatchPartyPlayerSync.h"
#include "watchparty/WatchPartySource.h"
#include "watchparty/WatchPartyUiController.h"
#include "player/streamserver.h"
#include "player/windowmodestore.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/BookTorrentDownloader.h"
#include "torrent/BookTorrents.h"
#include "torrent/engine/TorrentEngine.h"
// NOT Player-2 stuff: the stall probe is the app's own QGuiApplication subclass, used
// unconditionally in main(). It sat inside the COLOSSEUM_PLAYER2 block below for weeks and
// compiled only because P2 was always linked — the first stock (P2-off) build broke on it.
#include "GuiStallProbe.h"   // diagnostic GUI-thread stall probe (env-gated; see header)
// Player 2 LAST on purpose: its D3D11 headers drag in <windows.h>, and anything that pulls in the
// old WinSock.h before boost/asio (libtorrent, above) wants winsock2.h fails the build outright.
#ifdef COLOSSEUM_PLAYER2
#include "player2/Player2Backend.h"
#include "player2/video/Player2VideoItem.h"
#endif

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
               PosterScoreboard *scoreboard = nullptr,
               BiblioImageDiag *imageDiag = nullptr)
        : QNetworkAccessManager(parent),
          m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_useCache(useCache),
          m_scoreboard(scoreboard),
          m_imageDiag(imageDiag) {
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
            if (m_imageDiag) m_imageDiag->track(inner, req.url());
            evictOnFailure(inner, r.url());
            return new GunzipReply(inner);
        }
        QNetworkReply *reply = QNetworkAccessManager::createRequest(op, r, outgoing);
        watch(host, reply);
        // Per-URL diagnostics keyed by the PRE-rewrite URL — the one QML's Image
        // asked for, so a card's `source` property matches its rows exactly.
        if (m_imageDiag) m_imageDiag->track(reply, req.url());
        // The key is the url AFTER any pin rewrite — that is what the cache stored under.
        evictOnFailure(reply, r.url());
        return reply;
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
    bool m_useCache = true;
    PosterScoreboard *m_scoreboard = nullptr;
    BiblioImageDiag *m_imageDiag = nullptr;

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

    // Self-heal: never let a bad answer become a permanent one.
    //
    // The disk cache below is what made a transient outage look like a broken app. During
    // the dead-AAAA ISP stall the wallpaper and cover requests failed, those failures were
    // written into the cache, and every launch afterwards served them back with NO network
    // request and NO error — so the KDE Plasma shelf and the comics covers stayed blank for
    // days while the URLs, the CDN, the proxy, the pin and the delegate were all provably
    // fine. Found 2026-07-26 by parking the cache directory and watching the shelf return.
    //
    // So: anything that does not classify as Arrived is evicted on the spot, and the next
    // load refetches. The reply's own bytes are untouched — this only stops the failure
    // being remembered. Bound to `this` deliberately, unlike watch() above: remove() runs
    // on the NAM's own thread, where its cache lives.
    void evictOnFailure(QNetworkReply *reply, const QUrl &cacheKey) {
        if (!m_useCache)
            return;
        const bool webp = m_scoreboard && m_scoreboard->webpDecoderPresent();
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey, webp] {
            const int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const bool netErr = reply->error() != QNetworkReply::NoError;
            const bool bad = PosterScoreboard::classify(status, ct, netErr, webp)
                             != PosterScoreboard::Bucket::Arrived;
            // A 200 that declares an EMPTY body is the case classify() cannot see: it is
            // not a transport error and not an undecodable format, so it scores as Arrived
            // — and then gets cached as zero bytes and served back forever as a blank tile.
            // Judged only from a valid Content-Length; bytesAvailable() is unreliable here
            // because the image loader may already have drained the reply.
            const QVariant clen = reply->header(QNetworkRequest::ContentLengthHeader);
            const bool emptyBody = status == 200 && clen.isValid() && clen.toLongLong() <= 0;
            if (!bad && !emptyBody)
                return;
            if (QAbstractNetworkCache *c = cache())
                c->remove(cacheKey);
        });
    }
};

class CachingNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    CachingNamFactory(QStringList pinnedHosts, QHash<QString, QString> ipv4ByHost,
                      PosterScoreboard *scoreboard, BiblioImageDiag *imageDiag = nullptr)
        : m_pinnedHosts(std::move(pinnedHosts)),
          m_ipv4ByHost(std::move(ipv4ByHost)),
          m_scoreboard(scoreboard),
          m_imageDiag(imageDiag) {}

    QNetworkAccessManager *create(QObject *parent) override {
        return new CachingNam(m_pinnedHosts, m_ipv4ByHost, parent, /*useCache=*/true,
                              m_scoreboard, m_imageDiag);
    }

private:
    QStringList m_pinnedHosts;
    QHash<QString, QString> m_ipv4ByHost;
    PosterScoreboard *m_scoreboard = nullptr;   // owned by the app, outlives every NAM
    BiblioImageDiag *m_imageDiag = nullptr;     // same ownership contract as the scoreboard
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
    // Player 2 draws through D3D11 texture sharing and REFUSES to initialise on any other RHI, while
    // mpvqt and WebEngine require OpenGL. Qt picks the RHI once per PROCESS, before QGuiApplication -
    // so the two video backends can never coexist in one running app, and the backend choice is a
    // BOOT choice, not a runtime one. Env var (not the ini) because this must be decided before any
    // Qt object exists. Default stays OpenGL: a normal launch is unchanged.
    // TASK 18 - THE DEFAULT FLIP (Hemanth's explicit go, 2026-07-26: "let's do it"). Player 2 is now
    // the default engine when it is compiled in, and mpv is the ESCAPE HATCH rather than the default.
    // He keeps a one-click way back: COLOSSEUM_PLAYER1=1 boots the old player, and there is a Desktop
    // launcher for it. This is deliberately still a BOOT choice both ways - the RHI is process-wide,
    // so it can never be a switch inside the running app.
    //
    // COLOSSEUM_PLAYER2 is still honoured as an explicit opt-IN so every existing launcher, probe and
    // gate keeps working unchanged; it simply is no longer required.
    // DEFAULT REVERTED to mpv, 2026-07-26 late evening - the Task 18 flip was PREMATURE and this is
    // the honest walk-back, on the day's own evidence: his first evenings on the flipped default hit
    // dead hotkeys (the rebuilt chrome never carried production's focus wiring), silent progress
    // tracking, a bare pause banner and seek-time frame drops. The ENGINE measured well all along;
    // the integration shell around it is what is not finished. So his daily driver goes back to the
    // player that is COMPLETE, and Player 2 stays fully built one launcher away (COLOSSEUM_PLAYER2=1,
    // the same opt-in it always honoured) while the shell is ported - not patched - to parity.
    // Re-flipping the default is Task 18 again, and it happens when HE cannot tell the two apart in
    // an evening of use - not when a ledger says so.
    const bool forcePlayer1 = qEnvironmentVariableIsSet("COLOSSEUM_PLAYER1")
                              || qEnvironmentVariableIsSet("COLOSSEUM_MPV");
    bool bootPlayer2 = qEnvironmentVariableIsSet("COLOSSEUM_PLAYER2") && !forcePlayer1;
#ifndef COLOSSEUM_PLAYER2
    // Not compiled in: there is nothing to boot, and the old player is the only engine present.
    bootPlayer2 = false;
#endif
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
#ifdef COLOSSEUM_PLAYER2
    if (bootPlayer2)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    else
#endif
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

    // Diagnostic only (2026-07-29 stutter investigation): ProbedGuiApplication IS a QGuiApplication
    // and behaves identically unless COLOSSEUM_GUI_STALL_PROBE is set, in which case it times every
    // GUI-thread event and ranks what blocks the thread. Frame pacing proved the stutter is a
    // GUI-thread stall, not the video engine; this names the work. See native/GuiStallProbe.h.
    ProbedGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Colosseum"));
    app.setApplicationVersion(QStringLiteral(COLOSSEUM_VERSION));
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

    // Always-on rolling log (2026-08-05). Hemanth hit a Downloads cancel that
    // did nothing and printed nothing; the launcher he double-clicks discards
    // stderr, so there was no evidence to read afterwards. Installed HERE —
    // after the app identity and the dltest AppData tag are settled — so the
    // log follows the same isolation every other AppData-backed store gets.
    AppLog::install();

    // The video player surface (mpv), reached from QML as `import Colosseum.Player`.
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");
    qmlRegisterType<SeekThumbnailer>("Colosseum.Player", 1, 0, "SeekThumbnailer");
#ifdef COLOSSEUM_PLAYER2
    // The Player 2 backend, opt-in (build flag COLOSSEUM_PLAYER2_IN_APP). Registering the types costs
    // nothing at runtime — the engine is only constructed if QML instantiates Player2Page.
    qmlRegisterType<Colosseum::Player2::Player2VideoItem>("Colosseum.Player2", 1, 0, "Player2VideoItem");
    qmlRegisterType<Colosseum::Player2::Player2Backend>("Colosseum.Player2", 1, 0, "Player2Backend");

    // "Your Colosseum" playback activity sampler (CPP-PORT-CONTRACT.md �8), reached from QML as
    // `import Colosseum.Activity`. One transient instance per lane (Player 1, Player 2, audiobook);
    // each binds its own `sink: ProfileActivity` (the profile-scoped ActivityStore context property).
    qmlRegisterType<ActivityPlaybackTracker>("Colosseum.Activity", 1, 0, "ActivityPlaybackTracker");
#endif

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    QQmlApplicationEngine engine;

    // Auto-update is a post-first-paint service.  It has its own network manager,
    // cache, and installer bridge so release traffic never shares a catalogue lane
    // and no check can block construction of the QML tree.
    auto *updateNam = new QNetworkAccessManager(&app);
    auto *updateCache = new Colosseum::Update::UpdateCache(
        Colosseum::Update::UpdateCache::productionRoot());
    auto *updateDownloader = new Colosseum::Update::UpdateDownload(updateNam, updateCache, &app);
    Colosseum::Update::ReleaseClientConfig updateConfig;
    updateConfig.latestReleaseUrl = QUrl(
        QStringLiteral("https://api.github.com/repos/kingoftheseas56/Colosseum/releases/latest"));
    updateConfig.repository = QStringLiteral("kingoftheseas56/Colosseum");
    updateConfig.publicKey = QByteArray(Colosseum::Update::embeddedUpdatePublicKey().data(),
                                        Colosseum::Update::embeddedUpdatePublicKey().size());
    auto *updateClient = new Colosseum::Update::UpdateReleaseClient(
        updateNam, updateConfig, &app);
    auto *updateBridge = new Colosseum::Update::UpdateInstallBridge(&app);

    struct ActiveUpdateCallbacks final {
        std::function<void(qint64, qint64, qint64)> progress;
        std::function<void(const QString&)> completed;
        std::function<void(const QString&, bool)> failed;
    };
    const auto updateCallbacks = std::make_shared<ActiveUpdateCallbacks>();
    QObject::connect(updateDownloader, &Colosseum::Update::UpdateDownload::progress,
                     &app, [updateCallbacks](qint64 received, qint64 total, qint64 speed) {
        if (updateCallbacks->progress)
            updateCallbacks->progress(received, total, speed);
    });
    QObject::connect(updateDownloader, &Colosseum::Update::UpdateDownload::completed,
                     &app, [updateCallbacks](const QString& path) {
        if (updateCallbacks->completed)
            updateCallbacks->completed(path);
    });
    QObject::connect(updateDownloader, &Colosseum::Update::UpdateDownload::failed,
                     &app, [updateCallbacks](const QString& code, bool resumable) {
        if (updateCallbacks->failed)
            updateCallbacks->failed(code, resumable);
    });

    Colosseum::Update::UpdateServiceHooks updateHooks;
    updateHooks.checkLatest = [updateClient](const QString& priorEtag,
                                              Colosseum::Update::UpdateReleaseClient::Callback done) {
        updateClient->checkLatest(priorEtag, std::move(done));
    };
    updateHooks.startDownload = [updateDownloader, updateCallbacks](
        const Colosseum::Update::DownloadRequest& request,
        Colosseum::Update::UpdateServiceHooks::DownloadProgress progress,
        Colosseum::Update::UpdateServiceHooks::DownloadCompleted completed,
        Colosseum::Update::UpdateServiceHooks::DownloadFailed failed) {
        updateCallbacks->progress = std::move(progress);
        updateCallbacks->completed = std::move(completed);
        updateCallbacks->failed = std::move(failed);
        updateDownloader->start(request);
    };
    updateHooks.cancelDownload = [updateDownloader] { updateDownloader->cancel(); };
    updateHooks.installLauncher = [updateBridge](const QString& installer,
                                                 const Colosseum::Update::Version& target,
                                                 QString* error) {
        const auto launch = updateBridge->prepare(installer, target, error);
        return launch.has_value() && updateBridge->launchDetached(*launch, error);
    };
    // Installer waits on /WAITPID=<our PID>; queue the quit so it runs after
    // this call stack (and any QML state signals from setState) unwind.
    updateHooks.requestShutdown = [&app] { QTimer::singleShot(0, &app, &QCoreApplication::quit); };
    updateHooks.fetchArtwork = [updateNam](const QString&, const QUrl& url, qint64 cap,
                                           Colosseum::Update::UpdateServiceHooks::ArtworkCompleted done,
                                           Colosseum::Update::UpdateServiceHooks::ArtworkFailed failed) {
        if (!url.isValid() || url.scheme() != QLatin1String("https")) {
            failed(QStringLiteral("unsafe_artwork_url"));
            return;
        }
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "Colosseum/1.1.1");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = updateNam->get(request);
        const auto bytes = std::make_shared<QByteArray>();
        const auto settled = std::make_shared<bool>(false);
        QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, bytes, settled, cap, failed] {
            if (*settled)
                return;
            bytes->append(reply->readAll());
            if (bytes->size() > cap) {
                *settled = true;
                reply->abort();
                failed(QStringLiteral("artwork_too_large"));
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, reply,
                         [reply, bytes, settled, done, failed] {
            if (*settled) {
                reply->deleteLater();
                return;
            }
            bytes->append(reply->readAll());
            *settled = true;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
                failed(QStringLiteral("artwork_download_failed"));
            else
                done(*bytes);
            reply->deleteLater();
        });
    };

    QString installedVersionText = QStringLiteral(COLOSSEUM_VERSION);
#ifdef COLOSSEUM_UPDATE_TESTING
    // Lanista's UpToDate fixture uses the same signed release chronicle as the
    // Available case, but boots the test binary at that release version so the
    // service can exercise its no-newer-release branch without a second signing key.
    if (qEnvironmentVariableIsSet("COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION"))
        installedVersionText = qEnvironmentVariable("COLOSSEUM_UPDATE_TEST_INSTALLED_VERSION");
#endif
    const auto installedVersion = Colosseum::Update::Version::parseCanonical(installedVersionText);
    if (!installedVersion)
        return -1;
    // Bundled installed-chronicle seed: extract the signed manifest + signature +
    // artwork from the qrc to a cache subdir so the runtime highlightMap SHA256
    // check works on real files (qrc bytes cannot be memory-mapped/hashed by
    // QFile). Extracted once per launch; the loader re-verifies the signature
    // before use, so a stale or tampered extraction is rejected.
    {
        const QString chronicleCacheDir =
            QDir(updateCache->rootPath()).filePath(QStringLiteral("installed-chronicle"));
        const QString artworkCacheDir = QDir(chronicleCacheDir).filePath(QStringLiteral("artwork"));
        QDir().mkpath(artworkCacheDir);
        const auto extract = [&chronicleCacheDir, &artworkCacheDir](const QString& qrcPath,
                                                                    const QString& destName,
                                                                    const QString& destDir) {
            QFile src(QStringLiteral(":/installed-chronicle/") + qrcPath);
            if (!src.exists())
                return false;
            const QString dest = QDir(destDir).filePath(destName);
            // Skip if already extracted with identical bytes (idempotent across launches).
            if (QFile::exists(dest)) {
                QFile existing(dest);
                if (existing.open(QIODevice::ReadOnly) && src.open(QIODevice::ReadOnly)
                    && existing.readAll() == src.readAll())
                    return true;
            }
            if (!src.open(QIODevice::ReadOnly))
                return false;
            QFile out(dest);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return out.write(src.readAll()) == src.size();
        };
        if (extract(QStringLiteral("installed-manifest.json"), QStringLiteral("installed-manifest.json"), chronicleCacheDir)
            && extract(QStringLiteral("installed-manifest.json.sig"), QStringLiteral("installed-manifest.json.sig"), chronicleCacheDir)) {
            // Extract each artwork asset. The manifest lists them; we extract by
            // scanning the qrc artwork prefix (the manifest is already on disk).
            updateHooks.installedChronicleManifestPath =
                QDir(chronicleCacheDir).filePath(QStringLiteral("installed-manifest.json"));
            updateHooks.installedChronicleSignaturePath =
                QDir(chronicleCacheDir).filePath(QStringLiteral("installed-manifest.json.sig"));
            updateHooks.installedChronicleArtworkRoot = artworkCacheDir;
            // Extract artwork assets named in the manifest.
            QFile manifestFile(updateHooks.installedChronicleManifestPath);
            if (manifestFile.open(QIODevice::ReadOnly)) {
                const auto doc = QJsonDocument::fromJson(manifestFile.readAll());
                const auto artwork = doc.object().value(QStringLiteral("artwork")).toArray();
                for (const auto& value : artwork)
                    extract(QStringLiteral("artwork/") + value.toObject().value(QStringLiteral("asset")).toString(),
                            value.toObject().value(QStringLiteral("asset")).toString(),
                            artworkCacheDir);
            }
        }
    }
    auto *updates = new Colosseum::Update::UpdateService(
        *installedVersion, updateCache->rootPath(), std::move(updateHooks), &app);
#ifdef COLOSSEUM_UPDATE_TESTING
    if (qEnvironmentVariableIsSet("COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE")) {
        const QString requestedState = qEnvironmentVariable("COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE");
        Colosseum::Update::UpdateService::State state = Colosseum::Update::UpdateService::Idle;
        if (requestedState == QStringLiteral("Downloading"))
            state = Colosseum::Update::UpdateService::Downloading;
        else if (requestedState == QStringLiteral("Paused"))
            state = Colosseum::Update::UpdateService::Paused;
        else if (requestedState == QStringLiteral("Verifying"))
            state = Colosseum::Update::UpdateService::Verifying;
        else if (requestedState == QStringLiteral("Ready"))
            state = Colosseum::Update::UpdateService::Ready;
        else
            qWarning("[update] ignored unknown test presentation state: %s",
                     requestedState.toUtf8().constData());

        bool receivedOk = false;
        bool totalOk = false;
        const qint64 received = qEnvironmentVariable("COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES")
                                    .toLongLong(&receivedOk);
        const qint64 total = qEnvironmentVariable("COLOSSEUM_UPDATE_TEST_TOTAL_BYTES")
                                 .toLongLong(&totalOk);
        if (state != Colosseum::Update::UpdateService::Idle && receivedOk && totalOk)
            updates->setTestingPresentationState(state, received, total);
        else if (state != Colosseum::Update::UpdateService::Idle)
            qWarning("[update] ignored invalid test presentation byte counts");
    }
#endif
    updateBridge->acknowledgeHealthyBoot(QCoreApplication::arguments());
    engine.rootContext()->setContextProperty(QStringLiteral("Updates"), updates);
    const bool installedUpdateEligible = updateBridge->installedBuildEligible();
#ifdef COLOSSEUM_UPDATE_TESTING
    constexpr bool updateTestingBuild = true;
#else
    constexpr bool updateTestingBuild = false;
#endif
    if (!installedUpdateEligible && !updateTestingBuild)
        qInfo("[update] automatic checks disabled (source/dev build or missing installed layout)");
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
        // Tankoban Mode (2026-07-15, eyes-on): cover thumbnails ride uploads.mangadex.org
        // (QML Image via this factory — still the source of Catalog.js's Monster tile);
        // Apple Books + Open Library per-volume synopsis lookups ride itunes.apple.com /
        // openlibrary.org. All publish a dead AAAA on this ISP → ~21s stall per request
        // (covers never painted; Apple synopses trickled a scattered few). Same scar, same
        // fix. NOTE: Open Library's IPv4 is ALSO ISP-firewalled — pinning only makes it fail
        // fast so the Apple fallback runs sooner; it never returns data here.
        // The MangaDex catalog API host was pinned here for the volume catalog; that client
        // is retired (2026-07-29, ComickCatalogClient) and nothing contacts the host now.
        QStringLiteral("uploads.mangadex.org"),
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
    // Sweep zero-byte entries out of the image cache before any NAM can read them.
    //
    // A zero-byte cache file is served as a HIT: no request, no error, blank tile, forever.
    // That is what kept the KDE Plasma shelf and the comics covers empty for days while the
    // URLs, CDN, proxy, pin and delegates were all provably fine — found 2026-07-26 by
    // parking the cache directory and watching the shelf come back. The parked copy held
    // 168 sub-kilobyte entries out of 6029, many of them exactly 0 bytes.
    //
    // evictOnFailure() above stops us WRITING new ones. This heals what is already there,
    // including files truncated by a hard kill mid-write, which no reply handler can catch
    // because there is no reply. Cheap: a stat per file, once, before the UI exists.
    {
        const QString imgCacheDir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/colosseum-images");
        int swept = 0;
        QDirIterator it(imgCacheDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (it.fileInfo().size() > 0)
                continue;
            if (QFile::remove(it.filePath()))
                ++swept;
        }
        if (swept > 0)
            qInfo("[img] swept %d empty entries from the image cache (they render as blank "
                  "tiles and never re-fetch)", swept);
    }
    // Per-URL image diagnostics behind the Lanista biblio.imageDiag probe (decision
    // brief 2026-08-06 §4) — same lifetime contract as the scoreboard beside it.
    auto *imageDiag = new BiblioImageDiag(&app);
    engine.setNetworkAccessManagerFactory(
        new CachingNamFactory(namPinnedHosts, ipv4ByHost, scoreboard, imageDiag));
    engine.rootContext()->setContextProperty(QStringLiteral("NetScoreboard"), scoreboard);
    engine.rootContext()->setContextProperty(QStringLiteral("BiblioImageDiag"), imageDiag);
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
    // The FRESH reader's native seam (Task 16 swap): Biblio's reader layer is
    // qml/reader2/ReaderShell over the vendored Anx foliate paper. QML sees the full
    // bridge; the paper's QWebChannel gets ONLY its filesRead/paperEvent gate
    // (Reader2Bridge.paperGate — least privilege). It reads and writes the SAME
    // BookStores files (native/reader/BookStores.h) the retired TB2 BookBridge used,
    // byte-identically, so old-reader progress/marks resume unchanged.
    //
    // BookBridge itself was DELETED 2026-08-07: the Task 16 swap replaced its door
    // (Main.qml loads reader2/ReaderShell), and the one caller it was being kept for
    // — the standalone audiobook strip — was retired 2026-07-18 (Main.qml:1412). It
    // had no live caller in qml/, tests/ or native/. BookStores stays: it is shared
    // storage, not part of the old reader.
    auto *reader2Bridge = new Reader2Bridge(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Reader2Bridge"), reader2Bridge);

    // From-scratch native Comic Reader backend (Agent 1, plan 2026-07-23, Task 7)
    // exposed to QML as `ComicReaderCore`: the ONE orchestrator over the five pure
    // comicreader/ engine units (types+pairing, pinned LRU cache, generation-safe
    // decode coordinator, auto-coupling probe, strip geometry). Its read-only
    // image://comicreader/ provider serves decoded pages from the pinned cache and
    // returns null for a superseded generation. The engine takes ownership of the
    // provider (addImageProvider); the core (parented to app) owns the cache + the
    // live-generation the provider reads. Registered now so the seam is live and
    // boot-tested; the QML surfaces (Task 9+) are what consume it. Scheme is
    // `comicreader`, distinct from Reader2's Biblio book reader.
    auto *comicReaderCore = new comicreader::ComicReaderCore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("ComicReaderCore"), comicReaderCore);
    engine.addImageProvider(QStringLiteral("comicreader"), comicReaderCore->createProvider());

    // image://comiccover/ (2026-08-06 CBZ-in-place arc, Task 3): a fully
    // stateless cover-thumbnail provider for archive-shaped comic rows, which
    // have no loose page file for the library grid to point at. Engine takes
    // ownership via addImageProvider, same as the comicreader provider above.
    engine.addImageProvider(QStringLiteral("comiccover"), new Colosseum::ComicCoverProvider());
    // image://vaultbookcover/ (Vault Slice 17 foundation): stateless bounded EPUB cover decode.
    engine.addImageProvider(QStringLiteral("vaultbookcover"),
                            new Colosseum::VaultBookCoverProvider());

    // ── Vault local-media: the launch pillar (execution plan Slice 8) ──────────
    // LocalLaunch routes a handed-in file (taskbar Open Media…, OS drag-drop,
    // Ctrl+O) to the right reader/player and rejects the unopenable BEFORE any
    // session is created (no dead taskbar tiles). VaultPageStore adapts a local
    // CBZ to ComicReaderShell's injected-store contract so it reads with zero
    // reader edits. C++ decides the route; Main.qml paints the door.
    const QString vaultDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/vault");
    QDir().mkpath(vaultDir);
    auto *localLaunch = new LocalLaunch(vaultDir, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("LocalLaunch"), localLaunch);
    auto *vaultPageStore = new VaultPageStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("VaultPageStore"), vaultPageStore);

    // VaultLibrary — the Vault's single QML façade: the read-model the page + door + shelves
    // paint from, plus the scan/confirm commands. It wraps the rebuildable VaultIndex (SQLite at
    // <vaultDir>/index-v1.sqlite), the cancellable off-thread VaultScanner, and the VaultConfig
    // user-intent store (roots + kind overrides), so QML fires one gesture (addFolder/confirmRoot)
    // and C++ owns the scan/publish threading and multi-step sequence. VaultEnricher (covers,
    // durations) is still deferred — census facts shelve first; enrichment repaint lands later.
    auto *vaultIndex = new VaultIndex(vaultDir + QStringLiteral("/index-v1.sqlite"), &app);
    auto *vaultConfig = new VaultConfig(vaultDir, &app);
    auto *vaultIdentity = new VaultIdentity(vaultDir, &app);
    localLaunch->setIdentity(vaultIdentity);
    auto *vaultScanner = new VaultScanner(vaultIndex, vaultIdentity, &app);
    // Slice 15: VaultLibrary OWNS the VaultWatcher (per-root QFileSystemWatcher + debounce);
    // it needs the identity to build arrival rows identical to the census's.
    // Browse-artwork execution plan, Slice 3 part 2: `vaultDir` is reused as-is — the SAME
    // VaultStoreIo-managed dir vaultConfig/vaultIdentity/vaultIndex already sit under — so
    // VaultLibrary's owned VaultThumbnailer/VaultPosterFetcher (thumbs/, posters/ subdirs) land
    // beside config.json/identity.json/index-v1.sqlite instead of scattering a second cache root.
    auto *vaultLibrary =
        new VaultLibrary(vaultIndex, vaultScanner, vaultConfig, vaultIdentity, vaultDir, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("VaultLibrary"), vaultLibrary);

    // Vault cover enrichment (execution plan Slice 12): after a publish, read each comic's
    // CBZ cover ENTRY off the GUI thread (pure file I/O — VaultEnricher::readComicFacts, no
    // SQLite), then write the enriched rows back in ONE batch on the GUI thread so the shelf
    // tiles paint real covers via the already-registered image://comiccover provider. Video
    // durations + book covers stay deferred (their art is a later slice) — enriching video
    // here would run ffprobe on the GUI thread. An index-revision guard drops stale enrichment
    // after ANY superseding index mutation, so scanner and watcher changes cannot be resurrected.
    auto runVaultCoverEnrichment = [vaultIndex]() {
        // Capture the exact index snapshot this async work is derived from. Any successful
        // scanner publish, watcher reconciliation, away-state change, or other mutation advances
        // the revision and makes this worker's eventual write-back ineligible.
        const quint64 baseRevision = vaultIndex->revision();
        // Only comics still missing a cover — so a re-publish or a re-launch of an already
        // enriched library does no redundant CBZ reads (this returns nothing after the first pass).
        QList<VaultIndex::FileRow> todo;
        for (const VaultIndex::FileRow& r : vaultIndex->rowsForKind(QStringLiteral("comic"))) {
            if (!r.away && QDir(r.rootPath).exists() && QFileInfo::exists(r.path)
                && r.errorState.isEmpty() && r.coverRef.isEmpty())
                todo.append(r);
        }
        for (const VaultIndex::FileRow& r : vaultIndex->rowsForKind(QStringLiteral("video"))) {
            if (!r.away && QDir(r.rootPath).exists() && QFileInfo::exists(r.path)
                && r.errorState != QLatin1String("rejected")
                && r.admissionVerdict.isEmpty())
                todo.append(r);
        }
        for (const VaultIndex::FileRow& r : vaultIndex->rowsForKind(QStringLiteral("book"))) {
            if (!r.away && QDir(r.rootPath).exists() && QFileInfo::exists(r.path)
                && QFileInfo(r.path).suffix().compare(QStringLiteral("epub"), Qt::CaseInsensitive) == 0
                && r.errorState.isEmpty() && r.metadataSource.isEmpty())
                todo.append(r);
        }
        if (todo.isEmpty())
            return; // a zero-work newer publication still advanced VaultIndex::revision()
        auto *watcher = new QFutureWatcher<QList<VaultIndex::FileRow>>();
        QObject::connect(
            watcher, &QFutureWatcher<QList<VaultIndex::FileRow>>::finished, vaultIndex,
            [vaultIndex, watcher, baseRevision]() {
                const QList<VaultIndex::FileRow> enriched = watcher->result();
                watcher->deleteLater();
                // Conditional write-back is the resurrection barrier: stale workers cannot
                // reinsert rows after a scanner publish OR a live watcher deletion/replacement.
                vaultIndex->upsertManyIfRevision(enriched, baseRevision);
            });
        watcher->setFuture(QtConcurrent::run([todo]() {
            QList<VaultIndex::FileRow> out;
            out.reserve(todo.size());
            for (VaultIndex::FileRow r : todo) {
                if (r.kind == QLatin1String("comic")) {
                    const VaultEnricher::ComicFacts cf = VaultEnricher::readComicFacts(r.path);
                    if (cf.ok) {
                        r.pages = cf.pages;
                        r.coverRef = cf.coverEntry;
                        r.errorState.clear();
                        r.errorDetail.clear();
                    } else {
                        r.errorState = QStringLiteral("corrupt");
                        r.errorDetail = cf.errorDetail;
                    }
                } else if (r.kind == QLatin1String("video")) {
                    const MediaAdmissionProbe::Result admission =
                        MediaAdmissionProbe::probe(r.path);
                    switch (admission.verdict) {
                    case MediaAdmissionProbe::Verdict::Admitted:
                        r.admissionVerdict = QStringLiteral("Admitted");
                        break;
                    case MediaAdmissionProbe::Verdict::RejectedNoVideo:
                        r.admissionVerdict = QStringLiteral("RejectedNoVideo");
                        break;
                    case MediaAdmissionProbe::Verdict::RejectedError:
                        r.admissionVerdict = QStringLiteral("RejectedError");
                        break;
                    case MediaAdmissionProbe::Verdict::RejectedTimeout:
                        r.admissionVerdict = QStringLiteral("RejectedTimeout");
                        break;
                    }
                    r.admissionDetail = admission.detail;
                    if (admission.verdict == MediaAdmissionProbe::Verdict::Admitted) {
                        r.errorState.clear();
                        r.errorDetail.clear();
                    } else {
                        r.errorState = QStringLiteral("rejected");
                        r.errorDetail = admission.detail;
                    }
                } else if (r.kind == QLatin1String("book")) {
                    r.format = QFileInfo(r.path).suffix().toLower();
                    const VaultEnricher::BookFacts book = VaultEnricher::readBookFacts(r.path);
                    if (book.ok) {
                        if (!book.title.isEmpty())
                            r.displayTitle = book.title;
                        r.author = book.author;
                        r.synopsis = book.synopsis;
                        r.coverRef = book.coverEntry;
                        r.metadataSource = QStringLiteral("EPUB");
                        r.errorState.clear();
                        r.errorDetail.clear();
                    } else {
                        r.errorState = QStringLiteral("corrupt");
                        r.errorDetail = book.errorDetail;
                    }
                }
                out.append(r);
            }
            return out;
        }));
    };
    QObject::connect(vaultScanner, &VaultScanner::indexPublished, vaultLibrary,
                     [runVaultCoverEnrichment]() { runVaultCoverEnrichment(); });
    // Enrich an already-indexed library once at boot too, so covers appear on a relaunch
    // without re-adding the folder (a fresh index published this run enriches via the signal).
    runVaultCoverEnrichment();

    // Torrent stream engine (Stremio sidecar) exposed to QML as `Stream`. Lazy: the
    // runtime only spawns on the first Stream.play() call.
    auto *stream = new StreamServer(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Stream"), stream);
    // Warm the torrent engine shortly AFTER the window is up -- not during launch, and no longer
    // lazily on the first play. Lazy-on-play made every session pay the engine's cold start (DHT
    // bootstrap + tracker announce + handshake + unchoke) at the exact moment Play was pressed, which
    // is why one torrent play was instant and the next -- same file, same ~100 seeders -- took
    // minutes. The 3s delay keeps it off the cold-launch critical path; adopt-first means an
    // already-running official Stremio Service is adopted rather than clashed with. Cost accepted
    // knowingly: a comics- or books-only session spawns a runtime it never uses, but any video
    // session would have spawned it seconds later anyway. (2026-07-30, Theatre lane)
    QTimer::singleShot(3000, stream, [stream]() { stream->warmUp(); });

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

    // Baked Tankoban volume catalogue (catalogue-independence Slice 1, 2026-08-20): same
    // doctrine as MalCatalog — read-only seam, script-built db in data/, dormant when absent.
    auto *tankobanCatalog = new TankobanCatalog(QStringLiteral("data/tankoban_catalog.db"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("TankobanCatalog"), tankobanCatalog);
    auto* imdbCatalog = new ImdbCatalog(QStringLiteral("data/imdb_catalog.db"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("ImdbCatalog"), imdbCatalog);
    auto *vaultIdentifier = new VaultIdentifier(vaultIndex, comicsCatalog, malCatalog,
                                                 imdbCatalog, &app);
    vaultLibrary->setIdentifier(vaultIdentifier);

    // Agent Visibility Phase 2, Slice F1-Bridge: the live, app-owned Vault forensic
    // projection (F1-Core). Composes vaultLibrary only (F0's named safe seam,
    // docs/visibility/vault-forensic-owner-thread.md §10) — no second SQLite
    // connection, no writer. Parented to &app like every other Vault object (F0 §1),
    // so it shares vaultLibrary's GUI-thread lifetime; wired into LanistaServer below.
    auto *vaultForensics = new VaultForensics(vaultLibrary, &app);

    // BiblioCatalog Discover/Explore keyless daily refresh service (spec
    // 2026-08-01, plan 2026-08-03 Task 4): a writable per-user SQLite cache
    // (unlike the pipeline-deployed read-only catalogues above), refreshed at
    // most once a local day from Apple Books + Open Library. Construction
    // never blocks startup: a prior day's cached snapshot (if any) is already
    // browsable through BiblioCatalog.ready/discoverPage before the first
    // network reply lands, and refreshIfDue() is fire-and-forget.
    const QString biblioCatalogPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/catalog/biblio-v1.sqlite");
    auto *biblioCatalog = new BiblioCatalog(biblioCatalogPath, nullptr, &app);
    engine.rootContext()->setContextProperty(QStringLiteral("BiblioCatalog"), biblioCatalog);
    biblioCatalog->refreshIfDue();

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

    // Slice 18 — the synthetic downloads root: derives VaultIndex::FileRows from
    // Colosseum's own download backbones (videos + CBZ comics + CBZ tankoban
    // volumes + epub/pdf books) so the Vault shelves them as ONE quiet, pre-
    // confirmed trusted root. The path is a logical identifier (the rows come
    // from the backbones, NOT a filesystem scan), pinned under AppData.
    auto *vaultDownloadsRoot = new VaultDownloadsRoot(download, books, comics,
                                                      tankobanVolumes, &app);
    const QString vaultDownloadsRootPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/downloads");
    vaultLibrary->setDownloadsRoot(vaultDownloadsRoot, vaultDownloadsRootPath);
    // Heal stale derived Vault rows once per process after every download backbone is wired.
    QTimer::singleShot(0, vaultLibrary, [vaultLibrary]() { vaultLibrary->republishAtBoot(); });

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

    // Watch Party Slice 2: source eligibility is a separate, read-only decision seam.
    // The existing local RoomStore remains untouched; no room/network/account behavior
    // is adopted here. Player 1 asks this object only for a credential-free descriptor.
    auto *watchPartySource = new Colosseum::WatchParty::SourceInspector(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("WatchPartySource"),
                                             watchPartySource);

    // Watch Party Slice 3: narrow Player 1 timeline synchronization policy. It starts
    // inactive and owns neither room transport nor account/session state; later room
    // integration feeds it authoritative timeline state and consumes explicit commands.
    auto *watchPartySync = new Colosseum::WatchParty::PlayerSyncController(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("WatchPartySync"),
                                             watchPartySync);

    // Watch Party Slice 6: QML-facing room/UI coordinator. Account ownership stays outside
    // this object; the real account bridge is bound after AccountRuntime construction below,
    // and until then only the accountless guest flow exists. The service endpoint is
    // deployment configuration, never room/shared state.
    auto *watchPartyUi = new Colosseum::WatchParty::UiController(watchPartySync, &app);
    if (qEnvironmentVariableIsSet("COLOSSEUM_WATCH_PARTY_URL")) {
        const QUrl watchPartyUrl(qEnvironmentVariable("COLOSSEUM_WATCH_PARTY_URL"));
        if (!watchPartyUi->configureServiceUrl(watchPartyUrl))
            qWarning() << "[watch-party] ignored invalid service endpoint";
    }
    engine.rootContext()->setContextProperty(QStringLiteral("WatchPartyUi"),
                                             watchPartyUi);

    // Native player window modes exposed to QML as `WindowMode` for PiP/fullscreen parity.
    auto *windowMode = new WindowModeStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("WindowMode"), windowMode);

    // Playback power-inhibit exposed to QML as `Power`, matching Harbor's play-only
    // OS/display sleep prevention.
    auto *power = new PowerStore(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("Power"), power);

    // Efficiency gate: auto-play this local file through the app's REAL player on startup. Both
    // backends are then measured inside the same application, playing the same file, through the
    // same session machinery - the only difference being which engine draws. That removes every
    // asymmetry a synthetic side-by-side introduces (the previous attempt compared a bare lab
    // harness against a probe window that decoded without ever painting).
    engine.rootContext()->setContextProperty(QStringLiteral("DevAbbaClip"),
                                             qEnvironmentVariable("COLOSSEUM_ABBA_CLIP"));

    // Whether THIS PROCESS actually booted on D3D11 - a boot fact, not a build flag or a saved
    // setting. False in a stock build (nothing linked in) and false whenever COLOSSEUM_PLAYER2 was
    // unset at launch, so QML can never route playback into a backend this process cannot render.
#ifdef COLOSSEUM_PLAYER2
    // Available only when the process ACTUALLY booted on D3D11. Reporting true on an OpenGL boot is
    // what made Player 2 take a playback it could never render (2026-07-25).
    engine.rootContext()->setContextProperty(QStringLiteral("Player2Available"), bootPlayer2);
#else
    engine.rootContext()->setContextProperty(QStringLiteral("Player2Available"), false);
#endif

    // Account + personal-profile runtime (Bundle 8C adoption, 2026-08-16).
    // AccountRuntime owns the personal store runtime, which is now the SOLE
    // owner/binder of the Progress/Collection/SearchHistory/AudioPairing context
    // properties (plus ProfilePreferences/ProfileHistory/ProfileContext and the
    // AccountController/AccountRecoveryKey surfaces). Boot starts sealed behind
    // the onboarding gate; the user's choice (create account / continue local)
    // rebinds to the adopted account profile or the legacy stores exactly as
    // designed. Replaces the four raw store constructions this block used to
    // hold — the split-brain risk of two owners binding the same QML names is
    // closed by construction.
    auto *accountRuntime = new AccountRuntime(&app);
    accountRuntime->prepareForQml(&engine);

    // One-time WC-era chapter migration (catalogue-independence Slice 5, 2026-08-20;
    // progress-purge rebind fix, closing-sweep 2026-08-21). Hemanth's explicit lock:
    // chapters are deleted completely, on-disk bytes included. Hooked HERE, not at the
    // earlier AppLog::install() point this class's own header comment originally assumed,
    // because ground-truthing this boot sequence during Slice 5 found the Bundle 8C
    // account/profile runtime (just above) is now the SOLE constructor of ProgressStore --
    // no store exists any earlier in main() to purge kind:"manga" records from.
    //
    // Boot always starts behind ProfileStoreRuntime's Sealed placeholder (a throwaway
    // QTemporaryDir-backed store -- see ProfileStoreRuntime::createSealedStores); the
    // user's onboarding choice ("continue local" / sign in) or a restored remembered
    // session later rebinds it to the real, durable store. TankobanChapterMigration::run's
    // `progressStoreIsDurable` flag (ground-truthed by the closing sweep, 2026-08-21: the
    // progress purge was silently landing on the Sealed placeholder and burning the
    // once-only marker before the real store was ever touched) keeps the marker withheld
    // while Sealed. Running it once now covers the case a remembered session already
    // rebound synchronously inside prepareForQml() above; the storesChanged connection
    // covers every later rebind (continue-local, sign-in, sign-out-to-local, ...) so the
    // real store gets the real purge exactly once, whenever it first becomes durable.
    //
    // One more transitional state storesChanged fires for: ProfileStoreRuntime::
    // suspendPersonalStoresForMigration() (the first half of "continue local"/sign-in,
    // called by FirstAccountProfileCoordinator::prepareLocalOnly() before it activates
    // anything) tears the current store set down to nullptr and emits storesChanged
    // BEFORE the replacement store exists. progressStore() returning null there is NOT
    // the disk-only "no store handed in" contract TankobanChapterMigration::run's null
    // parameter otherwise means (that contract is for a caller that deliberately never
    // wants the progress step) -- it is a live boot mid-rebind, and calling run() on it
    // would purge nothing, write nothing, and (with a null progress arg) still burn the
    // marker on the disk-only path, exactly re-creating the bug one signal later. Skip
    // the call outright when there is no store yet; the very next storesChanged (once
    // the real store is bound) runs it for real.
    auto runTankobanChapterMigration = [accountRuntime]() {
        ProfileStoreRuntime *stores = accountRuntime->profileStores();
        ProgressStore *progress = stores->progressStore();
        if (!progress)
            return;
        const bool durable = stores->activeProfile().kind() != ProfilePaths::Kind::Sealed;
        TankobanChapterMigration::run(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                                      progress, durable);
    };
    runTankobanChapterMigration();
    QObject::connect(accountRuntime->profileStores(), &ProfileStoreRuntime::storesChanged,
                     &app, runTankobanChapterMigration);

    // Watch Party account bridge (arc 03): signed-in identity + bearer stay native.
    // Sign-out or identity replacement tears down any authenticated party session.
    auto watchPartyAccountBridge = accountRuntime->createWatchPartyAccountBridge();
    watchPartyUi->setAccountBridge(watchPartyAccountBridge.get());
    QObject::connect(accountRuntime->controller(), &AccountController::signedIn,
                     watchPartyUi, &Colosseum::WatchParty::UiController::handleAccountIdentityChanged);
    QObject::connect(accountRuntime->controller(), &AccountController::signedOut,
                     watchPartyUi, &Colosseum::WatchParty::UiController::handleAccountIdentityChanged);
    auto *audioPairing = accountRuntime->profileStores()->audioPairingStore();

    // (Deleted 2026-08-07 with BookBridge: two setters that handed the retired bridge the
    // audiobook library + pairing store. Nothing read them — the reader takes `AudioPairing`
    // straight as a context property, see qml/reader2/ReaderShell.qml:310-314.)

    // Read-along auto-attach (Task 12): when an audiobook finishes downloading from a
    // book's page, the downloader writes the pairing under the reader's bookId itself —
    // no pairing UI. Same store instance QML uses, so the reader's Audio tab reads it.
    audiobooks->setPairing(audioPairing);

    // (SearchHistory is owned + bound by the account runtime's profile stores above.)

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
    if (installedUpdateEligible && !updateTestingBuild)
        QTimer::singleShot(0, updates, &Colosseum::Update::UpdateService::startAutomaticChecks);

    // Lanista dev-control bridge — ALWAYS ON for reads/grabs (Hemanth, spec
    // 2026-08-01 §3). Local named pipe only, never a network port. Driving and
    // mutation commands gate on env INSIDE the server, per command.
    auto *lanistaServer = new LanistaServer(&engine, &app);
    // F1-Bridge: hand the bridge the live forensic projection constructed above.
    lanistaServer->setVaultForensics(vaultForensics);

    // Live-reload only in dev (dev.bat sets COLOSSEUM_DEV). Production is untouched.
    if (qEnvironmentVariableIsSet("COLOSSEUM_DEV")) {
        new QmlReloader(&engine, qmlPath, &app);
        manga->selfTest(QStringLiteral("Berserk"));  // log WeebCentral chapter count at startup
        // DEBUG: log volume resolution. There is no WeebCentral id at startup, so this
        // exercises the LIVE Comick scrape + completeness gate only — the volume-DB read
        // is keyed by the WC id and is proved by opening a real series page.
        manga->volumes(QString(), QStringLiteral("One Piece"));
    }

    return app.exec();
}
