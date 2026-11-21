#include "VaultScanner.h"
#include "VaultBookStateMigrator.h"
#include "VaultLocation.h"

#include <QDir>
#include <atomic>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QThread>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtConcurrent>

namespace {
std::atomic<bool> g_storageAccessDenied{false};
bool genericFilmTitle(const QString& title)
{
    static const QRegularExpression generic(QStringLiteral(
        "(?i)^(?:movie|video|feature|main|sample|clip|part[._\\s-]*\\d+|cd[._\\s-]*\\d+|disc[._\\s-]*\\d+)$"));
    return generic.match(title).hasMatch();
}
}

void VaultScanner::setStorageAccessDenied(bool denied) { g_storageAccessDenied.store(denied); }
bool VaultScanner::storageAccessDenied() { return g_storageAccessDenied.load(); }

VaultScanner::VaultScanner(VaultIndex* index, VaultIdentity* identity, QObject* parent)
    : QObject(parent), m_index(index), m_identity(identity)
{
    m_scanPool.setMaxThreadCount(1);
    m_scanPool.setThreadPriority(QThread::LowPriority);
    m_activityTimer.setInterval(110);
    connect(&m_activityTimer, &QTimer::timeout, this, [this] { reportActivity(); });
}

VaultScanner::~VaultScanner()
{
    cancel();
    m_scanPool.waitForDone();
}

std::shared_ptr<VaultScanner::Activity> VaultScanner::beginActivity(const QStringList& roots)
{
    m_activityRoots = roots;
    const auto activity = std::make_shared<Activity>();
    m_activity = activity;
    m_activityTimer.start();
    reportActivity();
    return activity;
}

void VaultScanner::reportActivity(bool publishing)
{
    if (!m_activity)
        return;
    const int index = m_activity->rootIndex.load();
    const QString phase = publishing ? QStringLiteral("publishing")
        : m_activity->processing.load() ? QStringLiteral("processing") : QStringLiteral("discovering");
    const QString root = m_activityRoots.value(index);
    const int discovered = m_activity->discovered.load();
    const bool processing = phase != QLatin1String("discovering");
    const int done = processing ? m_activity->processed.load() : discovered;
    emit scanActivity(root, done, processing ? discovered : 0, phase);
    emit progress(root, done, processing ? discovered : 0, QString());
}

void VaultScanner::setApplySuspended(bool on)
{
    if (m_applySuspended == on)
        return;
    m_applySuspended = on;
    if (on || m_deferredApplyKind == DeferredApplyKind::None)
        return;

    const DeferredApplyKind kind = m_deferredApplyKind;
    m_deferredApplyKind = DeferredApplyKind::None;
    if (kind == DeferredApplyKind::Result) {
        const RawResult result = m_deferredResult;
        m_deferredResult = RawResult();
        applyResult(result);
        return;
    }
    const QList<RawResult> results = m_deferredPublishResults;
    const quint64 generation = m_deferredPublishGeneration;
    const QList<VaultIndex::FileRow> extraRows = m_deferredPublishExtraRows;
    m_deferredPublishResults.clear();
    m_deferredPublishExtraRows.clear();
    m_deferredPublishGeneration = 0;
    applyPublish(results, generation, extraRows);
}

QString VaultScanner::subfolderOf(const QString& subtree, const QString& filePath)
{
    const QString parent = QFileInfo(filePath).absolutePath();
    const QString rel = QDir(subtree).relativeFilePath(parent);
    if (rel == QLatin1String("."))
        return QString();
    return rel;
}

bool VaultScanner::hasMixedVideoTitles(const QStringList& files)
{
    QSet<QString> titles;
    for (const QString& file : files) {
        if (VaultKit::kindForFile(file) != VaultKit::MediaKind::Video) continue;
        if (VaultKit::isSampleVideo(file)) continue;
        const auto context = VaultKit::videoEpisodeContext(file, files);
        QString title = context.title;
        if (!context.number.matched)
            title = VaultKit::standaloneVideoTitle(file);
        if (!title.isEmpty() && !genericFilmTitle(title)) {
            QString key = VaultKit::normalizedTitle(title);
            if (!context.number.matched) key += QStringLiteral("::year::")
                + QString::number(VaultKit::parsedTitleYear(QFileInfo(file).completeBaseName()));
            titles.insert(key);
        }
        if (titles.size() > 1) return true;
    }
    return false;
}

