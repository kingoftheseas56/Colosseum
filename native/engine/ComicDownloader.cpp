#include "ComicDownloader.h"
#include "DownloadFileOps.h"

#include "ComicDlsParse.h"
#include "engine/ComicCoverId.h"
#include "engine/ComicPackLabels.h"   // parsePackLabel() — demux volume label parser (Slice 1)
#include "torrent/ComicTorrents.h"
#include "torrent/ComicTorrentMagnet.h"

#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>

namespace {

// GetComics fronts with Cloudflare-adjacent checks: a browser UA + the site
// Referer is what the live-proven curl path used (2026-07-04).
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0 Safari/537.36";
constexpr const char* kReferer = "https://getcomics.org/";

constexpr qint64 kDiskSpaceSafetyBytes = 50LL * 1024 * 1024;
constexpr int    kProgressThrottleMs    = 500;
constexpr qint64 kProgressThrottleBytes = 512LL * 1024;
constexpr int    kMaxAttempts           = 3;   // per URL, 2/4/8s backoff

int attemptDelayMs(int attempt)
{
    switch (attempt) {
    case 0:  return 0;
    case 1:  return 2000;
    case 2:  return 4000;
    default: return 8000;
    }
}

// filesystem-safe path segment (MangaDownloader's convention)
QString safeSeg(const QString& v)
{
    QString out;
    out.reserve(v.size());
    for (const QChar c : v) {
        if (c.isLetterOrNumber() || c == QChar('.') || c == QChar('_') || c == QChar('-')
            || c == QChar(' '))
            out.append(c);
        else
            out.append(QChar('_'));
    }
    out = out.trimmed();
    while (out.endsWith(QChar('.'))) out.chop(1);
    if (out.isEmpty()) out = QStringLiteral("item");
    return out.left(80);
}

QString hash10(const QString& v)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

bool isImageFile(const QString& name)
{
    static const QSet<QString> kExts = { QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("gif"),
        QStringLiteral("avif"), QStringLiteral("bmp") };
    return kExts.contains(QFileInfo(name).suffix().toLower());
}

// Order-independent set equality between a probe result's entry names and a
// row's `files` list -- the gate legacy migration uses to accept a freshly
// packed (or a crash-recovery leftover) canonical. writeImagesAtomic() writes
// entries in `files` order, but probe() re-sorts by a numeric collator, so a
// positional comparison would spuriously fail an otherwise-correct archive
// whose pages are numbered out of collation order; and a plain size check
// would accept an archive that dropped one page but gained a duplicate. Names
// as a set (plus a size match to reject dupes/extras) is the honest check.
bool probeEntriesMatchFiles(const MangaTankoban::CbzProbeResult& probe, const QStringList& files)
{
    if (probe.entries.size() != files.size()) return false;
    QSet<QString> got;
    // Normalize separators on BOTH sides: writeImagesAtomic() stores an entry
    // name as QDir::fromNativeSeparators(relative) (CbzArchive.cpp), so a
    // legacy `files` entry carrying a native backslash subpath would otherwise
    // never match its own packed entry.
    for (const auto& e : probe.entries) got.insert(QDir::fromNativeSeparators(e.name));
    if (got.size() != files.size()) return false;   // a duplicate name in the archive
    for (const QString& n : files)
        if (!got.contains(QDir::fromNativeSeparators(n))) return false;
    return true;
}

// Stronger than probe()'s {first,middle,last} sniff sample: for the one-shot,
// IRREVERSIBLE migration of an irreplaceable comic, verify EVERY page. Because
// writeImagesAtomic() stores uncompressed (MZ_NO_COMPRESSION), each packed
// entry's stored uncompressed size must equal its loose source file's size
// exactly -- a free check (the central directory already carries the size, no
// inflation) that catches a truncated or mis-sourced page OUTSIDE probe()'s
// sample window before Pass 2 can ever delete the loose original. Requires the
// loose `dir` to still exist (true in both Pass 1 and Pass 2 by construction).
bool archiveMatchesSourceExactly(const MangaTankoban::CbzProbeResult& probe,
                                 const QString& dir, const QStringList& files)
{
    if (!probeEntriesMatchFiles(probe, files)) return false;
    QHash<QString, quint64> sizeByName;
    for (const auto& e : probe.entries)
        sizeByName.insert(QDir::fromNativeSeparators(e.name), e.uncompressedBytes);
    for (const QString& name : files) {
        const auto it = sizeByName.constFind(QDir::fromNativeSeparators(name));
        if (it == sizeByName.constEnd()) return false;
        const qint64 srcSize = QFileInfo(QDir(dir).absoluteFilePath(name)).size();
        if (static_cast<qint64>(it.value()) != srcSize) return false;
    }
    return true;
}

bool looksLikeHtml(const QByteArray& firstChunk, const QString& contentType)
{
    if (contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)) return true;
    const QByteArray head = firstChunk.left(512).trimmed().toLower();
    return head.startsWith("<!doctype") || head.startsWith("<html");
}

QString sevenZipPath()
{
    // Harness-only override: an explicitly present value (including empty)
    // makes extractor-start/error paths deterministic without touching the
    // machine's installed tools.
    const QByteArray overridePath = qgetenv("COLOSSEUM_COMIC_7ZIP_PATH");
    if (!overridePath.isNull()) return QString::fromLocal8Bit(overridePath);
#ifdef Q_OS_WIN
    const QString bundled = QStringLiteral("C:/Program Files/7-Zip/7z.exe");
    if (QFileInfo::exists(bundled)) return bundled;
#endif
    QString executable = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (executable.isEmpty()) executable = QStandardPaths::findExecutable(QStringLiteral("7zz"));
    return executable;
}

QString bsdtarPath()
{
    const QByteArray overridePath = qgetenv("COLOSSEUM_COMIC_BSDTAR_PATH");
    if (!overridePath.isNull()) return QString::fromLocal8Bit(overridePath);
#ifdef Q_OS_WIN
    const QString sys = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sys)) return sys;
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
#else
    // ZIP/CBZ and RAR extraction requires libarchive semantics. GNU tar may
    // be named `tar` on Linux but cannot satisfy this contract.
    return QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
#endif
}

// ── COLOSSEUM_COMIC_PACK_DLTEST fixture table (Task 11) ─────────────────────
// The canonical edition identity for each fixture id the seed harness
// (tests/comic_torrent_pack_seed_harness.cpp) bakes into its ONE loopback
// torrent. "compendium-v01"/"v02" resolve via the exact-title tier against
// "Compendiums/Invincible Compendium vNN.cbz"; "issue-set" resolves via the
// collected-issue-set tier against "Issues/Guardians #1..3.cbz" — a
// DIFFERENT series name than the Compendiums on purpose, so the issue tier's
// series-agreement check can never accidentally match a Compendium archive.
struct DltestPackFixture {
    QString seriesId;
    QString seriesTitle;
    QString editionTitle;
    QString isbn;
    QString collects;
};

DltestPackFixture dltestPackFixture(const QString& fixtureId)
{
    if (fixtureId == QStringLiteral("compendium-v01")) {
        return { QStringLiteral("gc:series:dltest-invincible"), QStringLiteral("Invincible"),
                 QStringLiteral("Invincible Compendium v01"), QString(), QString() };
    }
    if (fixtureId == QStringLiteral("compendium-v02")) {
        return { QStringLiteral("gc:series:dltest-invincible"), QStringLiteral("Invincible"),
                 QStringLiteral("Invincible Compendium v02"), QString(), QString() };
    }
    if (fixtureId == QStringLiteral("issue-set")) {
        return { QStringLiteral("gc:series:dltest-guardians"), QStringLiteral("Guardians"),
                 QStringLiteral("Guardians Fixture Set"), QString(), QStringLiteral("#1-3") };
    }
    return {};
}

// editionId for a DLTEST fixture — deterministic so a "restart" launch
// derives the SAME id a prior "single" launch used on the same AppData root.
QString dltestPackEditionId(const QString& fixtureId)
{
    return QStringLiteral("comicpack-dltest-") + fixtureId;
}

} // namespace

ComicDownloader::ComicDownloader(QNetworkAccessManager* nam, QObject* parent)
    : ComicDownloader(nam, nullptr, nullptr, parent)
{
}

ComicDownloader::ComicDownloader(QNetworkAccessManager* nam, QNetworkAccessManager* searchNam,
                                 TorrentEngine* torrentEngine, QObject* parent)
    : QObject(parent), m_nam(nam)
{
    loadIndex();
    loadPacks();   // pack demux (Slice 2/3): resume any in-flight pack manifests
    // Slice 3: defer resume to the event loop — no extract subprocess inside
    // the constructor (Qt needs the event loop running for QProcess signals +
    // the queue to drain). A QTimer::singleShot(0, ...) lands on the first
    // spin of the caller's loop (QML's or the harness's QCoreApplication).
    if (!m_packs.isEmpty())
        QTimer::singleShot(0, this, [this]() { resumeIncompletePacks(); });
    if (searchNam && torrentEngine) {
        m_torrents = new ComicTorrents(searchNam, torrentEngine, this);
        connect(m_torrents, &ComicTorrents::progress, this, &ComicDownloader::progress);
        connect(m_torrents, &ComicTorrents::failed, this, &ComicDownloader::failed);
        connect(m_torrents, &ComicTorrents::archiveReady, this,
                [this](const QString& issueId, const QString& seriesId,
                       const QString& seriesTitle, const QString& issueLabel,
                       const QString& archivePath) {
            ingestLocalArchive(issueId, seriesId, seriesTitle, issueLabel, archivePath);
        });
        connect(m_torrents, &ComicTorrents::sourcesUpdated,
                this, &ComicDownloader::torrentSourcesUpdated);
        connect(m_torrents, &ComicTorrents::sourceSearchFailed,
                this, &ComicDownloader::torrentSourceSearchFailed);
        connect(m_torrents, &ComicTorrents::archiveSelectionRequired,
                this, &ComicDownloader::torrentArchiveSelectionRequired);
        connect(m_torrents, &ComicTorrents::archiveSelected,
                this, &ComicDownloader::torrentArchiveSelected);
        connect(m_torrents, &ComicTorrents::resolving,
                this, &ComicDownloader::resolving);
        connect(m_torrents, &ComicTorrents::combinedArchiveConfirmationRequired,
                this, &ComicDownloader::torrentCombinedArchiveConfirmationRequired);
        connect(m_torrents, &ComicTorrents::incompleteIssueSetDetected,
                this, &ComicDownloader::torrentIncompleteIssueSetDetected);
    }
}

ComicDownloader::~ComicDownloader()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    if (m_active) {
        // A background copy/pack worker may still be reading extractTmp/
        // archivePath (InFlight::packing) -- it runs on the global
        // QThreadPool, independent of `this`, and keeps running to
        // completion even after this destructor returns (an accepted
        // consequence: it may leave a finished, unindexed canonical on disk,
        // which adoptExistingCanonicalIfValid() reclaims on a later launch).
        // Deleting those files here would sabotage that still-running worker
        // AND destroy the exact source Task 4 exists to protect -- on
        // ordinary app quit, not just a hard process kill.
        if (!m_active->packing) {
            closeAndDeletePart(*m_active);
            cleanupExtract(*m_active);
        }
        delete m_active;
        m_active = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// disk + index
// ─────────────────────────────────────────────────────────────────────────────

QString ComicDownloader::baseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/comics");
}

QString ComicDownloader::issueDir(const QString& seriesId, const QString& label,
                                  const QString& id) const
{
    return baseDir() + QChar('/') + safeSeg(seriesId) + QChar('/')
           + safeSeg(label) + QChar('-') + hash10(id);
}

// The canonical archive location (Task 4) -- a FILE sibling to issueDir()'s
// directory path, never colliding with it (a file named "X.cbz" can never
// collide with a directory named "X"), so Task 7's migration can have both
// `archive` and `dir` populated for one boot without a path collision.
QString ComicDownloader::issueArchivePath(const QString& seriesId, const QString& label,
                                          const QString& id) const
{
    return issueDir(seriesId, label, id) + QStringLiteral(".cbz");
}

bool ComicDownloader::adoptExistingCanonicalIfValid(const QString& id, const QString& seriesId,
                                                    const QString& seriesTitle,
                                                    const QString& label)
{
    const QString canonical = issueArchivePath(seriesId, label, id);
    QString error;
    const MangaTankoban::CbzProbeResult probe = MangaTankoban::CbzArchive::probe(canonical, &error);
    if (!probe.nativelyReadable) {
        qWarning() << "[ComicDownloader] adoption: stale leftover canonical invalid, discarding"
                   << canonical << error;
        QFile::remove(canonical);
        return false;
    }

    Entry e;
    e.seriesId    = seriesId;
    e.seriesTitle = seriesTitle;
    e.label       = label;
    e.archive     = canonical;
    for (const auto& pageEntry : probe.entries) e.files.append(pageEntry.name);
    e.bytes       = QFileInfo(canonical).size();
    e.addedAt     = QDateTime::currentMSecsSinceEpoch();
    // Pack demux (Slice 2/3): a resumed pack child adopts with the role/order
    // its manifest recorded (the orphaned canonical has no parsed identity of
    // its own). Look it up; ordinary issues have no manifest entry.
    for (const PackManifest& m : std::as_const(m_packs)) {
        for (const PackChild& c : m.children) {
            if (c.id == id) { e.packRole = c.role; e.packOrder = c.order; break; }
        }
    }
    m_index.insert(id, e);
    saveIndex();
    qInfo() << "[ComicDownloader] adopted an interrupted prior attempt id=" << id
            << "pages=" << e.files.size() << "archive=" << canonical;
    maybeReclaimPack(id);   // a resumed child completing may complete the pack too
    return true;
}

void ComicDownloader::loadIndex()
{
    QFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    // Close the read handle BEFORE the migration below can saveIndex() -- on
    // Windows a still-open ReadOnly handle (no FILE_SHARE_DELETE) blocks
    // QSaveFile::commit()'s atomic rename over index.json, so the migration
    // would compute correctly and then silently fail to persist (Task 7).
    f.close();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.seriesId    = o.value(QStringLiteral("seriesId")).toString();
        e.seriesTitle = o.value(QStringLiteral("seriesTitle")).toString();
        e.label       = o.value(QStringLiteral("label")).toString();
        e.dir         = o.value(QStringLiteral("dir")).toString();
        e.archive     = o.value(QStringLiteral("archive")).toString();
        e.bytes       = static_cast<qint64>(o.value(QStringLiteral("bytes")).toDouble());
        e.addedAt     = static_cast<qint64>(o.value(QStringLiteral("addedAt")).toDouble());
        // Pack-demux fields are OPTIONAL (Slice 1): absent on every legacy row,
        // so a missing key leaves the Entry defaults (packRole empty, packOrder
        // -1) and the row behaves exactly as an ordinary single issue.
        e.packRole    = o.value(QStringLiteral("packRole")).toString();
        e.packOrder   = o.value(QStringLiteral("packOrder")).toInt(-1);
        for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray())
            e.files.append(v.toString());
        for (const QJsonValue& v : o.value(QStringLiteral("groups")).toArray())
            e.groups.append(v.toInt());
        // Drop stale entries whose backing storage was removed outside the app --
        // an archive row checks the archive FILE, a legacy row checks the dir.
        // (Task 2: the old `!e.dir.isEmpty()`-only condition silently dropped
        // every archive-shaped row on the very first restart.)
        const bool archiveOk = e.usesArchive() && QFileInfo(e.archive).isFile() && !e.files.isEmpty();
        const bool dirOk = !e.dir.isEmpty() && QDir(e.dir).exists() && !e.files.isEmpty();
        if (archiveOk) {
            m_index.insert(it.key(), e);
        } else if (dirOk) {
            // The row survives on its dir, but its `archive` field (if any)
            // names a file that's gone -- demote back to a plain legacy row so
            // every archive-first reader (isDownloaded, and Task 3/4's
            // localPages/downloadedIssues) agrees on which storage is real,
            // instead of `usesArchive()` staying true for a dead path.
            e.archive.clear();
            m_index.insert(it.key(), e);
        }
    }

    // One-shot, synchronous legacy migration (Task 7). Runs after every row is
    // loaded/demoted above, so it sees each row's final on-disk shape -- and
    // saves the index itself if anything changed.
    migrateLegacyComicsInPlace();
}

