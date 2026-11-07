#include "AndroidProof.h"

#include "NuvioManifest.h"
#include "NuvioRepositoryStore.h"
#include "NuvioSourceResolver.h"
#include "NuvioTmdbCredentials.h"
#include "NuvioWorkerProtocol.h"
#include "engine/ExtensionsStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJniObject>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QtCore/qcoreapplication_platform.h>

namespace {
constexpr int kScreenshotHoldMs = 1800;
constexpr int kLiveDeadlineMs = 90000;
constexpr auto kStremioId = "org.colosseum.proof.publicdomain";
// Exercise the unchanged native authorizer without reading credentials, sending
// requests, or exposing test values to JavaScript or logs. This is a synthetic
// boundary check, not successful authentication against the TMDB service.
bool syntheticTmdbAuthorizationSelfTest()
{
    using Authorization = NuvioTmdb::Authorization;
    const NuvioTmdb::Credential fake{QByteArray("synthetic-proof-value-not-a-real-key"), false};
    const auto suffix = QStringLiteral("/3/movie/10331?api_key=")
        + QLatin1String(NuvioTmdb::placeholder);
    for (const auto &origin : {"https://api.themoviedb.org.evil.example", "https://example.org",
                               "http://api.themoviedb.org", "https://api.themoviedb.org:444",
                               "https://user@api.themoviedb.org"}) {
        QNetworkRequest request{QUrl(QString::fromLatin1(origin) + suffix)};
        request.setRawHeader("Cookie", "proof-cookie=untrusted");
        const auto originalUrl = request.url();
        if (NuvioTmdb::authorize(request, fake) != Authorization::Unchanged
            || request.url() != originalUrl || request.hasRawHeader("Authorization")
            || request.rawHeader("Cookie") != QByteArray("proof-cookie=untrusted"))
            return false;
    }
    QNetworkRequest request{QUrl(QStringLiteral("https://api.themoviedb.org") + suffix)};
    if (NuvioTmdb::authorize(request, {}) != Authorization::Missing)
        return false;
    request.setRawHeader("Referer", "https://example.org/");
    request.setRawHeader("Cookie", "proof-cookie=untrusted");
    request.setRawHeader("X-Untrusted", "must-be-removed");
    if (NuvioTmdb::authorize(request, fake) != Authorization::Attached
        || QUrlQuery(request.url()).queryItemValue(QStringLiteral("api_key")) != QString::fromLatin1(fake.secret)
        || request.hasRawHeader("Referer") || request.hasRawHeader("Cookie")
        || request.hasRawHeader("X-Untrusted") || request.hasRawHeader("Authorization")
        || request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt() != QNetworkRequest::Manual
        || request.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt() != QNetworkRequest::Manual)
        return false;

    const NuvioTmdb::Credential bearer{QByteArray("synthetic.proof.invalid"), true};
    QNetworkRequest bearerRequest{QUrl(QStringLiteral("https://api.themoviedb.org") + suffix)};
    if (NuvioTmdb::authorize(bearerRequest, bearer) != Authorization::Attached
        || bearerRequest.rawHeader("Authorization") != QByteArray("Bearer ") + bearer.secret
        || QUrlQuery(bearerRequest.url()).hasQueryItem(QStringLiteral("api_key")))
        return false;
    return true;
}

constexpr auto kFaultManifest = R"JSON({"name":"Local fault instrumentation (not a live provider)","scrapers":[
    {"id":"crash","name":"Native crash instrumentation","version":"1","filename":"crash.js","supportedTypes":["movie"]},
    {"id":"timeout","name":"Deadline instrumentation","version":"1","filename":"timeout.js","supportedTypes":["movie"]}
]})JSON";