void VaultScanner::assignVideoGroup(VaultIndex::FileRow& row, bool loose, bool mixedTitles,
                                    const QStringList& siblings)
{
    if (row.kind != QLatin1String("video")) return;
    if (VaultKit::isSampleVideo(row.path)) {
        row.groupKey = row.subtreePath = row.path;
        row.groupTitle = VaultKit::standaloneVideoTitle(row.realName);
        row.parsedYear = VaultKit::parsedTitleYear(QFileInfo(row.realName).completeBaseName());
        return;
    }
    const auto context = VaultKit::videoEpisodeContext(row.path, siblings);
    Q_UNUSED(loose);
    if (context.number.matched) {
        row.groupTitle = context.title;
        const QString folder = VaultKit::normalizedTitle(QFileInfo(context.root).fileName());
        const QString title = VaultKit::normalizedTitle(context.title);
        row.groupKey = row.subtreePath = folder == title || folder.startsWith(title + QLatin1Char(' '))
            ? context.root : VaultKit::episodeShowKey(context.root, context.title);
        row.subfolder = subfolderOf(context.root, row.path);
        row.parsedYear = VaultKit::parsedTitleYear(QFileInfo(context.root).fileName());
        return;
    }
    const QString parent = QFileInfo(row.path).absolutePath();
    row.groupKey = mixedTitles ? row.path : parent;
    row.subtreePath = row.groupKey;
    QString filmTitle = VaultKit::standaloneVideoTitle(row.realName);
    // An opaque release-credit clip is not its parent feature. Only explicit
    // multipart/main labels are eligible to inherit a physical film folder.
    if (filmTitle.isEmpty() || genericFilmTitle(filmTitle))
        filmTitle = VaultKit::cleanMediaFolderTitle(QFileInfo(parent).fileName());
    row.groupTitle = filmTitle;
    const int fileYear = VaultKit::parsedTitleYear(QFileInfo(row.realName).completeBaseName());
    row.parsedYear = fileYear > 0 ? fileYear : VaultKit::parsedTitleYear(QFileInfo(parent).fileName());
    row.subfolder = subfolderOf(parent, row.path);
}

void VaultScanner::assignBookGroup(VaultIndex::FileRow& row)
{
    if (row.kind != QLatin1String("book")) return;
    row.groupKey = row.subtreePath = row.path;
    row.groupTitle = row.displayTitle;
    row.parsedYear = VaultKit::parsedTitleYear(QFileInfo(row.path).completeBaseName());
}