// ── Task 7: boot-time legacy CBZ-in-place migration ─────────────────────────
// Repair-before-prune, two-boot. See the header comment for the full contract.
// The two passes are disjoint by a row's AS-LOADED shape: a Pass-1 candidate
// arrives with NO archive; a Pass-2 candidate arrives with a real archive AND
// a leftover dir. So a single pass over the loaded rows, branching on that
// shape, correctly does Pass 1 OR Pass 2 for each -- a row Pass 1 migrates
// this boot (now archive+dir) is not re-examined for Pass 2 until the NEXT
// boot loads it in that shape, which is exactly the one-boot delay the design
// wants.
//
// ASSUMPTION (documented, not enforced): loadIndex() runs exactly ONCE per
// process (only the constructor calls it today). The "one boot delay" between
// packing and reclaiming is therefore a one-PROCESS delay. If a future
// "refresh library" ever re-runs loadIndex() in the same process, Pass 1's
// output would be seen by Pass 2 in that same process, collapsing the delay --
// still not data loss (Pass 2 independently re-verifies the archive page-for-
// page against the loose source before deleting anything), but it removes the
// human eyes-on window. A re-run path must gate migration behind a
// process-scoped guard before shipping.
void ComicDownloader::migrateLegacyComicsInPlace()
{
    bool changed = false;
    const QList<QString> ids = m_index.keys();   // stable snapshot; no keys added/removed here
    for (const QString& id : ids) {
        Entry& e = m_index[id];

        // ── Pass 2: a row already migrated on a PRIOR boot -- archive is a
        //    real file and a leftover legacy dir still sits beside it. Only
        //    fires when the dir actually EXISTS (there are loose files to
        //    reclaim); a stale/empty dir string on a valid archive row is
        //    inert and left alone. ──
        if (e.usesArchive() && QFileInfo(e.archive).isFile()
            && !e.dir.isEmpty() && QDir(e.dir).exists()) {
            QString err;
            const MangaTankoban::CbzProbeResult probe =
                MangaTankoban::CbzArchive::probe(e.archive, &err);
            // Independent re-verification of the delete's precondition -- and
            // deliberately STRICTER than a bare openable check: the archive
            // must hold exactly this row's pages at exactly the loose source's
            // byte sizes. Pass 2 guards the one irreversible operation in the
            // whole migration, so it must never re-verify LESS than Pass 1 did.
            if (probe.nativelyReadable
                && archiveMatchesSourceExactly(probe, e.dir, e.files)) {
                // Remove the redundant loose files FIRST, THEN clear `dir`.
                // Safe ordering here (unlike the download path's save-then-
                // delete): the archive is already the durable, INDEXED copy
                // from a prior boot and was just re-verified page-for-page, so
                // the loose dir is pure redundancy. Clear `dir` ONLY if the
                // removal fully succeeded -- a partially-removed dir keeps
                // `dir` set and is retried next boot, never silently orphaned.
                const QString legacyDir = e.dir;
                if (QDir(legacyDir).removeRecursively()) {
                    e.dir.clear();
                    changed = true;
                    qInfo() << "[comics] legacy migration pass 2: reclaimed loose dir"
                            << legacyDir << "id=" << id;
                } else {
                    qWarning() << "[comics] legacy migration pass 2: could not remove loose "
                                  "dir, leaving `dir` set to retry next boot" << legacyDir
                               << "id=" << id;
                }
            } else {
                // Present but not decodable / not matching -- demote back to
                // the dir so the next boot's Pass 1 re-packs from the loose
                // source. Never deletes anything.
                qWarning() << "[comics] legacy migration: archive present but unreadable/"
                              "mismatched, demoting to dir for re-pack" << e.archive << err
                           << "id=" << id;
                e.archive.clear();
                changed = true;
            }
            continue;
        }

        // ── Pass 1: a legacy dir-only row -- pack into the canonical CBZ this
        //    boot, set `archive`, and LEAVE `dir` alone (reclaimed a boot
        //    later by Pass 2). ──
        if (!e.usesArchive() && !e.dir.isEmpty() && !e.files.isEmpty()) {
            // Every listed page must be present, or migrate not at all --
            // untouched, warned, nothing packed, nothing deleted. The row
            // still works off its dir; a later run migrates it once whole.
            // Sum the source bytes in the same sweep for the space preflight.
            bool allPresent = true;
            qint64 sourceBytes = 0;
            for (const QString& name : e.files) {
                const QFileInfo pageInfo(QDir(e.dir).absoluteFilePath(name));
                if (name.isEmpty() || !pageInfo.isFile()) {
                    allPresent = false;
                    break;
                }
                sourceBytes += pageInfo.size();
            }
            if (!allPresent) {
                qWarning() << "[comics] legacy migration: a listed page is missing, "
                              "leaving the row untouched" << "id=" << id << e.dir;
                continue;
            }

            const QString canonical = issueArchivePath(e.seriesId, e.label, id);
            QString err;
            bool haveValidCanonical = false;

            // Crash recovery: a canonical from an interrupted prior migration
            // already sits on disk. Adopt it (no repack) if it round-trips AND
            // holds exactly this row's pages at exactly the loose byte sizes;
            // otherwise discard + re-pack.
            if (QFileInfo(canonical).isFile()) {
                const MangaTankoban::CbzProbeResult probe =
                    MangaTankoban::CbzArchive::probe(canonical, &err);
                if (probe.nativelyReadable
                    && archiveMatchesSourceExactly(probe, e.dir, e.files)) {
                    haveValidCanonical = true;
                    qInfo() << "[comics] legacy migration: adopted an interrupted-prior canonical "
                               "(no repack)" << canonical << "id=" << id;
                } else {
                    qWarning() << "[comics] legacy migration: stale/partial canonical, removing "
                                  "and re-packing" << canonical << err << "id=" << id;
                    if (!QFile::remove(canonical)) {
                        qWarning() << "[comics] legacy migration: cannot remove stale canonical, "
                                      "skipping (loose source preserved)" << canonical << "id=" << id;
                        continue;
                    }
                }
            }

            if (!haveValidCanonical) {
                // Space preflight: the pack is uncompressed (a straight copy),
                // and both the loose dir AND the new archive coexist until a
                // LATER boot reclaims the loose files -- so refuse rather than
                // risk filling the volume (a disk-full mid-write also fails the
                // saveIndex() commit). Skip-with-warning, never partial-write.
                const qint64 avail =
                    QStorageInfo(QFileInfo(canonical).absolutePath()).bytesAvailable();
                if (avail >= 0 && avail < sourceBytes + kDiskSpaceSafetyBytes) {
                    qWarning() << "[comics] legacy migration: insufficient free space to pack, "
                                  "skipping (need" << sourceBytes << "have" << avail << ") id=" << id;
                    continue;
                }
                // A multi-GB legacy comic packs synchronously here, before the
                // UI is up -- log start/finish with the byte count so a
                // several-second startup pause is never mistaken for a hang.
                qInfo() << "[comics] legacy migration: packing" << e.files.size() << "pages ("
                        << sourceBytes << "bytes) id=" << id
                        << "-- one-time step, app start may pause";
                if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, e.dir, e.files, &err)) {
                    qWarning() << "[comics] legacy migration: pack failed, leaving the row "
                                  "untouched (loose source preserved)" << "id=" << id << err;
                    continue;
                }
                const MangaTankoban::CbzProbeResult probe =
                    MangaTankoban::CbzArchive::probe(canonical, &err);
                if (!probe.nativelyReadable
                    || !archiveMatchesSourceExactly(probe, e.dir, e.files)) {
                    qWarning() << "[comics] legacy migration: packed archive failed page-for-page "
                                  "verify, discarding (loose source preserved)" << "id=" << id << err;
                    QFile::remove(canonical);
                    continue;
                }
            }

            // Persist `archive`; LEAVE `dir` set (both populated) for exactly
            // one boot. `files` stays the legacy page names -- writeImagesAtomic
            // wrote under exactly those names, so readEntry() resolves them, and
            // probe()'s collator sort must never leak into the page list (the
            // Task 5 order-preservation lesson).
            e.archive = canonical;
            e.bytes   = QFileInfo(canonical).size();
            changed   = true;
            qInfo() << "[comics] legacy migration pass 1: packed id=" << id
                    << "pages=" << e.files.size() << "archive=" << canonical
                    << "(dir left for reclaim next boot)";
        }
    }

    if (changed) saveIndex();
}

void ComicDownloader::saveIndex() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("seriesId")]    = it.value().seriesId;
        o[QStringLiteral("seriesTitle")] = it.value().seriesTitle;
        o[QStringLiteral("label")]       = it.value().label;
        o[QStringLiteral("dir")]         = it.value().dir;
        o[QStringLiteral("archive")]     = it.value().archive;
        o[QStringLiteral("bytes")]       = static_cast<double>(it.value().bytes);
        o[QStringLiteral("addedAt")]     = static_cast<double>(it.value().addedAt);
        QJsonArray files;
        for (const QString& n : it.value().files) files.append(n);
        o[QStringLiteral("files")] = files;
        QJsonArray groups;
        for (int g : it.value().groups) groups.append(g);
        o[QStringLiteral("groups")] = groups;
        // Pack-demux fields are written ONLY when set (Slice 1), so a legacy
        // single-issue row saves byte-identically to its pre-demux form — no
        // spurious keys, no index churn for unchanged rows.
        if (!it.value().packRole.isEmpty())
            o[QStringLiteral("packRole")] = it.value().packRole;
        if (it.value().packOrder != -1)
            o[QStringLiteral("packOrder")] = it.value().packOrder;
        root[it.key()] = o;
    }
    // Atomic write (mirrors MangaDownloader.cpp) so a crash or a failed commit
    // (e.g. the destination locked) never corrupts the previously-saved file.
    QSaveFile f(baseDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning() << "[comics] saveIndex commit failed -- previous index.json left intact";
}

// ─────────────────────────────────────────────────────────────────────────────
// QML entry points
// ─────────────────────────────────────────────────────────────────────────────

QVariantList ComicDownloader::localPages(const QString& issueId) const
{
    QVariantList out;
    auto it = m_index.constFind(issueId.trimmed());
    if (it == m_index.constEnd()) return out;
    const Entry& e = it.value();

    if (e.usesArchive()) {
        // Guard on the archive FILE once, mirroring the legacy branch's
        // dir.exists() early-out below -- otherwise a runtime-deleted archive
        // returns N pages that all resolve to MissingFile reader-side instead
        // of zero pages here, and the reader would report "ready" for a comic
        // that opens to nothing.
        if (!QFileInfo(e.archive).isFile()) return out;
        // Pure in-memory map from the stored files/groups lists -- no zip
        // open per call, since Task 1's probe() already verified every entry
        // decodable at ingest time and re-checking here on every navigation
        // call would defeat the point.
        for (int i = 0; i < e.files.size(); ++i) {
            QVariantMap m;
            m[QStringLiteral("index")]   = i;
            m[QStringLiteral("archive")] = e.archive;
            m[QStringLiteral("entry")]   = e.files.at(i);
            // `url` too (not just archive/entry): ComicReaderCore::parsePages()
            // checks archive/entry FIRST and only falls back to url, so this
            // costs the reader nothing -- but qml/ComicSeriesPage.qml's
            // firstLocalUrl() reads lp[0].url directly for the series-page
            // thumbnail and has no archive/entry fallback of its own.
            m[QStringLiteral("url")] = QStringLiteral("image://comiccover/")
                + Colosseum::buildComicCoverId(e.archive, e.files.at(i));
            m[QStringLiteral("group")] = e.groups.value(i, -1);
            out.append(m);
        }
        return out;
    }

    const QDir dir(e.dir);
    if (!dir.exists()) return out;
    int idx = 0;
    for (int i = 0; i < e.files.size(); ++i) {
        const QString abs = dir.absoluteFilePath(e.files.at(i));
        if (!QFileInfo::exists(abs)) continue;
        QVariantMap m;
        m[QStringLiteral("index")] = idx++;
        m[QStringLiteral("url")]   = QUrl::fromLocalFile(abs).toString();
        // Existing GetComics/single-archive/torrent-issue rows leave `groups`
        // empty, so QList::value() falls back to -1 — byte-for-byte the same
        // as before this field existed. Assembled editions (Task 7) populate
        // real per-page group values (see ingestAssembledEdition).
        m[QStringLiteral("group")] = e.groups.value(i, -1);
        out.append(m);
    }
    return out;
}

bool ComicDownloader::isDownloaded(const QString& issueId) const
{
    auto it = m_index.constFind(issueId.trimmed());
    if (it == m_index.constEnd()) return false;
    const Entry& e = it.value();
    return e.usesArchive() ? QFileInfo(e.archive).isFile() : QDir(e.dir).exists();
}