// Resolve imports against the very same deployed QML directory as Main.qml.
// The production SourcesSheet, its native services, and AddonClient remain
// unmodified. Only torrent prefetch is suppressed: this proof retrieves source
// metadata and must not start downloading or playing a movie.
constexpr auto kOverlay = R"QML(
import QtQuick
import QtQuick.Controls
import "."
Item {
    id: proof
    objectName: "nuvioAndroidProofOverlay"
    anchors.fill: parent
    z: 1000000
    Rectangle { anchors.fill: parent; color: "#141210" }
    QtObject {
        id: fixtureExtensions
        function installed() {
            // Restrict this controlled protocol proof to its disclosed fixture.
            // The real Extensions registry (including defaults) is not modified
            // or replaced, and its full relevant state is fingerprinted natively.
            return Extensions.installed().filter(function (entry) {
                return entry.id === "org.colosseum.proof.publicdomain"
            })
        }
    }
    SourcesSheet {
        id: sources
        anchors.fill: parent
        objectName: "nuvioAndroidProofSources"
        extensionsService: fixtureExtensions
        nuvioSourceService: NuvioAndroidProof.sourceService
        function warmTopRow() {}
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 84
        z: 100
        color: "#272018"
        Text {
            anchors.fill: parent
            anchors.margins: 12
            color: "#efce8f"
            font.pixelSize: 14
            wrapMode: Text.Wrap
            text: "NUVIO ANDROID EMULATOR PROOF\n" + NuvioAndroidProof.status
        }
    }
    Connections {
        target: NuvioAndroidProof
        function onShowSourcesRequested() {
            sources.show("movie", NuvioAndroidProof.imdbId, NuvioAndroidProof.title,
                         { tmdbId: Number(NuvioAndroidProof.mediaId), title: NuvioAndroidProof.title })
        }
        function onRunFaultRequested() {
            // Do not call show(): it clears Stremio rows. Replace only the
            // Nuvio request so retention is tested in the actual source picker.
            sources.startNuvio("movie", NuvioAndroidProof.imdbId,
                               { tmdbId: Number(NuvioAndroidProof.mediaId) })
        }
    }
    Timer {
        interval: 100
        repeat: true
        running: true
        onTriggered: NuvioAndroidProof.observeSources(sources.rows, sources.nuvioRows,
                            sources.nuvioPending, sources.loading, sources.askedCount)
    }
}
)QML";
}

AndroidProof::AndroidProof(QQmlApplicationEngine *engine, NuvioRepositoryStore *repositories,
                           NuvioSourceResolver *sources, ExtensionsStore *extensions, QObject *parent)
    : QObject(parent), m_engine(engine), m_repositories(repositories), m_sources(sources),
      m_extensions(extensions), m_currentSources(sources),
      m_faultDirectory(QDir::tempPath() + QStringLiteral("/nuvio-android-proof-XXXXXX"))
{
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] { fail(QStringLiteral("stage_deadline")); });
    connect(m_repositories, &NuvioRepositoryStore::busyChanged, this, [this] {
        if (!m_repositories->busy())
            QTimer::singleShot(0, this, &AndroidProof::repositorySettled);
    });
    connect(m_sources, &NuvioSourceResolver::streamsReady, this,
            [this](int, const QVariantList &streams) {
        if (m_phase != Phase::Resolving && m_phase != Phase::Rechecking)
            return;
        for (const auto &entry : streams) {
            const auto row = entry.toMap();
            if (row.value(QStringLiteral("repoId")).toString() == m_repositoryId
                && row.value(QStringLiteral("pluginId")).toString() == m_pluginId)
                m_directStreams.append(entry);
        }
    });
    connect(m_sources, &NuvioSourceResolver::finished, this, [this](int) {
        if (m_phase == Phase::Resolving || m_phase == Phase::Rechecking)
            m_liveFinished = true;
    });
    connect(m_extensions, &ExtensionsStore::installFailed, this, [this](const QString &, const QString &) {
        if (m_phase == Phase::AddingStremio) fail(QStringLiteral("stremio_fixture_install_failed"));
    });
    connect(m_extensions, &ExtensionsStore::installFinished, this,
            [this](const QString &id, const QString &) {
        if (m_phase != Phase::AddingStremio || id != QLatin1String(kStremioId)) return;
        m_phase = Phase::Between;
        setStatus(QStringLiteral("Controlled HTTPS Stremio fixture installed through the existing registry"));
        record(QStringLiteral("stremio_fixture_installed"), {{"addonId", id},
            {"manifestUrl", m_stremioManifest}, {"sourceKind", "live_https_controlled_fixture"}});
        QTimer::singleShot(kScreenshotHoldMs, this, [this] {
            if (m_phase != Phase::Between) return;
            m_phase = Phase::Resolving;
            m_watchdog.start(kLiveDeadlineMs);
            setStatus(QStringLiteral("Resolving live Archive metadata and the controlled HTTPS Stremio fixture"));
            emit showSourcesRequested();
        });
    });
}

