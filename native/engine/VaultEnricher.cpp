#include "VaultEnricher.h"

#include "CbzArchive.h"
#include "VaultKit.h"      // CancellationToken
#include "VaultStoreIo.h"
#include "player/MediaAdmissionProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QThread>

namespace {
// Map the probe's enum to the exact durable verdict string the index/QML contract expects.
QString admissionVerdictName(MediaAdmissionProbe::Verdict verdict)
{
    switch (verdict) {
    case MediaAdmissionProbe::Verdict::Admitted:
        return QStringLiteral("Admitted");
    case MediaAdmissionProbe::Verdict::RejectedNoVideo:
        return QStringLiteral("RejectedNoVideo");
    case MediaAdmissionProbe::Verdict::RejectedError:
        return QStringLiteral("RejectedError");
    case MediaAdmissionProbe::Verdict::RejectedTimeout:
        return QStringLiteral("RejectedTimeout");
    }
    return QStringLiteral("RejectedError");
}
} // namespace

VaultEnricher::VaultEnricher(VaultIndex* index, QString cacheDir, QObject* parent)
    : QObject(parent), m_index(index), m_cacheDir(std::move(cacheDir))
{
    loadDurationCache();
}

// ── Comic facts ───────────────────────────────────────────────────────
QString VaultEnricher::pickCoverEntry(const QStringList& imageEntryNames)
{
    if (imageEntryNames.isEmpty())
        return QString();

    // Prefer a cover.*/folder.* basename.
    for (const QString& name : imageEntryNames) {
        const QString base = QFileInfo(name).completeBaseName().toLower();
        if (base == QLatin1String("cover") || base == QLatin1String("folder"))
            return name;
    }
    // Else the first image in natural order.
    QStringList sorted = imageEntryNames;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return VaultIndex::naturalSortKey(a) < VaultIndex::naturalSortKey(b);
    });
    return sorted.first();
}

VaultEnricher::ComicFacts VaultEnricher::readComicFacts(const QString& cbzPath)
{
    ComicFacts f;
    QString err;
    const auto entries = MangaTankoban::CbzArchive::imageEntries(cbzPath, &err);
    if (entries.isEmpty()) {
        f.errorDetail = err.isEmpty() ? QStringLiteral("archive contains no readable pages") : err;
        return f; // ok stays false — corrupt / unreadable / not a comic archive
    }
    QStringList names;
    names.reserve(entries.size());
    for (const auto& e : entries)
        names.append(e.name);
    f.pages = names.size();
    f.coverEntry = pickCoverEntry(names);
    f.ok = true;
    return f;
}

// ── Video duration cache ──────────────────────────────────────────────
QString VaultEnricher::durationKey(const QString& path, qint64 size, qint64 mtimeMs)
{
    QString n = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    n = n.toLower();
#endif
    return n + QStringLiteral("::") + QString::number(size)
        + QStringLiteral("::") + QString::number(mtimeMs);
}

void VaultEnricher::loadDurationCache()
{
    const QJsonObject o = VaultStoreIo::load(m_cacheDir, QStringLiteral("durations.json"));
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        m_durationCache.insert(it.key(), it.value().toDouble());
}

void VaultEnricher::saveDurationCache()
{
    QJsonObject o;
    for (auto it = m_durationCache.constBegin(); it != m_durationCache.constEnd(); ++it)
        o.insert(it.key(), it.value());
    VaultStoreIo::save(m_cacheDir, QStringLiteral("durations.json"), o);
}

double VaultEnricher::cachedDuration(const QString& path, qint64 size, qint64 mtimeMs) const
{
    return m_durationCache.value(durationKey(path, size, mtimeMs), -1.0);
}

void VaultEnricher::putDuration(const QString& path, qint64 size, qint64 mtimeMs, double sec)
{
    m_durationCache.insert(durationKey(path, size, mtimeMs), sec);
}

double VaultEnricher::durationForVideo(const QString& path, qint64 size, qint64 mtimeMs)
{
    const double hit = cachedDuration(path, size, mtimeMs);
    if (hit >= 0.0)
        return hit;
    const double probed = probeDurationSec(path);
    if (probed >= 0.0)
        putDuration(path, size, mtimeMs, probed);
    return probed;
}

