#include "ComicTorrentDownloader.h"

#include "ComicRequestLedger.h"
#include "ComicTorrentMagnet.h"
#include "TorrentResult.h"   // humanSize
#include "engine/ComicDownloader.h"
#include "engine/ComicEditionAssembler.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

namespace {
constexpr int kProgressThrottleMs = 500;
}

using ComicEditionFileSelector::ComicPayloadKind;
using ComicEditionFileSelector::ComicSelectedFile;
using ComicEditionFileSelector::ComicSelectionFailure;

// ── Construction / teardown ─────────────────────────────────────────────────

ComicTorrentDownloader::ComicTorrentDownloader(IComicTorrentEngine* engine, QObject* parent)
    : ComicTorrentDownloader(engine, nullptr, QString(), QString(), QString(), parent)
{
}

ComicTorrentDownloader::ComicTorrentDownloader(IComicTorrentEngine* engine, ComicDownloader* ingestTarget,
                                               const QString& ledgerPath, const QString& saveRoot,
                                               const QString& stagingRoot, QObject* parent)
    : QObject(parent), m_engine(engine), m_ingestTarget(ingestTarget),
      m_packSaveRoot(saveRoot), m_stagingRoot(stagingRoot)
{
    if (m_engine) {
        connect(m_engine, &IComicTorrentEngine::metadataReady,
                this, &ComicTorrentDownloader::onMetadataReady);
        connect(m_engine, &IComicTorrentEngine::torrentProgress,
                this, &ComicTorrentDownloader::onEngineProgress);
        connect(m_engine, &IComicTorrentEngine::torrentFinished,
                this, &ComicTorrentDownloader::onEngineFinished);
        connect(m_engine, &IComicTorrentEngine::torrentError,
                this, &ComicTorrentDownloader::onEngineFailed);
        connect(m_engine, &IComicTorrentEngine::torrentAddFailed,
                this, &ComicTorrentDownloader::onEngineFailed);
    }
    if (!ledgerPath.isEmpty()) {
        m_ledger = new ComicRequestLedger(ledgerPath);
        m_ledger->load();
        m_assembler = new ComicEditionAssembler(this);
        if (m_ingestTarget) {
            connect(m_ingestTarget, &ComicDownloader::finished, this,
                    [this](const QString& id) {
                        if (m_ledger) m_ledger->setState(id, QStringLiteral("completed"));
                    });
            connect(m_ingestTarget, &ComicDownloader::failed, this,
                    [this](const QString& id, const QString&) {
                        if (m_ledger) m_ledger->setState(id, QStringLiteral("failed"));
                    });
        }
        replayPackActive();
    }
}

ComicTorrentDownloader::~ComicTorrentDownloader()
{
    qDeleteAll(m_byHash);
    qDeleteAll(m_packJobs);
    delete m_ledger;
}

// ── Legacy single-archive path (COMPATIBILITY WRAPPER — unchanged behavior) ──

ComicTorrentDownloader::Job* ComicTorrentDownloader::jobForHash(const QString& infoHash) const
{
    return m_byHash.value(infoHash.toLower(), nullptr);
}

ComicTorrentDownloader::Job* ComicTorrentDownloader::jobForIssue(const QString& issueId) const
{
    return jobForHash(m_hashByIssue.value(issueId.trimmed()));
}

bool ComicTorrentDownloader::alive(Job* job) const
{
    return job && m_byHash.value(job->infoHash) == job;
}

void ComicTorrentDownloader::download(const QString& issueIdIn, const QString& infoHashIn,
                                      const QString& title, const QString& magnetUri)
{
    const QString issueId = issueIdIn.trimmed();
    const QString infoHash = infoHashIn.trimmed().toLower();
    if (issueId.isEmpty() || infoHash.size() != 40) {
        emit failed(issueId, QStringLiteral("bad issue id / infoHash"));
        return;
    }
    if (m_hashByIssue.contains(issueId)) return;
    if (m_byHash.contains(infoHash)) {
        emit failed(issueId, QStringLiteral("torrent already serves another comic"));
        return;
    }
    if (!m_engine) {
        emit failed(issueId, QStringLiteral("torrent engine unavailable"));
        return;
    }
    if (!m_engine->isRunning()) m_engine->start();

    auto* job = new Job;
    job->issueId = issueId;
    job->infoHash = infoHash;
    job->title = title;
    job->saveDir = dirFor(infoHash);
    m_byHash.insert(infoHash, job);
    m_hashByIssue.insert(issueId, infoHash);
    emit resolving(issueId);

    QDir().mkpath(job->saveDir);
    const QString added = m_engine->addMagnet(ComicTorrentMagnet::build(infoHash, magnetUri),
                                               job->saveDir, false);
    if (added.isEmpty()) {
        failJob(job, QStringLiteral("engine rejected magnet"));
        return;
    }
    // Hybrid torrents may be requested by v1 BTIH while the engine reports its
    // canonical handle key from the v2 hash. Every engine signal uses the
    // returned key, so rebind immediately but retain the original save path.
    if (added != job->infoHash) {
        m_byHash.remove(job->infoHash);
        job->infoHash = added;
        m_byHash.insert(job->infoHash, job);
        m_hashByIssue.insert(issueId, job->infoHash);
    }
    const QJsonArray existing = m_engine->torrentFiles(added);
    if (!existing.isEmpty()) applyMetadata(job, existing);
}