QObject *AndroidProof::sourceService() const
{
    return m_currentSources;
}

QString AndroidProof::intentString(const char *key)
{
    const auto activity = QNativeInterface::QAndroidApplication::context();
    const auto intent = activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
    if (!intent.isValid())
        return {};
    const auto name = QJniObject::fromString(QString::fromLatin1(key));
    return intent.callObjectMethod("getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;",
                                   name.object<jstring>()).toString();
}

void AndroidProof::startFromIntent(const QString &qmlPath)
{
    if (m_phase != Phase::Idle || intentString("colosseum.nuvio.proof") != QLatin1String("true"))
        return;
    m_elapsed.start();
    m_nonce = intentString("colosseum.nuvio.proof.nonce");
    m_repository = intentString("colosseum.nuvio.proof.repository");
    m_pluginId = intentString("colosseum.nuvio.proof.plugin");
    const auto media = intentString("colosseum.nuvio.proof.media");
    const auto imdb = intentString("colosseum.nuvio.proof.imdb");
    const auto title = intentString("colosseum.nuvio.proof.title");
    if (!media.isEmpty()) m_mediaId = media;
    if (!imdb.isEmpty()) m_imdbId = imdb;
    if (!title.isEmpty()) m_title = title;

    static const QRegularExpression noncePattern(QStringLiteral("\\A[a-fA-F0-9]{16,64}\\z"));
    static const QRegularExpression pluginPattern(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,79}\\z"));
    const QUrl repositoryUrl(m_repository, QUrl::StrictMode);
    // Keep the automated proof tied to one independently established public-
    // domain title, rather than treating arbitrary input as rights clearance.
    if (!noncePattern.match(m_nonce).hasMatch() || !NuvioManifest::usableUrl(repositoryUrl)
        || !pluginPattern.match(m_pluginId).hasMatch()
        || m_mediaId != QLatin1String("10331") || m_imdbId != QLatin1String("tt0063350")
        || m_title != QLatin1String("Night of the Living Dead (1968)")) {
        fail(QStringLiteral("invalid_proof_arguments"));
        return;
    }
    // This instrumentation is destructive to its own temporary fixtures only.
    // A clean emulator installation is required, never a user's saved roster.
    if (!m_repositories->repositories().isEmpty()) {
        fail(QStringLiteral("proof_requires_fresh_app_data"));
        return;
    }
    m_repositoryId = QString::fromLatin1(QCryptographicHash::hash(
        repositoryUrl.toEncoded(), QCryptographicHash::Sha256).toHex());
    m_engine->rootContext()->setContextProperty(QStringLiteral("NuvioAndroidProof"), this);
    if (!createOverlay(qmlPath)) {
        fail(QStringLiteral("proof_overlay_failed"));
        return;
    }
    if (!syntheticTmdbAuthorizationSelfTest()) {
        fail(QStringLiteral("synthetic_tmdb_authorization_self_test_failed"));
        return;
    }
    record(QStringLiteral("started"), {{"repository", m_repository}, {"pluginId", m_pluginId},
                                      {"mediaId", m_mediaId}, {"imdbId", m_imdbId}, {"title", m_title},
                                      {"tmdbAuthorizationSelfTest", true},
                                      {"tmdbAuthorizationSelfTestKind", "synthetic_no_network"}});
    setStatus(QStringLiteral("Adding the live HTTPS repository"));
    m_phase = Phase::Adding;
    m_watchdog.start(kLiveDeadlineMs);
    m_repositories->addRepository(m_repository);
}

bool AndroidProof::createOverlay(const QString &qmlPath)
{
    if (m_engine->rootObjects().isEmpty())
        return false;
    auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
    if (!window)
        return false;
    QQmlComponent component(m_engine);
    component.setData(kOverlay, QUrl::fromLocalFile(qmlPath).resolved(QUrl(QStringLiteral("AndroidProofHarness.qml"))));
    if (component.isError()) {
        // QML source is constant and contains no credentials.
        qWarning().noquote() << "[nuvio proof overlay]" << component.errorString();
        return false;
    }
    auto *object = component.create(m_engine->rootContext());
    m_overlay = qobject_cast<QQuickItem *>(object);
    if (!m_overlay) {
        delete object;
        return false;
    }
    m_overlay->setParent(this);
    m_overlay->setParentItem(window->contentItem());
    return true;
}

