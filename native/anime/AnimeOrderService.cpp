#include "anime/AnimeOrderService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrentRun>

#include <utility>

// AnimeOrderService owns the runtime lifecycle around AnimeOrderIndex:
//   * load a validated cache generation on startup (parsed off the GUI thread);
//   * otherwise download both sources, bounded and HTTPS-only, write an
//     immutable generation directory, verify hashes, parse, and only then swap
//     current.json atomically;
//   * refresh a valid-but-stale generation once per process, never damaging the
//     last good generation on failure.
// resolve() is a cheap synchronous snapshot of the current immutable index.

namespace {

constexpr qint64 kFribbCap = 16LL * 1024 * 1024;
constexpr qint64 kXmlCap = 12LL * 1024 * 1024;
constexpr qint64 kRefreshAgeMs = 7LL * 24 * 60 * 60 * 1000;
constexpr int kMaxRedirects = 5;

const char* const kFribbFile = "fribb-anime-list.json";
const char* const kXmlFile = "anime-list-master.xml";
const char* const kGenFile = "generation.json";
const char* const kPointerFile = "current.json";

const char* const kFribbUrl = "https://raw.githubusercontent.com/Fribb/anime-lists/master/anime-list-mini.json";
const char* const kMappingsUrl = "https://raw.githubusercontent.com/Anime-Lists/anime-lists/master/anime-list-master.xml";

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QByteArray readWhole(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool writeWhole(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data) != data.size())
        return false;
    return f.commit();
}

} // namespace

AnimeOrderService::AnimeOrderService(QNetworkAccessManager* nam, QObject* parent)
    : AnimeOrderService(nam,
                        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/anime-order"),
                        Sources{QUrl(QString::fromLatin1(kFribbUrl)),
                                QUrl(QString::fromLatin1(kMappingsUrl)), false},
                        parent) {}

AnimeOrderService::AnimeOrderService(QNetworkAccessManager* nam, QString cacheRoot, Sources sources,
                                     QObject* parent)
    : QObject(parent), m_nam(nam), m_cacheRoot(std::move(cacheRoot)), m_sources(std::move(sources))
{
    // Defer startup to the event loop so callers can connect to changed() first.
    QTimer::singleShot(0, this, [this] { initialize(); });
}

int AnimeOrderService::revision() const { return m_revision; }
QString AnimeOrderService::state() const { return m_state; }

void AnimeOrderService::setStateAndNotify(const QString& state)
{
    m_state = state;
    emit changed();
}

bool AnimeOrderService::hasIndex() const
{
    QReadLocker lock(&m_indexLock);
    return m_index != nullptr;
}

void AnimeOrderService::installIndex(std::shared_ptr<const AnimeOrderIndex> index)
{
    QWriteLocker lock(&m_indexLock);
    m_index = std::move(index);
}

QVariantMap AnimeOrderService::resolve(const QVariantMap& identities,
                                       const QVariantList& providerEpisodes) const
{
    std::shared_ptr<const AnimeOrderIndex> index;
    {
        QReadLocker lock(&m_indexLock);
        index = m_index;
    }
    if (!index)
        return unavailableResult(providerEpisodes);
    return index->resolve(identities, providerEpisodes);
}

QVariantMap AnimeOrderService::unavailableResult(const QVariantList& providerEpisodes)
{
    return {{QStringLiteral("status"), QStringLiteral("unavailable")},
            {QStringLiteral("ids"), QVariantMap{}},
            {QStringLiteral("episodes"), providerEpisodes},
            {QStringLiteral("seasons"), QVariantList{}},
            {QStringLiteral("absoluteComplete"), false},
            {QStringLiteral("defaultOrder"), QStringLiteral("seasons")},
            {QStringLiteral("diagnostic"), QStringLiteral("anime ordering index unavailable")}};
}

QString AnimeOrderService::genIdFor(const QByteArray& fribb, const QByteArray& xml)
{
    return sha256Hex((sha256Hex(fribb) + sha256Hex(xml)).toUtf8());
}

bool AnimeOrderService::isStale(qint64 fetchedAt)
{
    return QDateTime::currentMSecsSinceEpoch() - fetchedAt > kRefreshAgeMs;
}

void AnimeOrderService::initialize()
{
    setStateAndNotify(QStringLiteral("loading"));

    CacheHit hit;
    if (loadFromCache(&hit)) {
        // Parse the validated cache off-thread; refresh afterward only if stale.
        launchParse(hit.fribb, hit.xml, hit.genId, hit.fetchedAt, /*activate=*/false);
    } else {
        startDownload();
    }
}

