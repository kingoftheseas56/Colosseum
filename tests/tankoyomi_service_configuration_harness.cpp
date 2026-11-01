#include "engine/TankoyomiChapterService.h"
#include "engine/TankoyomiConfigurationStore.h"
#include "engine/MangaImageHostResolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cstring>
#include <memory>

static int failures = 0;
static void check(bool ok, const char *message)
{
    qInfo().noquote() << (ok ? "  ok  " : "  FAIL") << message;
    if (!ok) ++failures;
}

static QStringList ids(const QList<TankoyomiProviderDescriptor> &providers)
{
    QStringList out;
    for (const auto &provider : providers) out.append(provider.id);
    return out;
}

namespace {
constexpr qint64 kRemoteManifestLimit = 1024 * 1024;
const QUrl kWorkerBase(QStringLiteral("https://worker.fixture"));

struct ReplyProbe {
    int aborts = 0;
};

struct ManifestResponse {
    int status = 200;
    QByteArray body;
    QUrl redirect;
    std::shared_ptr<ReplyProbe> probe = std::make_shared<ReplyProbe>();
};

class ManifestReply final : public QNetworkReply {
public:
    ManifestReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation,
                  ManifestResponse response, QObject *parent)
        : QNetworkReply(parent), m_response(std::move(response))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setOpenMode(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
        if (!m_response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, m_response.redirect);
        QTimer::singleShot(0, this, [this] {
            if (isFinished()) return;
            emit readyRead();
            if (isFinished()) return;
            setFinished(true);
            emit finished();
        });
    }

    void abort() override
    {
        if (isFinished()) return;
        ++m_response.probe->aborts;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("fixture aborted"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return qint64(m_response.body.size()) - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *destination, qint64 maximum) override
    {
        const qint64 available = qint64(m_response.body.size()) - m_offset;
        const qint64 count = qMin(maximum, available);
        if (count <= 0) return -1;
        std::memcpy(destination, m_response.body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    ManifestResponse m_response;
    qint64 m_offset = 0;
};

class ManifestNam final : public QNetworkAccessManager {
public:
    explicit ManifestNam(ManifestResponse response) : m_response(std::move(response)) {}

    QList<QNetworkRequest> requests;
    QList<Operation> operations;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        Q_UNUSED(outgoingData)
        requests.append(request);
        operations.append(operation);
        return new ManifestReply(request, operation, m_response, this);
    }

private:
    ManifestResponse m_response;
};

QJsonObject providerObject(const QString &id, const QString &name, int priority, bool enabled)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("entry"), QStringLiteral("languages/en/%1.js").arg(id)},
            {QStringLiteral("priority"), priority},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("pageAccessPolicy"), QStringLiteral("public-https")},
            {QStringLiteral("allowedHosts"), QJsonArray{id + QStringLiteral(".example")}}};
}