void ComicTorrentDownloader::applyMetadata(Job* job, const QJsonArray& files)
{
    const QList<ManifestFile> manifest = BookTorrentMagnet::filesToManifest(files);
    const ComicArchiveDecision decision = ComicTorrentFilePicker::decide(job->title, manifest);
    if (decision.candidates.isEmpty()) {
        m_engine->removeTorrent(job->infoHash, true);
        failJob(job, QStringLiteral("this torrent has no CBR/CBZ/CB7/CBT file (%1 file(s))")
                         .arg(manifest.size()));
        return;
    }
    if (decision.requiresChoice) {
        // Ambiguous / multi-volume: pause, zero every priority, and wait for the
        // user to pick one archive. Nothing downloads until chooseFile() lands.
        m_engine->pauseTorrent(job->infoHash);
        m_engine->setFilePriorities(job->infoHash, QVector<int>(manifest.size(), 0));
        job->choosing = true;
        job->candidates = decision.candidates;
        job->manifestSize = manifest.size();
        qInfo() << "[ComicTorrentDownloader]" << job->issueId << "awaiting archive choice among"
                << decision.candidates.size() << "candidates";
        emit fileSelectionRequired(job->issueId, toVariantFiles(decision.candidates));
        return;
    }
    qint64 bytes = 0;
    for (const ManifestFile& file : manifest)
        if (file.idx == decision.selected.idx) bytes = file.length;
    applyPickedFile(job, decision.selected.idx, decision.selected.name, bytes,
                    manifest.size(), true);
}

void ComicTorrentDownloader::applyPickedFile(Job* job, int pickedIdx, const QString& name,
                                             qint64 bytes, int fileCount, bool automatic)
{
    const bool wasChoosing = job->choosing;
    job->pickedIdx = pickedIdx;
    job->fileName = name;
    job->fileName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    job->totalBytes = bytes;
    job->picked = true;
    job->choosing = false;
    job->candidates.clear();
    m_engine->setFilePriorities(job->infoHash,
        BookTorrentMagnet::pickToPriorities(pickedIdx, fileCount));
    // A manual choice resumes the torrent we paused; an auto-pick was never paused.
    if (wasChoosing) m_engine->resumeTorrent(job->infoHash);
    qInfo() << "[ComicTorrentDownloader]" << job->issueId << job->infoHash
            << (automatic ? "auto-picked" : "user-picked") << job->fileName
            << job->totalBytes << "bytes";
    emit fileSelected(job->issueId, job->fileName, automatic);
}

bool ComicTorrentDownloader::chooseFile(const QString& issueId, int fileIndex)
{
    Job* job = jobForIssue(issueId);
    if (!alive(job) || !job->choosing) return false;
    // Only an eligible comic archive index is accepted — never a non-comic file.
    const ComicArchiveCandidate* chosen = nullptr;
    for (const ComicArchiveCandidate& c : job->candidates)
        if (c.index == fileIndex) { chosen = &c; break; }
    if (!chosen) return false;
    const int idx = chosen->index;
    const QString name = chosen->name;
    const qint64 bytes = chosen->bytes;
    applyPickedFile(job, idx, name, bytes, job->manifestSize, false);
    return true;
}

QVariantList ComicTorrentDownloader::toVariantFiles(
    const QList<ComicArchiveCandidate>& candidates) const
{
    QVariantList out;
    out.reserve(candidates.size());
    for (const ComicArchiveCandidate& c : candidates) {
        out.append(QVariantMap{
            {QStringLiteral("index"), c.index},
            {QStringLiteral("name"), c.name},
            {QStringLiteral("extension"), c.extension},
            {QStringLiteral("sizeBytes"), QVariant::fromValue(c.bytes)},
            {QStringLiteral("sizeText"), humanSize(c.bytes)},
            {QStringLiteral("exactTitle"), c.exactTitle},
            {QStringLiteral("tokenCoverage"), c.tokenCoverage}
        });
    }
    return out;
}