bool AnimeOrderService::loadFromCache(CacheHit* out) const
{
    const QByteArray pointerBytes = readWhole(m_cacheRoot + QLatin1Char('/') + kPointerFile);
    if (pointerBytes.isEmpty())
        return false;
    const QJsonObject pointer = QJsonDocument::fromJson(pointerBytes).object();
    const QString genId = pointer.value(QStringLiteral("active")).toString();
    if (genId.isEmpty())
        return false;

    const QString genDir = m_cacheRoot + QStringLiteral("/generations/") + genId;
    const QByteArray manifestBytes = readWhole(genDir + QLatin1Char('/') + kGenFile);
    if (manifestBytes.isEmpty())
        return false;
    const QJsonObject manifest = QJsonDocument::fromJson(manifestBytes).object();
    if (manifest.isEmpty())
        return false;

    const QByteArray fribb = readWhole(genDir + QLatin1Char('/') + kFribbFile);
    const QByteArray xml = readWhole(genDir + QLatin1Char('/') + kXmlFile);
    if (fribb.isEmpty() || xml.isEmpty())
        return false;

    if (sha256Hex(fribb) != manifest.value(QStringLiteral("fribbSha256")).toString())
        return false;
    if (sha256Hex(xml) != manifest.value(QStringLiteral("mappingsSha256")).toString())
        return false;

    out->fribb = fribb;
    out->xml = xml;
    out->genId = genId;
    out->fetchedAt = static_cast<qint64>(manifest.value(QStringLiteral("fetchedAt")).toDouble());
    return true;
}

void AnimeOrderService::startDownload()
{
    if (m_refreshInFlight)
        return; // coalesce concurrent refreshes into one in-flight download
    m_refreshInFlight = true;

    fetchUrl(m_sources.fribb, kFribbCap, 0, [this](bool ok, QByteArray fribb) {
        if (!ok) {
            finishFailure();
            return;
        }
        fetchUrl(m_sources.mappings, kXmlCap, 0, [this, fribb](bool ok2, QByteArray xml) {
            if (!ok2) {
                finishFailure();
                return;
            }
            onDownloadsReady(fribb, xml);
        });
    });
}

void AnimeOrderService::fetchUrl(const QUrl& url, qint64 cap, int redirectDepth,
                                 const std::function<void(bool, QByteArray)>& done)
{
    if (url.scheme() != QLatin1String("https") && !m_sources.allowHttpForTests) {
        done(false, {});
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = m_nam->get(request);

    auto buffer = std::make_shared<QByteArray>();
    auto aborted = std::make_shared<bool>(false);

    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, cap, aborted] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > cap) {
            *aborted = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, buffer, cap, aborted] {
        buffer->append(reply->readAll());
        if (buffer->size() > cap) {
            *aborted = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, buffer, cap, aborted, url, redirectDepth, done] {
                reply->deleteLater();
                if (*aborted) {
                    done(false, {});
                    return;
                }
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QVariant redirect =
                    reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
                if (status >= 300 && status < 400 && redirect.isValid()) {
                    if (redirectDepth >= kMaxRedirects) {
                        done(false, {});
                        return;
                    }
                    const QUrl next = url.resolved(redirect.toUrl());
                    if (next.scheme() != QLatin1String("https") && !m_sources.allowHttpForTests) {
                        done(false, {});
                        return;
                    }
                    fetchUrl(next, cap, redirectDepth + 1, done);
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    done(false, {});
                    return;
                }
                if (status < 200 || status >= 300) {
                    done(false, {});
                    return;
                }
                buffer->append(reply->readAll());
                if (buffer->size() > cap) {
                    done(false, {});
                    return;
                }
                done(true, *buffer);
            });
}

void AnimeOrderService::onDownloadsReady(const QByteArray& fribb, const QByteArray& xml)
{
    const qint64 fetchedAt = QDateTime::currentMSecsSinceEpoch();
    const QString cacheRoot = m_cacheRoot;
    const Sources sources = m_sources;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this,
            [this, watcher, fribb, xml, fetchedAt] {
                const QString genId = watcher->result();
                watcher->deleteLater();
                if (genId.isEmpty()) {
                    finishFailure();
                    return;
                }
                launchParse(fribb, xml, genId, fetchedAt, /*activate=*/true);
            });
    watcher->setFuture(QtConcurrent::run([cacheRoot, sources, fribb, xml, fetchedAt] {
        return AnimeOrderService::writeGeneration(cacheRoot, sources, fribb, xml, fetchedAt);
    }));
}