void AndroidProof::repositorySettled()
{
    if (m_phase != Phase::Adding && m_phase != Phase::Enabling)
        return;
    if (!m_repositories->error().isEmpty()) {
        fail(m_phase == Phase::Adding ? QStringLiteral("repository_add_failed")
                                      : QStringLiteral("plugin_enable_failed"));
        return;
    }
    if (m_phase == Phase::Adding) {
        QVariantMap repository;
        for (const auto &value : m_repositories->repositories()) {
            if (value.toMap().value(QStringLiteral("id")).toString() == m_repositoryId)
                repository = value.toMap();
        }
        bool found = false;
        for (const auto &value : repository.value(QStringLiteral("plugins")).toList()) {
            if (value.toMap().value(QStringLiteral("id")).toString() == m_pluginId)
                found = true;
        }
        if (!found) {
            fail(QStringLiteral("selected_plugin_absent"));
            return;
        }
        m_phase = Phase::Between;
        setStatus(QStringLiteral("Live repository added"));
        record(QStringLiteral("repo_added"), {{"repository", m_repository}, {"repoId", m_repositoryId},
                                             {"repositoryName", repository.value(QStringLiteral("name"))}});
        QTimer::singleShot(kScreenshotHoldMs, this, [this] {
            if (m_phase != Phase::Between) return;
            m_phase = Phase::Enabling;
            setStatus(QStringLiteral("Enabling the selected live plugin"));
            m_repositories->setPluginEnabled(m_repositoryId, m_pluginId, true);
        });
        return;
    }
    const auto enabled = m_repositories->enabledPlugins(QStringLiteral("movie"));
    if (enabled.size() != 1 || enabled.first().repoId != m_repositoryId || enabled.first().id != m_pluginId) {
        fail(QStringLiteral("selected_plugin_not_enabled"));
        return;
    }
    m_phase = Phase::Between;
    setStatus(QStringLiteral("Live plugin enabled: ") + enabled.first().name);
    record(QStringLiteral("plugin_enabled"), {{"repoId", m_repositoryId}, {"pluginId", m_pluginId},
                                             {"pluginName", enabled.first().name},
                                             {"scriptSha256", QString::fromLatin1(enabled.first().sha256)}});
    QTimer::singleShot(kScreenshotHoldMs, this, [this] {
        if (m_phase != Phase::Between) return;
        installStremioFixture();
    });
}

void AndroidProof::installStremioFixture()
{
    m_stremioManifest = QUrl(m_repository).resolved(QUrl(QStringLiteral("stremio/manifest.json"))).toString();
    if (m_extensions->isInstalled(QString::fromLatin1(kStremioId))) {
        fail(QStringLiteral("stremio_fixture_requires_fresh_install"));
        return;
    }
    m_phase = Phase::AddingStremio;
    m_watchdog.start(kLiveDeadlineMs);
    setStatus(QStringLiteral("Installing the disclosed Stremio protocol fixture over HTTPS"));
    m_extensions->install(m_stremioManifest);
}

QByteArray AndroidProof::fingerprint(const QVariant &value)
{
    return QCryptographicHash::hash(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact),
                                     QCryptographicHash::Sha256).toHex();
}

QByteArray AndroidProof::registryFingerprint() const
{
    QVariantList roster;
    for (const auto &entry : m_extensions->installed()) {
        const auto row = entry.toMap();
        const auto manifest = row.value(QStringLiteral("manifest")).toMap();
        roster.append(QVariantMap{{"id", row.value(QStringLiteral("id"))},
                                  {"transportUrl", row.value(QStringLiteral("transportUrl"))},
                                  {"enabled", row.value(QStringLiteral("enabled"))},
                                  {"resources", manifest.value(QStringLiteral("resources"))},
                                  {"types", manifest.value(QStringLiteral("types"))}});
    }
    // Only the digest is emitted, never configured provider URLs or headers.
    return fingerprint(roster);
}