void ComicTorrentDownloader::finalizeJob(Job* job)
{
    const QString path = job->saveDir + QLatin1Char('/') + job->fileName;
    if (!QFileInfo::exists(path)) {
        m_engine->removeTorrent(job->infoHash, true);
        failJob(job, QStringLiteral("finished but comic archive is missing: %1")
                         .arg(job->fileName));
        return;
    }
    m_engine->removeTorrent(job->infoHash, false);
    const QString issueId = job->issueId;
    m_hashByIssue.remove(issueId);
    m_byHash.remove(job->infoHash);
    delete job;
    emit finished(issueId, path);
}

void ComicTorrentDownloader::failJob(Job* job, const QString& reason)
{
    const QString issueId = job->issueId;
    qWarning() << "[ComicTorrentDownloader] FAILED" << issueId << reason;
    m_hashByIssue.remove(issueId);
    m_byHash.remove(job->infoHash);
    delete job;
    emit failed(issueId, reason);
}

bool ComicTorrentDownloader::cancel(const QString& issueId)
{
    Job* job = jobForIssue(issueId);
    if (!alive(job)) return false;
    if (m_engine) m_engine->removeTorrent(job->infoHash, true);
    failJob(job, QStringLiteral("cancelled by user"));
    return true;
}

QVariantMap ComicTorrentDownloader::statusOf(const QString& issueId) const
{
    QVariantMap status{{QStringLiteral("state"), QStringLiteral("none")},
                       {QStringLiteral("done"), 0.0},
                       {QStringLiteral("total"), 0.0}};
    if (Job* job = jobForIssue(issueId)) {
        status[QStringLiteral("state")] = job->choosing ? QStringLiteral("choosing")
                                        : job->picked   ? QStringLiteral("downloading")
                                                        : QStringLiteral("resolving");
        status[QStringLiteral("done")] = static_cast<double>(job->received);
        status[QStringLiteral("total")] = static_cast<double>(job->totalBytes);
    }
    return status;
}

QVariantList ComicTorrentDownloader::activeJobs() const
{
    QVariantList rows;
    for (Job* job : m_byHash) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), job->issueId},
            {QStringLiteral("label"), job->title},
            {QStringLiteral("state"), job->choosing ? QStringLiteral("choosing")
                                    : job->picked   ? QStringLiteral("downloading")
                                                    : QStringLiteral("resolving")},
            {QStringLiteral("done"), static_cast<double>(job->received)},
            {QStringLiteral("total"), static_cast<double>(job->totalBytes)}
        });
    }
    return rows;
}

QString ComicTorrentDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/comics-torrent");
}

QString ComicTorrentDownloader::dirFor(const QString& infoHash) const
{
    return baseDir() + QLatin1Char('/') + infoHash.toLower();
}

// ── Edition pack transport (Task 9) ─────────────────────────────────────────

QString ComicTorrentDownloader::packSaveDirFor(const QString& infoHash) const
{
    return m_packSaveRoot + QLatin1Char('/') + infoHash.toLower();
}

QString ComicTorrentDownloader::reasonForFailure(ComicSelectionFailure failure)
{
    switch (failure) {
    case ComicSelectionFailure::TargetMissing:
        return QStringLiteral("this torrent has no file matching the edition");
    case ComicSelectionFailure::Ambiguous:
        return QStringLiteral("two files match this edition equally");
    case ComicSelectionFailure::CombinedOnly:
        return QStringLiteral("only a combined multi-edition archive covers this edition");
    case ComicSelectionFailure::IncompleteIssueSet:
        return QStringLiteral("the pack is missing required issues for this edition");
    case ComicSelectionFailure::UnsupportedPayload:
        return QStringLiteral("this torrent's payload type is not supported");
    case ComicSelectionFailure::None:
        break;
    }
    return QStringLiteral("could not isolate this edition");
}

QList<ComicEditionFileSelector::ManifestFile> ComicTorrentDownloader::toManifest(const QJsonArray& files)
{
    QList<ComicEditionFileSelector::ManifestFile> out;
    out.reserve(files.size());
    for (const QJsonValue& v : files) {
        const QJsonObject o = v.toObject();
        ComicEditionFileSelector::ManifestFile mf;
        mf.index = o.value(QStringLiteral("index")).toInt(-1);
        mf.path = o.value(QStringLiteral("name")).toString();
        mf.path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        mf.bytes = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
        out.append(mf);
    }
    return out;
}

