#include "Reader2Bridge.h"
#include "../reader/BookStores.h"

#include <QAbstractSocket>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

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
    QString p = filePath;
    if (p.startsWith(QStringLiteral("file:///"))) p = QUrl(p).toLocalFile();
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromLatin1(f.readAll().toBase64());
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

// Resolve a host to its first IPv4 address ("" if none / lookup failed).
static QString resolveHostIpv4(const QString& host)
{
    const QHostInfo info = QHostInfo::fromName(host);
    for (const QHostAddress& a : info.addresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol) return a.toString();
    return {};
}

void Reader2Bridge::dictLookup(const QString& word)
{
    const QString host = QStringLiteral("en.wiktionary.org");
    QUrl url(QStringLiteral("https://") + host
             + QStringLiteral("/api/rest_v1/page/definition/")
             + QString::fromUtf8(QUrl::toPercentEncoding(word)));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum/1.0 (book reader)"));

    // IPv4 PIN (house scar: Wikimedia publishes AAAA records; Qt-on-Windows tries the dead
    // IPv6 route first and stalls ~21s — the same black hole main.cpp's CachingNam pins the
    // poster/indexer hosts against). Rewrite the URL host to the resolved IPv4 and carry the
    // real host in the Host header + TLS SNI (setPeerVerifyName), and disable HTTP/2. Applied
    // proactively — this is the first LIVE dict call, en.wiktionary.org is an AAAA host, and
    // the fix is the proven one; if resolution fails we fall back to the plain hostname.
    // Resolved once (m_wiktResolved) so only the first lookup pays the DNS cost.
    if (!m_wiktResolved) { m_wiktIpv4 = resolveHostIpv4(host); m_wiktResolved = true; }
    if (!m_wiktIpv4.isEmpty()) {
        req.setRawHeader("Host", host.toUtf8());
        req.setPeerVerifyName(host);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        QUrl pinned = url;
        pinned.setHost(m_wiktIpv4);
        req.setUrl(pinned);
    }
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, word] {
        rep->deleteLater();
        const bool ok = rep->error() == QNetworkReply::NoError;
        emit dictResult(word, ok ? QString::fromUtf8(rep->readAll()) : QString(), ok);
    });
}