QVariantMap AndroidProof::integrityEvidence(const QVariantList &stremioRows) const
{
    const auto rowsFingerprint = fingerprint(stremioRows);
    const auto rosterFingerprint = registryFingerprint();
    return {{"stremioRows", stremioRows.size()},
            {"stremioFingerprint", QString::fromLatin1(rowsFingerprint)},
            {"registryFingerprint", QString::fromLatin1(rosterFingerprint)},
            {"stremioIntact", !stremioRows.isEmpty() && rowsFingerprint == m_stremioFingerprint
                               && rosterFingerprint == m_registryFingerprint}};
}

void AndroidProof::observeSources(const QVariantList &stremioRows, const QVariantList &nuvioRows,
                                  bool nuvioPending, bool stremioPending, int askedCount)
{
    if (m_phase == Phase::Rechecking) {
        if (!m_liveFinished || nuvioPending || stremioPending) return;
        auto evidence = integrityEvidence(stremioRows);
        if (!evidence.value(QStringLiteral("stremioIntact")).toBool()
            || m_directStreams.isEmpty() || nuvioRows.isEmpty() || askedCount != 1
            || m_directStreams.constFirst().toMap().value(QStringLiteral("url"))
                != stremioRows.constFirst().toMap().value(QStringLiteral("url"))) {
            fail(QStringLiteral("post_fault_live_requery_failed"));
            return;
        }
        m_watchdog.stop();
        m_phase = Phase::Between;
        evidence.insert(QStringLiteral("sourceKind"), QStringLiteral("live_https_controlled_fixture"));
        evidence.insert(QStringLiteral("stremioRequery"), true);
        evidence.insert(QStringLiteral("nuvioRequery"), true);
        setStatus(QStringLiteral("Fresh Nuvio and Stremio queries succeeded after both failures"));
        record(QStringLiteral("stremio_requeried"), evidence);
        QTimer::singleShot(kScreenshotHoldMs, this, [this, evidence] {
            if (m_phase != Phase::Between) return;
            m_phase = Phase::Complete;
            setStatus(QStringLiteral("PASS: direct stream, crash, timeout and fresh Stremio protocol query"));
            record(QStringLiteral("completed"), evidence);
        });
        return;
    }
    if (m_phase == Phase::Resolving) {
        if (!m_liveFinished || nuvioPending || stremioPending)
            return;
        if (m_directStreams.isEmpty() || nuvioRows.isEmpty()) {
            fail(QStringLiteral("live_plugin_returned_no_direct_stream"));
            return;
        }
        if (stremioRows.size() != 1 || askedCount != 1
            || stremioRows.constFirst().toMap().value(QStringLiteral("addonId")).toString() != QLatin1String(kStremioId)) {
            fail(QStringLiteral("controlled_stremio_baseline_missing"));
            return;
        }
        const auto row = m_directStreams.constFirst().toMap();
        const auto streamUrl = row.value(QStringLiteral("url")).toString();
        const QUrl url(streamUrl, QUrl::StrictMode);
        if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("archive.org")
            || !url.path().startsWith(QLatin1String("/download/Night_Of_The_Living_Dead_raw_HD_WS/"))
            || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
            || stremioRows.constFirst().toMap().value(QStringLiteral("url")).toString() != streamUrl) {
            fail(QStringLiteral("live_stream_invalid"));
            return;
        }
        m_stremioFingerprint = fingerprint(stremioRows);
        m_registryFingerprint = registryFingerprint();
        m_stremioRows = stremioRows.size();
        auto evidence = integrityEvidence(stremioRows);
        evidence.insert(QStringLiteral("streamUrl"), streamUrl);
        evidence.insert(QStringLiteral("title"), m_title);
        evidence.insert(QStringLiteral("mediaId"), m_mediaId);
        evidence.insert(QStringLiteral("imdbId"), m_imdbId);
        evidence.insert(QStringLiteral("pluginId"), m_pluginId);
        evidence.insert(QStringLiteral("stremioManifestUrl"), m_stremioManifest);
        evidence.insert(QStringLiteral("stremioProviderId"), QString::fromLatin1(kStremioId));
        evidence.insert(QStringLiteral("directRows"), m_directStreams.size());
        evidence.insert(QStringLiteral("stremioAskedCount"), askedCount);
        evidence.insert(QStringLiteral("sourceKind"), QStringLiteral("live_https_controlled_fixture"));
        m_watchdog.stop();
        m_phase = Phase::Between;
        setStatus(QStringLiteral("Live direct stream found; %1 Stremio source rows intact").arg(m_stremioRows));
        record(QStringLiteral("direct_stream"), evidence);
        QTimer::singleShot(kScreenshotHoldMs, this, [this] {
            if (m_phase == Phase::Between) beginFault(QStringLiteral("crash"));
        });
        return;
    }
    if ((m_phase == Phase::Crash || m_phase == Phase::Timeout)
        && m_faultFinished && !nuvioPending && !stremioPending)
        finishFault(stremioRows);
}