QList<int> ComicTorrentDownloader::indicesOf(const ComicEditionFileSelector::ComicPayloadDecision& decision)
{
    QList<int> idx;
    idx.reserve(decision.files.size());
    for (const ComicSelectedFile& f : decision.files) idx.append(f.index);
    return idx;
}

void ComicTorrentDownloader::replayPackActive()
{
    if (!m_ledger) return;
    const QList<ComicEditionRequestRow> rows = m_ledger->active();
    for (const ComicEditionRequestRow& row : rows) {
        PackJob* job = m_packJobs.value(row.infoHash);
        if (!job) {
            job = new PackJob;
            job->infoHash = row.infoHash;
            job->magnetUri = row.magnetUri;
            job->saveDir = row.savePath.isEmpty() ? packSaveDirFor(row.infoHash) : row.savePath;
            m_packJobs.insert(row.infoHash, job);
        }
        EditionIntent intent;
        intent.editionId = row.editionId;
        intent.target.editionId = row.editionId;
        intent.target.seriesId = row.seriesId;
        intent.target.seriesTitle = row.seriesTitle;
        intent.target.editionTitle = row.editionTitle;
        intent.target.format = row.format;
        intent.target.ordinal = row.ordinal;
        intent.target.isbnDigits = row.isbnDigits;
        intent.target.collectedIssues = row.collectedIssues;
        // Persisted file choices are deliberately FORGOTTEN for execution: the
        // ledger doesn't carry collectedIssuesComplete/formatAmbiguous, so a
        // fresh selection pass (once metadata returns) is the honest source
        // of truth rather than a stale/partial reconstruction.
        intent.target.collectedIssuesComplete = !row.collectedIssues.isEmpty();
        job->intents.append(intent);
        m_hashByEdition.insert(row.editionId, row.infoHash);
        m_ledger->setState(row.editionId, QStringLiteral("awaiting_metadata"));
    }
    for (PackJob* job : m_packJobs) {
        QDir().mkpath(job->saveDir);
        if (m_engine) m_engine->addMagnet(job->magnetUri, job->saveDir, /*paused=*/true);
    }
}

ComicTorrentDownloader::EditionIntent*
ComicTorrentDownloader::packIntentFor(PackJob* job, const QString& editionId) const
{
    for (EditionIntent& intent : job->intents)
        if (intent.editionId == editionId) return &intent;
    return nullptr;
}

void ComicTorrentDownloader::writePackLedgerRow(PackJob* job,
                                                const ComicEditionIdentity::ComicEditionTarget& target,
                                                const QString& state)
{
    if (!m_ledger) return;
    ComicEditionRequestRow row;
    row.editionId = target.editionId;
    row.infoHash = job->infoHash;
    row.magnetUri = job->magnetUri;
    row.seriesId = target.seriesId;
    row.seriesTitle = target.seriesTitle;
    row.editionTitle = target.editionTitle;
    row.format = target.format;
    row.ordinal = target.ordinal;
    row.isbnDigits = target.isbnDigits;
    row.collectedIssues = target.collectedIssues;
    row.savePath = job->saveDir;
    row.pickedFileIndices = {};
    row.payloadKind = ComicPayloadKind::None;
    row.state = state;
    m_ledger->upsert(row);
}

void ComicTorrentDownloader::addPackIntent(PackJob* job,
                                           const ComicEditionIdentity::ComicEditionTarget& target,
                                           const QString& state)
{
    EditionIntent intent;
    intent.editionId = target.editionId;
    intent.target = target;
    job->intents.append(intent);
    m_hashByEdition.insert(target.editionId, job->infoHash);
    writePackLedgerRow(job, target, state);
}

