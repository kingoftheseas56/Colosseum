#include "net/CachingNam.h"

#include <QAbstractNetworkCache>
#include <QDir>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QQmlNetworkAccessManagerFactory>
#include <QDebug>

#include <memory>

#include "net/BiblioImageDiag.h"
#include "net/Ipv4PinStore.h"
#include "net/PinProxyFactory.h"
#include "net/PosterScoreboard.h"
#include "net/PosterTimingProbe.h"
#include "third_party/miniz/miniz.h"  // gunzip for the Jikan Accept-Encoding workaround

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
// only inherited QNetworkReply signals are emitted (matches the original style).
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

CachingNam::CachingNam(QStringList pinnedHosts, Ipv4PinStore *pinStore, QObject *parent,
                       bool useCache, PosterScoreboard *scoreboard,
                       BiblioImageDiag *imageDiag, PosterTimingProbe *posterTiming,
                       CoverDeliveryState *coverState, QString cacheDirOverride)
    : QNetworkAccessManager(parent),
      // Proxy routing must see the original hostname, including redirects and
      // the search/smoke NAMs. Keep URL/IP rewriting unchanged by default.
      m_pinnedHosts(PinProxyFactory::systemProxyEnabled() ? QStringList() : std::move(pinnedHosts)),
      m_pinStore(pinStore),
      m_useCache(useCache),
      m_scoreboard(scoreboard),
      m_imageDiag(imageDiag),
      m_posterTiming(posterTiming),
      m_coverState(coverState) {
    if (m_useCache) {
        auto *cache = new QNetworkDiskCache(this);
        const QString dir = cacheDirOverride.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                  + QStringLiteral("/colosseum-images")
            : cacheDirOverride;
        QDir().mkpath(dir);
        cache->setCacheDirectory(dir);
        cache->setMaximumCacheSize(qint64(1024) * 1024 * 1024);
        setCache(cache);
    }
}

QNetworkRequest CachingNam::prepareRequest(const QNetworkRequest &req) const {
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

        const QString ipv4 = m_pinStore ? m_pinStore->pinForHost(host) : QString();
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
    return r;
}

QNetworkReply *CachingNam::createRequest(Operation op, const QNetworkRequest &req,
                                         QIODevice *outgoing) {
    QNetworkRequest prepared = prepareRequest(req);
    if (m_useCache && m_coverState && op == QNetworkAccessManager::GetOperation
        && outgoing == nullptr && m_coverState->isManagedCover(req.url())) {
        // Managed cover GET: the outer facade is returned synchronously; admission
        // (per-host cap, FIFO queue, Retry-After cooldown) decides when the real
        // request enters the SAME dispatch path below. UserVerifiedRedirectPolicy
        // pauses cross-host redirects so they re-enter destination admission
        // instead of bypassing the cap with Qt's automatic follow.
        prepared.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::UserVerifiedRedirectPolicy);
        auto *outer = new DeferredCoverReply(m_coverState, this, op, req, prepared, outgoing, this);
        outer->begin();
        return outer;
    }
    return dispatchPreparedRequest(op, prepared, outgoing, req.url(), req.url().host());
}

QNetworkReply *CachingNam::dispatchPreparedRequest(Operation op, const QNetworkRequest &prepared,
                                                   QIODevice *outgoing, const QUrl &diagUrl,
                                                   const QString &logicalHost) {
    const int timingId = m_posterTiming ? m_posterTiming->networkStart(diagUrl, logicalHost) : 0;

    // Jikan's origin proxy returns 504 for Qt's default multi-codec Accept-Encoding
    // (br/zstd) but 200 for a plain "gzip" — this silently emptied every Jikan-fed
    // surface (genre index / Jump registry / Theatre anime). Force gzip and hand the
    // reply through GunzipReply, which decompresses it since Qt won't for a manual
    // encoding. Scoped to api.jikan.moe so nothing else changes shape.
    if (logicalHost == m_jikanHost) {
        QNetworkRequest jikan(prepared);
        jikan.setRawHeader("Accept-Encoding", "gzip");
        QNetworkReply *inner = QNetworkAccessManager::createRequest(op, jikan, outgoing);
        watch(logicalHost, inner, timingId);   // INNER reply carries the attributes
        if (m_imageDiag) m_imageDiag->track(inner, diagUrl);
        evictOnFailure(inner, jikan.url());
        return new GunzipReply(inner);
    }
    QNetworkReply *reply = QNetworkAccessManager::createRequest(op, prepared, outgoing);
    watch(logicalHost, reply, timingId);
    // Per-URL diagnostics keyed by the PRE-rewrite URL — the one QML's Image
    // asked for, so a card's `source` property matches its rows exactly.
    if (m_imageDiag) m_imageDiag->track(reply, diagUrl);
    // The key is the url AFTER any pin rewrite — that is what the cache stored under.
    evictOnFailure(reply, prepared.url());
    return reply;
}