VaultScanner::RawResult VaultScanner::buildScan(
    QString root, QStringList scanIgnore, quint64 generation,
    std::shared_ptr<VaultKit::CancellationToken> cancel,
    const QMap<QString, QString>& kindOverrides,
    const std::function<void(int, int, const QString&)>& onProgress,
    const VaultLocation::PersonalPaths& personal,
    const std::function<void(bool, int)>& onWork)
{
    RawResult r;
    r.root = root;
    r.generation = generation;

    const VaultKit::CancellationToken* c = cancel.get();
    if (c && c->isCancelled()) {
        r.cancelled = true;
        return r;
    }
    if (personal.excludes(root, true))
        return r;
    const QFileInfo rootInfo(root);
    if ((VaultScanner::storageAccessDenied() && !VaultLocation::isContentUri(root))
        || (rootInfo.isDir() && !rootInfo.isReadable())) {
        r.unreadable = true;
        return r;
    }
    if (!rootInfo.isDir() || !rootInfo.isReadable()
        || (!personal.isEmpty() && !VaultKit::isDirectDirectory(rootInfo)))
        return r;
    r.scanned = true;

    const QStringList needles = VaultKit::sanitizeIgnoreNeedles(scanIgnore);
    int mediaFound = 0;
    qsizetype work = 0;
    auto pace = [&work, &onWork]() {
        // Pause at bounded intervals so a scan on a 1-core system or busy disk
        // leaves time for interactive work. Limit total concurrent scans too.
        if (onWork && ++work % 128 == 0)
            QThread::msleep(1);
    };
    auto onEntry = [&](bool media) {
        if (media) ++mediaFound;
        if (onWork && (media || work % 128 == 0))
            onWork(false, mediaFound);
        pace();
    };
    auto groups = VaultKit::groupByFirstLevelSubdir(
        {root}, VaultKit::allMediaFilters(), c, needles, onProgress, personal, onEntry);
    if (onWork)
        onWork(true, 0);
    // groupByFirstLevelSubdir swallows cancellation and returns early-empty, so
    // check here too: a cancelled scan must report cancelled (NOT an empty
    // result, which applyResult would publish and wipe the previous contents).
    if (c && c->isCancelled()) {
        r.cancelled = true;
        return r;
    }

    // Match VaultConfig::norm so an override keyed by the normalized subtree resolves.
    auto normPath = [](const QString& p) {
        QString n = QDir::cleanPath(p);
#ifdef Q_OS_WIN
        n = n.toLower();
#endif
        return n;
    };

    QMap<QString, QString> physicalOverrides, comicOverrides, originalScopes;
    for (auto override = kindOverrides.constBegin(); override != kindOverrides.constEnd(); ++override) {
        if (override.key().startsWith(QLatin1String("comic::")))
            comicOverrides.insert(override.key().mid(7), override.value());
        else physicalOverrides.insert(override.key(), override.value());
    }
    for (auto group = groups.constBegin(); group != groups.constEnd(); ++group) {
        for (const QString& file : group.value()) {
            if (c && c->isCancelled()) {
                r.cancelled = true;
                return r;
            }
            pace();
            originalScopes.insert(file, group.key().endsWith(QLatin1String("::LOOSE")) ? root : group.key());
        }
    }

    // An explicit nested override keeps its scope when an ancestor root is added
    // or removed. Partition before classification so only that scope is relabelled.
    const QStringList physicalScopes = physicalOverrides.keys();
    const QStringList comicScopes = comicOverrides.keys();
    if (!physicalOverrides.isEmpty()) {
        QMap<QString, QStringList> scoped;
        for (auto group = groups.constBegin(); group != groups.constEnd(); ++group) {
            for (const QString& file : group.value()) {
                if (c && c->isCancelled()) {
                    r.cancelled = true;
                    return r;
                }
                pace();
                const QString scope = VaultKit::ownerRootForPath(physicalScopes, file);
                scoped[scope.isEmpty() ? group.key() : file.left(scope.size())].append(file);
            }
        }
        groups = scoped;
    }

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (c && c->isCancelled()) {
            r.cancelled = true;
            return r;
        }
        const QString key = it.key();
        const bool loose = key.endsWith(QStringLiteral("::LOOSE"));
        const QString subtree = loose ? root : key;
        const QStringList files = it.value();
        QMap<QString, QStringList> videosByParent;
        for (const QString& file : files) {
            if (VaultKit::kindForFile(file) == VaultKit::MediaKind::Video)
                videosByParent[QFileInfo(file).absolutePath()].append(file);
        }
        QMap<QString, bool> mixedByParent;
        for (auto parent = videosByParent.constBegin(); parent != videosByParent.constEnd(); ++parent)
            mixedByParent.insert(parent.key(), hasMixedVideoTitles(parent.value()));

        const VaultKit::LeafClassification cls = VaultKit::classifyLeaf(files);
        if (cls.dominant == VaultKit::MediaKind::Unknown)
            continue;

        // The kind this slice shelves under is the user's chip override if one exists,
        // else the content-dominant kind (Thread A). Which FILES shelve:
        //  - the override kind IS present in the folder → shelve exactly those; the other
        //    kinds fall to the leftover line (mixed-leaf reassignment, spec §5);
        //  - the override names a kind NOT present ("these .cbz are art books") → the user
        //    has declared the whole folder that kind, so its dominant-content files shelve
        //    re-labelled and the rest stay leftover.
        VaultKit::MediaKind effectiveKind = cls.dominant;
        const QString ov = physicalOverrides.value(normPath(subtree));
        if (!ov.isEmpty()) {
            const VaultKit::MediaKind ovKind = VaultKit::kindFromName(ov);
            if (ovKind != VaultKit::MediaKind::Unknown)
                effectiveKind = ovKind;
        }
        const bool overridePresent = cls.counts.value(effectiveKind, 0) > 0;
        const VaultKit::MediaKind shelveKind = overridePresent ? effectiveKind : cls.dominant;
        const QString effectiveKindName = VaultKit::kindName(effectiveKind);

        // Vault ux uplift S16: capture the RAW name's year where the cleaner strips it, so
        // the identifier can re-match a remade title ("Dune (2021)") against the catalogue's
        // year filter instead of seeing a bare "Dune" ambiguity.
        const QString groupRaw = QFileInfo(subtree).fileName();
        QString groupTitle = VaultKit::cleanMediaFolderTitle(groupRaw);
        if (groupTitle.isEmpty())
            groupTitle = groupRaw;

        // Enrichment gathered over the shelved files (Thread B): distinct 2nd-level
        // subgroups (the card's "· N series"), a small sample line, and total size.
        QSet<QString> subgroupKeys;
        QStringList subgroupSample; // distinct cleaned 2nd-level names (preferred sample)
        QStringList looseSample;    // cleaned file basenames (fallback when leaf is flat)
        QSet<QString> looseSeen;
        int shelvedCount = 0, leftoverCount = 0;
        qint64 sizeBytes = 0;

        for (const QString& f : files) {
            if (c && c->isCancelled()) {
                r.cancelled = true;
                return r;
            }
            pace();
            const VaultKit::MediaKind fk = VaultKit::kindForFile(f);
            if (fk == VaultKit::MediaKind::Unknown)
                continue;
            // Other supported media retain their own medium and remain indexed.
            if (fk != shelveKind) ++leftoverCount;

            const QFileInfo fi(f);
            if (fk == shelveKind) ++shelvedCount;
            sizeBytes += fi.size();

            const QString rel = subfolderOf(subtree, f);
            const QString second =
                rel.isEmpty() ? QString() : rel.split(QLatin1Char('/')).first();
            if (!second.isEmpty()) {
                const QString sk = second.toLower();
                if (!subgroupKeys.contains(sk)) {
                    subgroupKeys.insert(sk);
                    if (subgroupSample.size() < 3) {
                        const QString t = VaultKit::cleanMediaFolderTitle(second);
                        subgroupSample.append(t.isEmpty() ? second : t);
                    }
                }
            } else if (looseSample.size() < 3) {
                const QString base = fi.completeBaseName();
                QString t = VaultKit::cleanMediaFolderTitle(base);
                if (t.isEmpty())
                    t = base;
                if (!looseSeen.contains(t.toLower())) {
                    looseSeen.insert(t.toLower());
                    looseSample.append(t);
                }
            }

            VaultIdentity::FileFacts ff;
            ff.path = f;
            ff.size = fi.size();
            ff.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
            r.facts.append(ff);

            VaultIndex::FileRow row;
            row.rootPath = root;
            row.subtreePath = subtree;
            row.groupKey = subtree;
            row.groupTitle = groupTitle;
            row.parsedYear = VaultKit::parsedTitleYear(groupRaw); // S16
            row.kind = fk == shelveKind ? effectiveKindName : VaultKit::kindName(fk);
            const QString physicalScope = VaultKit::ownerRootForPath(physicalScopes, f);
            const QString comicScope = fk == VaultKit::MediaKind::Comic
                ? VaultKit::ownerRootForPath(comicScopes, f) : QString();
            const bool fileOverride = !physicalScope.isEmpty() && normPath(physicalScope) == normPath(f);
            if (!comicScope.isEmpty() && !fileOverride && comicScope.size() >= physicalScope.size()) {
                const auto declared = VaultKit::kindFromName(comicOverrides.value(comicScope));
                if (declared != VaultKit::MediaKind::Unknown) row.kind = VaultKit::kindName(declared);
            }
            if (row.kind == QLatin1String("comic")) {
                // A return from a per-file book group restores the original comic series
                // scope; the typed folder declaration still supplies that scope after a
                // broader root is added. Non-comic files never inherit this selector.
                if (fk == VaultKit::MediaKind::Comic && (!comicScope.isEmpty() || fileOverride))
                    row.subtreePath = !comicScope.isEmpty() ? f.left(comicScope.size()) : originalScopes.value(f);
                row.groupKey = QStringLiteral("comic::") + row.subtreePath;
                row.groupTitle = VaultKit::cleanMediaFolderTitle(QFileInfo(row.subtreePath).fileName());
            }
            row.path = f;
            row.realName = fi.fileName();
            QString disp = VaultKit::cleanMediaFolderTitle(fi.completeBaseName());
            row.displayTitle = disp.isEmpty() ? fi.completeBaseName() : disp;
            row.subfolder = row.kind == QLatin1String("comic")
                ? subfolderOf(row.subtreePath, f) : rel;
            row.size = ff.size;
            row.mtimeMs = ff.mtimeMs;
            row.format = fi.suffix().toLower();
            assignVideoGroup(row, loose, mixedByParent.value(fi.absolutePath()), videosByParent.value(fi.absolutePath()));
            assignBookGroup(row);
            r.rows.append(row);
            if (onWork) onWork(true, r.rows.size());
        }

        if (shelvedCount == 0)
            continue; // nothing shelves under the chosen kind — no slice, no rows

        const QStringList& sample = !subgroupSample.isEmpty() ? subgroupSample : looseSample;

        QVariantMap sm;
        sm[QStringLiteral("subtreePath")] = subtree;
        sm[QStringLiteral("groupTitle")] = groupTitle;
        sm[QStringLiteral("kind")] = effectiveKindName;
        sm[QStringLiteral("count")] = shelvedCount;
        sm[QStringLiteral("mixed")] = cls.mixed;
        sm[QStringLiteral("loose")] = loose;
        sm[QStringLiteral("leftoverCount")] = leftoverCount;
        sm[QStringLiteral("seriesCount")] = subgroupKeys.size();
        sm[QStringLiteral("sample")] = sample.join(QStringLiteral("  ·  "));
        sm[QStringLiteral("sizeBytes")] = static_cast<double>(sizeBytes);
        r.sliceModel.append(sm);
    }
    return r;
}