void ComicTorrentDownloader::downloadEdition(const ComicEditionIdentity::ComicEditionTarget& targetIn,
                                             const QString& infoHashIn, const QString& magnetUri)
{
    ComicEditionIdentity::ComicEditionTarget target = targetIn;
    target.editionId = target.editionId.trimmed();
    const QString editionId = target.editionId;
    const QString hash = infoHashIn.trimmed().toLower();
    if (editionId.isEmpty() || hash.size() != 40) {
        emit failed(editionId, QStringLiteral("bad edition id / infoHash"));
        return;
    }
    if (m_hashByEdition.contains(editionId)) return;   // already live
    if (!m_engine) {
        emit failed(editionId, QStringLiteral("torrent engine unavailable"));
        return;
    }
    if (!m_ledger) {
        emit failed(editionId, QStringLiteral("edition transport unavailable"));
        return;
    }

    PackJob* job = m_packJobs.value(hash);
    if (!job) {
        // Brand-new torrent: add PAUSED, journal the intent, and wait for
        // metadata before touching the payload.
        job = new PackJob;
        job->infoHash = hash;
        job->magnetUri = magnetUri;
        job->saveDir = packSaveDirFor(hash);
        m_packJobs.insert(hash, job);

        QDir().mkpath(job->saveDir);
        m_engine->addMagnet(magnetUri, job->saveDir, /*paused=*/true);
        addPackIntent(job, target, QStringLiteral("awaiting_metadata"));
        emit resolving(editionId);

        // Mirror the legacy path: if the engine already holds metadata (e.g. a
        // re-add of a live torrent) resolve immediately instead of waiting.
        const QJsonArray existing = m_engine->torrentFiles(hash);
        if (!existing.isEmpty()) {
            job->metadataKnown = true;
            job->files = existing;
            resolvePackJob(job, job->payloadFinished);
        }
        return;
    }

    // Existing torrent: join it, grow the union. Never re-add the magnet. A
    // prior request for this edition may have gone terminal — revive that
    // same intent in place rather than appending a duplicate.
    if (EditionIntent* existing = packIntentFor(job, editionId)) {
        if (!existing->terminal) return;   // already live on this job
        EditionIntent fresh;
        fresh.editionId = editionId;
        fresh.target = target;
        *existing = fresh;
        m_hashByEdition.insert(editionId, job->infoHash);
        writePackLedgerRow(job, target, QStringLiteral("awaiting_metadata"));
        emit resolving(editionId);
        if (job->metadataKnown) resolvePackJob(job, job->payloadFinished);
        return;
    }

    addPackIntent(job, target, QStringLiteral("awaiting_metadata"));
    emit resolving(editionId);
    if (job->metadataKnown) resolvePackJob(job, job->payloadFinished);
}

// ── Resolve every live, unresolved intent against known metadata, union the
// live selections into one priority vector, then start the payload. Intents
// already resolved on a prior pass are left untouched. When the job's payload
// has ALREADY finished (a later edition joining an already-downloaded pack),
// any intent that resolves safely on THIS pass assembles immediately instead
// of waiting for another torrentFinished — libtorrent fires that signal once.
void ComicTorrentDownloader::resolvePackJob(PackJob* job, bool payloadAlreadyFinished)
{
    const QList<ComicEditionFileSelector::ManifestFile> manifest = toManifest(job->files);
    QList<ComicEditionFileSelector::ComicPayloadDecision> liveDecisions;
    QList<EditionIntent*> justResolvedLive;

    for (EditionIntent& intent : job->intents) {
        if (intent.terminal) continue;
        if (!intent.resolved) {
            intent.decision = ComicEditionFileSelector::select(intent.target, manifest);
            intent.resolved = true;
            if (intent.decision.failure == ComicSelectionFailure::None) {
                intent.awaitingChoice = false;
                qint64 sum = 0;
                for (const ComicSelectedFile& f : intent.decision.files) sum += f.bytes;
                intent.fileSize = sum;
                if (m_ledger) {
                    m_ledger->setSelection(intent.editionId, indicesOf(intent.decision), intent.decision.kind);
                    m_ledger->setState(intent.editionId, QStringLiteral("downloading"));
                }
                justResolvedLive << &intent;
            } else if (intent.decision.failure == ComicSelectionFailure::TargetMissing
                    || intent.decision.failure == ComicSelectionFailure::UnsupportedPayload) {
                intent.terminal = true;
                if (m_ledger) m_ledger->setState(intent.editionId, QStringLiteral("failed"));
                m_hashByEdition.remove(intent.editionId);
                emit failed(intent.editionId, reasonForFailure(intent.decision.failure));
                continue;
            } else {
                // Ambiguous / CombinedOnly / IncompleteIssueSet: zero priority,
                // await chooseFiles()/confirmCombined() — never auto-download.
                intent.awaitingChoice = true;
                emitTypedSelection(intent);
                continue;
            }
        }
        if (!intent.awaitingChoice && intent.decision.failure == ComicSelectionFailure::None)
            liveDecisions << intent.decision;
    }

    if (!hasLivePackIntent(job)) {
        maybeTearDownPackJob(job);
        return;
    }

    if (m_engine) {
        const QVector<int> priorities =
            ComicEditionFileSelector::unionPriorities(liveDecisions, job->files.size());
        m_engine->setFilePriorities(job->infoHash, priorities);
        // Never start the payload while every live intent is still awaiting a
        // manual choice (Ambiguous/CombinedOnly/IncompleteIssueSet) — mirrors
        // the legacy path staying paused during chooseFile(). Once ANY intent
        // has a live selection, starting is safe: zero-priority files simply
        // never fetch.
        if (!job->started && !liveDecisions.isEmpty()) {
            m_engine->startTorrent(job->infoHash, job->saveDir);
            job->started = true;
        }
    }

    if (payloadAlreadyFinished) {
        for (EditionIntent* intent : justResolvedLive)
            if (!intent->terminal && !intent->awaitingChoice)
                assembleAndPublish(job, *intent);
        maybeTearDownPackJob(job);
    }
}