void ComicDownloader::ingestLocalArchive(const QString& issueIdIn, const QString& seriesId,
                                          const QString& seriesTitle, const QString& issueLabel,
                                          const QString& archivePathIn)
{
    const QString id = issueIdIn.trimmed();
    const QString archivePath = QDir::cleanPath(archivePathIn);
    const QFileInfo archive(archivePath);
    const QString absSource = archive.absoluteFilePath();
    static const QSet<QString> allowed{
        QStringLiteral("cbr"), QStringLiteral("cbz"),
        QStringLiteral("cb7"), QStringLiteral("cbt")
    };
    if (id.isEmpty()) {
        emit failed(id, QStringLiteral("empty issue id"));
        return;
    }
    if (!archive.isFile() || !allowed.contains(archive.suffix().toLower())) {
        emit failed(id, QStringLiteral("local comic archive missing or unsupported"));
        return;
    }
    if (isDownloaded(id)) {
        // Redundant re-ingest of an already-downloaded id -- the source this
        // call brought is surplus, so honor the delete-on-success contract...
        // UNLESS the caller browsed to a live library archive itself (e.g.
        // re-importing the canonical after an index loss): deleting that would
        // destroy the comic the index still points at (Task 6 review).
        if (!isLiveLibraryArchive(absSource)) QFile::remove(absSource);
        emit finished(id);
        return;
    }
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;

    // Crash-recovery adoption (Task 6): the same check downloadIssue() runs.
    // ingestLocalArchive() is the single-archive ingest boundary every torrent
    // path funnels through too, so an interrupted prior attempt for this id can
    // leave a valid canonical with no index row (the app was killed after the
    // move/repack but before saveIndex). Adopt it directly rather than
    // re-ingesting -- and, critically for this path, the ownership-transfer
    // contract means the caller's source may already be gone (a same-volume
    // fast-path rename consumed it last time), so re-ingesting isn't always
    // even possible; the orphaned canonical is the surviving copy. On adoption
    // the source this call brought is redundant, so honor the delete-on-success
    // contract by removing it -- again, unless it IS a live library archive
    // (the user re-imported the canonical the adopted row now points at).
    const QString adoptLabel = issueLabel.isEmpty() ? id : issueLabel;
    if (!m_index.contains(id)
        && QFileInfo(issueArchivePath(seriesId, adoptLabel, id)).isFile()
        && adoptExistingCanonicalIfValid(id, seriesId, seriesTitle, adoptLabel)) {
        if (!isLiveLibraryArchive(absSource)) QFile::remove(absSource);
        emit finished(id);
        return;
    }

    InFlight flight;
    flight.id = id;
    flight.seriesId = seriesId;
    flight.seriesTitle = seriesTitle;
    flight.label = adoptLabel;
    flight.archivePath = archive.absoluteFilePath();
    flight.receivedBytes = archive.size();
    flight.expectedBytes = archive.size();
    flight.localArchive = true;

    emit progress(id, static_cast<double>(flight.receivedBytes),
                  static_cast<double>(flight.expectedBytes));
    if (m_active) {
        m_queue.append(std::move(flight));
        return;
    }
    m_active = new InFlight(std::move(flight));
    // Task 6: converge on the SAME two-path ingest onFinished() uses -- a
    // natively-readable imported CBZ moves in with no extraction; a CBR (or an
    // unreadable CBZ) still extracts-then-repacks. Both consume archivePath on
    // success, preserving this function's ownership-transfer contract. Was an
    // unconditional beginExtract().
    ingestArchiveByProbe(*m_active);
}

void ComicDownloader::ingestAssembledEdition(const QString& editionIdIn, const QString& seriesId,
                                             const QString& seriesTitle, const QString& editionLabel,
                                             const QString& stagingDirIn, const QStringList& orderedFiles,
                                             const QList<int>& groups)
{
    const QString id = editionIdIn.trimmed();
    const QString stagingDir = QDir::cleanPath(stagingDirIn);
    if (id.isEmpty()) {
        emit failed(id, QStringLiteral("empty edition id"));
        return;
    }
    if (orderedFiles.isEmpty()) {
        emit failed(id, QStringLiteral("assembled edition has no pages"));
        if (!stagingDir.isEmpty()) QDir(stagingDir).removeRecursively();
        return;
    }
    if (!groups.isEmpty() && groups.size() != orderedFiles.size()) {
        emit failed(id, QStringLiteral("assembled edition group count does not match page count"));
        if (!stagingDir.isEmpty()) QDir(stagingDir).removeRecursively();
        return;
    }
    if (stagingDir.isEmpty() || !QDir(stagingDir).exists()) {
        emit failed(id, QStringLiteral("assembled staging directory missing"));
        return;
    }
    if (isDownloaded(id)) {
        // Already published under this id (re-publish/replay) — the staging
        // dir this call brought is now redundant.
        QDir(stagingDir).removeRecursively();
        emit finished(id);
        return;
    }
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;

    InFlight flight;
    flight.id                   = id;
    flight.seriesId              = seriesId;
    flight.seriesTitle           = seriesTitle;
    flight.label                 = editionLabel.isEmpty() ? id : editionLabel;
    flight.assembledIngest       = true;
    flight.assembledStagingDir   = stagingDir;
    flight.assembledOrderedFiles = orderedFiles;
    flight.assembledGroups       = groups;

    emit progress(id, 0.0, static_cast<double>(orderedFiles.size()));
    // Queue behind the EXISTING single extraction/publication lane — never a
    // second concurrent publisher (design: "ComicDownloader ingest boundary").
    if (m_active) {
        m_queue.append(std::move(flight));
        return;
    }
    m_active = new InFlight(std::move(flight));
    publishAssembledEdition(*m_active);
}

QVariantMap ComicDownloader::statusOf(const QString& issueId) const
{
    const QString id = issueId.trimmed();
    QVariantMap s;
    s[QStringLiteral("done")]  = 0.0;
    s[QStringLiteral("total")] = 0.0;
    if (isDownloaded(id)) {
        s[QStringLiteral("state")] = QStringLiteral("done");
        s[QStringLiteral("done")]  = static_cast<double>(m_index.value(id).bytes);
        s[QStringLiteral("total")] = static_cast<double>(m_index.value(id).bytes);
        return s;
    }
    if (m_active && m_active->id == id) {
        s[QStringLiteral("state")] = m_active->extracting ? QStringLiteral("extracting")
                                                          : QStringLiteral("downloading");
        s[QStringLiteral("done")]  = static_cast<double>(m_active->receivedBytes);
        s[QStringLiteral("total")] = static_cast<double>(
            m_active->extracting ? m_active->receivedBytes : m_active->expectedBytes);
        return s;
    }
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) { s[QStringLiteral("state")] = QStringLiteral("resolving"); return s; }
    for (const InFlight& q : m_queue)
        if (q.id == id) { s[QStringLiteral("state")] = QStringLiteral("queued"); return s; }
    if (m_torrents) {
        const QVariantMap torrent = m_torrents->statusOf(id);
        if (torrent.value(QStringLiteral("state")).toString() != QStringLiteral("none"))
            return torrent;
    }
    s[QStringLiteral("state")] = QStringLiteral("none");
    return s;
}

void ComicDownloader::downloadIssueTorrent(const QString& issueIdIn, const QString& seriesId,
                                            const QString& seriesTitle, const QString& issueLabel,
                                            const QString& query)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || query.trimmed().isEmpty()) {
        emit failed(id, QStringLiteral("empty issue id / torrent query"));
        return;
    }
    if (isDownloaded(id)) {
        emit finished(id);
        return;
    }
    if (!m_torrents) {
        emit failed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    if (m_torrents->contains(id)) return;
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;
    m_torrents->downloadIssue(id, seriesId, seriesTitle, issueLabel, query);
}

void ComicDownloader::searchTorrentSources(const QString& issueIdIn, const QString& seriesTitle,
                                           const QString& editionTitle, const QString& isbn,
                                           const QString& collects, const QString& catalogFormat)
{
    const QString id = issueIdIn.trimmed();
    if (!m_torrents) {
        emit torrentSourceSearchFailed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    m_torrents->searchSources(id, seriesTitle, editionTitle, isbn, collects, catalogFormat);
}

void ComicDownloader::searchTorrentSourcesQuery(const QString& issueIdIn, const QString& query)
{
    const QString id = issueIdIn.trimmed();
    if (!m_torrents) {
        emit torrentSourceSearchFailed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    m_torrents->searchSourcesQuery(id, query);
}

void ComicDownloader::cancelTorrentSourceSearch(const QString& issueIdIn)
{
    if (m_torrents) m_torrents->cancelSourceSearch(issueIdIn.trimmed());
}

void ComicDownloader::chooseTorrentArchive(const QString& issueIdIn, int fileIndex)
{
    if (m_torrents) m_torrents->chooseArchive(issueIdIn.trimmed(), fileIndex);
}

void ComicDownloader::downloadTorrentSource(const QString& issueIdIn, const QString& seriesId,
                                            const QString& seriesTitle, const QString& issueLabel,
                                            const QString& infoHash, const QString& releaseTitle,
                                            const QString& magnetUri)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || infoHash.trimmed().isEmpty()) {
        emit failed(id, QStringLiteral("empty issue id / infoHash"));
        return;
    }
    if (isDownloaded(id)) { emit finished(id); return; }
    if (!m_torrents) {
        emit failed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    if (m_torrents->contains(id)) return;
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;
    // The user has chosen a source — stop browsing and acquire it. The canonical
    // edition title (issueLabel) is the archive picker's matching title; the
    // chosen torrent's releaseTitle is diagnostic-only and never becomes it.
    m_torrents->cancelSourceSearch(id);
    qInfo() << "[ComicDownloader] torrent source chosen" << id << "release=" << releaseTitle;
    m_torrents->downloadInfoHash(id, seriesId, seriesTitle, issueLabel, infoHash,
                                 /*pickerTitle=*/issueLabel, magnetUri);
}

// ── Automatic pack-selection path (v2, Task 10) ─────────────────────────────

void ComicDownloader::downloadTorrentEdition(const QString& issueIdIn, const QString& seriesId,
                                             const QString& seriesTitle, const QString& editionTitle,
                                             const QString& isbn, const QString& collects,
                                             const QString& catalogFormat,
                                             const QString& infoHash, const QString& magnetUri)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || infoHash.trimmed().isEmpty()) {
        emit failed(id, QStringLiteral("empty edition id / infoHash"));
        return;
    }
    if (isDownloaded(id)) { emit finished(id); return; }
    if (!m_torrents) {
        emit failed(id, QStringLiteral("comic torrent service unavailable"));
        return;
    }
    if (m_torrents->contains(id)) return;
    if (m_active && m_active->id == id) return;
    for (const InFlight& queued : m_queue)
        if (queued.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;
    // Stop browsing (a live source-search session may still be open behind
    // this call) and hand off to the automatic pack transport — it isolates
    // the edition itself; the transport's own idempotency guard (one live
    // intent per editionId) makes a duplicate call here harmless.
    m_torrents->cancelSourceSearch(id);
    qInfo() << "[ComicDownloader] torrent edition chosen" << id << "hash=" << infoHash;
    m_torrents->downloadEditionTorrent(id, seriesId, seriesTitle, editionTitle, isbn, collects,
                                       catalogFormat, infoHash, magnetUri);
}

void ComicDownloader::chooseTorrentFiles(const QString& issueIdIn, const QVariantList& indices)
{
    if (!m_torrents) return;
    QList<int> idx;
    idx.reserve(indices.size());
    for (const QVariant& v : indices) idx.append(v.toInt());
    m_torrents->chooseEditionFiles(issueIdIn.trimmed(), idx);
}

void ComicDownloader::confirmCombinedArchive(const QString& issueIdIn)
{
    if (m_torrents) m_torrents->confirmEditionCombined(issueIdIn.trimmed());
}

void ComicDownloader::downloadIssue(const QString& issueIdIn, const QString& postUrl,
                                    const QString& seriesId, const QString& seriesTitle,
                                    const QString& issueLabel, double expectedBytes)
{
    const QString id = issueIdIn.trimmed();
    if (id.isEmpty() || postUrl.isEmpty()) { emit failed(id, QStringLiteral("empty issue id / post url")); return; }
    if (isDownloaded(id)) { emit finished(id); return; }
    if (m_active && m_active->id == id) return;
    for (const InFlight& q : m_queue) if (q.id == id) return;
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        if (it.value().id == id) return;

    // Crash-recovery adoption (Task 4): a canonical archive from an
    // interrupted prior attempt (a safe-move/repack that finished on disk but
    // never reached saveIndex() -- the app was killed, or quit while a
    // background worker was still running past this object's destruction,
    // see InFlight::packing) sits at issueArchivePath(...) with no index row.
    // Adopt it directly -- no network, no repack -- rather than the hard
    // "already exists" dead end a naive check would produce.
    const QString adoptLabel = issueLabel.isEmpty() ? id : issueLabel;
    if (!m_index.contains(id)
        && QFileInfo(issueArchivePath(seriesId, adoptLabel, id)).isFile()
        && adoptExistingCanonicalIfValid(id, seriesId, seriesTitle, adoptLabel)) {
        emit finished(id);
        return;
    }

    // Slice 3: retry re-uses the preserved pack source. A pack that previously
    // failed at the demux/extract stage left its fully-downloaded .archive
    // staging file on disk (failPreservingSource). Re-pressing retry (or a new
    // downloadIssue() for the same id) must NOT re-download a byte — route the
    // staged file straight into the ingest lane (parent-shaped InFlight, no
    // resolve, no NAM touch), where probe/extract → demux-or-single-issue runs
    // as normal. A partial .part never qualifies; only a fully-renamed .archive.
    // If ingest terminally fails at PACK level (unextractable), the staging file
    // is discarded and failed() carries a reason naming the discard — the NEXT
    // attempt re-downloads cleanly (see the failAndCleanup path below).
    {
        const QString stagedArchive = baseDir() + QStringLiteral("/dl_") + hash10(id)
                                      + QStringLiteral(".archive");
        if (QFileInfo(stagedArchive).isFile()) {
            qInfo() << "[ComicDownloader] retry re-using staged archive, no re-download id=" << id
                    << "archive=" << stagedArchive;
            InFlight f;
            f.id            = id;
            f.seriesId      = seriesId;
            f.seriesTitle   = seriesTitle;
            f.label         = adoptLabel;
            f.archivePath   = stagedArchive;
            f.expectedBytes = QFileInfo(stagedArchive).size();
            f.localArchive  = true;
            f.stagedRetrySource = true;   // discard on terminal failure → re-download next
            if (m_active) {
                m_queue.append(std::move(f));
            } else {
                m_active = new InFlight(std::move(f));
                ingestArchiveByProbe(*m_active);
            }
            return;
        }
    }

    // Resolve the FULL signed DOWNLOAD NOW href from the release post.
    QNetworkRequest req{QUrl(postUrl)};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    InFlight f;
    f.id            = id;
    f.postUrl       = postUrl;
    f.seriesId      = seriesId;
    f.seriesTitle   = seriesTitle;
    f.label         = issueLabel.isEmpty() ? id : issueLabel;
    f.expectedBytes = static_cast<qint64>(expectedBytes);
    m_resolving.insert(reply, f);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onResolveFinished(reply); });
}

void ComicDownloader::onResolveFinished(QNetworkReply* reply)
{
    if (!reply) return;
    InFlight f = m_resolving.take(reply);
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errStr = reply->errorString();
    reply->deleteLater();

    if (f.id.isEmpty()) return;   // cancelled

    if (err != QNetworkReply::NoError) {
        emit failed(f.id, QStringLiteral("release post fetch failed: %1").arg(errStr));
        return;
    }
    f.urls = parsePostHtml(body);
    if (f.urls.isEmpty()) {
        // mirror-only post (only pixeldrain, which is dropped ISP-side): no usable source.
        // "no-source" prefix = TERMINAL — the UI must not offer an unwinnable retry.
        emit failed(f.id, QStringLiteral("no-source | no direct download link on this release"));
        return;
    }
    qInfo() << "[ComicDownloader] resolved" << f.urls.size() << "link(s) for" << f.id << f.label;
    startDownload(std::move(f));
}

QStringList ComicDownloader::parsePostHtml(const QByteArray& html) const
{
    // extracted to a free function (ComicDlsParse) so the contract is testable
    return parseDlsLinks(html);
}