bool AndroidProof::prepareFaultStore()
{
    if (m_faultSources)
        return true;
    if (!m_faultDirectory.isValid())
        return false;
    m_faultRepositories = new NuvioRepositoryStore(m_faultDirectory.path(),
        [](const QUrl &url, qsizetype, QObject *, NuvioRepositoryStore::FetchCompletion complete) {
            // Synthetic inputs are strictly confined to the failure stages.
            // Nothing in this store is a substitute for the live-provider gate.
            if (url.fileName() == QLatin1String("manifest.json"))
                complete(kFaultManifest, url, {});
            else if (url.fileName() == QLatin1String("crash.js"))
                complete("module.exports.getStreams = async () => { __proofNativeCrash(); return []; };", url, {});
            else if (url.fileName() == QLatin1String("timeout.js"))
                complete("module.exports.getStreams = async () => { while (true) {} };", url, {});
            else
                complete({}, url, QStringLiteral("unknown_fixture"));
        }, this);
    m_faultRepositories->addRepository(QStringLiteral("https://nuvio-proof.invalid/manifest.json"));
    if (!m_faultRepositories->error().isEmpty() || m_faultRepositories->repositories().size() != 1)
        return false;
    m_faultRepositoryId = m_faultRepositories->repositories().constFirst().toMap().value(QStringLiteral("id")).toString();
    m_faultSources = new NuvioSourceResolver(m_faultRepositories, QStringLiteral("android:private-nuvio-service"), this);
    connect(m_faultSources, &NuvioSourceResolver::proofWorkerStarted, this,
            [this](int, qint64 pid) {
        if (m_phase != Phase::Crash && m_phase != Phase::Timeout) return;
        m_workerPid = pid;
        if (pid <= 0 || pid == QCoreApplication::applicationPid()) {
            fail(QStringLiteral("fault_worker_not_separate_process"));
            return;
        }
        record(QStringLiteral("worker_started"), {{"fault", m_fault}, {"workerPid", pid},
                                                  {"syntheticFaultScript", true}});
    });
    connect(m_faultSources, &NuvioSourceResolver::proofWorkerFinished, this,
            [this](int, qint64 pid, const QString &outcome) {
        if (m_phase != Phase::Crash && m_phase != Phase::Timeout) return;
        if (pid > 0) m_workerPid = pid;
        m_faultOutcome = outcome;
    });
    connect(m_faultSources, &NuvioSourceResolver::finished, this, [this](int) {
        if (m_phase == Phase::Crash || m_phase == Phase::Timeout)
            m_faultFinished = true;
    });
    return true;
}

void AndroidProof::beginFault(const QString &fault)
{
    if (!prepareFaultStore()) {
        fail(QStringLiteral("fault_fixture_setup_failed"));
        return;
    }
    for (const auto &id : {QStringLiteral("crash"), QStringLiteral("timeout")})
        m_faultRepositories->setPluginEnabled(m_faultRepositoryId, id, id == fault);
    if (!m_faultRepositories->error().isEmpty()
        || m_faultRepositories->enabledPlugins(QStringLiteral("movie")).size() != 1) {
        fail(QStringLiteral("fault_fixture_enable_failed"));
        return;
    }
    m_phase = fault == QLatin1String("crash") ? Phase::Crash : Phase::Timeout;
    m_fault = fault;
    m_faultOutcome.clear();
    m_workerPid = 0;
    m_faultFinished = false;
    m_currentSources = m_faultSources;
    emit sourceServiceChanged();
    m_faultElapsed.start();
    m_watchdog.start(NuvioWorkerProtocol::processDeadlineMs + 15000);
    setStatus(fault == QLatin1String("crash")
        ? QStringLiteral("Forcing a native plugin-service crash; Stremio rows stay visible")
        : QStringLiteral("Forcing a plugin timeout with the unchanged 20s / 22s limits"));
    record(fault + QStringLiteral("_started"), {{"fault", fault}, {"syntheticFaultScript", true}});
    emit runFaultRequested();
}