void ComicTorrentDownloader::emitTypedSelection(const EditionIntent& intent)
{
    switch (intent.decision.failure) {
    case ComicSelectionFailure::Ambiguous:
        emit fileSelectionRequired(intent.editionId, intent.decision.manualCandidates);
        break;
    case ComicSelectionFailure::CombinedOnly: {
        QVariantList files;
        for (const ComicSelectedFile& f : intent.decision.files) {
            files.append(QVariantMap{
                {QStringLiteral("index"), f.index},
                {QStringLiteral("path"), f.path},
                {QStringLiteral("bytes"), QVariant::fromValue(f.bytes)}
            });
        }
        emit combinedArchiveConfirmationRequired(intent.editionId, files);
        break;
    }
    case ComicSelectionFailure::IncompleteIssueSet:
        emit incompleteIssueSetDetected(intent.editionId, intent.decision.missingIssues);
        break;
    default:
        break;
    }
}

void ComicTorrentDownloader::reapplyPackPriorities(PackJob* job)
{
    if (!m_engine || !job->metadataKnown) return;
    QList<ComicEditionFileSelector::ComicPayloadDecision> live;
    for (const EditionIntent& intent : job->intents)
        if (!intent.terminal && !intent.awaitingChoice && intent.resolved
            && intent.decision.failure == ComicSelectionFailure::None)
            live << intent.decision;
    const QVector<int> priorities = ComicEditionFileSelector::unionPriorities(live, job->files.size());
    m_engine->setFilePriorities(job->infoHash, priorities);
}

// Runs ComicEditionAssembler::assemble() SYNCHRONOUSLY (it blocks on
// extraction, same as ComicDownloader's own extraction). By the time this
// returns, the intent is done reading/copying whatever it needed from the
// shared job root — safe to mark terminal for reference-counting purposes
// regardless of whether the async ComicDownloader publish queue has actually
// finished moving the staging dir into the library yet.
void ComicTorrentDownloader::assembleAndPublish(PackJob* job, EditionIntent& intent)
{
    if (m_ledger) m_ledger->setState(intent.editionId, QStringLiteral("assembling"));

    ComicAssembleRequest req;
    req.editionId = intent.editionId;
    req.jobRoot = job->saveDir;
    req.kind = intent.decision.kind;
    req.files = intent.decision.files;
    req.stagingRoot = m_stagingRoot;

    ComicEditionAssembler::Result result;
    if (m_assembler) result = m_assembler->assemble(req);

    intent.terminal = true;
    m_hashByEdition.remove(intent.editionId);

    if (!m_assembler || !result.ok) {
        const QString reason = m_assembler ? result.error : QStringLiteral("assembler unavailable");
        if (m_ledger) m_ledger->setState(intent.editionId, QStringLiteral("failed"));
        emit failed(intent.editionId, reason);
        return;
    }

    job->anySuccess = true;
    if (m_ledger) m_ledger->setState(intent.editionId, QStringLiteral("publishing"));
    if (m_ingestTarget) {
        m_ingestTarget->ingestAssembledEdition(intent.editionId, intent.target.seriesId,
            intent.target.seriesTitle, intent.target.editionTitle,
            result.stagingDir, result.orderedFiles, result.groups);
    } else if (m_ledger) {
        // No publication target wired (production, until a later task wires a
        // real ComicDownloader in) — the assembled staging dir is honest
        // evidence of success even though nothing consumes it yet.
        m_ledger->setState(intent.editionId, QStringLiteral("completed"));
    }
}

bool ComicTorrentDownloader::cancelEdition(const QString& editionId)
{
    PackJob* job = m_packJobs.value(m_hashByEdition.value(editionId));
    if (!job) return false;
    EditionIntent* intent = packIntentFor(job, editionId);
    if (!intent || intent->terminal) return false;

    intent->terminal = true;
    if (m_ledger) m_ledger->setState(editionId, QStringLiteral("cancelled"));
    m_hashByEdition.remove(editionId);

    if (hasLivePackIntent(job)) {
        // Other editions still ride this torrent — stop only the cancelled
        // file(s) by re-narrowing the priority union to the survivors.
        reapplyPackPriorities(job);
        return true;
    }
    // Last live intent cancelled — tear the torrent down (unless an earlier
    // intent already succeeded, in which case the job stays resident).
    maybeTearDownPackJob(job);
    return true;
}

