#include "downloadstore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

DownloadStore::DownloadStore(QObject *parent)
    : QObject(parent)
    , m_defaultDownloadDir(buildDefaultDownloadDir()) {
    loadIndex();
    loadQueue();
    if (!m_jobs.isEmpty())
        QMetaObject::invokeMethod(this, [this]() { pump(); }, Qt::QueuedConnection);
}

DownloadStore::~DownloadStore() {
    for (Job &j : m_jobs)
        cleanupJob(j);
}

// ── status: mirror of the active job (player panel contract) ──

QVariantMap DownloadStore::status() const {
    const int i = activeIndex();
    if (i < 0)
        return {{QStringLiteral("kind"), QStringLiteral("idle")}};
    const Job &j = m_jobs.at(i);
    return {
        {QStringLiteral("kind"), j.state == QStringLiteral("resolving")
                                     ? QStringLiteral("preparing") : QStringLiteral("downloading")},
        {QStringLiteral("receivedBytes"), j.received},
        {QStringLiteral("totalBytes"), j.total > 0 ? QVariant(j.total) : QVariant()},
        {QStringLiteral("ratio"), j.ratio},
        {QStringLiteral("path"), j.outputPath},
        {QStringLiteral("title"), j.request.value(QStringLiteral("title"))},
        {QStringLiteral("id"), j.id}
    };
}

// ── queue core ──

int DownloadStore::activeIndex() const {
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs.at(i).state == QStringLiteral("resolving")
            || m_jobs.at(i).state == QStringLiteral("downloading"))
            return i;
    return -1;
}

int DownloadStore::jobIndex(const QString &id) const {
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs.at(i).id == id)
            return i;
    return -1;
}

void DownloadStore::touch() {
    ++m_queueRevision;
    emit changed();
}

void DownloadStore::enqueue(const QVariantMap &request) {
    QString id = request.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = QStringLiteral("video-%1").arg(QDateTime::currentMSecsSinceEpoch());
    if (jobIndex(id) >= 0 || m_index.contains(id))
        return;   // already queued or already on disk — idempotent
    Job j;
    j.request = request;
    j.request.insert(QStringLiteral("id"), id);
    j.request.insert(QStringLiteral("groupKey"), groupKeyFor(j.request));
    j.id = id;
    j.url = request.value(QStringLiteral("url")).toString();
    j.state = QStringLiteral("queued");
    m_jobs.append(j);
    saveQueue();
    touch();
    pump();
}

void DownloadStore::enqueueBatch(const QVariantList &requests) {
    for (const QVariant &r : requests)
        enqueue(r.toMap());
}

QString DownloadStore::groupKeyFor(const QVariantMap &request) {
    const QString id = request.value(QStringLiteral("id")).toString();
    const int season = request.value(QStringLiteral("season")).toInt();
    if (request.value(QStringLiteral("kind")).toString() == QStringLiteral("episode")
        && season > 0) {
        // Series base = id minus the trailing ":season:episode" pair — handles both
        // Cinemeta (tt123:2:5 -> tt123) and anime (mal:12345:3:4 -> mal:12345) ids.
        // Fewer than 3 segments -> empty -> falls through to the per-id key.
        const QString base = id.section(QLatin1Char(':'), 0, -3);
        if (!base.isEmpty())
            return base + QStringLiteral(":s") + QString::number(season);
    }
    return id;
}

void DownloadStore::startDownload(const QVariantMap &request) {
    enqueue(request);
}

void DownloadStore::cancelDownload() {
    const int i = activeIndex();
    if (i >= 0)
        cancelJob(m_jobs.at(i).id);
}

void DownloadStore::cancelJob(const QString &id) {
    const int i = jobIndex(id);
    if (i < 0)
        return;
    cleanupJob(m_jobs[i]);
    QFile::remove(m_jobs.at(i).partPath);
    m_jobs.removeAt(i);
    saveQueue();
    touch();
    pump();
}

void DownloadStore::retryJob(const QString &id) {
    const int i = jobIndex(id);
    if (i < 0 || m_jobs.at(i).state != QStringLiteral("failed"))
        return;
    Job &j = m_jobs[i];
    j.state = QStringLiteral("queued");
    j.url.clear();            // identity payload survives; the URL never does
    j.error.clear();
    j.ratio = 0; j.received = 0; j.total = 0;
    saveQueue();
    touch();
    pump();
}