void VaultScanner::applyResult(const RawResult& result)
{
    if (result.generation != m_generation)
        return; // stale — a newer scan superseded this one
    if (m_applySuspended) {
        m_deferredApplyKind = DeferredApplyKind::Result;
        m_deferredResult = result;
        m_deferredPublishResults.clear();
        m_deferredPublishExtraRows.clear();
        m_deferredPublishGeneration = 0;
        return;
    }

    m_activityTimer.stop();
    m_scanning = false;

    // applyResult only DELIVERS the candidate census for the confirmation card
    // (Slice 11). Publication is a separate, confirm-triggered step (publishConfirmed
    // → applyPublish) that aggregates ALL confirmed roots — so a single root's census
    // never reaches VaultIndex::publish() alone, where the whole-index replace would
    // wipe the other roots.
    const bool cancelled = result.cancelled
        || (m_asyncGeneration == result.generation && m_cancel && m_cancel->isCancelled());
    emit scanFinished(result.root, result.sliceModel, cancelled);

    if (!m_pending.isEmpty()) {
        const auto next = m_pending.takeFirst();
        scanRoot(next.first, next.second);
    }
}

void VaultScanner::applyPublish(const QList<RawResult>& results, quint64 generation,
                                const QList<VaultIndex::FileRow>& extraRows)
{
    if (generation != m_generation)
        return; // a newer scan/publish superseded this aggregate
    if (m_applySuspended) {
        m_deferredApplyKind = DeferredApplyKind::Publish;
        m_deferredPublishResults = results;
        m_deferredPublishGeneration = generation;
        m_deferredPublishExtraRows = extraRows;
        m_deferredResult = RawResult();
        return;
    }

    m_activityTimer.stop();
    m_scanning = false;

    if (m_asyncGeneration == generation && m_cancel && m_cancel->isCancelled()) {
        emit publicationFinished(false, true, false);
        return;
    }

    // A cancelled census in the set aborts the WHOLE publish — no partial truth; the
    // previous index generation stands (VaultIndex::publish is atomic regardless).
    for (const RawResult& r : results) {
        if (r.cancelled) {
            emit publicationFinished(false, true, false);
            return;
        }
    }

    // Aggregate every confirmed root's facts + rows, reconcile identity ONCE, assign
    // ids, then publish the UNION in a single transactional replace. A missing root is
    // represented by its last indexed rows before reconciliation: it is present-but-away,
    // not an empty census. That prevents an unrelated available file with the same
    // size/mtime from borrowing an absent drive's canonical identity.
    reportActivity(true);
    const auto& personal = m_index->personalPaths();
    QStringList roots;
    for (const RawResult& result : results) roots.append(result.root);
    QMap<QString, VaultIndex::FileRow> selected;
    QList<VaultIndex::FileRow> previous;
    bool loadedPrevious = false;
    bool scannedUserRoot = false;
    for (const RawResult& result : results) {
        QList<VaultIndex::FileRow> candidates = result.rows;
        const bool available = QDir(result.root).exists() && !result.unreadable;
        scannedUserRoot = scannedUserRoot || (result.scanned && available);
        if (!available) {
            // Upgrade old overlapping indexes too: an absent child may previously have
            // had its ownership overwritten by an ancestor. Recover its path membership.
            if (!loadedPrevious) {
                for (const QString& kind : m_index->kinds()) previous.append(m_index->rowsForKind(kind));
                loadedPrevious = true;
            }
            candidates = previous;
        }
        QMap<QString, QStringList> videosByParent;
        if (!available) {
            for (const auto& row : candidates)
                if (row.kind == QLatin1String("video") && !personal.excludes(row.path))
                    videosByParent[QFileInfo(row.path).absolutePath()].append(row.path);
        }
        for (VaultIndex::FileRow row : candidates) {
            if (personal.excludes(row.path) || VaultKit::ownerRootForPath(roots, row.path) != result.root) continue;
            if (!available) {
                assignVideoGroup(row, false,
                    hasMixedVideoTitles(videosByParent.value(QFileInfo(row.path).absolutePath())),
                    videosByParent.value(QFileInfo(row.path).absolutePath()));
                assignBookGroup(row);
            }
            if (row.kind == QLatin1String("comic") && !row.groupKey.startsWith(QLatin1String("comic::")))
                row.groupKey.prepend(QStringLiteral("comic::"));
            row.rootPath = result.root;
            row.away = !available;
            selected.insert(VaultLocation::normalize(row.path), row);
        }
    }
    // Synthetic rows deliberately win over physical imports. Include their unique
    // facts BEFORE reconcile too, so downloaded identities participate in continuity.
    for (VaultIndex::FileRow row : extraRows) {
        if (personal.excludes(row.path)) continue;
        if (row.kind == QLatin1String("comic") && !row.groupKey.startsWith(QLatin1String("comic::")))
            row.groupKey.prepend(QStringLiteral("comic::"));
        selected.insert(VaultLocation::normalize(row.path), row);
    }
    QList<VaultIdentity::FileFacts> allFacts;
    QList<VaultIndex::FileRow> allRows = selected.values();
    for (const VaultIndex::FileRow& row : allRows)
        allFacts.append({row.path, row.size, row.mtimeMs});
    const VaultIdentity::ReconcileResult reconciliation = m_identity->reconcile(allFacts);
    for (VaultIndex::FileRow& row : allRows)
        row.id = m_identity->idForFile(row.path, row.size, row.mtimeMs);

    // The Vault identity owns the alias record; the book lane owns the state shape. Use the
    // existing public BookStores contract only for book rows, so a comic/video rename never
    // invents a Reader 2 record and no Reader2Bridge/BookStores source edit is needed.
    for (const QStringList& migration : reconciliation.migrated) {
        if (migration.size() != 4)
            continue;
        const QString& newPath = migration.at(3);
        for (const VaultIndex::FileRow& row : allRows) {
            if (row.kind == QLatin1String("book") && row.path == newPath) {
                VaultBookStateMigrator::migrate(migration.at(2), newPath);
                break;
            }
        }
    }

    // Emit indexPublished ONLY on a successful publish — a failed/rolled-back publish
    // must NOT tell the shelves "new truth landed"; the previous generation is intact.
    const bool published = m_index->publish(allRows);
    if (published)
        emit indexPublished(QString(), m_index->itemCount());
    emit publicationFinished(published, false, scannedUserRoot);
}