bool ComicTorrentDownloader::chooseFiles(const QString& editionId, const QList<int>& indices)
{
    PackJob* job = m_packJobs.value(m_hashByEdition.value(editionId));
    if (!job) return false;
    EditionIntent* intent = packIntentFor(job, editionId);
    if (!intent || !intent->awaitingChoice) return false;
    if (intent->decision.failure != ComicSelectionFailure::Ambiguous) return false;
    if (indices.isEmpty()) return false;

    QSet<int> allowed;
    for (const QVariant& c : intent->decision.manualCandidates)
        allowed.insert(c.toMap().value(QStringLiteral("index")).toInt());

    const QList<ComicEditionFileSelector::ManifestFile> manifest = toManifest(job->files);
    QList<ComicSelectedFile> chosen;
    for (int idx : indices) {
        if (!allowed.contains(idx)) return false;
        ComicSelectedFile f;
        f.index = idx;
        f.order = chosen.size();
        for (const ComicEditionFileSelector::ManifestFile& mf : manifest) {
            if (mf.index == idx) { f.path = mf.path; f.bytes = mf.bytes; break; }
        }
        chosen.append(f);
    }
    if (chosen.isEmpty()) return false;

    intent->decision.kind = chosen.size() > 1 ? ComicPayloadKind::IssueArchiveSet
                                              : ComicPayloadKind::SingleArchive;
    intent->decision.failure = ComicSelectionFailure::None;
    intent->decision.files = chosen;
    intent->awaitingChoice = false;
    qint64 sum = 0;
    for (const ComicSelectedFile& f : chosen) sum += f.bytes;
    intent->fileSize = sum;

    if (m_ledger) {
        m_ledger->setSelection(editionId, indicesOf(intent->decision), intent->decision.kind);
        m_ledger->setState(editionId, QStringLiteral("downloading"));
    }
    reapplyPackPriorities(job);
    if (job->payloadFinished) {
        assembleAndPublish(job, *intent);
        maybeTearDownPackJob(job);
    }
    return true;
}

bool ComicTorrentDownloader::confirmCombined(const QString& editionId)
{
    PackJob* job = m_packJobs.value(m_hashByEdition.value(editionId));
    if (!job) return false;
    EditionIntent* intent = packIntentFor(job, editionId);
    if (!intent || !intent->awaitingChoice) return false;
    if (intent->decision.failure != ComicSelectionFailure::CombinedOnly) return false;

    intent->decision.failure = ComicSelectionFailure::None;
    intent->awaitingChoice = false;
    qint64 sum = 0;
    for (const ComicSelectedFile& f : intent->decision.files) sum += f.bytes;
    intent->fileSize = sum;

    if (m_ledger) {
        m_ledger->setSelection(editionId, indicesOf(intent->decision), intent->decision.kind);
        m_ledger->setState(editionId, QStringLiteral("downloading"));
    }
    reapplyPackPriorities(job);
    if (job->payloadFinished) {
        assembleAndPublish(job, *intent);
        maybeTearDownPackJob(job);
    }
    return true;
}

QVariantMap ComicTorrentDownloader::statusOfEdition(const QString& editionId) const
{
    QVariantMap status{{QStringLiteral("state"), QStringLiteral("none")},
                       {QStringLiteral("done"), 0.0},
                       {QStringLiteral("total"), 0.0}};
    const QString hash = m_hashByEdition.value(editionId);
    if (PackJob* job = m_packJobs.value(hash)) {
        for (const EditionIntent& intent : job->intents) {
            if (intent.editionId != editionId || intent.terminal) continue;
            const bool downloading = job->metadataKnown && intent.resolved && !intent.awaitingChoice
                && intent.decision.failure == ComicSelectionFailure::None;
            status[QStringLiteral("state")] = intent.awaitingChoice ? QStringLiteral("choosing")
                                             : downloading           ? QStringLiteral("downloading")
                                                                      : QStringLiteral("resolving");
            status[QStringLiteral("done")] = static_cast<double>(intent.received);
            status[QStringLiteral("total")] = static_cast<double>(intent.fileSize);
            return status;
        }
    }
    // No live intent in memory — fall back to the journal so a terminal or
    // restart-restored outcome survives.
    if (m_ledger) {
        for (const ComicEditionRequestRow& row : m_ledger->all()) {
            if (row.editionId == editionId) {
                status[QStringLiteral("state")] = row.state;
                break;
            }
        }
    }
    return status;
}

