#include "Reader2Bridge.h"
#include "../reader/BookStores.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <memory>

// Normalize a book path to a stable identity for authorization compares: strip a file:///
// prefix, resolve to the canonical on-disk path when the file exists (collapses '..', native
// separators, and — on Windows — the real casing), else fall back to a cleaned path. Both the
// authorized book and every filesRead request pass through this, so a rigged book cannot slip a
// different file past the check with an alternate spelling of the same-or-other path.
static QString normalizeBookPath(const QString& raw)
{
    QString s = raw;
    if (s.startsWith(QStringLiteral("file:///"))) s = QUrl(s).toLocalFile();
    const QString canon = QFileInfo(s).canonicalFilePath();
    return canon.isEmpty() ? QDir::cleanPath(s) : canon;
}

Reader2Bridge::Reader2Bridge(QObject* parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// paper-facing
// ─────────────────────────────────────────────────────────────────────────────

// Returns the file BASE64-encoded, as a QString. QWebChannel marshals a raw
// QByteArray return value by UTF-8-decoding it into a JS string, which CORRUPTS
// binary (an .epub's bytes are not valid UTF-8) — base64 is pure ASCII and
// survives the string transfer intact; the paper's glue atob-decodes it back
// into bytes (see resources/reader2/paper_glue.js base64ToFile). Same fix as
// the old reader's BookBridge::filesRead, just returning QString instead of
// QByteArray at the seam.
QString Reader2Bridge::filesRead(const QString& filePath)
{
    // AUTHORIZATION (hardening): serve ONLY the currently-open book. The paper is untrusted web
    // content; without this gate a rigged book could ask the bridge for ANY file on disk.
    // ReaderShell calls setAuthorizedBook(path) before every open; a request for any other path
    // (or before anything is authorized) is refused. Compare on the normalized/canonical form.
    if (m_authorizedBook.isEmpty() || normalizeBookPath(filePath) != m_authorizedBook) {
        qWarning() << "[reader2] filesRead refused — not the authorized book:" << filePath;
        return QString();
    }
    QString p = filePath;
    if (p.startsWith(QStringLiteral("file:///"))) p = QUrl(p).toLocalFile();
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromLatin1(f.readAll().toBase64());
}

// Authorize the one book filesRead may serve this open (see header + filesRead above).
void Reader2Bridge::setAuthorizedBook(const QString& absPath)
{
    m_authorizedBook = normalizeBookPath(absPath);
}

void Reader2Bridge::paperEvent(const QString& name, const QString& json)
{
    emit paperEventReceived(name, json);
}

// Canonical store key — delegates to BookStores::keyFor, the ONE derivation shared
// with the old reader (BookBridge::progressKey). This is the fingerprint under which
// the old reader wrote progress.json / bookmarks.json / annotations.json (it sets
// state.book.id = keyFor(path) before every save/read), so the fresh reader reads the
// same records. Both readers call the same function; neither owns its own copy.
QString Reader2Bridge::bookKey(const QString& absPath) const
{
    return BookStores::keyFor(absPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// QML-facing stores — delegate to BookStores (native/reader/BookStores.h),
// the SAME files/shapes the old BookBridge uses. Neither reader owns its own
// copy of the store logic.
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject Reader2Bridge::progressGet(const QString& bookId)
{
    return BookStores::get(QStringLiteral("progress.json"), bookId);
}

void Reader2Bridge::progressSave(const QString& bookId, const QJsonObject& data)
{
    BookStores::save(QStringLiteral("progress.json"), bookId, data);
}

QJsonObject Reader2Bridge::settingsGet()
{
    return BookStores::readStore(QStringLiteral("settings.json"));
}

void Reader2Bridge::settingsSave(const QJsonObject& data)
{
    BookStores::writeStore(QStringLiteral("settings.json"), data);
}

QJsonArray Reader2Bridge::bookmarksGet(const QString& bookId)
{
    return BookStores::listGet(QStringLiteral("bookmarks.json"), bookId);
}

QJsonObject Reader2Bridge::bookmarksSave(const QString& bookId, const QJsonObject& bm)
{
    return BookStores::listSave(QStringLiteral("bookmarks.json"), bookId, bm);
}

void Reader2Bridge::bookmarksDelete(const QString& bookId, const QString& id)
{
    BookStores::listDelete(QStringLiteral("bookmarks.json"), bookId, id);
}

QJsonArray Reader2Bridge::annotationsGet(const QString& bookId)
{
    return BookStores::listGet(QStringLiteral("annotations.json"), bookId);
}

QJsonObject Reader2Bridge::annotationsSave(const QString& bookId, const QJsonObject& an)
{
    return BookStores::listSave(QStringLiteral("annotations.json"), bookId, an);
}

void Reader2Bridge::annotationsDelete(const QString& bookId, const QString& id)
{
    BookStores::listDelete(QStringLiteral("annotations.json"), bookId, id);
}

// ─────────────────────────────────────────────────────────────────────────────
// dictionary — Wiktionary REST, C++ side (house rule: no network on the paper)
// ─────────────────────────────────────────────────────────────────────────────

static const QString kWiktHost = QStringLiteral("en.wiktionary.org");
static constexpr int kDictTimeoutMs = 8000;            // abort a wedged lookup after 8s
static constexpr qint64 kDictMaxBytes = 512 * 1024;    // a definition is a few KB; cap the body at 512 KB
static constexpr int kDictMaxWordLen = 64;             // a "word" from a selection is short, not a paragraph

void Reader2Bridge::dictLookup(const QString& word)
{
    // Bound the INPUT: a selection handed to Define should be one word. Cap at 64 chars so a
    // stray paragraph-length selection can't become a giant URL / lookup term. (firstWord already
    // narrows it QML-side; this is the defensive backstop.) Emit an empty result for an empty word.
    QString query = word;
    if (query.size() > kDictMaxWordLen) query = query.left(kDictMaxWordLen);
    if (query.isEmpty()) { emit dictResult(word, QString(), false); return; }

    // Host resolution is ASYNC (the fix): QHostInfo::fromName BLOCKS the GUI thread on DNS, so a
    // bad network froze the whole app for seconds. If we've already resolved (or know it failed),
    // fire immediately with the cached IPv4; otherwise kick off a non-blocking lookup and send
    // from its callback. The IPv4 is cached process-wide so only the first lookup pays any DNS.
    if (m_wiktResolved) { sendDictRequest(word, query); return; }
    QHostInfo::lookupHost(kWiktHost, this, [this, word, query](const QHostInfo& info) {
        QString ipv4;
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) { ipv4 = a.toString(); break; }
        m_wiktIpv4 = ipv4;          // "" if resolution failed → sendDictRequest falls back to the hostname
        // Cache ONLY on success. If this first lookup's DNS transiently failed (ipv4 empty), leave
        // m_wiktResolved false so the NEXT lookup retries the resolve — otherwise one bad first
        // lookup would permanently strand every future lookup on the un-pinned ~21s IPv6 stall path.
        if (!ipv4.isEmpty()) m_wiktResolved = true;
        sendDictRequest(word, query);   // still sends THIS lookup (pinned if resolved, else plain host)
    });
}

void Reader2Bridge::sendDictRequest(const QString& word, const QString& query)
{
    QUrl url(QStringLiteral("https://") + kWiktHost
             + QStringLiteral("/api/rest_v1/page/definition/")
             + QString::fromUtf8(QUrl::toPercentEncoding(query)));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum/1.0 (book reader)"));

    // IPv4 PIN (house scar: Wikimedia publishes AAAA records; Qt-on-Windows tries the dead IPv6
    // route first and stalls ~21s — the same black hole main.cpp's CachingNam pins the poster/
    // indexer hosts against). Rewrite the URL host to the resolved IPv4 and carry the real host in
    // the Host header + TLS SNI (setPeerVerifyName), and disable HTTP/2. If resolution failed
    // (m_wiktIpv4 empty) we fall back to the plain hostname.
    if (!m_wiktIpv4.isEmpty()) {
        req.setRawHeader("Host", kWiktHost.toUtf8());
        req.setPeerVerifyName(kWiktHost);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        QUrl pinned = url;
        pinned.setHost(m_wiktIpv4);
        req.setUrl(pinned);
    }
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* rep = m_nam->get(req);

    // `done` guards against a double terminal: timeout, size-cap, and finished can all race
    // (abort() synchronously fires finished). Whoever gets there first sets it; the others bail.
    auto done = std::make_shared<bool>(false);

    // TIMEOUT — abort + emit a failed result after 8s so a wedged network never leaves the card
    // stuck in 'loading'. The timer is parented to the reply, so it dies with it.
    QTimer* timer = new QTimer(rep);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, rep, word, done] {
        if (*done) return;
        *done = true;
        rep->abort();
        rep->deleteLater();
        emit dictResult(word, QString(), false);
    });
    timer->start(kDictTimeoutMs);

    // RESPONSE-SIZE CAP — abort if the body exceeds ~512 KB (a definition is tiny; an oversized
    // response is a misfire we won't buffer + JSON-parse onto the GUI thread).
    connect(rep, &QNetworkReply::downloadProgress, this, [this, rep, word, done](qint64 received, qint64) {
        if (*done) return;
        if (received > kDictMaxBytes) {
            *done = true;
            rep->abort();
            rep->deleteLater();
            emit dictResult(word, QString(), false);
        }
    });

    connect(rep, &QNetworkReply::finished, this, [this, rep, word, done] {
        if (*done) return;          // already resolved by the timeout / size-cap path
        *done = true;
        rep->deleteLater();
        const bool ok = rep->error() == QNetworkReply::NoError;
        emit dictResult(word, ok ? QString::fromUtf8(rep->readAll()) : QString(), ok);
    });
}