void VaultScanner::publishConfirmed(const QStringList& confirmedRoots,
                                    const QStringList& scanIgnore,
                                    const QMap<QString, QString>& kindOverrides,
                                    const QList<VaultIndex::FileRow>& extraRows,
                                    const VaultLocation::PersonalPaths& personal)
{
    // Supersede any in-flight scan/publish, then census EVERY confirmed root fresh
    // off-thread: the index is a rebuildable product, so a confirm rebuilds the whole
    // Vault from all confirmed roots and publishes their union atomically (decision 4 +
    // Preflight §3: preserve multi-root atomicity, never a per-root replace).
    invalidate();
    m_scanning = true;
    const quint64 gen = m_asyncGeneration = m_generation;
    m_cancel = std::make_shared<VaultKit::CancellationToken>();
    auto cancel = m_cancel;
    const auto activity = beginActivity(confirmedRoots);

    auto* watcher = new QFutureWatcher<Publication>(this);
    connect(watcher, &QFutureWatcher<Publication>::finished, this,
        [this, watcher, gen]() {
            const Publication result = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation)
                return;
            m_activityTimer.stop();
            if (!m_applySuspended && !m_cancel->isCancelled()) {
                reportActivity(true);
                // Let Qt Quick present the phase before the owner-thread commit.
                QTimer::singleShot(16, this, [this, result, gen] {
                    applyPublish(result.roots, gen, result.extraRows);
                });
            } else {
                applyPublish(result.roots, gen, result.extraRows);
            }
        });
    watcher->setFuture(QtConcurrent::run(&m_scanPool,
        [confirmedRoots, scanIgnore, gen, cancel, kindOverrides, personal, activity, extraRows]() {
            Publication out;
            int completed = 0;
            for (int i = 0; i < confirmedRoots.size() && !cancel->isCancelled(); ++i) {
                const QString& root = confirmedRoots.at(i);
                activity->rootIndex.store(i);
                activity->processing.store(false);
                auto skip = personal;
                const QString normalized = VaultLocation::normalize(root);
                for (const QString& other : confirmedRoots) {
                    const QString nested = VaultLocation::normalize(other);
                    if (nested != normalized && VaultLocation::containsPath(normalized, nested))
                        skip.directories.insert(nested);
                }
                auto onWork = [activity, completed](bool processing, int files) {
                    activity->processing.store(processing);
                    if (processing)
                        activity->processed.store(completed + files);
                    else
                        activity->discovered.store(completed + files);
                };
                out.roots.append(buildScan(root, scanIgnore, gen, cancel, kindOverrides, {}, skip, onWork));
                completed += out.roots.last().rows.size();
            }
            int checked = 0;
            for (const auto& row : extraRows) {
                if (cancel->isCancelled())
                    break;
                if (!personal.isEmpty()) {
                    if (++checked % 128 == 0)
                        QThread::msleep(1);
                    if (personal.excludes(row.path))
                        continue;
                    const QString resolved = QFileInfo(row.path).canonicalFilePath();
                    if (personal.excludes(resolved))
                        continue;
                }
                out.extraRows.append(row);
            }
            return out;
        }));
}

