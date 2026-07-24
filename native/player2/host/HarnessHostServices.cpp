#include "HarnessHostServices.h"

#include "player2/video/Player2VideoItem.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QMetaEnum>
#include <QtCore/QUrl>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

#include <algorithm>
#include <cstdlib>

// The D3D11 header chain (via HarnessHostServices.h -> D3D11VideoPipeline.h) drags in windows.h,
// whose min/max macros clobber std::max; undo them, as the rest of the engine does.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Colosseum::Player2 {

HarnessHostServices::HarnessHostServices(QObject *parent)
    : Player2HostServices(parent)
{
    m_session.setVideoPipeline(&m_pipeline);
    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &HarnessHostServices::produceFrame);
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        finish(false, QStringLiteral("Player 2 lab gate did not complete in 20 seconds"), 2);
    });
    connect(&m_session, &Player2Session::stateChanged, this, [this] {
        if (m_session.state() == Player2State::Playing)
            m_sawPlaying = true;
        emit metricsChanged();
    });
    connect(&m_session, &Player2Session::durationChanged, this,
            [this] { emit metricsChanged(); });
    connect(&m_session, &Player2Session::tracksChanged, this,
            [this] { emit metricsChanged(); });
    connect(&m_session, &Player2Session::errorOccurred, this,
            [this](const Player2Error &error) { finish(false, error.message, 2); });
}

// --- Deterministic orchestration fixtures (Task 14) -----------------------------------------------
// The lab host answers every seam from these builders; it never touches a real catalog/source/store.
// Requests are recorded (appendEvent) AND resolved exactly once via the base signal after a short
// async hop, so the shell's request->render flow is exercised the same as against a live host.
namespace {

constexpr int kResolveDelayMs = 15; // one async hop: proves the shell handles non-immediate results

QVariantMap fixtureEpisode(const QString &mediaId, int direction)
{
    if (mediaId == QStringLiteral("series-head") && direction < 0)
        return QVariantMap{{QStringLiteral("dead"), true}}; // real series boundary, not an error
    if (mediaId == QStringLiteral("boom"))
        return QVariantMap{{QStringLiteral("error"), QStringLiteral("episode lookup failed")}};
    const QString suffix = direction >= 0 ? QStringLiteral("#next") : QStringLiteral("#prev");
    return QVariantMap{
        {QStringLiteral("mediaId"), mediaId + suffix},
        {QStringLiteral("title"), (direction >= 0 ? QStringLiteral("Next Episode")
                                                  : QStringLiteral("Previous Episode"))},
        {QStringLiteral("season"), 1},
        {QStringLiteral("episode"), direction >= 0 ? 6 : 4},
        {QStringLiteral("durationSeconds"), 2640.0},
        {QStringLiteral("poster"), QStringLiteral("qrc:/player2/fixtures/poster.png")}};
}

QVariantList fixtureSources(const QString &mediaId)
{
    if (mediaId == QStringLiteral("empty"))
        return {};
    return QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("src-2160")},
                    {QStringLiteral("title"), QStringLiteral("2160p HDR · WEB")},
                    {QStringLiteral("url"), QStringLiteral("http://lab.invalid/2160")},
                    {QStringLiteral("quality"), QStringLiteral("2160p")},
                    {QStringLiteral("sizeBytes"), 8'000'000'000.0},
                    {QStringLiteral("seeders"), 240},
                    {QStringLiteral("current"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("src-1080")},
                    {QStringLiteral("title"), QStringLiteral("1080p · BluRay")},
                    {QStringLiteral("url"), QStringLiteral("http://lab.invalid/1080")},
                    {QStringLiteral("quality"), QStringLiteral("1080p")},
                    {QStringLiteral("sizeBytes"), 3'000'000'000.0},
                    {QStringLiteral("seeders"), 512},
                    {QStringLiteral("current"), false}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("src-720")},
                    {QStringLiteral("title"), QStringLiteral("720p · WEB")},
                    {QStringLiteral("url"), QStringLiteral("http://lab.invalid/720")},
                    {QStringLiteral("quality"), QStringLiteral("720p")},
                    {QStringLiteral("sizeBytes"), 1'200'000'000.0},
                    {QStringLiteral("seeders"), 90},
                    {QStringLiteral("dead"), true}}};
}

QVariantList fixtureSubtitles(const QString &mediaId)
{
    if (mediaId == QStringLiteral("empty"))
        return {};
    return QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("os-en-1")},
                    {QStringLiteral("url"), QStringLiteral("http://lab.invalid/en.srt")},
                    {QStringLiteral("lang"), QStringLiteral("en")},
                    {QStringLiteral("langName"), QStringLiteral("English")},
                    {QStringLiteral("provider"), QStringLiteral("OpenSubtitles")},
                    {QStringLiteral("downloads"), 4210},
                    {QStringLiteral("external"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("os-es-1")},
                    {QStringLiteral("url"), QStringLiteral("http://lab.invalid/es.srt")},
                    {QStringLiteral("lang"), QStringLiteral("es")},
                    {QStringLiteral("langName"), QStringLiteral("Spanish")},
                    {QStringLiteral("provider"), QStringLiteral("OpenSubtitles")},
                    {QStringLiteral("downloads"), 1180},
                    {QStringLiteral("external"), true}}};
}