void ComicDownloader::cancelDownload(const QString& issueIdIn)
{
    const QString id = issueIdIn.trimmed();
    // Slice 3: cancel of a pack child (or its parent) drops queued siblings and
    // marks the manifest inactive (sticky — no auto-resume after cancel). The
    // pack archive file is KEPT on disk. Landed children stay. This runs before
    // the specific-job cleanup so the family-wide drop + manifest marking land
    // regardless of whether the cancelled id is resolving/active/queued.
    cancelPackFamily(id);
    for (auto it = m_resolving.begin(); it != m_resolving.end(); ++it) {
        if (it.value().id == id) {
            QNetworkReply* r = it.key();
            m_resolving.erase(it);
            if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
            emit removed(id);
            return;
        }
    }
    if (m_active && m_active->id == id) {
        if (m_active->extracting && m_proc) {
            // QML calls this on the application thread.  kill() only requests
            // termination; waitForFinished() here used to hold that thread for
            // up to a second.  Keep the normal finished handler connected so it
            // remains the sole owner of process teardown and queue progression.
            if (m_active->cancelRequested) return;
            m_active->cancelRequested = true;
            QProcess* process = m_proc;
            if (process->state() == QProcess::NotRunning) {
                // FailedToStart may have emitted errorOccurred() without a
                // later finished() signal.  Keep cancellation non-reentrant:
                // route this already-terminal process through the same handler
                // on the next event-loop turn.
                const int exitCode = process->exitCode();
                QTimer::singleShot(0, this, [this, process, exitCode]() {
                    onExtractDone(process, exitCode, 0);
                });
            } else {
                process->kill();
            }
            return;
        }
        cancelAndCleanup(*m_active);
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id == id) {
            DownloadFileOps::Result result{true, QString()};
            if (m_queue[i].localArchive && !m_queue[i].archivePath.isEmpty())
                result = DownloadFileOps::removeFile(m_queue[i].archivePath);
            if (result.success && m_queue[i].assembledIngest
                && !m_queue[i].assembledStagingDir.isEmpty())
                result = DownloadFileOps::removeTree(m_queue[i].assembledStagingDir);
            m_queue.removeAt(i);
            if (!result.success) {
                qWarning() << "[downloads] cancel cleanup failed" << id << result.message;
                emit failed(id, result.message);
                return;
            }
            emit removed(id);
            return;
        }
    }
    if (m_torrents) m_torrents->cancel(id);
}

QVariantMap ComicDownloader::deleteIssue(const QString& issueIdIn)
{
    const QString id = issueIdIn.trimmed();
    auto it = m_index.find(id);
    if (it == m_index.end())
        return DownloadFileOps::toMap({true, QString()});
    const Entry& e = it.value();
    // An archive row's payload is the archive FILE; a dir may still be set
    // alongside it mid-migration (Task 7's first-boot pass), so remove both
    // when present rather than branching to exactly one. Dir (the reclaimable
    // copy) goes FIRST, archive (the canonical copy) LAST: if the dir removal
    // fails partway (a reader holding a page file open, an AV lock) the row
    // is left with its result reported as failure and the archive untouched
    // -- still fully valid and openable -- instead of the reverse order,
    // where a failed dir removal after the archive is already gone leaves a
    // row that looks downloaded but opens to nothing.
    auto result = !e.dir.isEmpty() ? DownloadFileOps::removeTree(e.dir)
                                    : DownloadFileOps::Result{true, QString()};
    if (result.success && e.usesArchive()) {
        const auto archiveResult = DownloadFileOps::removeFile(e.archive);
        if (!archiveResult.success) result = archiveResult;
    }
    if (!result.success) {
        qWarning() << "[downloads] delete failed" << id << result.message;
        return DownloadFileOps::toMap(result);
    }
    m_index.erase(it);
    saveIndex();
    emit removed(id);
    return DownloadFileOps::toMap(result);
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP streaming download (BookDownloader lineage)
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::startDownload(InFlight&& f)
{
    if (m_active) {
        m_queue.append(std::move(f));
        return;
    }
    m_active = new InFlight(std::move(f));
    startAttempt(*m_active);
}

void ComicDownloader::startAttempt(InFlight& f)
{
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("no-source | all download links exhausted"));
        return;
    }
    const QString url = f.urls.value(f.urlIdx);
    if (url.isEmpty()) { startNextUrlOrFail(f); return; }

    // Disk-space pre-check. Budget for the fallback path's worst case (Task
    // 4 review, D7): the downloaded archive + the loose extracted pages +
    // the freshly packed CBZ can all exist on disk AT ONCE briefly (index-
    // then-delete-source ordering keeps the source alive until the packed
    // result is verified and saved) -- and writeImagesAtomic() stores with
    // MZ_NO_COMPRESSION, so the packed CBZ is the UNCOMPRESSED page size,
    // larger than a deflate-packed source. Peak is >=3x; 4x for margin. The
    // fast (archive-in-place) path never holds more than the source archive
    // plus a same-directory rename-in-flight, well under this budget.
    if (f.expectedBytes > 0) {
        const QStorageInfo storage(baseDir());
        if (storage.isValid() && storage.isReady()
            && storage.bytesAvailable() < f.expectedBytes * 4 + kDiskSpaceSafetyBytes) {
            failAndCleanup(f, QStringLiteral("insufficient disk space for download + extract"));
            return;
        }
    }

    const int delay = attemptDelayMs(f.attempt);
    if (delay <= 0) {
        QDir().mkpath(baseDir());
        f.archivePath = baseDir() + QStringLiteral("/dl_") + hash10(f.id) + QStringLiteral(".archive");
        f.partPath    = f.archivePath + QStringLiteral(".part");
        f.file = new QFile(f.partPath);
        if (!f.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString err = f.file->errorString();
            delete f.file; f.file = nullptr;
            failAndCleanup(f, QStringLiteral("cannot open .part file: %1").arg(err));
            return;
        }
        f.receivedBytes = 0; f.sanityChecked = false;
        f.lastProgressEmit = 0; f.lastProgressBytes = 0;

        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", kUserAgent);
        req.setRawHeader("Referer", kReferer);
        req.setRawHeader("Accept", "*/*");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam->get(req);
        f.reply = reply;
        connect(reply, &QNetworkReply::readyRead,        this, &ComicDownloader::onReadyRead);
        connect(reply, &QNetworkReply::finished,         this, &ComicDownloader::onFinished);
        connect(reply, &QNetworkReply::downloadProgress, this, &ComicDownloader::onProgressFromReply);
        // Mirror rotation discipline: if the signed link redirects to a host we
        // KNOW is blocked from this ISP (pixeldrain, probed dead 2026-07-10),
        // abort and advance to the next candidate immediately — never sit out
        // the socket timeout on a dead host.
        connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl& to) {
            if (!m_active || m_active->reply.data() != reply) return;
            if (to.host().contains(QStringLiteral("pixeldrain"), Qt::CaseInsensitive)) {
                qInfo() << "[ComicDownloader] redirect to blocked host" << to.host() << "— skipping mirror";
                m_active->redirectBlocked = true;
                reply->abort();
            }
        });
    } else {
        const QString id = f.id;
        QTimer::singleShot(delay, this, [this, id]() {
            if (!m_active || m_active->id != id) return;
            m_active->attempt = 0;   // collapse to the immediate-issue branch
            startAttempt(*m_active);
        });
    }
}

void ComicDownloader::onReadyRead()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) return;

    if (!f.sanityChecked) {
        f.sanityChecked = true;
        const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (looksLikeHtml(chunk, ct)) {
            qWarning() << "[ComicDownloader] got HTML (ad-gate/interstitial) from"
                       << f.urls.value(f.urlIdx) << "— failing over";
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
            f.reply.clear();
            if (f.file) { f.file->close(); f.file->remove(); delete f.file; f.file = nullptr; }
            startNextUrlOrFail(f);
            return;
        }
    }

    if (f.file) {
        const qint64 written = f.file->write(chunk);
        if (written < 0) { failAndCleanup(f, QStringLiteral("disk write failed: %1").arg(f.file->errorString())); return; }
        f.receivedBytes += written;
    }
}

void ComicDownloader::onProgressFromReply(qint64 received, qint64 total)
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    if (total > 0) f.expectedBytes = total;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsedMs = (f.lastProgressEmit == 0) ? (kProgressThrottleMs + 1)
                                                       : (nowMs - f.lastProgressEmit);
    const qint64 deltaBytes = received - f.lastProgressBytes;
    if (elapsedMs >= kProgressThrottleMs || deltaBytes >= kProgressThrottleBytes) {
        f.lastProgressEmit = nowMs;
        f.lastProgressBytes = received;
        emit progress(f.id, static_cast<double>(received), static_cast<double>(total));
    }
}

void ComicDownloader::onFinished()
{
    if (!m_active || !m_active->reply) return;
    InFlight& f = *m_active;
    QNetworkReply* reply = f.reply.data();
    if (!reply) return;

    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (err == QNetworkReply::NoError) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && f.file) { f.file->write(tail); f.receivedBytes += tail.size(); }
    }
    reply->deleteLater();
    f.reply.clear();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[ComicDownloader] reply error" << err << "http=" << httpStatus << errString;
        if (f.redirectBlocked) {                       // deliberate abort — skip this URL, don't retry it
            f.redirectBlocked = false;
            closeAndDeletePart(f);
            startNextUrlOrFail(f);
            return;
        }
        retryOrFailover(f, QStringLiteral("HTTP error: %1 (status %2)").arg(errString).arg(httpStatus));
        return;
    }
    if (f.file) { f.file->close(); delete f.file; f.file = nullptr; }
    if (f.receivedBytes <= 0) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("server returned empty body"));
        return;
    }
    if (QFile::exists(f.archivePath)) QFile::remove(f.archivePath);
    if (!QFile::rename(f.partPath, f.archivePath)) {
        QFile::remove(f.partPath);
        failAndCleanup(f, QStringLiteral("archive rename failed"));
        return;
    }
    emit progress(f.id, static_cast<double>(f.receivedBytes), static_cast<double>(f.receivedBytes));
    ingestArchiveByProbe(f);
}