bool ComicTorrentDownloader::hasLivePackIntent(const PackJob* job) const
{
    for (const EditionIntent& intent : job->intents)
        if (!intent.terminal) return true;
    return false;
}

void ComicTorrentDownloader::maybeTearDownPackJob(PackJob* job)
{
    if (hasLivePackIntent(job)) return;
    if (job->anySuccess) return;   // stays resident — see header comment
    tearDownPackJob(job, /*deleteFiles=*/true);
}

void ComicTorrentDownloader::tearDownPackJob(PackJob* job, bool deleteFiles)
{
    // Idempotent guard: if this job is no longer the one registered under its
    // hash, a prior tearDown already handled it.
    if (m_packJobs.value(job->infoHash) != job) return;

    const QString hash = job->infoHash;
    for (const EditionIntent& intent : job->intents)
        m_hashByEdition.remove(intent.editionId);
    m_packJobs.remove(hash);

    if (m_engine) m_engine->removeTorrent(hash, deleteFiles);
    delete job;
}

// ── Unified engine dispatch (both subsystems share one engine signal set) ──

void ComicTorrentDownloader::onMetadataReady(const QString& infoHash, const QString&,
                                             qint64, const QJsonArray& files)
{
    if (Job* job = jobForHash(infoHash)) {
        if (alive(job) && !job->picked) applyMetadata(job, files);
        return;
    }
    if (PackJob* job = m_packJobs.value(infoHash.toLower())) {
        job->metadataKnown = true;
        job->files = files;
        resolvePackJob(job, /*payloadAlreadyFinished=*/false);
    }
}

void ComicTorrentDownloader::onEngineProgress(const QString& infoHash, float fraction,
                                              int, int, int, int)
{
    if (Job* job = jobForHash(infoHash)) {
        if (alive(job) && job->picked && job->totalBytes > 0) {
            job->received = static_cast<qint64>(fraction * static_cast<float>(job->totalBytes));
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (job->lastProgressEmit == 0 || now - job->lastProgressEmit >= kProgressThrottleMs) {
                job->lastProgressEmit = now;
                emit progress(job->issueId, static_cast<double>(job->received),
                              static_cast<double>(job->totalBytes));
            }
        }
        return;
    }
    if (PackJob* job = m_packJobs.value(infoHash.toLower())) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (EditionIntent& intent : job->intents) {
            if (intent.terminal || intent.awaitingChoice || !intent.resolved) continue;
            if (intent.decision.failure != ComicSelectionFailure::None) continue;
            if (intent.fileSize <= 0) continue;
            intent.received = static_cast<qint64>(fraction * static_cast<float>(intent.fileSize));
            if (intent.lastProgressEmit == 0 || now - intent.lastProgressEmit >= kProgressThrottleMs) {
                intent.lastProgressEmit = now;
                emit progress(intent.editionId, static_cast<double>(intent.received),
                              static_cast<double>(intent.fileSize));
            }
        }
    }
}

void ComicTorrentDownloader::onEngineFinished(const QString& infoHash)
{
    if (Job* job = jobForHash(infoHash)) {
        if (alive(job) && job->picked) finalizeJob(job);
        return;
    }
    if (PackJob* job = m_packJobs.value(infoHash.toLower())) {
        job->payloadFinished = true;
        for (EditionIntent& intent : job->intents) {
            if (intent.terminal || intent.awaitingChoice || !intent.resolved) continue;
            if (intent.decision.failure != ComicSelectionFailure::None) continue;
            assembleAndPublish(job, intent);
        }
        maybeTearDownPackJob(job);
    }
}

void ComicTorrentDownloader::onEngineFailed(const QString& infoHash, const QString& message)
{
    if (Job* job = jobForHash(infoHash)) {
        if (alive(job)) {
            m_engine->removeTorrent(infoHash, true);
            failJob(job, message.isEmpty() ? QStringLiteral("engine error") : message);
        }
        return;
    }
    if (PackJob* job = m_packJobs.value(infoHash.toLower())) {
        const QString reason = message.isEmpty() ? QStringLiteral("engine error") : message;
        for (EditionIntent& intent : job->intents) {
            if (intent.terminal) continue;
            intent.terminal = true;
            if (m_ledger) m_ledger->setState(intent.editionId, QStringLiteral("failed"));
            m_hashByEdition.remove(intent.editionId);
            emit failed(intent.editionId, reason);
        }
        tearDownPackJob(job, /*deleteFiles=*/true);
    }
}