QString VaultEnricher::findFfprobe()
{
    const QString exe =
#ifdef Q_OS_WIN
        QStringLiteral("ffprobe.exe");
#else
        QStringLiteral("ffprobe");
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& cand : {appDir + QLatin1Char('/') + exe,
                                appDir + QStringLiteral("/tools/") + exe}) {
        if (QFileInfo::exists(cand))
            return cand;
    }
    return exe; // fall back to PATH
}

double VaultEnricher::probeDurationSec(const QString& path)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(findFfprobe(), {QStringLiteral("-v"), QStringLiteral("quiet"),
                               QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                               QStringLiteral("-of"),
                               QStringLiteral("default=noprint_wrappers=1:nokey=1"), path});
    if (!proc.waitForFinished(5000)) {
        proc.kill();            // TB2's pattern leaked the process on timeout — kill it
        proc.waitForFinished(1000);
        return -1.0;
    }
    bool ok = false;
    const double sec = QString::fromLatin1(proc.readAll()).trimmed().toDouble(&ok);
    return (ok && sec > 0.0) ? sec : -1.0;
}

// ── Orchestration ─────────────────────────────────────────────────────
void VaultEnricher::enrich(const QList<VaultIndex::FileRow>& rows,
                           const VaultKit::CancellationToken* cancel)
{
    // Buffer the enriched rows and hand them to the owner thread in ONE batch at the end, instead
    // of an owner-thread DB write per file from this (possibly worker) thread.
    QList<VaultIndex::FileRow> enrichedRows;
    enrichedRows.reserve(rows.size());

    int done = 0;
    const int total = rows.size();
    for (const VaultIndex::FileRow& r0 : rows) {
        if (cancel && cancel->isCancelled())
            break;
        VaultIndex::FileRow row = r0;
        // A drive-away row is a truthful unavailable state, not an extraction failure. The
        // filesystem check also covers the narrow boot race before the watcher emits away=true.
        if (row.away || !QFileInfo::exists(row.path)) {
            enrichedRows.push_back(row);
            ++done;
            emit progress(done, total);
            continue;
        }
        if (row.kind == QLatin1String("comic")) {
            const ComicFacts cf = readComicFacts(row.path);
            if (cf.ok) {
                row.pages = cf.pages;
                row.coverRef = cf.coverEntry;
                row.errorState.clear();
                row.errorDetail.clear();
            } else {
                row.errorState = QStringLiteral("corrupt");
                row.errorDetail = cf.errorDetail;
            }
        } else if (row.kind == QLatin1String("video")) {
            row.durationSec = durationForVideo(row.path, row.size, row.mtimeMs);
            if (row.admissionVerdict.isEmpty()) {
                // Blocking by contract; MediaAdmissionProbe exposes no cancel token, so cancellation
                // is honored only BETWEEN files (the loop guard), never mid-probe.
                const MediaAdmissionProbe::Result admission =
                    MediaAdmissionProbe::probe(row.path);
                row.admissionVerdict = admissionVerdictName(admission.verdict);
                row.admissionDetail = admission.detail;
                if (row.admissionVerdict != QLatin1String("Admitted")) {
                    row.errorState = QStringLiteral("rejected");
                    row.errorDetail = row.admissionDetail;
                } else if (row.errorState == QLatin1String("rejected")) {
                    row.errorState.clear();
                    row.errorDetail.clear();
                }
            }
        } else if (row.kind == QLatin1String("book")) {
            row.format = QFileInfo(row.path).suffix().toLower();
        }
        enrichedRows.push_back(row);
        ++done;
        emit progress(done, total);
        if (done % 20 == 0)
            saveDurationCache();
    }
    saveDurationCache();
    commitRowsOnIndexThread(std::move(enrichedRows));
}

void VaultEnricher::commitRowsOnIndexThread(QList<VaultIndex::FileRow> rows)
{
    QPointer<VaultIndex> index(m_index);
    QPointer<VaultEnricher> self(this);

    auto commit = [index, self, rows = std::move(rows)]() mutable {
        if (index && !rows.isEmpty())
            index->upsertMany(rows);
        if (self)
            emit self->enrichmentFinished();
    };

    if (!m_index || QThread::currentThread() == m_index->thread()) {
        commit();
        return;
    }

    // Never fall back to a worker-thread QSqlDatabase write: hop to the index's thread.
    if (!QMetaObject::invokeMethod(m_index, std::move(commit), Qt::QueuedConnection))
        emit enrichmentFinished();
}