void DownloadStore::pump() {
    if (activeIndex() >= 0)
        return;   // MAX_ACTIVE_VIDEO = 1: one live job at a time
    for (int i = 0; i < m_jobs.size(); ++i) {
        Job &j = m_jobs[i];
        if (j.state != QStringLiteral("queued"))
            continue;
        if (j.url.isEmpty()) {
            j.state = QStringLiteral("resolving");
            touch();
            const QString mediaType =
                j.request.value(QStringLiteral("kind")).toString() == QStringLiteral("movie")
                    ? QStringLiteral("movie") : QStringLiteral("series");
            emit needResolve(j.id, j.id, mediaType);
        } else {
            startHttp(j);
        }
        return;
    }
}

void DownloadStore::feedUrl(const QString &id, const QString &url) {
    const int i = jobIndex(id);
    if (i < 0 || url.isEmpty())
        return;
    Job &j = m_jobs[i];
    j.url = url;
    if (j.state == QStringLiteral("resolving"))
        startHttp(j);
}

void DownloadStore::failJob(const QString &id, const QString &reason) {
    const int i = jobIndex(id);
    if (i < 0)
        return;
    Job &j = m_jobs[i];
    cleanupJob(j);
    QFile::remove(j.partPath);
    j.state = QStringLiteral("failed");
    j.error = reason.isEmpty() ? QStringLiteral("Download failed") : reason;
    saveQueue();
    touch();
    pump();
}

// ── HTTP transfer (unchanged mechanics, per-job state) ──