QNetworkReply *CachingNam::dispatchCoverRequest(QNetworkAccessManager::Operation op,
                                                const QNetworkRequest &prepared,
                                                QIODevice *outgoing, const QUrl &logicalUrl,
                                                const QString &logicalHost) {
    return dispatchPreparedRequest(op, prepared, outgoing, logicalUrl, logicalHost);
}

QNetworkRequest CachingNam::prepareCoverHop(const QUrl &target) const {
    QNetworkRequest hop = prepareRequest(QNetworkRequest(target));
    hop.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::UserVerifiedRedirectPolicy);
    return hop;
}

bool CachingNam::coverCacheContains(const QUrl &effectiveUrl) {
    QAbstractNetworkCache *c = cache();
    if (!c)
        return false;
    QIODevice *device = c->data(effectiveUrl);
    if (!device)
        return false;
    delete device;
    return true;
}

void CachingNam::watch(const QString &host, QNetworkReply *reply, int timingId) {
    PosterScoreboard *scoreboard = m_scoreboard;
    if (!scoreboard && !timingId) return;
    PosterTimingProbe *posterTiming = m_posterTiming;
    std::shared_ptr<qint64> received;
    if (timingId) {
        received = std::make_shared<qint64>(-1);
        QObject::connect(reply, &QNetworkReply::requestSent, [posterTiming, timingId] {
            posterTiming->requestSent(timingId);
        });
        QObject::connect(reply, &QNetworkReply::metaDataChanged, [posterTiming, timingId] {
            posterTiming->responseHeaders(timingId);
        });
        QObject::connect(reply, &QNetworkReply::downloadProgress,
                         [received](qint64 bytes, qint64) { *received = bytes; });
    }
    // No receiver context on purpose: the lambda runs on the reply's own thread and
    // record() is mutex-guarded. `host` is the ORIGINAL hostname — reply->url() may
    // carry the rewritten IPv4 literal for URL-pinned hosts.
    QObject::connect(reply, &QNetworkReply::finished, [scoreboard, posterTiming, timingId, received, host, reply] {
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QVariant clen = reply->header(QNetworkRequest::ContentLengthHeader);
        const qint64 bytes = received && *received >= 0 ? *received
            : (clen.isValid() ? clen.toLongLong() : reply->bytesAvailable());
        if (scoreboard)
            scoreboard->record(host, status, ct, bytes,
                               reply->error() != QNetworkReply::NoError);
        if (timingId) {
            const bool cached = reply->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool();
            const QString protocol = cached ? QStringLiteral("cache")
                : (reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()
                    ? QStringLiteral("h2") : QStringLiteral("h1"));
            posterTiming->networkDone(timingId, bytes, ct, protocol,
                                      cached ? QStringLiteral("hit") : QStringLiteral("miss"), status);
        }
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
void CachingNam::evictOnFailure(QNetworkReply *reply, const QUrl &cacheKey) {
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

CachingNamFactory::CachingNamFactory(QStringList pinnedHosts, Ipv4PinStore *pinStore,
                                     PosterScoreboard *scoreboard, BiblioImageDiag *imageDiag,
                                     PosterTimingProbe *posterTiming,
                                     CoverDeliveryState *coverState)
    : // Proxy routing must see the original hostname, including redirects and
      // the search/smoke NAMs. Keep URL/IP rewriting unchanged by default.
      m_pinnedHosts(PinProxyFactory::systemProxyEnabled() ? QStringList() : std::move(pinnedHosts)),
      m_pinStore(pinStore),
      m_scoreboard(scoreboard),
      m_imageDiag(imageDiag),
      m_posterTiming(posterTiming),
      m_coverState(coverState) {}

QNetworkAccessManager *CachingNamFactory::create(QObject *parent) {
    return new CachingNam(m_pinnedHosts, m_pinStore, parent, /*useCache=*/true,
                          m_scoreboard, m_imageDiag, m_posterTiming, m_coverState);
}