// The shared two-path-ingest decision (Task 4, extended to a shared helper in
// Task 6): probe the archive at f.archivePath first. A natively-readable CBZ
// is moved into the library archive-in-place -- no extraction, no repack.
// Anything else (a CBR, or a CBZ that fails probe()'s content checks) falls
// through to the existing extract-then-repack fallback. Not gated by file
// extension -- a source may lie about its suffix; probe by content, always.
//
// Both branches CONSUME f.archivePath on success: finalizeSafeMove() renames
// it into place (same volume) or copies-then-deletes it (cross volume);
// finalizeExtract() deletes it after saveIndex(). This is exactly the
// ownership-transfer contract ingestLocalArchive() documents (the caller's
// source file is deleted on success), so routing a local import through here
// preserves that contract for free -- no per-caller special-casing.
void ComicDownloader::ingestArchiveByProbe(InFlight& f)
{
    // CbzArchive::probe() opens the archive, walks its central directory, and
    // samples entries.  It is bounded by the archive size/entry limits, but it
    // is still unpredictable disk work and used to run inline from both
    // QNetworkReply::finished and ingestLocalArchive (the QML/torrent boundary).
    // Keep the InFlight alive while the worker owns the read path: cancellation
    // and destruction already treat `packing` workers as retired and leave the
    // source alone until the serial-checked completion path can reclaim it.
    const QString archivePath = f.archivePath;
    f.packing = true;
    f.serial = ++m_nextJobSerial;
    const quint64 serial = f.serial;
    runPackOrCopyThenPublish(serial,
        [archivePath]() -> PackOrCopyResult {
            PackOrCopyResult result;
            QString error;
            result.probe = MangaTankoban::CbzArchive::probe(archivePath, &error);
            result.error = error;
            result.ok = true; // probe rejection is the expected extract fallback, not worker failure
            result.cleanupPathsOnDiscard = {archivePath};
            return result;
        },
        [this](const PackOrCopyResult& result) {
            if (!m_active) return;
            InFlight& active = *m_active;
            active.packing = false;
            if (result.probe.nativelyReadable) {
                qInfo() << "[ComicDownloader] archive ready id=" << active.id
                        << "bytes=" << active.receivedBytes
                        << "— natively readable, moving into place (no extraction)";
                finalizeSafeMove(active, result.probe);
                return;
            }
            qInfo() << "[ComicDownloader] archive ready id=" << active.id
                    << "bytes=" << active.receivedBytes
                    << "— not natively readable, extracting";
            beginExtract(active);
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// two-path ingest (Task 4, CBZ-in-place plan)
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::runPackOrCopyThenPublish(quint64 serial, std::function<PackOrCopyResult()> work,
                                               std::function<void(const PackOrCopyResult&)> onDone)
{
    auto* watcher = new QFutureWatcher<PackOrCopyResult>(this);
    connect(watcher, &QFutureWatcher<PackOrCopyResult>::finished, this,
        [this, serial, onDone, watcher]() {
            const PackOrCopyResult result = watcher->result();
            watcher->deleteLater();
            if (!m_active || m_active->serial != serial) {
                // Retired: cancelled, or (impossible in today's single-lane
                // design, but future-proofed) superseded. Nobody else will
                // clean up what this job produced or consumed -- do it here.
                // EXCEPT a path some row in m_index now points at as its live
                // `archive` -- reachable even in single-lane design: this
                // worker's own canonical finished and got queued for
                // publish, but before this handler ran, a fresh
                // downloadIssue() for the SAME id raced ahead via
                // adoptExistingCanonicalIfValid() (or a fast-path redownload)
                // and indexed that exact file first. Blindly discarding it
                // here would silently un-download an issue the index still
                // claims exists, with no error anywhere (Task 4 review,
                // blocker) -- checked per-path, not all-or-nothing, so
                // genuine leftovers (extractTmp, a stale original archive)
                // still get cleaned up even when the canonical they'd have
                // replaced is spared.
                QSet<QString> liveArchivePaths;
                for (const Entry& e : std::as_const(m_index))
                    if (e.usesArchive()) liveArchivePaths.insert(e.archive);
                // A cancel-then-redownload of the same id can reuse the
                // deterministic .archive path before the retired worker's
                // completion arrives.  The index may not contain the new
                // job yet, so protect every path currently owned by the
                // replacement active/queued jobs as well.
                QSet<QString> inFlightPaths;
                const auto protect = [&inFlightPaths](const InFlight& job) {
                    for (const QString& path : {job.archivePath, job.partPath,
                                                 job.extractTmp, job.assembledStagingDir}) {
                        if (!path.isEmpty()) inFlightPaths.insert(QDir::cleanPath(path));
                    }
                };
                if (m_active) protect(*m_active);
                for (const InFlight& queued : std::as_const(m_queue)) protect(queued);
                for (const QString& path : result.cleanupPathsOnDiscard) {
                    if (path.isEmpty()
                        || liveArchivePaths.contains(path)
                        || inFlightPaths.contains(QDir::cleanPath(path))) continue;
                    if (QFileInfo(path).isDir()) QDir(path).removeRecursively();
                    else QFile::remove(path);
                }
                return;
            }
            onDone(result);
        });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

void ComicDownloader::failPreservingSource(InFlight& f, const QString& reason)
{
    // Repair-before-prune for the ORDINARY (non-crash) failure path, not just
    // a simulated kill: failAndCleanup()'s existing cleanup helpers already
    // no-op safely on an empty path (closeAndDeletePart/cleanupExtract), so
    // clearing these three fields first means delegating to it destroys
    // nothing. Using plain failAndCleanup() for a verification/finalize
    // failure would delete the exact source this task exists to protect --
    // on the failure path that runs far more often than a real crash does.
    qCritical() << "[ComicDownloader] preserving source on failure id=" << f.id << "reason=" << reason
               << "archivePath=" << f.archivePath << "extractTmp=" << f.extractTmp;
    f.archivePath.clear();
    f.partPath.clear();
    f.extractTmp.clear();
    failAndCleanup(f, reason);
}

void ComicDownloader::failIngest(InFlight& f, const QString& reason)
{
    if (!f.localArchive) {
        // HTTP download: the staging file is re-downloadable, so deleting it
        // (failAndCleanup's default) costs nothing and avoids a leak.
        failAndCleanup(f, reason);
        return;
    }
    if (f.stagedRetrySource) {
        // Slice 3: a staged-retry .archive (preserved from a prior
        // failPreservingSource) that terminally fails ingest is corrupt or
        // unextractable. Delete it so the NEXT retry re-downloads fresh instead
        // of looping on the same bad file. failAndCleanup deletes archivePath
        // by default; do NOT clear it first (unlike the imported-source path).
        qCritical() << "[ComicDownloader] discarding corrupt staged-retry source id=" << f.id
                   << "reason=" << reason << "source=" << f.archivePath
                   << "(next attempt will re-download)";
        f.partPath.clear();   // no .part for a staged reuse
        failAndCleanup(f, QStringLiteral("staged archive unextractable, discarded (%1)").arg(reason));
        return;
    }
    // Local-archive import (torrent-produced or user-picked): the source is
    // the ONLY copy. Preserve it -- but still let failAndCleanup clean up OUR
    // extraction temp dir (it's ours, not the source), so spare only
    // archivePath/partPath, NOT extractTmp (the difference from
    // failPreservingSource, which preserves the temp dir too for a
    // repair-before-prune retry -- pointless here, the extraction failed).
    qCritical() << "[ComicDownloader] preserving imported source on failure id=" << f.id
               << "reason=" << reason << "source=" << f.archivePath;
    f.archivePath.clear();
    f.partPath.clear();
    failAndCleanup(f, reason);
}

bool ComicDownloader::isLiveLibraryArchive(const QString& absPath) const
{
    if (absPath.isEmpty()) return false;
    for (const Entry& e : m_index)
        if (e.usesArchive() && QFileInfo(e.archive).absoluteFilePath() == absPath)
            return true;
    return false;
}

void ComicDownloader::finalizeSafeMove(InFlight& f, const MangaTankoban::CbzProbeResult& probe)
{
    Q_UNUSED(probe);   // its entries are re-read from the post-move reprobe -- the archive
                       // hasn't moved yet, so re-probing after is what proves the MOVED bytes
                       // (not just the pre-move bytes) are what actually got indexed.
    const QString canonical = issueArchivePath(f.seriesId, f.label, f.id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    // NOT ".part" -- writeImagesAtomic() (the fallback repack) uses that exact
    // temp name for the same canonical path, so a distinct suffix lets
    // post-crash forensics tell which stage left a file behind.
    const QString tempCanonical = canonical + QStringLiteral(".incoming");

    // A leftover .incoming from a PRIOR failed attempt for this same issue
    // would make both QFile::rename() and QFile::copy() below fail forever
    // (both refuse an existing destination) -- permanently bricking the
    // issue on retry (Task 4 review, blocker). Safe to clear here: probe()
    // already proved f.archivePath (the CURRENT source) nativelyReadable
    // before onFinished() ever called this function, so a verified
    // replacement is already in hand before this stale litter is touched --
    // the same rule completeSafeMove() applies for a stale CANONICAL.
    if (QFileInfo(tempCanonical).isFile()) {
        qWarning() << "[ComicDownloader] clearing a leftover .incoming from a prior attempt"
                   << tempCanonical << QFileInfo(tempCanonical).size() << "bytes";
        QFile::remove(tempCanonical);
    }

    if (QFile::rename(f.archivePath, tempCanonical)) {
        // Same-volume rename -- the overwhelmingly common case (the HTTP
        // staging path and the library both live under AppDataLocation) --
        // is an OS metadata op, near-instant even for a multi-GB file.
        // Reprobing here is cheap too (probe() samples 3 entries, not the
        // whole archive), so this branch stays on the GUI thread.
        QString reprobeError;
        const MangaTankoban::CbzProbeResult reprobe =
            MangaTankoban::CbzArchive::probe(tempCanonical, &reprobeError);
        completeSafeMove(f, tempCanonical, reprobe, /*wasCopy=*/false);
        return;
    }

    // Cross-volume fallback: unreachable with today's single AppData-rooted
    // layout (HTTP and torrent sources both stage under baseDir() already),
    // but Task 6 (ingestLocalArchive convergence) will feed this the same
    // safe-move sequence with a USER-PICKED file from any volume. A multi-GB
    // synchronous copy on the GUI thread would recreate tonight's
    // freeze-and-get-killed shape with a different verb -- so the copy (and
    // its post-copy reprobe) run on the SAME backgrounded worker the fallback
    // repack uses below, not inline.
    f.packing = true;
    f.serial = ++m_nextJobSerial;
    const quint64 serial = f.serial;
    const QString source = f.archivePath;
    runPackOrCopyThenPublish(serial,
        [source, tempCanonical]() -> PackOrCopyResult {
            PackOrCopyResult result;
            result.cleanupPathsOnDiscard = {source, tempCanonical};
            if (!QFile::copy(source, tempCanonical)) {
                result.error = QStringLiteral("cannot stage archive for library move (copy failed)");
                return result;
            }
            result.ok = true;
            result.probe = MangaTankoban::CbzArchive::probe(tempCanonical, &result.error);
            return result;
        },
        [this, tempCanonical](const PackOrCopyResult& result) {
            InFlight& active = *m_active;   // safe: onDone only runs when m_active->serial matches
            active.packing = false;
            if (!result.ok) { failPreservingSource(active, result.error); return; }
            completeSafeMove(active, tempCanonical, result.probe, /*wasCopy=*/true);
        });
}

void ComicDownloader::completeSafeMove(InFlight& f, const QString& tempCanonical,
                                       const MangaTankoban::CbzProbeResult& reprobe, bool wasCopy)
{
    if (!reprobe.nativelyReadable) {
        // repair-before-prune: leave tempCanonical (the renamed/copied
        // artifact) and, if this was a copy, the untouched original in place.
        failPreservingSource(f, QStringLiteral("post-move verification failed"));
        return;
    }

    const QString canonical = issueArchivePath(f.seriesId, f.label, f.id);
    if (QFileInfo(canonical).isFile()) {
        // A pre-existing canonical is stale litter by definition here -- we
        // ALREADY hold a fresh, independently-verified replacement (reprobe
        // just passed). Log before removing so a forensic trail survives.
        qWarning() << "[ComicDownloader] replacing stale leftover canonical" << canonical
                   << QFileInfo(canonical).size() << "bytes";
        if (!QFile::remove(canonical)) {
            failPreservingSource(f, QStringLiteral("cannot replace stale leftover canonical"));
            return;
        }
    }
    if (!QFile::rename(tempCanonical, canonical)) {
        failPreservingSource(f, QStringLiteral("cannot finalize library archive"));
        return;
    }

    Entry e;
    e.seriesId    = f.seriesId;
    e.seriesTitle = f.seriesTitle;
    e.label       = f.label;
    e.archive     = canonical;
    for (const auto& pageEntry : reprobe.entries) e.files.append(pageEntry.name);
    e.bytes       = QFileInfo(canonical).size();   // the actual artifact, not a separately-tracked counter
    e.addedAt     = QDateTime::currentMSecsSinceEpoch();
    // Pack demux (Slice 2): stamp the child's parsed role/order onto its Entry.
    e.packRole    = f.packRole;
    e.packOrder   = f.packOrder;
    m_index.insert(f.id, e);
    saveIndex();
    maybeReclaimPack(f.id);   // reclaim pack if this was the last expected child

    // Only delete the ORIGINAL source if this was a copy (a same-volume
    // rename already consumed it) -- and only now that saveIndex() has
    // returned. Never the reverse (tonight's original bug, relocated).
    if (wasCopy) QFile::remove(f.archivePath);

    const QString id = f.id;
    const int pageCount = e.files.size();
    // Detach and delete m_active BEFORE emit -- a QML slot on finished() can
    // re-enter cancelDownload(id)/cancelAndCleanup(), which would otherwise
    // find m_active still set, delete it a second time, and call
    // startNextQueued() a second time too (popping two queue entries for one
    // completion, leaking the first). delete on a nulled m_active below is a
    // harmless no-op if that reentrancy happens.
    delete m_active; m_active = nullptr;

    qInfo() << "[ComicDownloader] complete id=" << id << "pages=" << pageCount
            << "archive=" << canonical << "(archive-in-place, no extraction)";
    emit finished(id);
    startNextQueued();
}

void ComicDownloader::retryOrFailover(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    f.attempt += 1;
    if (f.attempt < kMaxAttempts) { startAttempt(f); return; }
    qInfo() << "[ComicDownloader] link exhausted, failover:" << reason;
    startNextUrlOrFail(f);
}

void ComicDownloader::startNextUrlOrFail(InFlight& f)
{
    f.urlIdx += 1;
    f.attempt = 0;
    // Every resolved mirror failed (CF-blocked / HTML-gated / offline) — no usable source.
    // "no-source" prefix = TERMINAL: JLU #1 (2024) lands here because its only comicfiles
    // mirror sits behind a CF managed challenge and its MEGA mirror isn't a direct-HTTP host.
    if (f.urlIdx >= f.urls.size()) {
        failAndCleanup(f, QStringLiteral("no-source | all mirrors unavailable (blocked or offline)"));
        return;
    }
    startAttempt(f);
}

void ComicDownloader::failAndCleanup(InFlight& f, const QString& reason)
{
    closeAndDeletePart(f);
    cleanupExtract(f);
    // Assembled-edition ingest (Task 7): any failure/cancellation before the
    // single atomic publish leaves NO index record — and leaves no orphaned
    // staging dir behind either.
    if (f.assembledIngest && !f.assembledStagingDir.isEmpty())
        QDir(f.assembledStagingDir).removeRecursively();
    const QString id = f.id;
    emit failed(id, reason);
    delete m_active; m_active = nullptr;
    startNextQueued();
}

void ComicDownloader::cancelAndCleanup(InFlight& f)
{
    // Cancellation has a user-visible removal contract. Close live handles,
    // then let the checked helper own every delete so a failure cannot be
    // silently converted into a successful "removed" event.
    closePart(f);
    const auto result = cleanupCancelledPayload(f);
    f.extracting = false;
    const QString id = f.id;
    delete m_active; m_active = nullptr;
    if (!result.success) {
        qWarning() << "[downloads] cancel cleanup failed" << id << result.message;
        emit failed(id, result.message);
    } else {
        emit removed(id);
    }
    startNextQueued();
}

DownloadFileOps::Result ComicDownloader::cleanupCancelledPayload(InFlight& f)
{
    // While a background copy/pack job may still be reading extractTmp/
    // archivePath (InFlight::packing), leave both alone -- removing them out
    // from under the worker thread is a real race (Task 4 review), and on
    // Windows a still-open read handle can make removeRecursively() fail
    // outright, turning a cancel the user asked for into a spurious "could
    // not delete" failure. The background job's own completion handler
    // (runPackOrCopyThenPublish's retired-job branch) owns that cleanup
    // instead, once it's actually safe.
    if (f.packing)
        return DownloadFileOps::removeFile(f.partPath);
    auto result = DownloadFileOps::removeFile(f.partPath);
    if (result.success)
        result = DownloadFileOps::removeFile(f.archivePath);
    if (result.success)
        result = DownloadFileOps::removeTree(f.extractTmp);
    if (result.success && f.assembledIngest)
        result = DownloadFileOps::removeTree(f.assembledStagingDir);
    return result;
}

void ComicDownloader::closeAndDeletePart(InFlight& f)
{
    closePart(f);
    if (!f.partPath.isEmpty() && QFile::exists(f.partPath))
        QFile::remove(f.partPath);
    if (!f.archivePath.isEmpty() && QFile::exists(f.archivePath))
        QFile::remove(f.archivePath);
}

void ComicDownloader::closePart(InFlight& f)
{
    if (f.reply) {
        QNetworkReply* r = f.reply.data();
        if (r) { r->disconnect(this); r->abort(); r->deleteLater(); }
        f.reply.clear();
    }
    if (f.file) {
        f.file->close();
        delete f.file; f.file = nullptr;
    }
}

void ComicDownloader::startNextQueued()
{
    // Re-entrancy guard (Task 5 review): every async publish site nulls
    // m_active BEFORE emit finished() and calls this AFTER. A finished() QML
    // slot that synchronously starts a new download would set m_active
    // non-null in between -- without this guard, this would then new-over that
    // job, leak its InFlight (and orphan its background worker into the
    // retired-cleanup branch, deleting its files with no failed() signal:
    // a silent disappearance). If a job is already active, the queued one
    // stays queued and starts when that job completes.
    if (m_active) return;
    if (!m_queue.isEmpty()) {
        m_active = new InFlight(std::move(m_queue[0]));
        m_queue.removeAt(0);
        if (m_active->assembledIngest)
            publishAssembledEdition(*m_active);
        else if (m_active->localArchive)
            ingestArchiveByProbe(*m_active);   // Task 6: two-path ingest, was beginExtract()
        else
            startAttempt(*m_active);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// extraction: OS bsdtar (reads RAR + zip) → 7-Zip fallback → pages dir
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::beginExtract(InFlight& f)
{
    f.extracting = true;
    f.extractTmp = f.archivePath + QStringLiteral(".x");
    QDir(f.extractTmp).removeRecursively();
    if (!QDir().mkpath(f.extractTmp)) {
        failIngest(f, QStringLiteral("cannot create extract dir"));
        return;
    }
    runExtractor(f, 0);
}

void ComicDownloader::runExtractor(InFlight& f, int which)
{
    QString exe;
    QStringList args;
    if (which == 0) {
        exe = bsdtarPath();
        args = { QStringLiteral("-xf"), QDir::toNativeSeparators(f.archivePath),
                 QStringLiteral("-C"), QDir::toNativeSeparators(f.extractTmp) };
    } else {
        exe = sevenZipPath();
        args = { QStringLiteral("x"), QStringLiteral("-y"),
                 QStringLiteral("-o") + QDir::toNativeSeparators(f.extractTmp),
                 QDir::toNativeSeparators(f.archivePath) };
    }
    if (exe.isEmpty()) {
        if (which == 0) { runExtractor(f, 1); return; }
        failIngest(f, QStringLiteral("no archive extractor available (tar/7z)"));
        return;
    }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setArguments(args);
    QProcess* process = m_proc;
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, which](int code, QProcess::ExitStatus) {
                onExtractDone(process, code, which);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, which](QProcess::ProcessError) {
        // FailedToStart can report only errorOccurred(), without a finished()
        // signal.  A terminal process error therefore uses the same completion
        // path as finished(), both for cancellation and for ordinary extractor
        // failures. Ignore errors while the process is still running; its
        // finished signal will win. The process identity guard prevents a late
        // error from an old extractor touching a queued successor.
        if (!m_active || m_proc != process
            || !m_proc
            || m_proc->state() != QProcess::NotRunning)
            return;
        onExtractDone(process, process->exitCode(), which);
    });
    qInfo() << "[ComicDownloader] extracting with" << exe;
    m_proc->start();
}

void ComicDownloader::onExtractDone(QProcess* process, int exitCode, int which)
{
    // A cancelled process may report errorOccurred() before finished().  Its
    // finished signal can then be delivered after startNextQueued() has
    // installed a new extractor, so never let a stale callback touch the new
    // m_proc or active job.
    if (m_proc != process) return;
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
    if (!m_active || !m_active->extracting) return;
    InFlight& f = *m_active;
    if (f.cancelRequested) {
        cancelAndCleanup(f);
        return;
    }
    if (exitCode != 0) {
        qWarning() << "[ComicDownloader] extractor" << which << "exit" << exitCode;
        if (which == 0 && !sevenZipPath().isEmpty()) {
            QDir(f.extractTmp).removeRecursively();
            QDir().mkpath(f.extractTmp);
            runExtractor(f, 1);
            return;
        }
        failIngest(f, QStringLiteral("archive extraction failed (not a cbr/cbz?)"));
        return;
    }
    finalizeExtract(f);
}

void ComicDownloader::finalizeExtract(InFlight& f)
{
    // Deliberately does NOT reset f.extracting here: statusOf()/activeIssueJobs()
    // key their "extracting" vs "downloading" state string off this flag, and
    // the pack-off-thread window below can run for a while on a large repack
    // -- reporting "downloading" (frozen at 100%) for that whole window would
    // be a real UI regression (Task 4 review). Cancel is unaffected either
    // way: cancelDownload()'s subprocess-kill branch already requires m_proc
    // too, which onExtractDone() nulled before calling this. cleanupExtract()
    // (failure path) and cancelAndCleanup() (cancel path) both already reset
    // this flag; the success path deletes the InFlight outright.

    // Collect images (recursive — many archives nest one folder), natural-sorted
    // by relative path so "…-0002" follows "…-0001" and 10 follows 9 -- AND
    // filtered through the SAME predicate probe()/readEntry() apply
    // (isAcceptedImageEntryName), not just a suffix check. A suffix-only
    // collect would pack e.g. __MACOSX/._page01.jpg (real, from Mac-authored
    // CBRs) into the archive; probe() then silently drops it on readback,
    // leaving Entry.files short of what's actually in the file with no error
    // (Task 4 review finding).
    QStringList rel;
    QDirIterator it(f.extractTmp, QDir::Files, QDirIterator::Subdirectories);
    const int prefixLen = f.extractTmp.length() + 1;
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString relPath = abs.mid(prefixLen);
        if (isImageFile(abs) && MangaTankoban::CbzArchive::isAcceptedImageEntryName(relPath))
            rel.append(relPath);
    }
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(rel.begin(), rel.end(), [&coll](const QString& a, const QString& b) {
        return coll.compare(a, b) < 0;
    });
    if (rel.isEmpty()) {
        // Pack demux (Slice 2): a pack's extracted tree holds zero page IMAGES
        // but may hold nested comic archives (the live Chew v1–v8 + Extras case:
        // a ZIP whose top folder has 12 .cbr/.cbz). Before failing "archive
        // contained no pages", scan for nested archives by content. If found,
        // demux into N child ingests under a shared seriesId and retire the
        // parent WITHOUT a failed() signal. If none, fall through to today's
        // exact failure (byte-identical behaviour for genuinely empty archives).
        if (demultiplexPack(f))
            return;
        failPreservingSource(f, QStringLiteral("archive contained no pages"));
        return;
    }

    const QString canonical = issueArchivePath(f.seriesId, f.label, f.id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());

    // Pack via writeImagesAtomic (a real CBZ, not loose page_NNN.ext files)
    // OFF the GUI thread — a multi-GB repack synchronously would recreate
    // tonight's freeze-and-get-killed shape with better ordering but the same
    // symptom. See InFlight::packing / runPackOrCopyThenPublish for the
    // cancel/destructor safety this requires.
    f.packing = true;
    f.serial = ++m_nextJobSerial;
    const quint64 serial = f.serial;
    const QString extractTmp = f.extractTmp;
    const QString originalArchive = f.archivePath;

    runPackOrCopyThenPublish(serial,
        [extractTmp, originalArchive, canonical, rel]() -> PackOrCopyResult {
            PackOrCopyResult result;
            result.cleanupPathsOnDiscard = {extractTmp, originalArchive, canonical};
            // A pre-existing canonical at this point is stale litter — this
            // worker holds a freshly-collected source about to be verified.
            // writeImagesAtomic() hard-refuses to replace an existing file,
            // so remove it first (log path+size for a forensic trail), the
            // same rule completeSafeMove() applies for the fast path.
            if (QFileInfo(canonical).isFile()) {
                qWarning() << "[ComicDownloader] replacing stale leftover canonical (repack)"
                           << canonical << QFileInfo(canonical).size() << "bytes";
                if (!QFile::remove(canonical)) {
                    result.error = QStringLiteral("cannot replace stale leftover canonical");
                    return result;
                }
            }
            QString packError;
            if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, extractTmp, rel, &packError)) {
                result.error = packError;
                return result;
            }
            result.ok = true;
            result.probe = MangaTankoban::CbzArchive::probe(canonical, &result.error);
            return result;
        },
        [this, canonical, extractTmp](const PackOrCopyResult& result) {
            InFlight& active = *m_active;   // safe: onDone only runs when m_active->serial matches
            active.packing = false;
            if (!result.ok || !result.probe.nativelyReadable) {
                failPreservingSource(active, result.error.isEmpty()
                    ? QStringLiteral("repack verification failed") : result.error);
                return;
            }

            Entry e;
            e.seriesId    = active.seriesId;
            e.seriesTitle = active.seriesTitle;
            e.label       = active.label;
            e.archive     = canonical;
            for (const auto& pageEntry : result.probe.entries) e.files.append(pageEntry.name);
            e.bytes       = QFileInfo(canonical).size();   // the actual artifact, not receivedBytes
                                                             // (writeImagesAtomic stores uncompressed,
                                                             // so this legitimately exceeds the download)
            e.addedAt     = QDateTime::currentMSecsSinceEpoch();
            // Pack demux (Slice 2): stamp the child's parsed role/order.
            e.packRole    = active.packRole;
            e.packOrder   = active.packOrder;
            m_index.insert(active.id, e);
            saveIndex();
            maybeReclaimPack(active.id);   // reclaim pack if this was the last expected child

            const QString id = active.id;
            const QString originalArchivePath = active.archivePath;
            const int pageCount = e.files.size();
            // Detach and delete m_active BEFORE emit -- see completeSafeMove()'s
            // identical note. `active` is a reference to *m_active, so every
            // field this handler still needs (id, archivePath) is copied out
            // above, before the delete below invalidates it.
            delete m_active; m_active = nullptr;

            // Save index, THEN delete source — never the reverse (the exact
            // ordering bug this whole arc exists to fix).
            QDir(extractTmp).removeRecursively();
            QFile::remove(originalArchivePath);

            qInfo() << "[ComicDownloader] complete id=" << id << "pages=" << pageCount
                    << "archive=" << canonical << "(repacked from extraction)";
            emit finished(id);
            startNextQueued();
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-volume pack demux (Slice 2, 2026-08-06)
// ─────────────────────────────────────────────────────────────────────────────

// The suffix pre-filter: cheap before the (sampling) content probe. A pack's
// nested archives are recognised comic-book extensions. Content check (below)
// still has the final word — a .cbz that isn't a zip is rejected by probe().
static const QSet<QString> kNestedArchiveSuffixes = {
    QStringLiteral("cbr"), QStringLiteral("cbz"),
    QStringLiteral("cb7"), QStringLiteral("cbt")
};

QStringList ComicDownloader::scanForNestedArchives(const QString& extractTmp) const
{
    // Recursive scan, natural-sorted by relative path (stable child order).
    // Suffix pre-filter, then a content probe: a zip-shaped file must pass
    // CbzArchive::probe() (nativelyReadable); anything else is accepted only
    // if it isn't itself a pack candidate we'd misdetect. We accept by suffix
    // for non-zip shapes (cbr/cb7/cbt) because those route to the extract
    // fallback which handles corruption itself; a zip-shaped file MUST probe,
    // because a stray .cbz that is actually __MACOSX litter or a zip bomb must
    // NOT be ingested as a volume.
    QStringList found;
    const int prefixLen = extractTmp.length() + 1;
    QDirIterator it(extractTmp, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString suffix = QFileInfo(abs).suffix().toLower();
        if (!kNestedArchiveSuffixes.contains(suffix)) continue;
        const QString rel = abs.mid(prefixLen);
        if (suffix == QStringLiteral("cbz")) {
            // zip-shaped: must be a genuinely readable CBZ.
            const MangaTankoban::CbzProbeResult probe = MangaTankoban::CbzArchive::probe(abs);
            if (!probe.nativelyReadable) {
                qWarning() << "[ComicDownloader] demux: nested .cbz failed probe, skipping"
                           << rel;
                continue;
            }
        }
        // cbr/cb7/cbt: accept by suffix — the extract fallback reports a real
        // corrupt file per-child (its nested source is preserved by failIngest).
        found.append(rel);
    }
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(found.begin(), found.end(), [&coll](const QString& a, const QString& b) {
        return coll.compare(a, b) < 0;
    });
    return found;
}

bool ComicDownloader::demultiplexPack(InFlight& f)
{
    const QStringList nested = scanForNestedArchives(f.extractTmp);
    if (nested.isEmpty())
        return false;   // no nested archives → caller falls through to today's fail

    qInfo() << "[ComicDownloader] demux: pack id=" << f.id << "nested=" << nested.size()
            << "archivePath=" << f.archivePath << "extractTmp=" << f.extractTmp;

    // Build the child list: deterministic id, parsed label/role/order, inherited
    // series identity. Stable order = scanForNestedArchives' natural sort.
    PackManifest manifest;
    manifest.archivePath = f.archivePath;
    manifest.extractTmp = f.extractTmp;
    manifest.seriesId = f.seriesId;
    manifest.seriesTitle = f.seriesTitle;
    manifest.active = true;
    QList<PackChild> children;
    children.reserve(nested.size());
    for (const QString& rel : nested) {
        PackChild c;
        c.rel = rel;
        c.id = f.id + QStringLiteral(":vol:") + hash10(rel);
        const MangaTankoban::PackLabel lbl = MangaTankoban::parsePackLabel(rel);
        c.label = lbl.label;
        c.role = lbl.role;
        c.order = lbl.order;
        children.append(c);
    }
    manifest.children = children;

    // Write the manifest BEFORE the first child ingests (crash recovery, Slice 3
    // wires the resume; the file's mere presence now is what makes the pack
    // reclaimable/retryable later).
    m_packs.insert(f.id, manifest);
    savePacks();

    // Enqueue one child InFlight per nested archive, mirroring ingestLocalArchive's
    // dedup + adoption checks. The child's archivePath is the nested file INSIDE
    // extractTmp (the pack's protected source stays f.archivePath). All children
    // share partGroupKey = parentId and groupUnit = "volumes" for the Downloads fold.
    for (const PackChild& c : std::as_const(manifest.children)) {
        const QString childArchivePath =
            manifest.extractTmp + QChar('/') + c.rel;
        // Idempotence: a child already indexed (a re-run over the same pack)
        // is skipped — adoptExistingCanonicalIfValid handled it on a prior run.
        if (m_index.contains(c.id)) {
            qInfo() << "[ComicDownloader] demux: child already indexed, skipping" << c.id;
            continue;
        }
        if (m_active && m_active->id == c.id) continue;
        bool inQueue = false;
        for (const InFlight& q : std::as_const(m_queue))
            if (q.id == c.id) { inQueue = true; break; }
        if (inQueue) continue;

        InFlight flight;
        flight.id = c.id;
        flight.seriesId = manifest.seriesId;
        flight.seriesTitle = manifest.seriesTitle;
        flight.label = c.label;
        flight.archivePath = childArchivePath;
        flight.receivedBytes = QFileInfo(childArchivePath).size();
        flight.expectedBytes = flight.receivedBytes;
        flight.localArchive = true;
        flight.partGroupKey = f.id;
        flight.groupUnit = QStringLiteral("volumes");
        // Stash the parsed role/order on the child so the publish tails can
        // stamp them onto the Entry at index time (re-use InFlight fields below).
        flight.packRole = c.role;
        flight.packOrder = c.order;
        // Always enqueue — NEVER dispatch a child inline here. `f` IS *m_active
        // (the parent), and dispatching inline would overwrite m_active with the
        // child, so the `delete m_active` retire below would destroy the child
        // mid-extract instead of the parent. The parent retires; startNextQueued
        // pops child[0] and the single lane serializes the volumes cleanly.
        m_queue.append(std::move(flight));
    }

    // Retire the parent WITHOUT an index row and WITHOUT failed(): its work is
    // done (it produced the children); the children's finished() are the real
    // completions. emit removed(parentId) tells the Downloads page to drop the
    // parent row. Clear the preserved-source fields first so nothing later
    // accidentally deletes the pack (the manifest + children own its lifecycle).
    const QString parentId = f.id;
    f.archivePath.clear();
    f.partPath.clear();
    f.extractTmp.clear();
    delete m_active; m_active = nullptr;
    qInfo() << "[ComicDownloader] demux: parent retired id=" << parentId
            << "children=" << children.size();
    emit removed(parentId);
    startNextQueued();
    return true;
}

void ComicDownloader::loadPacks()
{
    QFile f(baseDir() + QStringLiteral("/packs.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        PackManifest m;
        m.archivePath = o.value(QStringLiteral("archivePath")).toString();
        m.extractTmp = o.value(QStringLiteral("extractTmp")).toString();
        m.seriesId = o.value(QStringLiteral("seriesId")).toString();
        m.seriesTitle = o.value(QStringLiteral("seriesTitle")).toString();
        m.active = o.value(QStringLiteral("active")).toBool(true);
        for (const QJsonValue& cv : o.value(QStringLiteral("children")).toArray()) {
            const QJsonObject co = cv.toObject();
            PackChild c;
            c.id = co.value(QStringLiteral("id")).toString();
            c.rel = co.value(QStringLiteral("rel")).toString();
            c.label = co.value(QStringLiteral("label")).toString();
            c.role = co.value(QStringLiteral("role")).toString();
            c.order = co.value(QStringLiteral("order")).toInt(-1);
            m.children.append(c);
        }
        m_packs.insert(it.key(), m);
    }
}

void ComicDownloader::savePacks() const
{
    QDir().mkpath(baseDir());
    QJsonObject root;
    for (auto it = m_packs.constBegin(); it != m_packs.constEnd(); ++it) {
        const PackManifest& m = it.value();
        QJsonObject o;
        o[QStringLiteral("archivePath")] = m.archivePath;
        o[QStringLiteral("extractTmp")] = m.extractTmp;
        o[QStringLiteral("seriesId")] = m.seriesId;
        o[QStringLiteral("seriesTitle")] = m.seriesTitle;
        o[QStringLiteral("active")] = m.active;
        QJsonArray kids;
        for (const PackChild& c : m.children) {
            QJsonObject co;
            co[QStringLiteral("id")] = c.id;
            co[QStringLiteral("rel")] = c.rel;
            co[QStringLiteral("label")] = c.label;
            co[QStringLiteral("role")] = c.role;
            co[QStringLiteral("order")] = c.order;
            kids.append(co);
        }
        o[QStringLiteral("children")] = kids;
        root[it.key()] = o;
    }
    QSaveFile f(baseDir() + QStringLiteral("/packs.json"));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        qWarning() << "[comics] savePacks commit failed -- previous packs.json left intact";
}

void ComicDownloader::maybeReclaimPack(const QString& childId)
{
    if (m_packs.isEmpty()) return;
    // Find the parent manifest this child belongs to (child ids are
    // "<parentId>:vol:<hash>"; check by membership).
    QString parentId;
    for (auto it = m_packs.constBegin(); it != m_packs.constEnd(); ++it) {
        for (const PackChild& c : it.value().children)
            if (c.id == childId) { parentId = it.key(); break; }
        if (!parentId.isEmpty()) break;
    }
    if (parentId.isEmpty()) return;   // not a pack child — nothing to reclaim

    const PackManifest& m = m_packs.value(parentId);
    // Complete iff EVERY expected child is now indexed (idempotence: adoption
    // and a prior completion both satisfy this).
    for (const PackChild& c : m.children)
        if (!m_index.contains(c.id)) return;   // at least one missing — keep the pack

    qInfo() << "[ComicDownloader] demux: pack complete, reclaiming id=" << parentId
            << "children=" << m.children.size();
    // Reclaim the pack source + its extracted tree. extractTmp may hold
    // leftover child staging dirs; removeRecursively clears them. The pack
    // archive is the protected source — now safe to delete.
    if (!m.extractTmp.isEmpty()) QDir(m.extractTmp).removeRecursively();
    if (!m.archivePath.isEmpty()) QFile::remove(m.archivePath);
    m_packs.remove(parentId);
    savePacks();
}

void ComicDownloader::resumeIncompletePacks()
{
    if (m_packs.isEmpty()) return;
    // Snapshot the manifest ids — reextraction may mutate m_packs during the loop.
    const QList<QString> parentIds = m_packs.keys();
    for (const QString& parentId : parentIds) {
        const PackManifest m = m_packs.value(parentId);
        if (!m.active) continue;

        // Filter to children still missing from the index (and not already
        // active/queued — a prior resume this boot may have started them).
        QList<PackChild> missing;
        for (const PackChild& c : m.children) {
            if (m_index.contains(c.id)) continue;   // already landed
            if (m_active && m_active->id == c.id) continue;
            bool inQueue = false;
            for (const InFlight& q : std::as_const(m_queue))
                if (q.id == c.id) { inQueue = true; break; }
            if (inQueue) continue;
            missing.append(c);
        }
        if (missing.isEmpty()) {
            // Every child is indexed but the manifest still exists — either a
            // crash between the last child indexing and maybeReclaimPack, or a
            // partial reclaim. Finish the job: reclaim the pack + extractTmp,
            // clear the manifest. (Idempotent with maybeReclaimPack, which would
            // also fire on the next child publish — but there will be no next
            // publish, so we drive it here.)
            maybeReclaimPack(m.children.isEmpty() ? QString() : m.children.first().id);
            continue;
        }

        // Decide the re-enqueue path per the manifest's surviving sources.
        const bool extractTmpAlive = !m.extractTmp.isEmpty()
                                     && QDir(m.extractTmp).exists();
        const bool packArchiveAlive = !m.archivePath.isEmpty()
                                      && QFileInfo(m.archivePath).isFile();

        if (extractTmpAlive) {
            // The extracted tree survived the crash: enqueue each missing child
            // straight from its nested archive inside extractTmp.
            qInfo() << "[ComicDownloader] resume: pack" << parentId
                    << "extractTmp alive, enqueuing" << missing.size() << "missing children";
            for (const PackChild& c : std::as_const(missing)) {
                const QString childArchivePath = m.extractTmp + QChar('/') + c.rel;
                InFlight flight;
                flight.id = c.id;
                flight.seriesId = m.seriesId;
                flight.seriesTitle = m.seriesTitle;
                flight.label = c.label;
                flight.archivePath = childArchivePath;
                flight.receivedBytes = QFileInfo(childArchivePath).size();
                flight.expectedBytes = flight.receivedBytes;
                flight.localArchive = true;
                flight.partGroupKey = parentId;
                flight.groupUnit = QStringLiteral("volumes");
                flight.packRole = c.role;
                flight.packOrder = c.order;
                m_queue.append(std::move(flight));
            }
            startNextQueued();
        } else if (packArchiveAlive) {
            // extractTmp gone (OS temp cleanup or a prior partial reclaim) but
            // the protected pack archive survived: re-extract the parent, the
            // demux seam re-runs, and adoption skips children already indexed.
            qInfo() << "[ComicDownloader] resume: pack" << parentId
                    << "extractTmp gone, re-extracting from pack archive";
            reextractPackParent(parentId);
        } else {
            // Both gone — nothing recoverable. Clear the manifest so it doesn't
            // haunt every future boot; the already-landed children stay valid.
            qWarning() << "[ComicDownloader] resume: pack" << parentId
                       << "has no surviving source (pack + extractTmp both gone);"
                       << "clearing manifest," << missing.size()
                       << "missing children unrecoverable";
            m_packs.remove(parentId);
            savePacks();
        }
    }
}

void ComicDownloader::reextractPackParent(const QString& parentId)
{
    const PackManifest m = m_packs.value(parentId);
    if (m.archivePath.isEmpty() || !QFileInfo(m.archivePath).isFile()) return;

    // Build a parent-shaped InFlight and drive it through the same extract →
    // finalizeExtract → demultiplexPack path the original ingest took. The demux
    // seam re-runs; adoption skips children already indexed (idempotence check
    // inside demultiplexPack's enqueue loop). The manifest is overwritten with
    // a fresh extractTmp (the old tree is gone by the precondition above).
    InFlight flight;
    flight.id = parentId;
    flight.seriesId = m.seriesId;
    flight.seriesTitle = m.seriesTitle;
    flight.label = QStringLiteral("Pack");   // label is cosmetic for a re-extract parent
    flight.archivePath = m.archivePath;
    flight.localArchive = true;
    flight.partGroupKey.clear();
    flight.groupUnit = QStringLiteral("volumes");
    // The parent's extract will be driven by beginExtract via ingestArchiveByProbe.
    m_active = new InFlight(std::move(flight));
    ingestArchiveByProbe(*m_active);
}

bool ComicDownloader::cancelPackFamily(const QString& childOrParentId)
{
    if (m_packs.isEmpty()) return false;
    // Resolve the parent: the id may be the parent itself, or a child
    // ("<parentId>:vol:<hash>").
    QString parentId;
    if (m_packs.contains(childOrParentId)) {
        parentId = childOrParentId;
    } else {
        for (auto it = m_packs.constBegin(); it != m_packs.constEnd(); ++it) {
            for (const PackChild& c : it.value().children)
                if (c.id == childOrParentId) { parentId = it.key(); break; }
            if (!parentId.isEmpty()) break;
        }
    }
    if (parentId.isEmpty()) return false;   // not a pack family member

    // Drop every queued sibling of this manifest (the cancelled child itself is
    // handled by the caller's queue/m_active branch; we clear the REST here).
    const PackManifest m = m_packs.value(parentId);
    QSet<QString> familyIds;
    for (const PackChild& c : m.children) familyIds.insert(c.id);
    familyIds.insert(parentId);
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (familyIds.contains(m_queue[i].id)) {
            // A queued child's archivePath points INSIDE the parent's extractTmp
            // (a protected source) — do NOT delete it on cancel.
            m_queue.removeAt(i);
        }
    }

    // Mark the manifest cleared. The pack archive file is KEPT on disk
    // (spec: "Any failure or cancellation keeps the pack on disk"). Subsequent
    // sibling cancels no-op (the manifest is gone). A fresh construct will NOT
    // auto-resume a cancelled pack (active=false is sticky: resumeIncompletePacks
    // skips inactive manifests).
    if (m_packs.contains(parentId)) {
        m_packs[parentId].active = false;
        savePacks();
        qInfo() << "[ComicDownloader] cancel: pack family" << parentId
                << "marked inactive (cancel is sticky; pack file kept on disk)";
    }
    return true;
}

void ComicDownloader::cleanupExtract(InFlight& f)
{
    if (!f.extractTmp.isEmpty()) QDir(f.extractTmp).removeRecursively();
    f.extracting = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// assembled-edition publication (Task 7: ComicDownloader ingest boundary)
// ─────────────────────────────────────────────────────────────────────────────

// Validates the complete Task-6 ComicEditionAssembler staging directory (every
// page lives inside it, nothing escapes, nothing missing) and, only once that
// is proven, moves it into the library with ONE atomic rename — the same
// whole-directory rename the shipped app already uses to relocate its own
// AppData tree (main.cpp's org-name migration). `f` is always `*m_active`
// (mirrors beginExtract's convention), so any failure path can safely reuse
// failAndCleanup()/startNextQueued().
void ComicDownloader::publishAssembledEdition(InFlight& f)
{
    const QString root = QDir::cleanPath(QFileInfo(f.assembledStagingDir).absoluteFilePath());

    // Completeness + traversal safety FIRST — nothing is touched until every
    // page is proven to live inside stagingDir. A partial directory is never
    // published (design safety contract: "no filesystem path derived from
    // torrent metadata" — here, from the assembler — "before reading, moving,
    // or deleting it" without being cleaned/verified to stay inside its root).
    QSet<QString> seen;
    for (const QString& name : f.assembledOrderedFiles) {
        if (name.trimmed().isEmpty()) {
            failAndCleanup(f, QStringLiteral("assembled edition has an empty page filename"));
            return;
        }
        const QString candidate = QDir::cleanPath(root + QChar('/') + name);
        if (candidate != root && !candidate.startsWith(root + QChar('/'), Qt::CaseInsensitive)) {
            failAndCleanup(f, QStringLiteral("assembled page path escapes staging dir: %1").arg(name));
            return;
        }
        const QFileInfo fi(candidate);
        if (!fi.exists() || !fi.isFile()) {
            failAndCleanup(f, QStringLiteral("assembled page missing on disk: %1").arg(name));
            return;
        }
        if (seen.contains(candidate)) {
            failAndCleanup(f, QStringLiteral("duplicate assembled page: %1").arg(name));
            return;
        }
        seen.insert(candidate);
    }

    // Task 5 (CBZ-in-place plan): pack the validated staging pages into a real
    // canonical CBZ -- was QDir().rename(root, dirPath) into a loose page
    // folder, the last remaining legacy-`dir` writer. Off the GUI thread
    // (same background machinery as Task 4's repack), so a large edition's
    // pack can't freeze the UI.
    //
    // CRITICAL: unlike the download paths, an assembled edition's page ORDER
    // is authoritative (the assembler determined it) and `assembledGroups` is
    // parallel to `assembledOrderedFiles`. writeImagesAtomic() preserves that
    // order in the archive, but probe() re-sorts its returned entries by a
    // numeric collator -- so Entry.files stays `assembledOrderedFiles` and
    // Entry.groups stays `assembledGroups`, NEVER rebuilt from probe.entries
    // (which would silently reorder pages and desync the group mapping). The
    // post-pack probe here is a readability GATE only, not the source of the
    // page list.
    const QString canonical = issueArchivePath(f.seriesId, f.label, f.id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());

    f.packing = true;
    f.serial = ++m_nextJobSerial;
    const quint64 serial = f.serial;
    const QString stagingDir = f.assembledStagingDir;
    const QStringList orderedFiles = f.assembledOrderedFiles;

    runPackOrCopyThenPublish(serial,
        [canonical, root, orderedFiles]() -> PackOrCopyResult {
            PackOrCopyResult result;
            result.cleanupPathsOnDiscard = {canonical, root};
            // A pre-existing canonical is stale litter -- all validation above
            // already passed, so this worker holds a verified-source about to
            // be packed. writeImagesAtomic() hard-refuses to replace an
            // existing file, so remove it (logged) first, same rule the Task 4
            // paths apply.
            if (QFileInfo(canonical).isFile()) {
                qWarning() << "[ComicDownloader] replacing stale leftover canonical (assembled)"
                           << canonical << QFileInfo(canonical).size() << "bytes";
                if (!QFile::remove(canonical)) {
                    result.error = QStringLiteral("cannot replace stale leftover canonical");
                    return result;
                }
            }
            QString packError;
            if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, root, orderedFiles, &packError)) {
                result.error = packError;
                return result;
            }
            result.ok = true;
            result.probe = MangaTankoban::CbzArchive::probe(canonical, &result.error);
            return result;
        },
        [this, canonical, stagingDir](const PackOrCopyResult& result) {
            InFlight& active = *m_active;   // safe: onDone only runs when m_active->serial matches
            active.packing = false;
            if (!result.ok || !result.probe.nativelyReadable) {
                // writeImagesAtomic is atomic (renames to canonical only on
                // full success), so a writeImagesAtomic failure leaves no
                // canonical; only a post-write probe failure would -- remove
                // this job's own fresh file (safe: still the active job, no
                // newer row can point at it) before failAndCleanup(), which
                // cleans the staging dir but not the canonical.
                if (QFileInfo(canonical).isFile()) QFile::remove(canonical);
                failAndCleanup(active, result.error.isEmpty()
                    ? QStringLiteral("assembled edition pack verification failed") : result.error);
                return;
            }

            Entry e;
            e.seriesId    = active.seriesId;
            e.seriesTitle = active.seriesTitle;
            e.label       = active.label;
            e.archive     = canonical;
            e.files       = active.assembledOrderedFiles;   // authoritative order (NOT probe.entries)
            e.groups      = active.assembledGroups;         // index-parallel to files
            e.bytes       = QFileInfo(canonical).size();
            e.addedAt     = QDateTime::currentMSecsSinceEpoch();
            m_index.insert(active.id, e);
            saveIndex();

            const QString id = active.id;
            const int pageCount = e.files.size();
            // Detach before emit -- a finished() QML slot can re-enter
            // cancelDownload() (see completeSafeMove()'s identical note).
            delete m_active; m_active = nullptr;

            // Save index, THEN delete the staging source -- never the reverse.
            QDir(stagingDir).removeRecursively();

            qInfo() << "[ComicDownloader] assembled edition published id=" << id
                    << "pages=" << pageCount << "archive=" << canonical << "(archive-in-place)";
            emit finished(id);
            startNextQueued();
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// dev smoke
// ─────────────────────────────────────────────────────────────────────────────

void ComicDownloader::selfTest(const QString& postUrl)
{
    const QString id = QStringLiteral("selftest-") + hash10(postUrl);
    qInfo() << "[ComicDownloader] selfTest resolving + downloading" << postUrl << "id=" << id;
    connect(this, &ComicDownloader::finished, this, [this](const QString& i) {
        qInfo() << "[ComicDownloader] selfTest OK id=" << i
                << "pages=" << localPages(i).size();
    });
    connect(this, &ComicDownloader::failed, this, [](const QString& i, const QString& why) {
        qWarning() << "[ComicDownloader] selfTest FAILED id=" << i << "reason=" << why;
    });
    downloadIssue(id, postUrl, QStringLiteral("gc:selftest"), QStringLiteral("selftest"),
                  QStringLiteral("selftest"), 0);
}

void ComicDownloader::selfTestTorrent(const QString& magnetOrHash, const QString& seriesTitle,
                                      const QString& issueLabel)
{
    const QString infoHash = ComicTorrentMagnet::infoHash(magnetOrHash);
    const QString id = QStringLiteral("torrent-selftest-") + hash10(infoHash + issueLabel);
    if (!m_torrents) {
        qWarning() << "[comic-torrent-dl] FAIL service unavailable";
        QCoreApplication::exit(1);
        return;
    }
    deleteIssue(id);
    connect(this, &ComicDownloader::finished, this, [this, id](const QString& finishedId) {
        if (finishedId != id) return;
        const int pages = localPages(id).size();
        if (pages <= 0) {
            qWarning() << "[comic-torrent-dl] FAIL no reader pages" << id;
            QCoreApplication::exit(1);
            return;
        }
        qInfo() << "[comic-torrent-dl] DONE" << id << "pages=" << pages;
        QCoreApplication::exit(0);
    });
    connect(this, &ComicDownloader::failed, this, [id](const QString& failedId,
                                                        const QString& reason) {
        if (failedId != id) return;
        qWarning() << "[comic-torrent-dl] FAIL" << id << reason;
        QCoreApplication::exit(1);
    });
    m_torrents->downloadInfoHash(id, QStringLiteral("gc:torrent-selftest"),
                                 seriesTitle, issueLabel, infoHash,
                                 seriesTitle + QLatin1Char(' ') + issueLabel,
                                 magnetOrHash.startsWith(QStringLiteral("magnet:?"))
                                     ? magnetOrHash : QString());
}

// ── Test-only end-to-end self-test (COLOSSEUM_COMIC_PACK_DLTEST, Task 11) ───

void ComicDownloader::runPackSelfTest(const QString& spec)
{
    const QStringList parts = spec.split(QLatin1Char('|'));
    if (parts.size() < 3) {
        qWarning().noquote() << "[comic-pack-dltest] FAIL bad spec — expected "
                                 "<scenario>|<magnet>|<fixture-id>[|<fixture-id2>]";
        QCoreApplication::exit(2);
        return;
    }
    const QString scenario     = parts.at(0).trimmed().toLower();
    const QString magnetOrHash = parts.at(1).trimmed();
    const QString fixtureId1   = parts.at(2).trimmed();
    const QString fixtureId2   = parts.size() > 3 ? parts.at(3).trimmed() : QString();

    const QString hash = ComicTorrentMagnet::infoHash(magnetOrHash);
    const QString magnetUri = magnetOrHash.startsWith(QStringLiteral("magnet:?"), Qt::CaseInsensitive)
        ? magnetOrHash : QString();
    if (hash.isEmpty() || fixtureId1.isEmpty()) {
        qWarning().noquote() << "[comic-pack-dltest] FAIL bad spec — empty/unparseable "
                                 "magnet/infohash or empty fixture id";
        QCoreApplication::exit(2);
        return;
    }
    if (!m_torrents) {
        qWarning().noquote() << "[comic-pack-dltest] FAIL comic torrent service unavailable";
        QCoreApplication::exit(2);
        return;
    }

    // Hard backstop: never hang — matches the manga/tankoban DLTEST idiom.
    QTimer::singleShot(240000, this, [scenario]() {
        qWarning().noquote() << "[comic-pack-dltest] FAIL timeout scenario=" << scenario;
        QCoreApplication::exit(2);
    });

    if (scenario == QStringLiteral("single")) {
        const DltestPackFixture fx = dltestPackFixture(fixtureId1);
        const QString editionId = dltestPackEditionId(fixtureId1);
        connect(this, &ComicDownloader::finished, this, [this, editionId](const QString& id) {
            if (id != editionId) return;
            const int pages = localPages(editionId).size();
            if (pages <= 0) {
                qWarning().noquote() << "[comic-pack-dltest] FAIL no reader pages for" << editionId;
                QCoreApplication::exit(2);
                return;
            }
            qInfo().noquote() << QStringLiteral("COMIC_PACK_SINGLE_DONE pages=%1").arg(pages);
            QCoreApplication::exit(0);
        });
        connect(this, &ComicDownloader::failed, this,
                [editionId](const QString& id, const QString& reason) {
            if (id != editionId) return;
            qWarning().noquote() << "[comic-pack-dltest] FAIL" << reason;
            QCoreApplication::exit(2);
        });
        downloadTorrentEdition(editionId, fx.seriesId, fx.seriesTitle, fx.editionTitle,
                               fx.isbn, fx.collects, QString(), hash, magnetUri);
        return;
    }

    if (scenario == QStringLiteral("issues")) {
        const DltestPackFixture fx = dltestPackFixture(fixtureId1);
        const QString editionId = dltestPackEditionId(fixtureId1);
        connect(this, &ComicDownloader::finished, this, [this, editionId](const QString& id) {
            if (id != editionId) return;
            const QVariantList pages = localPages(editionId);
            if (pages.isEmpty()) {
                qWarning().noquote() << "[comic-pack-dltest] FAIL no reader pages for" << editionId;
                QCoreApplication::exit(2);
                return;
            }
            QSet<int> groups;
            for (const QVariant& p : pages)
                groups.insert(p.toMap().value(QStringLiteral("group")).toInt());
            qInfo().noquote() << QStringLiteral("COMIC_PACK_ISSUES_DONE pages=%1 groups=%2")
                                     .arg(pages.size()).arg(groups.size());
            QCoreApplication::exit(0);
        });
        connect(this, &ComicDownloader::failed, this,
                [editionId](const QString& id, const QString& reason) {
            if (id != editionId) return;
            qWarning().noquote() << "[comic-pack-dltest] FAIL" << reason;
            QCoreApplication::exit(2);
        });
        downloadTorrentEdition(editionId, fx.seriesId, fx.seriesTitle, fx.editionTitle,
                               fx.isbn, fx.collects, QString(), hash, magnetUri);
        return;
    }

    if (scenario == QStringLiteral("shared")) {
        if (fixtureId2.isEmpty()) {
            qWarning().noquote() << "[comic-pack-dltest] FAIL shared scenario needs two fixture ids";
            QCoreApplication::exit(2);
            return;
        }
        const DltestPackFixture fxA = dltestPackFixture(fixtureId1);
        const DltestPackFixture fxB = dltestPackFixture(fixtureId2);
        const QString editionA = dltestPackEditionId(fixtureId1);
        const QString editionB = dltestPackEditionId(fixtureId2);
        // Heap-owned (not a stack local) — the lambdas that reference it fire
        // later, from the event loop, after this function has returned.
        auto cancelledA = QSharedPointer<bool>::create(false);

        connect(this, &ComicDownloader::progress, this,
                [this, editionA, cancelledA](const QString& id, double done, double) {
            if (id != editionA || *cancelledA || done <= 0) return;
            *cancelledA = true;
            qInfo().noquote() << "[comic-pack-dltest] cancelling" << editionA << "mid-flight";
            cancelDownload(editionA);
        });
        connect(this, &ComicDownloader::finished, this, [this, editionA, editionB](const QString& id) {
            if (id == editionA) {
                qWarning().noquote() << "[comic-pack-dltest] FAIL cancelled edition finished anyway:"
                                      << editionA;
                QCoreApplication::exit(2);
                return;
            }
            if (id != editionB) return;
            const int pages = localPages(editionB).size();
            if (pages <= 0) {
                qWarning().noquote() << "[comic-pack-dltest] FAIL no reader pages for" << editionB;
                QCoreApplication::exit(2);
                return;
            }
            qInfo().noquote() << QStringLiteral("COMIC_PACK_SHARED_DONE pages=%1").arg(pages);
            QCoreApplication::exit(0);
        });
        connect(this, &ComicDownloader::failed, this,
                [editionA, editionB](const QString& id, const QString& reason) {
            if (id == editionA) {
                // Expected — this is OUR deliberate mid-flight cancel.
                if (reason.contains(QStringLiteral("cancelled"))) return;
                qWarning().noquote() << "[comic-pack-dltest] FAIL edition A failed unexpectedly:"
                                      << reason;
                QCoreApplication::exit(2);
                return;
            }
            if (id != editionB) return;
            qWarning().noquote() << "[comic-pack-dltest] FAIL" << reason;
            QCoreApplication::exit(2);
        });

        downloadTorrentEdition(editionA, fxA.seriesId, fxA.seriesTitle, fxA.editionTitle,
                               fxA.isbn, fxA.collects, QString(), hash, magnetUri);
        downloadTorrentEdition(editionB, fxB.seriesId, fxB.seriesTitle, fxB.editionTitle,
                               fxB.isbn, fxB.collects, QString(), hash, magnetUri);
        return;
    }

    if (scenario == QStringLiteral("restart")) {
        // The ledger replay this proves already ran INSIDE this object's own
        // construction (ComicTorrentDownloader's constructor replays every
        // active row, re-adding the shared torrent paused before this method
        // is ever reached from main.cpp) — this branch only OBSERVES that
        // in-flight replay to its terminal state. It must never start a fresh
        // request; doing so would defeat the point of proving replay works.
        const QString editionId = dltestPackEditionId(fixtureId1);
        auto reportDone = [this, editionId]() {
            const int pages = localPages(editionId).size();
            if (pages <= 0) {
                qWarning().noquote() << "[comic-pack-dltest] FAIL no reader pages for" << editionId;
                QCoreApplication::exit(2);
                return;
            }
            int records = 0;
            for (const QVariant& row : downloadedIssues())
                if (row.toMap().value(QStringLiteral("id")).toString() == editionId) ++records;
            qInfo().noquote() << QStringLiteral("COMIC_PACK_RESTART_DONE pages=%1 records=%2")
                                     .arg(pages).arg(records);
            QCoreApplication::exit(0);
        };
        if (isDownloaded(editionId)) { reportDone(); return; }
        connect(this, &ComicDownloader::finished, this, [editionId, reportDone](const QString& id) {
            if (id != editionId) return;
            reportDone();
        });
        connect(this, &ComicDownloader::failed, this,
                [editionId](const QString& id, const QString& reason) {
            if (id != editionId) return;
            qWarning().noquote() << "[comic-pack-dltest] FAIL" << reason;
            QCoreApplication::exit(2);
        });
        return;
    }

    qWarning().noquote() << "[comic-pack-dltest] FAIL unknown scenario" << scenario;
    QCoreApplication::exit(2);
}

QVariantMap ComicDownloader::downloadedIssueRow(const QString& id, const Entry& e) const
{
    // First page = the issue's own local cover (Downloads-page art). An
    // archive row has no loose page file to build a file:// URL from --
    // Task 3's image://comiccover/ provider decodes it straight from the
    // CBZ instead. A legacy dir row is untouched (Task 2/3 predate any
    // writer that could set `archive`, so every row today still takes
    // this branch; it stays correct once Task 4 starts producing them).
    bool missing = true;
    QString art;
    if (e.usesArchive()) {
        missing = e.files.isEmpty() || !QFileInfo(e.archive).isFile();
        if (!missing) {
            art = QStringLiteral("image://comiccover/")
                + Colosseum::buildComicCoverId(e.archive, e.files.first());
        }
    } else {
        const QString first = e.files.isEmpty()
            ? QString() : e.dir + QStringLiteral("/") + e.files.first();
        missing = first.isEmpty() || !QFile::exists(first);
        if (!missing) art = QUrl::fromLocalFile(first).toString();
    }
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("seriesId"), e.seriesId},
        {QStringLiteral("seriesTitle"), e.seriesTitle},
        {QStringLiteral("label"), e.label},
        {QStringLiteral("pages"), e.files.size()},
        {QStringLiteral("bytes"), e.bytes},
        {QStringLiteral("addedAt"), e.addedAt},
        {QStringLiteral("missing"), missing},
        {QStringLiteral("art"), art},
        // Pack-demux fields (Slice 1): absent/empty on every ordinary issue;
        // a demuxed volume carries its parsed role ("main"/"extra") and the
        // deterministic order the shelf/reader consume. Existing QML that
        // does not read these keys is unaffected (additive map entries).
        {QStringLiteral("packRole"), e.packRole},
        {QStringLiteral("packOrder"), e.packOrder}
    };
}

QVariantList ComicDownloader::downloadedIssues() const
{
    QVariantList out;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it)
        out.append(downloadedIssueRow(it.key(), it.value()));
    return out;
}

QVariantMap ComicDownloader::packVolumes(const QString& seriesId) const
{
    // Slice 4: the shelf/reader contract. Collect every indexed row for this
    // seriesId that has a non-empty packRole, split into mains/extras by role,
    // and sort each by packOrder ASCENDING (v1 first — natural reading order).
    // The QML reader adapts to its own newest-first chapters convention; this
    // API hands the volumes in the order a human reads them.
    QVariantList mains, extras;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        const Entry& e = it.value();
        if (e.seriesId != seriesId) continue;
        if (e.packRole.isEmpty()) continue;   // ordinary issue — not this API's business
        if (e.packRole == QStringLiteral("main"))
            mains.append(downloadedIssueRow(it.key(), e));
        else
            extras.append(downloadedIssueRow(it.key(), e));
    }
    auto byOrder = [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("packOrder")).toInt()
             < b.toMap().value(QStringLiteral("packOrder")).toInt();
    };
    std::sort(mains.begin(), mains.end(), byOrder);
    std::sort(extras.begin(), extras.end(), byOrder);
    return QVariantMap{
        {QStringLiteral("mains"), mains},
        {QStringLiteral("extras"), extras}
    };
}

QVariantList ComicDownloader::activeIssueJobs() const
{
    QVariantList out;
    auto row = [](const InFlight& f, const QString& state) {
        return QVariantMap{
            {QStringLiteral("id"), f.id},
            {QStringLiteral("seriesId"), f.seriesId},
            {QStringLiteral("seriesTitle"), f.seriesTitle},
            {QStringLiteral("label"), f.label},
            {QStringLiteral("state"), state},
            {QStringLiteral("done"), double(f.receivedBytes)},
            {QStringLiteral("total"), double(f.expectedBytes)},
            // Empty today (no producer sets partGroupKey yet) — falls back to the row's own id
            // in the Downloads page's grouping, i.e. today's exact single-row rendering. Ready
            // for a future multi-part fix with zero further plumbing here.
            {QStringLiteral("groupKey"), f.partGroupKey},
            // Pack demux (Slice 2): a pack's children emit "volumes" so the
            // Downloads page can render "3/8 volumes" instead of "3/8 parts".
            // Ordinary issues still emit "parts" (the InFlight default).
            {QStringLiteral("groupUnit"), f.groupUnit}
        };
    };
    if (m_active)
        out.append(row(*m_active, m_active->extracting
                       ? QStringLiteral("extracting") : QStringLiteral("downloading")));
    for (auto it = m_resolving.constBegin(); it != m_resolving.constEnd(); ++it)
        out.append(row(it.value(), QStringLiteral("resolving")));
    for (const InFlight& q : m_queue)
        out.append(row(q, QStringLiteral("queued")));
    if (m_torrents) {
        const QVariantList torrentJobs = m_torrents->activeJobs();
        for (const QVariant& job : torrentJobs) out.append(job);
    }
    return out;
}