void DownloadStore::startHttp(Job &job) {
    job.state = QStringLiteral("downloading");
    job.outputPath = buildOutputPath(job.request);
    job.partPath = job.outputPath + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(job.outputPath).absolutePath());

    job.file = new QFile(job.partPath, this);
    if (!job.file->open(QIODevice::WriteOnly)) {
        const QString message = QStringLiteral("open: %1").arg(job.file->errorString());
        failJob(job.id, message);
        return;
    }

    QNetworkRequest req{QUrl(job.url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Colosseum"));
    job.reply = m_network.get(req);
    const QString id = job.id;
    connect(job.reply, &QNetworkReply::readyRead, this, [this, id]() {
        const int i = jobIndex(id);
        if (i >= 0 && m_jobs.at(i).file && m_jobs.at(i).reply)
            m_jobs[i].file->write(m_jobs[i].reply->readAll());
    });
    connect(job.reply, &QNetworkReply::downloadProgress, this,
            [this, id](qint64 received, qint64 total) {
        const int i = jobIndex(id);
        if (i < 0)
            return;
        Job &j = m_jobs[i];
        j.received = received;
        j.total = total;
        j.ratio = total > 0 ? qBound(0.0, double(received) / double(total), 1.0) : 0.0;
        emit changed();
    });
    connect(job.reply, &QNetworkReply::finished, this, [this, id]() {
        const int i = jobIndex(id);
        if (i >= 0)
            finishHttp(m_jobs[i]);
    });
    touch();
}

void DownloadStore::finishHttp(Job &job) {
    if (!job.reply)
        return;
    const QNetworkReply::NetworkError err = job.reply->error();
    const QString errText = job.reply->errorString();
    if (job.file) {
        job.file->write(job.reply->readAll());
        job.file->flush();
        job.file->close();
    }
    job.reply->deleteLater();
    job.reply = nullptr;
    delete job.file;
    job.file = nullptr;

    if (err != QNetworkReply::NoError) {
        failJob(job.id, errText);
        return;
    }
    QFile::remove(job.outputPath);
    if (!QFile::rename(job.partPath, job.outputPath)) {
        failJob(job.id, QStringLiteral("rename failed"));
        return;
    }
    const qint64 bytes = QFileInfo(job.outputPath).size();
    m_lastDonePath = job.outputPath;
    recordFinished(job, bytes);
    const int i = jobIndex(job.id);
    if (i >= 0)
        m_jobs.removeAt(i);
    saveQueue();
    touch();
    pump();
}

void DownloadStore::cleanupJob(Job &job) {
    if (job.reply) {
        job.reply->disconnect(this);
        job.reply->abort();
        job.reply->deleteLater();
        job.reply = nullptr;
    }
    if (job.file) {
        job.file->close();
        delete job.file;
        job.file = nullptr;
    }
}

void DownloadStore::revealDownload() {
    QString path = m_lastDonePath;
    if (path.isEmpty()) {
        const int i = activeIndex();
        if (i >= 0)
            path = m_jobs.at(i).outputPath;
    }
    if (path.isEmpty())
        return;
    const QString folder = QFileInfo(path).absoluteDir().absolutePath();
    if (!folder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void DownloadStore::resetDownload() {
    // Player-panel "dismiss": nothing to do in queue-world; kept for compat.
    emit changed();
}

QVariantList DownloadStore::jobs() const {
    QVariantList out;
    for (const Job &j : m_jobs) {
        out.append(QVariantMap{
            {QStringLiteral("id"), j.id},
            {QStringLiteral("kind"), j.request.value(QStringLiteral("kind"))},
            {QStringLiteral("title"), j.request.value(QStringLiteral("title"))},
            {QStringLiteral("seriesTitle"), j.request.value(QStringLiteral("seriesTitle"))},
            {QStringLiteral("season"), j.request.value(QStringLiteral("season"))},
            {QStringLiteral("episode"), j.request.value(QStringLiteral("episode"))},
            {QStringLiteral("art"), j.request.value(QStringLiteral("art"))},
            {QStringLiteral("groupKey"), j.request.value(QStringLiteral("groupKey"), j.id)},
            {QStringLiteral("subtitle"), j.request.value(QStringLiteral("subtitle"))},
            {QStringLiteral("state"), j.state},
            {QStringLiteral("error"), j.error},
            {QStringLiteral("ratio"), j.ratio},
            {QStringLiteral("received"), j.received},
            {QStringLiteral("total"), j.total}
        });
    }
    return out;
}

bool DownloadStore::hasVideo(const QString &id) const {
    return m_index.contains(id);
}

// ── paths / naming (unchanged) ──

QString DownloadStore::buildDefaultDownloadDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    const QString dir = QDir(base).filePath(QStringLiteral("Colosseum"));
    QDir().mkpath(dir);
    return QDir::toNativeSeparators(dir);
}

QString DownloadStore::buildOutputPath(const QVariantMap &request) const {
    const QString requested = request.value(QStringLiteral("outputPath")).toString();
    if (!requested.isEmpty())
        return QDir::toNativeSeparators(requested);

    QString stem = sanitizeFilePart(request.value(QStringLiteral("title")).toString());
    if (stem.isEmpty())
        stem = QStringLiteral("video");
    const QString subtitle = sanitizeFilePart(request.value(QStringLiteral("subtitle")).toString());
    if (!subtitle.isEmpty())
        stem += QStringLiteral(" - ") + subtitle;
    stem += QStringLiteral(" - ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
    return QDir::toNativeSeparators(QDir(m_defaultDownloadDir).filePath(stem + QStringLiteral(".") + extensionFromUrl(request.value(QStringLiteral("url")).toString())));
}

QString DownloadStore::sanitizeFilePart(const QString &value) const {
    QString out = value.simplified();
    out.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("_"));
    out = out.trimmed();
    if (out.size() > 90)
        out = out.left(90).trimmed();
    return out;
}

QString DownloadStore::extensionFromUrl(const QString &url) const {
    const QString path = QUrl(url).path().toLower();
    const QString suffix = QFileInfo(path).suffix();
    if (suffix == QStringLiteral("mkv") || suffix == QStringLiteral("mp4") || suffix == QStringLiteral("webm") || suffix == QStringLiteral("mov"))
        return suffix;
    return QStringLiteral("mp4");
}

// ── persistence: library index + in-flight queue ──

QString DownloadStore::baseDir() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/videos");
    QDir().mkpath(base);
    return base;
}

void DownloadStore::loadIndex() {
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject rootObj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = rootObj.constBegin(); it != rootObj.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.id = it.key();
        e.kind = o.value(QStringLiteral("kind")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        e.subtitle = o.value(QStringLiteral("subtitle")).toString();
        e.seriesTitle = o.value(QStringLiteral("seriesTitle")).toString();
        e.season = o.value(QStringLiteral("season")).toInt();
        e.episode = o.value(QStringLiteral("episode")).toInt();
        e.path = o.value(QStringLiteral("path")).toString();
        e.art = o.value(QStringLiteral("art")).toString();
        e.bytes = qint64(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt = qint64(o.value(QStringLiteral("addedAt")).toDouble());
        if (!e.path.isEmpty())
            m_index.insert(e.id, e);
    }
    qInfo("[download] loaded video index: %d files", int(m_index.size()));
}

void DownloadStore::saveIndex() const {
    QJsonObject rootObj;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry &e = it.value();
        rootObj.insert(it.key(), QJsonObject{
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("title"), e.title},
            {QStringLiteral("subtitle"), e.subtitle},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("season"), e.season},
            {QStringLiteral("episode"), e.episode},
            {QStringLiteral("path"), e.path},
            {QStringLiteral("art"), e.art},
            {QStringLiteral("bytes"), double(e.bytes)},
            {QStringLiteral("addedAt"), double(e.addedAt)}
        });
    }
    QSaveFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
    f.commit();
}