QVariantList fixtureSegments(const QString &mediaId)
{
    if (mediaId == QStringLiteral("empty"))
        return {};
    return QVariantList{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("intro")},
                    {QStringLiteral("startSeconds"), 62.0},
                    {QStringLiteral("endSeconds"), 152.0},
                    {QStringLiteral("autoSkip"), false}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("credits")},
                    {QStringLiteral("startSeconds"), 2520.0},
                    {QStringLiteral("endSeconds"), 2640.0},
                    {QStringLiteral("autoSkip"), true}}};
}

QVariantMap fixtureMetadata(const QString &mediaId)
{
    if (mediaId == QStringLiteral("boom"))
        return QVariantMap{{QStringLiteral("error"), QStringLiteral("metadata hydration failed")}};
    return QVariantMap{
        {QStringLiteral("mediaId"), mediaId},
        {QStringLiteral("title"), QStringLiteral("Lab Title")},
        {QStringLiteral("logo"), QStringLiteral("qrc:/player2/fixtures/logo.png")},
        {QStringLiteral("backdrop"), QStringLiteral("qrc:/player2/fixtures/backdrop.png")},
        {QStringLiteral("seasons"), 4},
        {QStringLiteral("resumeSeconds"), 305.0}};
}

QVariantList fixtureSeasonEpisodes(const QString &mediaId, int season)
{
    if (mediaId == QStringLiteral("empty"))
        return {};
    // A believable season: the first episode fully watched, the second mid-progress, the rest fresh —
    // so the browser's watched-dim / progress-bar / fresh states all have something to render.
    static const char *const kTitles[] = {"The Target", "Old Cases", "Amsterdam", "Hard Cases",
                                          "Bad Dreams", "The Wire"};
    const int total = 6;
    QVariantList episodes;
    for (int number = 1; number <= total; ++number) {
        const double frac = number == 1 ? 1.0 : (number == 2 ? 0.42 : 0.0);
        episodes.append(QVariantMap{
            {QStringLiteral("mediaId"),
             QStringLiteral("%1:%2:%3").arg(mediaId).arg(season).arg(number)},
            {QStringLiteral("title"), QString::fromLatin1(kTitles[(number - 1) % 6])},
            {QStringLiteral("season"), season},
            {QStringLiteral("episode"), number},
            {QStringLiteral("durationSeconds"), 3120.0},
            {QStringLiteral("progressFraction"), frac},
            {QStringLiteral("watched"), number == 1}});
    }
    return episodes;
}

} // namespace

void HarnessHostServices::requestAdjacentEpisode(const QString &mediaId, int direction)
{
    appendEvent(QStringLiteral("adjacent-episode"),
                QStringLiteral("%1:%2").arg(mediaId).arg(direction));
    QTimer::singleShot(kResolveDelayMs, this, [this, mediaId, direction] {
        emit adjacentEpisodeResolved(mediaId, direction, fixtureEpisode(mediaId, direction));
    });
}

void HarnessHostServices::requestSeasonEpisodes(const QString &mediaId, int season)
{
    appendEvent(QStringLiteral("season-episodes"),
                QStringLiteral("%1:%2").arg(mediaId).arg(season));
    QTimer::singleShot(kResolveDelayMs, this, [this, mediaId, season] {
        emit seasonEpisodesResolved(mediaId, season, fixtureSeasonEpisodes(mediaId, season));
    });
}

void HarnessHostServices::requestAlternateSources(const QString &mediaId)
{
    appendEvent(QStringLiteral("alternate-sources"), mediaId);
    QTimer::singleShot(kResolveDelayMs, this, [this, mediaId] {
        emit alternateSourcesResolved(mediaId, fixtureSources(mediaId));
    });
}