QByteArray testManifest(bool refreshed = false)
{
    QJsonArray providers{
        providerObject(QStringLiteral("first"), refreshed ? QStringLiteral("First updated")
                                                          : QStringLiteral("First"), 1, true),
        providerObject(QStringLiteral("second"), refreshed ? QStringLiteral("Second updated")
                                                           : QStringLiteral("Second"), 2, true),
        providerObject(QStringLiteral("manifest-off"), QStringLiteral("Manifest Off"), 3, false)};
    if (refreshed) providers.append(providerObject(QStringLiteral("new-source"),
                                                   QStringLiteral("New Source"), 4, true));
    const QJsonObject language{{QStringLiteral("code"), QStringLiteral("en")},
                               {QStringLiteral("label"), QStringLiteral("English")},
                               {QStringLiteral("countryCode"), QStringLiteral("US")},
                               {QStringLiteral("providers"), providers}};
    const QJsonObject root{{QStringLiteral("id"), QStringLiteral("colosseum.well.tankoyomi")},
                           {QStringLiteral("contractVersion"), 1},
                           {QStringLiteral("version"), refreshed ? QStringLiteral("0.2.1")
                                                                  : QStringLiteral("0.2.0")},
                           {QStringLiteral("defaultLanguage"), QStringLiteral("en")},
                           {QStringLiteral("fallbackPolicy"), QStringLiteral("same-language-only")},
                           {QStringLiteral("languages"), QJsonArray{language}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QStringList rowIds(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &row : rows) out.append(row.toMap().value(QStringLiteral("id")).toString());
    return out;
}

QString manifestCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/tankoyomi/manifest-v1.json");
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool waitForManifestReply(ManifestNam &nam)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool finished = false;
    QObject::connect(&nam, &QNetworkAccessManager::finished, &loop, [&] {
        finished = true;
        // Let every direct reply-finished handler finish its cache/config update.
        QTimer::singleShot(0, &loop, &QEventLoop::quit);
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(2000);
    loop.exec();
    return finished;
}

bool setUserOverlay(TankoyomiConfigurationStore &store)
{
    return store.setProviderEnabled(QStringLiteral("en"), QStringLiteral("first"), false)
        && store.setProviderEnabled(QStringLiteral("en"), QStringLiteral("manifest-off"), true)
        && store.moveProvider(QStringLiteral("en"), QStringLiteral("second"), 0);
}

void checkRefreshRequest(const ManifestNam &nam, const char *caseName)
{
    const bool requestOk = nam.requests.size() == 1 && nam.operations.size() == 1
        && nam.operations.first() == QNetworkAccessManager::GetOperation
        && nam.requests.first().url() == kWorkerBase.resolved(QUrl(QStringLiteral("/manifest.json")))
        && nam.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
            == QNetworkRequest::ManualRedirectPolicy;
    check(requestOk, caseName);
}

void checkRejectedRefresh(const QString &name, ManifestResponse response,
                          const TankoyomiProviderRegistry &cachedRegistry,
                          const QByteArray &cachedBytes, const QString &cachePath,
                          QTemporaryDir &temp)
{
    check(writeBytes(cachePath, cachedBytes), qPrintable(name + QStringLiteral(": seed cache")));
    const QString settingsPath = temp.filePath(name + QStringLiteral(".ini"));
    TankoyomiConfigurationStore store(cachedRegistry, settingsPath);
    ManifestNam nam(std::move(response));
    TankoyomiChapterService service(&nam, &store, 100, MangaImageHostResolver::Lookup(),
                                    cachedRegistry, kWorkerBase);
    check(setUserOverlay(store), qPrintable(name + QStringLiteral(": set user overlay before async reply")));
    const bool gotReply = waitForManifestReply(nam);
    check(gotReply, qPrintable(name + QStringLiteral(": completes asynchronous refresh")));
    checkRefreshRequest(nam, qPrintable(name + QStringLiteral(": one manual-redirect GET to /manifest.json")));
    check(ids(service.candidateProviders(QStringLiteral("en")))
              == QStringList{QStringLiteral("second"), QStringLiteral("manifest-off")},
          qPrintable(name + QStringLiteral(": rejected response retains cached enabled/order overlay")));
    check(rowIds(store.providers(QStringLiteral("en")))
              == QStringList{QStringLiteral("second"), QStringLiteral("first"),
                             QStringLiteral("manifest-off")},
          qPrintable(name + QStringLiteral(": rejected response retains cached provider inventory")));
    check(readBytes(cachePath) == cachedBytes,
          qPrintable(name + QStringLiteral(": rejected response leaves on-disk manifest cache unchanged")));
}

void verifyRemoteManifestRefresh(QTemporaryDir &temp)
{
    const QByteArray cachedBytes = testManifest();
    const QByteArray refreshedBytes = testManifest(true);
    const TankoyomiProviderRegistry cachedRegistry(cachedBytes);
    check(cachedRegistry.isValid(), "remote refresh fixture cached registry is valid");
    const QString cachePath = manifestCachePath();
    check(QDir().mkpath(QFileInfo(cachePath).absolutePath()),
          "remote refresh fixture cache directory exists");

    check(writeBytes(cachePath, cachedBytes), "remote refresh fixture seeds prior cache");
    TankoyomiConfigurationStore updatedStore(cachedRegistry, temp.filePath(QStringLiteral("updated.ini")));
    ManifestResponse success;
    success.body = refreshedBytes;
    ManifestNam successfulNam(success);
    TankoyomiChapterService updatedService(&successfulNam, &updatedStore, 100,
        MangaImageHostResolver::Lookup(), cachedRegistry, kWorkerBase);
    check(setUserOverlay(updatedStore), "user enabled/order overlay is set before refresh completes");
    check(waitForManifestReply(successfulNam), "valid Worker manifest refresh completes asynchronously");
    checkRefreshRequest(successfulNam, "valid refresh uses one manual-redirect GET to /manifest.json");
    check(rowIds(updatedStore.providers(QStringLiteral("en")))
              == QStringList{QStringLiteral("second"), QStringLiteral("first"),
                             QStringLiteral("manifest-off"), QStringLiteral("new-source")},
          "valid refresh updates inventory while preserving configured order and appending new providers");
    check(ids(updatedService.candidateProviders(QStringLiteral("en")))
              == QStringList{QStringLiteral("second"), QStringLiteral("manifest-off"),
                             QStringLiteral("new-source")},
          "valid refresh preserves user enabled flags over changed manifest defaults");
    const QVariantList refreshedRows = updatedStore.providers(QStringLiteral("en"));
    check(refreshedRows.size() == 4
              && refreshedRows.at(0).toMap().value(QStringLiteral("name")).toString()
                  == QStringLiteral("Second updated"),
          "valid refresh exposes the received provider metadata");
    check(readBytes(cachePath) == refreshedBytes,
          "valid refresh commits the validated manifest to the cache");

    ManifestResponse invalidJson;
    invalidJson.body = QByteArrayLiteral("not-json");
    checkRejectedRefresh(QStringLiteral("invalid-json"), invalidJson, cachedRegistry,
                         cachedBytes, cachePath, temp);

    ManifestResponse invalidRegistry;
    invalidRegistry.body = QByteArrayLiteral(
        R"JSON({"id":"colosseum.well.tankoyomi","contractVersion":1,"defaultLanguage":"en","fallbackPolicy":"same-language-only","languages":[]})JSON");
    checkRejectedRefresh(QStringLiteral("invalid-registry"), invalidRegistry, cachedRegistry,
                         cachedBytes, cachePath, temp);

    ManifestResponse oversized;
    oversized.body = QByteArray(kRemoteManifestLimit + 1, 'x');
    const auto oversizedProbe = oversized.probe;
    checkRejectedRefresh(QStringLiteral("oversized"), oversized, cachedRegistry,
                         cachedBytes, cachePath, temp);
    check(oversizedProbe->aborts == 1,
          "oversized Worker manifest is aborted after reading only the bounded prefix");

    ManifestResponse redirect;
    redirect.status = 302;
    redirect.body = refreshedBytes;
    redirect.redirect = QUrl(QStringLiteral("https://attacker.fixture/manifest.json"));
    checkRejectedRefresh(QStringLiteral("redirect"), redirect, cachedRegistry,
                         cachedBytes, cachePath, temp);

    ManifestResponse httpError;
    httpError.status = 503;
    httpError.body = refreshedBytes;
    checkRejectedRefresh(QStringLiteral("non-200"), httpError, cachedRegistry,
                         cachedBytes, cachePath, temp);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ColosseumTankoyomiTests"));
    app.setApplicationName(QStringLiteral("RemoteManifestHarness_%1").arg(app.applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temp;
    check(temp.isValid(), "temporary service configuration directory exists");

    const TankoyomiProviderRegistry registry = TankoyomiProviderRegistry::fromResource();
    check(registry.isValid(), "embedded Tankoyomi manifest is valid");
    TankoyomiConfigurationStore store(registry, temp.filePath(QStringLiteral("service.ini")));
    QNetworkAccessManager nam;
    TankoyomiChapterService service(&nam, &store, 50000, MangaImageHostResolver::Lookup(),
                                    registry, QUrl());

    QString language;
    QList<TankoyomiProviderDescriptor> manifest;
    for (const QVariant &rowValue : registry.languages()) {
        const QString candidate = rowValue.toMap().value(QStringLiteral("code")).toString();
        const auto providers = registry.allProvidersForLanguage(candidate);
        if (providers.size() > 1) {
            language = candidate;
            manifest = providers;
            break;
        }
    }
    check(!manifest.isEmpty(), "embedded multi-provider language is available");
    if (!manifest.isEmpty()) {
        const QString first = manifest.first().id;
        check(!service.candidateProviders(QString()).isEmpty(),
              "empty service language resolves the persisted default language");
        check(store.setDefaultLanguage(language),
              "service policy accepts a configured default language");
        check(ids(service.candidateProviders(QString())) == ids(service.candidateProviders(language)),
              "empty service language follows the current configured default");
        const auto initialCandidates = service.candidateProviders(language);
        int enabledManifestCount = 0;
        for (const auto &provider : manifest) {
            if (provider.manifestEnabled) {
                ++enabledManifestCount;
            } else {
                check(!ids(initialCandidates).contains(provider.id),
                      "manifest-disabled providers stay out of the initial candidate ladder");
            }
        }
        check(initialCandidates.size() == enabledManifestCount,
              "service initially resolves only manifest-enabled providers");
        check(store.setProviderEnabled(language, first, false),
              "service policy can disable the manifest-first provider");
        const auto afterDisable = service.candidateProviders(language);
        check(!afterDisable.isEmpty() && !ids(afterDisable).contains(first),
              "service resolves the current enabled overlay without restart");
        check(store.setProviderEnabled(language, first, true),
              "service policy can re-enable a provider");
        check(store.moveProvider(language, first, manifest.size() - 1),
              "service policy can move a provider to the end");
        const auto reordered = service.candidateProviders(language);
        check(!reordered.isEmpty() && reordered.last().id == first,
              "service resolves the current configured provider order");
        check(service.candidateProviders(QStringLiteral("zz")).isEmpty(),
              "service never crosses language boundaries for unsupported requests");

        bool failedWithoutCandidates = false;
        QObject::connect(&service, &TankoyomiChapterService::catalogueFailed, &app,
                         [&](const QString &requestId, const QString &) {
            if (requestId == QLatin1String("disabled")) failedWithoutCandidates = true;
        });
        for (const auto &provider : manifest)
            check(store.setProviderEnabled(language, provider.id, false),
                  "service policy can disable every provider in a language");
        service.fetchCatalogue(QStringLiteral("disabled"), QStringLiteral("No Network"), language);
        check(failedWithoutCandidates,
              "fetchCatalogue consumes the current empty candidate ladder without network fallback");
    }

    verifyRemoteManifestRefresh(temp);
    const QString cachePath = manifestCachePath();
    QFile::remove(cachePath);
    QDir().rmdir(QFileInfo(cachePath).absolutePath());

    if (failures) return 1;
    qInfo() << "PASS — Tankoyomi service configuration contract";
    return 0;
}