void VaultScanner::scanRoot(const QString& root, const QStringList& scanIgnore)
{
    if (m_scanning) {
        m_pending.append(qMakePair(root, scanIgnore)); // buffered, never dropped
        return;
    }
    m_scanning = true;
    const quint64 gen = m_asyncGeneration = nextGeneration();
    m_cancel = std::make_shared<VaultKit::CancellationToken>();
    auto cancel = m_cancel;

    const auto activity = beginActivity({root});
    const auto personal = m_index ? m_index->personalPaths() : VaultLocation::PersonalPaths{};
    auto onWork = [activity](bool processing, int files) {
        activity->processing.store(processing);
        if (processing)
            activity->processed.store(files);
        else
            activity->discovered.store(files);
    };

    auto* watcher = new QFutureWatcher<RawResult>(this);
    connect(watcher, &QFutureWatcher<RawResult>::finished, this, [this, watcher]() {
        const RawResult res = watcher->result();
        watcher->deleteLater();
        applyResult(res);
    });
    watcher->setFuture(QtConcurrent::run(&m_scanPool, [root, scanIgnore, gen, cancel, personal, onWork]() {
        return VaultScanner::buildScan(root, scanIgnore, gen, cancel, {}, {}, personal, onWork);
    }));
}

void VaultScanner::cancel()
{
    if (m_cancel)
        m_cancel->cancel();
}

void VaultScanner::invalidate()
{
    cancel();
    nextGeneration();
    m_scanning = false;
    m_activityTimer.stop();
    m_activity.reset();
    m_deferredApplyKind = DeferredApplyKind::None;
    m_deferredResult = RawResult();
    m_deferredPublishResults.clear();
    m_deferredPublishExtraRows.clear();
    m_pending.clear();
}