void HarnessHostServices::requestOnlineSubtitles(const QString &mediaId)
{
    appendEvent(QStringLiteral("online-subtitles"), mediaId);
    QTimer::singleShot(kResolveDelayMs, this, [this, mediaId] {
        emit onlineSubtitlesResolved(mediaId, fixtureSubtitles(mediaId));
    });
}

void HarnessHostServices::requestSkipSegments(const QString &mediaId)
{
    appendEvent(QStringLiteral("skip-segments"), mediaId);
    QTimer::singleShot(kResolveDelayMs, this, [this, mediaId] {
        emit skipSegmentsResolved(mediaId, fixtureSegments(mediaId));
    });
}

void HarnessHostServices::requestDownload(const QString &mediaId, const QString &sourceId)
{
    appendEvent(QStringLiteral("download"), QStringLiteral("%1:%2").arg(mediaId, sourceId));
    // A monotonic STATE STREAM, not resolve-once: queued -> active -> (ready | failed).
    const auto emitState = [this, mediaId](const QString &state, double progress,
                                           const QString &error) {
        QVariantMap s{{QStringLiteral("state"), state}, {QStringLiteral("progress"), progress}};
        if (!error.isEmpty())
            s.insert(QStringLiteral("error"), error);
        emit downloadStateChanged(mediaId, s);
    };
    QTimer::singleShot(kResolveDelayMs, this, [emitState] { emitState(QStringLiteral("queued"), 0.0, {}); });
    QTimer::singleShot(kResolveDelayMs * 2, this,
                       [emitState] { emitState(QStringLiteral("active"), 0.5, {}); });
    if (sourceId == QStringLiteral("no")) {
        QTimer::singleShot(kResolveDelayMs * 3, this, [emitState] {
            emitState(QStringLiteral("failed"), 0.5, QStringLiteral("source is dead"));
        });
    } else {
        QTimer::singleShot(kResolveDelayMs * 3, this,
                           [emitState] { emitState(QStringLiteral("ready"), 1.0, {}); });
    }
}

void HarnessHostServices::requestMetadata(const QString &mediaId)
{
    appendEvent(QStringLiteral("metadata"), mediaId);
    QTimer::singleShot(kResolveDelayMs, this, [this, mediaId] {
        emit metadataResolved(mediaId, fixtureMetadata(mediaId));
    });
}

void HarnessHostServices::reportProgress(const QString &mediaId, double position, double duration)
{
    appendEvent(QStringLiteral("progress"),
                QStringLiteral("%1:%2/%3").arg(mediaId).arg(position).arg(duration));
}

QString HarnessHostServices::adapter() const { return m_metrics.qtAdapter; }
QString HarnessHostServices::source() const
{
    if (!m_url.isEmpty())
        return m_url;
    return m_filePath.isEmpty() ? QStringLiteral("Synthetic D3D11 frames")
                                : QFileInfo(m_filePath).fileName();
}
QString HarnessHostServices::decodePath() const
{
    return m_metrics.hardwareFormat.isEmpty() ? QStringLiteral("D3D11 shared texture ring")
                                               : m_metrics.hardwareFormat;
}
QString HarnessHostServices::status() const { return m_status; }
quint64 HarnessHostServices::generated() const { return m_metrics.submitted; }
quint64 HarnessHostServices::presented() const { return m_metrics.presented; }
quint64 HarnessHostServices::dropped() const { return m_metrics.producerStarved; }
quint64 HarnessHostServices::late() const { return 0; }
quint64 HarnessHostServices::cpuTransfers() const { return m_metrics.cpuTransfers; }
quint64 HarnessHostServices::deviceErrors() const { return m_metrics.deviceErrors; }
bool HarnessHostServices::adapterMatch() const { return m_metrics.adapterMatch; }
QString HarnessHostServices::sessionState() const
{
    return QMetaEnum::fromType<Player2State>().valueToKey(static_cast<int>(m_session.state()));
}
double HarnessHostServices::duration() const { return m_session.duration(); }
int HarnessHostServices::trackCount() const { return m_session.tracks().size(); }

void HarnessHostServices::setReportPath(const QString &path) { m_reportPath = path; }
void HarnessHostServices::setMinimumRunSeconds(int seconds)
{
    m_minimumRunSeconds = std::max(0, seconds);
}