void DownloadStore::loadQueue() {
    QFile f(baseDir() + QStringLiteral("/queue.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Job j;
        j.request = o.value(QStringLiteral("request")).toObject().toVariantMap();
        j.id = j.request.value(QStringLiteral("id")).toString();
        if (j.id.isEmpty())
            continue;
        if (j.request.value(QStringLiteral("groupKey")).toString().isEmpty())
            j.request.insert(QStringLiteral("groupKey"), groupKeyFor(j.request));
        const QString state = o.value(QStringLiteral("state")).toString();
        // in-flight states rehydrate as queued: the URL was ephemeral anyway
        j.state = state == QStringLiteral("failed") ? state : QStringLiteral("queued");
        j.error = o.value(QStringLiteral("error")).toString();
        m_jobs.append(j);
    }
    if (!m_jobs.isEmpty())
        qInfo("[download] restored %d queued video jobs", int(m_jobs.size()));
}

void DownloadStore::saveQueue() const {
    QJsonArray arr;
    for (const Job &j : m_jobs) {
        arr.append(QJsonObject{
            {QStringLiteral("request"), QJsonObject::fromVariantMap(j.request)},
            {QStringLiteral("state"), j.state},
            {QStringLiteral("error"), j.error}
        });
    }
    QSaveFile f(baseDir() + QStringLiteral("/queue.json"));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}

void DownloadStore::recordFinished(const Job &job, qint64 bytes) {
    Entry e;
    e.id = job.id;
    e.kind = job.request.value(QStringLiteral("kind")).toString();
    e.season = job.request.value(QStringLiteral("season")).toInt();
    e.episode = job.request.value(QStringLiteral("episode")).toInt();
    if (e.kind.isEmpty())
        e.kind = (e.season > 0 || e.episode > 0) ? QStringLiteral("episode") : QStringLiteral("movie");
    e.title = job.request.value(QStringLiteral("title")).toString();
    e.subtitle = job.request.value(QStringLiteral("subtitle")).toString();
    e.seriesTitle = job.request.value(QStringLiteral("seriesTitle")).toString();
    if (e.seriesTitle.isEmpty() && e.kind == QStringLiteral("episode"))
        e.seriesTitle = e.title;
    e.art = job.request.value(QStringLiteral("art")).toString();
    e.path = job.outputPath;
    e.bytes = bytes;
    e.addedAt = QDateTime::currentSecsSinceEpoch();
    m_index.insert(e.id, e);
    saveIndex();
    emit libraryChanged();
}

QVariantList DownloadStore::downloadedVideos() const {
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry &e = it.value();
        out.append(QVariantMap{
            {QStringLiteral("id"), e.id},
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("title"), e.title},
            {QStringLiteral("subtitle"), e.subtitle},
            {QStringLiteral("seriesTitle"), e.seriesTitle},
            {QStringLiteral("season"), e.season},
            {QStringLiteral("episode"), e.episode},
            {QStringLiteral("path"), e.path},
            {QStringLiteral("art"), e.art},
            {QStringLiteral("bytes"), e.bytes},
            {QStringLiteral("addedAt"), e.addedAt},
            {QStringLiteral("missing"), !QFile::exists(e.path)}
        });
    }
    return out;
}

void DownloadStore::removeVideo(const QString &id) {
    const auto it = m_index.constFind(id);
    if (it == m_index.constEnd())
        return;
    QFile::remove(it.value().path);
    m_index.remove(id);
    saveIndex();
    emit libraryChanged();
}

// ── dev selftest harness (headless proof, house pattern) ──