QString AnimeOrderService::writeGeneration(const QString& cacheRoot, const Sources& sources,
                                           const QByteArray& fribb, const QByteArray& xml,
                                           qint64 fetchedAt)
{
    const QString genId = genIdFor(fribb, xml);
    const QString genDir = cacheRoot + QStringLiteral("/generations/") + genId;
    if (!QDir().mkpath(genDir))
        return {};

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("fetchedAt"), static_cast<double>(fetchedAt));
    manifest.insert(QStringLiteral("fribbUrl"), sources.fribb.toString());
    manifest.insert(QStringLiteral("mappingsUrl"), sources.mappings.toString());
    manifest.insert(QStringLiteral("fribbBytes"), fribb.size());
    manifest.insert(QStringLiteral("mappingsBytes"), xml.size());
    manifest.insert(QStringLiteral("fribbSha256"), sha256Hex(fribb));
    manifest.insert(QStringLiteral("mappingsSha256"), sha256Hex(xml));
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);

    if (!writeWhole(genDir + QLatin1Char('/') + kFribbFile, fribb))
        return {};
    if (!writeWhole(genDir + QLatin1Char('/') + kXmlFile, xml))
        return {};
    if (!writeWhole(genDir + QLatin1Char('/') + kGenFile, manifestBytes))
        return {};

    // Reopen and hash-check all three before this generation is eligible.
    if (sha256Hex(readWhole(genDir + QLatin1Char('/') + kFribbFile)) != sha256Hex(fribb))
        return {};
    if (sha256Hex(readWhole(genDir + QLatin1Char('/') + kXmlFile)) != sha256Hex(xml))
        return {};
    if (QJsonDocument::fromJson(readWhole(genDir + QLatin1Char('/') + kGenFile)).isNull())
        return {};

    return genId;
}

void AnimeOrderService::launchParse(const QByteArray& fribb, const QByteArray& xml,
                                    const QString& genId, qint64 fetchedAt, bool activate)
{
    auto* watcher = new QFutureWatcher<std::shared_ptr<const AnimeOrderIndex>>(this);
    connect(watcher, &QFutureWatcher<std::shared_ptr<const AnimeOrderIndex>>::finished, this,
            [this, watcher, genId, fetchedAt, activate] {
                std::shared_ptr<const AnimeOrderIndex> index = watcher->result();
                watcher->deleteLater();

                if (!index) {
                    if (activate) {
                        finishFailure();
                    } else {
                        // A supposedly-valid cache failed to parse: fall back to
                        // a fresh download rather than trusting it.
                        startDownload();
                    }
                    return;
                }

                if (activate) {
                    const QString previousActive = m_currentGenId;
                    if (!writeCurrentPointer(genId)) {
                        finishFailure();
                        return;
                    }
                    installIndex(index);
                    m_previousGenId = previousActive;
                    m_currentGenId = genId;
                    m_currentFetchedAt = fetchedAt;
                    ++m_revision;
                    m_refreshInFlight = false;
                    pruneGenerations(genId, previousActive);
                    setStateAndNotify(QStringLiteral("ready"));
                    return;
                }

                // Cache load succeeded.
                installIndex(index);
                m_currentGenId = genId;
                m_currentFetchedAt = fetchedAt;
                ++m_revision;
                setStateAndNotify(QStringLiteral("ready"));
                if (isStale(fetchedAt) && !m_autoRefreshDone) {
                    m_autoRefreshDone = true;
                    startDownload();
                }
            });

    watcher->setFuture(QtConcurrent::run([fribb, xml]() -> std::shared_ptr<const AnimeOrderIndex> {
        QString error;
        return AnimeOrderIndex::fromSources(fribb, xml, &error);
    }));
}

bool AnimeOrderService::writeCurrentPointer(const QString& genId) const
{
    QJsonObject pointer;
    pointer.insert(QStringLiteral("schemaVersion"), 1);
    pointer.insert(QStringLiteral("active"), genId);
    return writeWhole(m_cacheRoot + QLatin1Char('/') + kPointerFile,
                      QJsonDocument(pointer).toJson(QJsonDocument::Compact));
}

void AnimeOrderService::pruneGenerations(const QString& keepActive, const QString& keepPrevious) const
{
    QDir gens(m_cacheRoot + QStringLiteral("/generations"));
    const QStringList names = gens.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : names) {
        if (name == keepActive || name == keepPrevious)
            continue;
        QDir(gens.filePath(name)).removeRecursively();
    }
}

void AnimeOrderService::finishFailure()
{
    m_refreshInFlight = false;
    // A failed refresh keeps the last good generation (stale); a failed cold
    // start with no cache surfaces error. No refresh failure blocks the UI.
    setStateAndNotify(hasIndex() ? QStringLiteral("stale") : QStringLiteral("error"));
}

void AnimeOrderService::refreshIfStale()
{
    if (m_refreshInFlight)
        return; // coalesce
    if (!hasIndex()) {
        startDownload();
        return;
    }
    if (isStale(m_currentFetchedAt))
        startDownload();
}