void HarnessHostServices::setNormalizationMode(NormalizationMode mode)
{
    // Set before the file opens so the session carries the mode into the new playback session.
    m_session.setNormalizationMode(mode);
    m_reportNormalization =
        QString::fromLatin1(QMetaEnum::fromType<NormalizationMode>().valueToKey(
            static_cast<int>(mode)));
}

bool HarnessHostServices::startScenario(const QString &scenario, QString *error)
{
    if (scenario != QStringLiteral("synthetic")) {
        if (error)
            *error = QStringLiteral("Task 4 supports only --scenario synthetic");
        return false;
    }
    m_started = true;
    m_runTimer.start();
    appendEvent(QStringLiteral("scenario-started"), scenario);
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start(20'000);
    return true;
}

bool HarnessHostServices::startFile(const QString &path, QString *error)
{
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("Media file does not exist: %1").arg(path);
        return false;
    }
    m_filePath = QFileInfo(path).absoluteFilePath();
    m_started = true;
    m_runTimer.start();
    appendEvent(QStringLiteral("file-started"), QFileInfo(path).fileName());
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start((m_minimumRunSeconds + 30) * 1'000);
    emit metricsChanged();
    return true;
}

bool HarnessHostServices::startUrl(const QString &url, const QString &headersJsonPath, bool live,
                                   QString *error)
{
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty()) {
        if (error)
            *error = QStringLiteral("Invalid stream URL: %1").arg(url);
        return false;
    }
    if (!headersJsonPath.isEmpty()) {
        QFile file(headersJsonPath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Could not read headers JSON: %1").arg(headersJsonPath);
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error)
                *error = QStringLiteral("Headers JSON must be an object of string values");
            return false;
        }
        const QJsonObject object = document.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            m_headers.insert(it.key().toUtf8(), it.value().toString().toUtf8());
    }
    m_url = url;
    m_live = live;
    m_started = true;
    m_runTimer.start();
    appendEvent(QStringLiteral("url-started"), parsed.host());
    m_frameTimer.start();
    if (!m_reportPath.isEmpty())
        m_watchdog.start((m_minimumRunSeconds + 40) * 1'000);
    emit metricsChanged();
    return true;
}

void HarnessHostServices::attachVideoItem(QObject *object)
{
    auto *item = qobject_cast<Player2VideoItem *>(object);
    if (!item) {
        finish(false, QStringLiteral("Harness QML supplied an invalid video item"), 2);
        return;
    }
    m_item = item;
    item->setSession(&m_session);
    item->setVideoPipeline(&m_pipeline);
    connect(item, &Player2VideoItem::initializationFailed, this,
            [this](const QString &message) { finish(false, message, 2); });
    item->update();
}