void AndroidProof::finishFault(const QVariantList &stremioRows)
{
    const QString expected = m_fault == QLatin1String("crash") ? QStringLiteral("crashed")
                                                              : QStringLiteral("deadline");
    auto evidence = integrityEvidence(stremioRows);
    if (m_workerPid <= 0 || m_workerPid == QCoreApplication::applicationPid()
        || m_faultOutcome != expected || !evidence.value(QStringLiteral("stremioIntact")).toBool()) {
        fail(QStringLiteral("fault_or_stremio_integrity_not_proven"));
        return;
    }
    const auto elapsed = m_faultElapsed.elapsed();
    if (m_fault == QLatin1String("timeout")
        && (elapsed < NuvioWorkerProtocol::pluginDeadlineMs - 1000
            || elapsed > NuvioWorkerProtocol::processDeadlineMs + 10000)) {
        fail(QStringLiteral("timeout_did_not_use_desktop_deadline"));
        return;
    }
    evidence.insert(QStringLiteral("fault"), m_fault);
    evidence.insert(QStringLiteral("result"), m_faultOutcome);
    evidence.insert(QStringLiteral("workerPid"), m_workerPid);
    evidence.insert(QStringLiteral("syntheticFaultScript"), true);
    evidence.insert(QStringLiteral("elapsedMs"), elapsed);
    m_watchdog.stop();
    const bool wasCrash = m_phase == Phase::Crash;
    m_phase = Phase::Between;
    setStatus(wasCrash ? QStringLiteral("Native plugin crash survived; Stremio sources unchanged")
                       : QStringLiteral("Plugin timeout survived; Stremio sources unchanged"));
    record(m_fault + QStringLiteral("_survived"), evidence);
    QTimer::singleShot(kScreenshotHoldMs, this, [this, wasCrash, evidence] {
        if (m_phase != Phase::Between) return;
        if (wasCrash) {
            beginFault(QStringLiteral("timeout"));
        } else {
            m_phase = Phase::Rechecking;
            m_directStreams.clear();
            m_liveFinished = false;
            m_currentSources = m_sources;
            emit sourceServiceChanged();
            m_watchdog.start(kLiveDeadlineMs);
            setStatus(QStringLiteral("Re-querying the real Nuvio plugin and unchanged Stremio protocol after both faults"));
            record(QStringLiteral("stremio_requery_started"), evidence);
            emit showSourcesRequested();
        }
    });
}

void AndroidProof::record(const QString &stage, QVariantMap details)
{
    details.insert(QStringLiteral("nonce"), m_nonce);
    details.insert(QStringLiteral("stage"), stage);
    details.insert(QStringLiteral("ok"), stage != QLatin1String("failed"));
    details.insert(QStringLiteral("appPid"), QCoreApplication::applicationPid());
    details.insert(QStringLiteral("sinceStartMs"), m_elapsed.isValid() ? m_elapsed.elapsed() : 0);
    qInfo().noquote() << "NUVIO_ANDROID_PROOF="
                     + QString::fromUtf8(QJsonDocument::fromVariant(details).toJson(QJsonDocument::Compact));
}

void AndroidProof::setStatus(const QString &status)
{
    m_status = status;
    emit statusChanged();
}

void AndroidProof::fail(const QString &reason)
{
    if (m_phase == Phase::Failed || m_phase == Phase::Complete)
        return;
    m_phase = Phase::Failed;
    m_watchdog.stop();
    setStatus(QStringLiteral("FAILED: ") + reason);
    record(QStringLiteral("failed"), {{"reason", reason}, {"fault", m_fault},
                                     {"workerPid", m_workerPid}, {"result", m_faultOutcome}});
}
