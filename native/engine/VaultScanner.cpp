#include "VaultScanner.h"
#include "VaultBookStateMigrator.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QVariantMap>
#include <QtConcurrent>

VaultScanner::VaultScanner(VaultIndex* index, VaultIdentity* identity, QObject* parent)
    : QObject(parent), m_index(index), m_identity(identity)
{
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
VaultScanner::RawResult VaultScanner::buildScan(
    QString root, QStringList scanIgnore, quint64 generation,
    std::shared_ptr<VaultKit::CancellationToken> cancel,
    const QMap<QString, QString>& kindOverrides,
    const std::function<void(int, int, const QString&)>& onProgress)
{
    RawResult r;
    r.root = root;
    r.generation = generation;

    const VaultKit::CancellationToken* c = cancel.get();
    const QStringList needles = VaultKit::sanitizeIgnoreNeedles(scanIgnore);
    const auto groups = VaultKit::groupByFirstLevelSubdir(
        {root}, VaultKit::allMediaFilters(), c, needles, onProgress);
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

    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (c && c->isCancelled()) {
            r.cancelled = true;
            return r;
        }
        const QString key = it.key();
        const bool loose = key.endsWith(QStringLiteral("::LOOSE"));
        const QString subtree = loose ? root : key;
        const QStringList files = it.value();

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
        const QString ov = kindOverrides.value(normPath(subtree));
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
            const VaultKit::MediaKind fk = VaultKit::kindForFile(f);
            if (fk == VaultKit::MediaKind::Unknown)
                continue;
            if (fk != shelveKind) {
                ++leftoverCount; // other-kind file — named-but-unshelved (leftover line)
                continue;
            }

            const QFileInfo fi(f);
            ++shelvedCount;
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
            row.kind = effectiveKindName; // the user's chip choice wins the shelf
            row.path = f;
            row.realName = fi.fileName();
            QString disp = VaultKit::cleanMediaFolderTitle(fi.completeBaseName());
            row.displayTitle = disp.isEmpty() ? fi.completeBaseName() : disp;
            row.subfolder = rel;
            row.size = ff.size;
            row.mtimeMs = ff.mtimeMs;
            row.format = fi.suffix().toLower();
            r.rows.append(row);
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

    m_scanning = false;

    // applyResult only DELIVERS the candidate census for the confirmation card
    // (Slice 11). Publication is a separate, confirm-triggered step (publishConfirmed
    // → applyPublish) that aggregates ALL confirmed roots — so a single root's census
    // never reaches VaultIndex::publish() alone, where the whole-index replace would
    // wipe the other roots.
    emit scanFinished(result.root, result.sliceModel, result.cancelled);

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

    m_scanning = false;

    // A cancelled census in the set aborts the WHOLE publish — no partial truth; the
    // previous index generation stands (VaultIndex::publish is atomic regardless).
    for (const RawResult& r : results) {
        if (r.cancelled)
            return;
    }

    // Aggregate every confirmed root's facts + rows, reconcile identity ONCE, assign
    // ids, then publish the UNION in a single transactional replace. A missing root is
    // represented by its last indexed rows before reconciliation: it is present-but-away,
    // not an empty census. That prevents an unrelated available file with the same
    // size/mtime from borrowing an absent drive's canonical identity.
    QList<VaultIdentity::FileFacts> allFacts;
    QList<VaultIndex::FileRow> allRows;
    for (const RawResult& r : results) {
        if (QDir(r.root).exists()) {
            allFacts.append(r.facts);
            allRows.append(r.rows);
            continue;
        }

        const QList<VaultIndex::FileRow> oldRows = m_index->rowsForRoot(r.root);
        for (VaultIndex::FileRow row : oldRows) {
            row.away = true;
            allRows.append(row);
            allFacts.append({row.path, row.size, row.mtimeMs});
        }
    }
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

    // Slice 18: fold the synthetic downloads root's derived rows into the same
    // UNION. Their ids are assigned here via idForFile (same path as scanned rows),
    // so a file reachable via BOTH a user root and the downloads root dedupes
    // automatically — the index's INSERT OR REPLACE on file id keeps ONE row.
    for (VaultIndex::FileRow row : extraRows) {
        if (row.id.isEmpty())
            row.id = m_identity->idForFile(row.path, row.size, row.mtimeMs);
        allRows.append(row);
    }

    // Emit indexPublished ONLY on a successful publish — a failed/rolled-back publish
    // must NOT tell the shelves "new truth landed"; the previous generation is intact.
    if (m_index->publish(allRows))
        emit indexPublished(QString(), m_index->itemCount());
}

void VaultScanner::publishConfirmed(const QStringList& confirmedRoots,
                                    const QStringList& scanIgnore,
                                    const QMap<QString, QString>& kindOverrides,
                                    const QList<VaultIndex::FileRow>& extraRows)
{
    // Supersede any in-flight scan/publish, then census EVERY confirmed root fresh
    // off-thread: the index is a rebuildable product, so a confirm rebuilds the whole
    // Vault from all confirmed roots and publishes their union atomically (decision 4 +
    // Preflight §3: preserve multi-root atomicity, never a per-root replace).
    m_scanning = true;
    const quint64 gen = nextGeneration();
    m_cancel = std::make_shared<VaultKit::CancellationToken>();
    auto cancel = m_cancel;

    auto* watcher = new QFutureWatcher<QList<RawResult>>(this);
    connect(watcher, &QFutureWatcher<QList<RawResult>>::finished, this,
        [this, watcher, gen, extraRows]() {
            const QList<RawResult> results = watcher->result();
            watcher->deleteLater();
            applyPublish(results, gen, extraRows);
        });
    watcher->setFuture(QtConcurrent::run(
        [confirmedRoots, scanIgnore, gen, cancel, kindOverrides]() {
            QList<RawResult> out;
            for (const QString& root : confirmedRoots)
                out.append(buildScan(root, scanIgnore, gen, cancel, kindOverrides, {}));
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
    const quint64 gen = nextGeneration();
    m_cancel = std::make_shared<VaultKit::CancellationToken>();
    auto cancel = m_cancel;

    // Marshal off-thread census progress back to the GUI thread → the scan pill. Late
    // progress from a superseded generation is dropped (a newer scan/publish took over).
    VaultScanner* self = this;
    auto onProgress = [self, root, gen](int done, int total, const QString& name) {
        const QString nameCopy = name;
        QMetaObject::invokeMethod(
            self,
            [self, root, done, total, nameCopy, gen]() {
                if (gen != self->m_generation)
                    return;
                emit self->progress(root, done, total, nameCopy);
            },
            Qt::QueuedConnection);
    };

    auto* watcher = new QFutureWatcher<RawResult>(this);
    connect(watcher, &QFutureWatcher<RawResult>::finished, this, [this, watcher]() {
        const RawResult res = watcher->result();
        watcher->deleteLater();
        applyResult(res);
    });
    watcher->setFuture(QtConcurrent::run([root, scanIgnore, gen, cancel, onProgress]() {
        return VaultScanner::buildScan(root, scanIgnore, gen, cancel, {}, onProgress);
    }));
}

void VaultScanner::cancel()
{
    if (m_cancel)
        m_cancel->cancel();
}