void HarnessHostServices::produceFrame()
{
    if (!m_started || m_finished || !m_item)
        return;
    refreshMetrics();
    m_maxAudioQueueMs = std::max(m_maxAudioQueueMs, m_session.audioQueueMs());
    m_sawAudioClock = m_sawAudioClock || m_session.audioClock().valid;
    if (hasMedia() && m_fileOpened) {
        // Anchored playback sample: the accumulator measures rates from the first valid audio clock,
        // never from run start — so device init + loudnorm priming can't manufacture a low fps.
        PlaybackMetricSample sample;
        sample.monotonicMs = m_runTimer.isValid() ? m_runTimer.elapsed() : 0;
        sample.decoded = m_metrics.decoded;
        sample.presented = m_metrics.presented;
        sample.audioQueueMs = m_session.audioQueueMs();
        sample.audioClockValid = m_session.audioClock().valid;
        sample.avErrorUs = m_metrics.lastAvErrorUs;
        sample.audioUnderruns = m_session.audioUnderruns();
        m_playbackMetrics.add(sample);
    }
    if (!m_metrics.adapterMatch) {
        m_item->update();
        return;
    }

    if (hasMedia()) {
        if (!m_fileOpened) {
            m_fileOpened = true;
            if (!m_url.isEmpty()) {
                PlaybackRequest request;
                request.source = QUrl(m_url);
                request.mediaId = QStringLiteral("stream");
                request.title = QStringLiteral("stream");
                request.headers = m_headers;
                request.stream = true;
                request.live = m_live;
                m_session.open(request);
            } else {
                m_session.open(PlaybackRequest{QUrl::fromLocalFile(m_filePath),
                                               QFileInfo(m_filePath).baseName(),
                                               QFileInfo(m_filePath).completeBaseName()});
            }
        }
        m_item->update();
    } else {
        QString error;
        const VideoFrameToken token{1, m_sequence + 1,
                                    static_cast<qint64>(m_sequence * 16'000)};
        if (m_pipeline.submitSyntheticFrame(token, m_sequence * 0.055, &error)) {
            ++m_sequence;
            m_item->update();
        } else if (!error.isEmpty() &&
                   error != QStringLiteral("Video pipeline is not initialized")) {
            finish(false, error, 2);
            return;
        }
    }
    refreshMetrics();
    if (m_metrics.deviceErrors != 0 || m_metrics.cpuTransfers != 0) {
        finish(false, QStringLiteral("Zero-copy diagnostics reported a pipeline violation"), 2);
        return;
    }
    if (!m_reportPath.isEmpty()) {
        const quint64 target = hasMedia() && m_session.duration() < 10.0 ? 30 : 320;
        bool hasAudioTrack = false;
        for (const QVariant &trackValue : m_session.tracks()) {
            if (trackValue.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("audio")) {
                hasAudioTrack = true;
                break;
            }
        }
        const bool audioReady = !hasAudioTrack || m_sawAudioClock;
        const bool minimumRunMet = m_minimumRunSeconds == 0 ||
            (m_runTimer.isValid() && m_runTimer.elapsed() >= m_minimumRunSeconds * 1'000);
        // A streamed source may report an unknown duration (live/no-length), so it is not required.
        const bool durationReady = !m_url.isEmpty() || m_session.duration() > 0.0;
        if ((hasMedia() && minimumRunMet && m_metrics.presented >= target && m_sawPlaying &&
             durationReady && !m_session.tracks().isEmpty() && audioReady) ||
            (!hasMedia() && m_metrics.presented >= target)) {
            finish(true, !hasMedia() ? QStringLiteral("Synthetic D3D11 gate passed")
                                     : (m_url.isEmpty() ? QStringLiteral("Local file gate passed")
                                                        : QStringLiteral("Stream gate passed")), 0);
        }
    }
}

void HarnessHostServices::refreshMetrics()
{
    m_metrics = m_pipeline.diagnostics();
    if (m_metrics.adapterMatch)
        m_status = QStringLiteral("Zero-copy ring active");
    emit metricsChanged();
}

void HarnessHostServices::finish(bool passed, const QString &message, int exitCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_frameTimer.stop();
    m_watchdog.stop();
    if (hasMedia()) {
        m_reportDuration = m_session.duration();
        m_reportTrackCount = m_session.tracks().size();
        m_reportAudioDevice = m_session.audioDevice();
        m_reportAudioFormat = m_session.audioFormat();
        m_finalAudioQueueMs = m_session.audioQueueMs();
        m_reportAudioUnderruns = m_session.audioUnderruns();
        m_reportAvP95Ms = m_pipeline.schedulingP95AbsoluteErrorUs() / 1000.0;
        m_reportNormalizationLatencyMs = m_session.normalizationLatencyMs();
        for (const QVariant &trackValue : m_session.tracks()) {
            const QVariantMap track = trackValue.toMap();
            if (track.value(QStringLiteral("type")).toString() == QStringLiteral("video")) {
                m_reportVideoCodec = track.value(QStringLiteral("codec")).toString();
                break;
            }
        }
        m_session.close();
    }
    refreshMetrics();
    m_status = message;
    emit metricsChanged();
    appendEvent(passed ? QStringLiteral("scenario-passed") : QStringLiteral("scenario-failed"),
                message);
    if (!m_reportPath.isEmpty()) {
        if (!writeReport(passed, message))
            exitCode = 3;
        QTimer::singleShot(0, qApp, [exitCode] { QCoreApplication::exit(exitCode); });
    }
}

bool HarnessHostServices::writeReport(bool passed, const QString &message) const
{
    // Playback-anchored truth: fps measured from the first valid audio clock, plus the signals that
    // map to Hemanth's symptoms — low-water audio queue (crackle) and signed A/V drift (sync).
    const PlaybackMetricsReport playback = m_playbackMetrics.report();
    QJsonArray windowFps;
    for (const PlaybackMetricWindow &window : playback.windows) {
        windowFps.append(QJsonObject{
            {QStringLiteral("startSeconds"), window.startSeconds},
            {QStringLiteral("fps"), window.fps},
            {QStringLiteral("minAudioQueueMs"), window.minAudioQueueMs}});
    }
    const QJsonObject report{
        {QStringLiteral("scenario"), !hasMedia() ? QStringLiteral("synthetic")
                                    : (m_url.isEmpty() ? QStringLiteral("file")
                                                       : QStringLiteral("stream"))},
        {QStringLiteral("passed"), passed},
        {QStringLiteral("message"), message},
        {QStringLiteral("source"), source()},
        {QStringLiteral("decodePath"), decodePath()},
        {QStringLiteral("qtAdapter"), m_metrics.qtAdapter},
        {QStringLiteral("producerAdapter"), m_metrics.producerAdapter},
        {QStringLiteral("adapterMatch"), m_metrics.adapterMatch},
        {QStringLiteral("sharedFences"), m_metrics.sharedFences},
        {QStringLiteral("generated"), static_cast<qint64>(m_metrics.submitted)},
        {QStringLiteral("decoded"), static_cast<qint64>(m_metrics.decoded)},
        {QStringLiteral("presented"), static_cast<qint64>(m_metrics.presented)},
        {QStringLiteral("dropped"), static_cast<qint64>(m_metrics.producerStarved)},
        {QStringLiteral("scheduledLateDrops"), static_cast<qint64>(m_metrics.scheduledLateDrops)},
        {QStringLiteral("avP95Ms"), m_reportAvP95Ms},
        {QStringLiteral("late"), 0},
        {QStringLiteral("cpuTransfers"), static_cast<qint64>(m_metrics.cpuTransfers)},
        {QStringLiteral("deviceErrors"), static_cast<qint64>(m_metrics.deviceErrors)}
        ,{QStringLiteral("reachedPlaying"), m_sawPlaying}
        ,{QStringLiteral("duration"), m_reportDuration}
        ,{QStringLiteral("trackCount"), m_reportTrackCount}
        ,{QStringLiteral("videoCodec"), m_reportVideoCodec}
        ,{QStringLiteral("inputFormat"), m_metrics.inputFormat}
        ,{QStringLiteral("audioDevice"), m_reportAudioDevice}
        ,{QStringLiteral("audioFormat"), m_reportAudioFormat}
        ,{QStringLiteral("audioClockValid"), m_sawAudioClock}
        ,{QStringLiteral("audioUnderruns"), static_cast<qint64>(m_reportAudioUnderruns)}
        ,{QStringLiteral("maxAudioQueueMs"), m_maxAudioQueueMs}
        ,{QStringLiteral("finalAudioQueueMs"), m_finalAudioQueueMs}
        ,{QStringLiteral("elapsedSeconds"), m_runTimer.isValid() ? m_runTimer.elapsed() / 1000.0 : 0.0}
        ,{QStringLiteral("playbackAnchored"), playback.anchored}
        ,{QStringLiteral("playbackSeconds"), playback.playbackSeconds}
        ,{QStringLiteral("sustainedFps"), playback.sustainedFps}
        ,{QStringLiteral("decodedFps"), playback.decodedFps}
        ,{QStringLiteral("minAudioQueueMs"), playback.minAudioQueueMs}
        ,{QStringLiteral("avDriftMinMs"), playback.avErrorMinUs / 1000.0}
        ,{QStringLiteral("avDriftMaxMs"), playback.avErrorMaxUs / 1000.0}
        ,{QStringLiteral("avDriftMaxAbsMs"), playback.avErrorMaxAbsUs / 1000.0}
        ,{QStringLiteral("avDriftMeanMs"), playback.avErrorMeanUs / 1000.0}
        ,{QStringLiteral("windowFps"), windowFps}
        ,{QStringLiteral("finalState"), sessionState()}
        ,{QStringLiteral("normalization"), m_reportNormalization}
        ,{QStringLiteral("normalizationLatencyMs"), m_reportNormalizationLatencyMs}
    };
    const QFileInfo reportInfo(m_reportPath);
    QDir().mkpath(reportInfo.absolutePath());
    QSaveFile file(reportInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0)
        return false;
    return file.commit();
}

void HarnessHostServices::appendEvent(const QString &event, const QString &message) const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    QFile ledger(QDir(directory).filePath(QStringLiteral("events.jsonl")));
    if (!ledger.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const QJsonObject record{
        {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("event"), event},
        {QStringLiteral("message"), message}
    };
    ledger.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    ledger.write("\n");
}

} // namespace Colosseum::Player2