void DownloadStore::selfTest(const QString &mode) {
    auto check = [](bool ok, const char *what) {
        qInfo("[videoq-selftest] %s: %s", ok ? "PASS" : "FAIL", what);
    };
    if (!m_jobs.isEmpty()) {
        qWarning("[videoq-selftest] SKIPPED: %d real jobs in the queue - the harness "
                 "needs an empty queue (its pump would promote a real job before QML "
                 "can answer needResolve). Finish or cancel them first.",
                 int(m_jobs.size()));
        return;
    }
    if (mode == QStringLiteral("exactrow")) {
        // Job A promotes to resolving (the "active" job; no resolver answers in
        // the harness, so it just holds the slot). Job B stays queued.
        enqueue({{QStringLiteral("id"), QStringLiteral("selftest:1:1")},
                 {QStringLiteral("kind"), QStringLiteral("episode")},
                 {QStringLiteral("title"), QStringLiteral("Selftest A")},
                 {QStringLiteral("seriesTitle"), QStringLiteral("Selftest")},
                 {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}});
        enqueue({{QStringLiteral("id"), QStringLiteral("selftest:1:2")},
                 {QStringLiteral("kind"), QStringLiteral("episode")},
                 {QStringLiteral("title"), QStringLiteral("Selftest B")},
                 {QStringLiteral("seriesTitle"), QStringLiteral("Selftest")},
                 {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 2}});
        failJob(QStringLiteral("selftest:1:2"), QStringLiteral("selftest failure"));
        const int f = jobIndex(QStringLiteral("selftest:1:2"));
        check(f >= 0 && m_jobs.at(f).state == QStringLiteral("failed"),
              "job B failed and stays listed");
        cancelJob(QStringLiteral("selftest:1:2"));   // the bug's exact click
        check(jobIndex(QStringLiteral("selftest:1:2")) < 0, "failed row removed");
        const int a = jobIndex(QStringLiteral("selftest:1:1"));
        check(a >= 0 && m_jobs.at(a).state == QStringLiteral("resolving"),
              "active job untouched by exact-row cancel");
        cancelJob(QStringLiteral("selftest:1:1"));   // clean up
    }
    else if (mode == QStringLiteral("group")) {
        enqueueBatch({
            QVariantMap{{QStringLiteral("id"), QStringLiteral("selftest:2:1")},
                        {QStringLiteral("kind"), QStringLiteral("episode")},
                        {QStringLiteral("title"), QStringLiteral("Selftest S2E1")},
                        {QStringLiteral("seriesTitle"), QStringLiteral("Selftest")},
                        {QStringLiteral("season"), 2}, {QStringLiteral("episode"), 1}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("selftest:2:2")},
                        {QStringLiteral("kind"), QStringLiteral("episode")},
                        {QStringLiteral("title"), QStringLiteral("Selftest S2E2")},
                        {QStringLiteral("seriesTitle"), QStringLiteral("Selftest")},
                        {QStringLiteral("season"), 2}, {QStringLiteral("episode"), 2}}});
        const QString gk1 = m_jobs.at(jobIndex(QStringLiteral("selftest:2:1")))
                                .request.value(QStringLiteral("groupKey")).toString();
        const QString gk2 = m_jobs.at(jobIndex(QStringLiteral("selftest:2:2")))
                                .request.value(QStringLiteral("groupKey")).toString();
        check(gk1 == QStringLiteral("selftest:s2") && gk1 == gk2,
              "batch shares one derived groupKey");
        enqueue({{QStringLiteral("id"), QStringLiteral("mal:111:2:1")},
                 {QStringLiteral("kind"), QStringLiteral("episode")},
                 {QStringLiteral("title"), QStringLiteral("Selftest Anime A")},
                 {QStringLiteral("seriesTitle"), QStringLiteral("Anime A")},
                 {QStringLiteral("season"), 2}, {QStringLiteral("episode"), 1}});
        enqueue({{QStringLiteral("id"), QStringLiteral("mal:222:2:1")},
                 {QStringLiteral("kind"), QStringLiteral("episode")},
                 {QStringLiteral("title"), QStringLiteral("Selftest Anime B")},
                 {QStringLiteral("seriesTitle"), QStringLiteral("Anime B")},
                 {QStringLiteral("season"), 2}, {QStringLiteral("episode"), 1}});
        const QString ga = m_jobs.at(jobIndex(QStringLiteral("mal:111:2:1")))
                               .request.value(QStringLiteral("groupKey")).toString();
        const QString gb = m_jobs.at(jobIndex(QStringLiteral("mal:222:2:1")))
                               .request.value(QStringLiteral("groupKey")).toString();
        check(ga == QStringLiteral("mal:111:s2") && gb == QStringLiteral("mal:222:s2"),
              "different anime series never share a groupKey");
        enqueue({{QStringLiteral("id"), QStringLiteral("selftest-movie")},
                 {QStringLiteral("kind"), QStringLiteral("movie")},
                 {QStringLiteral("title"), QStringLiteral("Selftest Movie")}});
        check(m_jobs.at(jobIndex(QStringLiteral("selftest-movie")))
                  .request.value(QStringLiteral("groupKey")).toString()
                  == QStringLiteral("selftest-movie"),
              "a movie keys by its own id");
        cancelJob(QStringLiteral("mal:111:2:1"));
        cancelJob(QStringLiteral("mal:222:2:1"));
        cancelJob(QStringLiteral("selftest-movie"));
        cancelJob(QStringLiteral("selftest:2:1"));
        cancelJob(QStringLiteral("selftest:2:2"));
    }
}
