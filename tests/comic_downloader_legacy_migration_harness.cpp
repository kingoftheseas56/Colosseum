// Task 7 (CBZ-in-place plan, docs/superpowers/plans/2026-08-06-comics-cbz-in-
// place.md): boot-time legacy migration -- repair-before-prune, two-boot
// reclaim. A pre-existing legacy loose-folder comic row (dir set, no archive)
// is packed into a canonical CBZ on FIRST boot with `archive` set and `dir`
// LEFT ALONE (Pass 1); the loose files are reclaimed only on a LATER boot,
// once the archive independently re-verifies openable (Pass 2). This is the
// deliberate amendment past manga's own migrateLegacy() (which clears the dir
// in the same pass): comics never destroys the loose source until a separate
// boot has proven the replacement good.
//
// Migration runs synchronously inside loadIndex(), so a fresh ComicDownloader
// construction over an isolated AppData index IS the "boot". A second
// construction over the same (now-migrated) index simulates the next boot.
// Everything is asserted through the public API + the raw index.json, never a
// reach into private internals -- the same house style as
// comic_downloader_archive_ingest_harness.cpp, whose fixture/isolation
// conventions this file mirrors.
#include "engine/CbzArchive.h"
#include "engine/ComicDownloader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

namespace {

// ── AppData-rooted paths (the real ones ComicDownloader derives) ────────────

QString baseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/comics");
}
QString indexPath() { return baseDir() + QStringLiteral("/index.json"); }

// Mirrors of ComicDownloader's private path helpers so this harness can
// independently predict where the canonical archive lands, WITHOUT reaching
// into private internals. If these drift from the real implementation every
// scenario fails loudly (canonical not where predicted), never silently green.
QString hash10Mirror(const QString& v)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}
QString safeSegMirror(const QString& v)
{
    QString out;
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
QString issueDirMirror(const QString& seriesId, const QString& label, const QString& id)
{
    return baseDir() + QChar('/') + safeSegMirror(seriesId) + QChar('/')
           + safeSegMirror(label) + QChar('-') + hash10Mirror(id);
}
QString issueArchivePathMirror(const QString& seriesId, const QString& label, const QString& id)
{
    return issueDirMirror(seriesId, label, id) + QStringLiteral(".cbz");
}

// Wipe the whole comics library so each scenario boots from a clean slate --
// migration runs on EVERY construction, so a leftover row from a prior
// scenario would otherwise be migrated under the next scenario's feet.
void resetLibrary()
{
    QDir(baseDir()).removeRecursively();
    QDir().mkpath(baseDir());
}

void writeIndexFixture(const QJsonObject& root)
{
    QDir().mkpath(baseDir());
    QFile f(indexPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QJsonObject readIndexRaw()
{
    QFile f(indexPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// A legacy loose-folder row exactly as the pre-CBZ-in-place download path
// produced it: `dir` set, `files` listed, NO `archive` field.
QJsonObject legacyRow(const QString& seriesId, const QString& seriesTitle,
                      const QString& label, const QString& dir, const QStringList& files)
{
    QJsonObject o;
    o[QStringLiteral("seriesId")]    = seriesId;
    o[QStringLiteral("seriesTitle")] = seriesTitle;
    o[QStringLiteral("label")]       = label;
    o[QStringLiteral("dir")]         = dir;
    o[QStringLiteral("bytes")]       = 4321.0;
    o[QStringLiteral("addedAt")]     = 2000.0;
    QJsonArray fa;
    for (const QString& f : files) fa.append(f);
    o[QStringLiteral("files")] = fa;
    o[QStringLiteral("groups")] = QJsonArray();
    return o;
}

// Build a legacy loose page directory of real, decodable JPEG images (the
// probe()/round-trip gate byte-sniffs, so placeholder text bytes would fail).
bool makeLegacyDir(const QString& dir, const QStringList& pageNames)
{
    QDir().mkpath(dir);
    for (int i = 0; i < pageNames.size(); ++i) {
        QImage img(48, 48, QImage::Format_ARGB32);
        img.fill(qRgb(15 * (i + 1), 90, 180));
        if (!img.save(dir + QChar('/') + pageNames.at(i), "JPEG")) return false;
    }
    return true;
}

QByteArray fileBytes(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

// Every listed page must decode out of the archive named on the row.
bool archiveRoundTrips(const QString& archive, const QStringList& files)
{
    for (const QString& name : files) {
        QString err;
        if (MangaTankoban::CbzArchive::readEntry(archive, name, &err).isEmpty()) {
            std::printf("      (round-trip: entry \"%s\" did not decode: %s)\n",
                        qPrintable(name), qPrintable(err));
            return false;
        }
    }
    return true;
}

// localPages() must resolve every page via the archive (archive+entry present,
// entry order preserved, each decodes) -- the reader-facing proof migration
// actually produced a usable comic.
bool localPagesResolveViaArchive(ComicDownloader& comics, const QString& id,
                                 const QStringList& files)
{
    const QVariantList pages = comics.localPages(id);
    if (pages.size() != files.size()) {
        std::printf("      (localPages size %d != files %d)\n",
                    (int)pages.size(), (int)files.size());
        return false;
    }
    for (int i = 0; i < pages.size(); ++i) {
        const QVariantMap p = pages.at(i).toMap();
        const QString archive = p.value(QStringLiteral("archive")).toString();
        const QString entry   = p.value(QStringLiteral("entry")).toString();
        if (archive.isEmpty() || entry.isEmpty()) {
            std::printf("      (localPages[%d] not archive-shaped)\n", i);
            return false;
        }
        if (entry != files.at(i)) {
            std::printf("      (localPages[%d] entry \"%s\" != \"%s\" -- page order leaked)\n",
                        i, qPrintable(entry), qPrintable(files.at(i)));
            return false;
        }
        QString err;
        if (MangaTankoban::CbzArchive::readEntry(archive, entry, &err).isEmpty()) {
            std::printf("      (localPages[%d] does not decode: %s)\n", i, qPrintable(err));
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 1: the two-boot happy path (Pass 1 packs+keeps dir; Pass 2 reclaims)
// ─────────────────────────────────────────────────────────────────────────────
bool runTwoBootMigrationScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-descender");
    const QString seriesId = QStringLiteral("gcd_119237");
    const QString seriesTitle = QStringLiteral("Descender");
    const QString label = QStringLiteral("The Deluxe Edition Vol. 1");
    const QStringList files{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg"),
                            QStringLiteral("page_002.jpg")};

    const QString looseDir = fixtures.path() + QStringLiteral("/twoboot-legacy");
    if (!makeLegacyDir(looseDir, files)) {
        std::printf("FAIL: could not build the legacy loose-page fixture\n");
        return false;
    }
    QJsonObject root;
    root[id] = legacyRow(seriesId, seriesTitle, label, looseDir, files);
    writeIndexFixture(root);

    const QString canonical = issueArchivePathMirror(seriesId, label, id);

    // ── Boot 1: Pass 1 must pack, set `archive`, and LEAVE `dir` alone. ──
    {
        ComicDownloader comics(nam);   // loadIndex() -> migrateLegacyComicsInPlace() runs here

        if (!QFileInfo(canonical).isFile()) {
            std::printf("FAIL: Pass 1 did not create the canonical CBZ at %s\n", qPrintable(canonical));
            return false;
        }
        if (!archiveRoundTrips(canonical, files)) {
            std::printf("FAIL: Pass 1 canonical CBZ does not round-trip every page\n");
            return false;
        }
        if (!QDir(looseDir).exists()) {
            std::printf("FAIL: Pass 1 deleted the loose dir the SAME boot (should keep it one boot)\n");
            return false;
        }
        const QJsonObject rawAfter1 = readIndexRaw().value(id).toObject();
        if (rawAfter1.value(QStringLiteral("archive")).toString() != canonical) {
            std::printf("FAIL: Pass 1 did not persist `archive` = canonical\n");
            return false;
        }
        if (rawAfter1.value(QStringLiteral("dir")).toString() != looseDir) {
            std::printf("FAIL: Pass 1 changed/cleared `dir` (must be left alone for one boot)\n");
            return false;
        }
        const QJsonArray rawFiles = rawAfter1.value(QStringLiteral("files")).toArray();
        if (rawFiles.size() != files.size()) {
            std::printf("FAIL: Pass 1 rewrote the files list (should stay the legacy page names)\n");
            return false;
        }
        for (int i = 0; i < files.size(); ++i)
            if (rawFiles.at(i).toString() != files.at(i)) {
                std::printf("FAIL: Pass 1 reordered/renamed files[%d]\n", i);
                return false;
            }
        if (!comics.isDownloaded(id)) {
            std::printf("FAIL: migrated row not reported downloaded after Pass 1\n");
            return false;
        }
        if (!localPagesResolveViaArchive(comics, id, files)) {
            std::printf("FAIL: after Pass 1, localPages() does not resolve via the archive\n");
            return false;
        }
    }

    // ── Boot 2: Pass 2 must reclaim the loose dir and clear `dir`. ──
    {
        ComicDownloader comics(nam);   // second boot over the archive+dir row

        if (QDir(looseDir).exists()) {
            std::printf("FAIL: Pass 2 did not reclaim the loose dir on the second boot\n");
            return false;
        }
        const QJsonObject rawAfter2 = readIndexRaw().value(id).toObject();
        if (!rawAfter2.value(QStringLiteral("dir")).toString().isEmpty()) {
            std::printf("FAIL: Pass 2 did not clear `dir` after reclaiming the loose files\n");
            return false;
        }
        if (rawAfter2.value(QStringLiteral("archive")).toString() != canonical) {
            std::printf("FAIL: Pass 2 disturbed the archive field\n");
            return false;
        }
        if (!comics.isDownloaded(id) || !localPagesResolveViaArchive(comics, id, files)) {
            std::printf("FAIL: after Pass 2, the comic is no longer a readable archive row\n");
            return false;
        }
    }

    // ── Boot 3: idempotent -- nothing left to do, nothing broken. ──
    {
        ComicDownloader comics(nam);
        if (!comics.isDownloaded(id) || !localPagesResolveViaArchive(comics, id, files)) {
            std::printf("FAIL: a third boot disturbed a fully-migrated archive row\n");
            return false;
        }
    }

    std::printf("OK: two-boot migration -- Pass 1 packs & keeps dir, Pass 2 reclaims, boot 3 idempotent\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 2: a listed page missing -> migrate not at all, nothing deleted
// ─────────────────────────────────────────────────────────────────────────────
bool runMissingPageSkipsScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-torn");
    const QString seriesId = QStringLiteral("gcd_torn");
    const QString seriesTitle = QStringLiteral("Torn Series");
    const QString label = QStringLiteral("Issue Torn");
    // The row LISTS three pages but the dir only has two -- page_002 is gone.
    const QStringList listed{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg"),
                             QStringLiteral("page_002.jpg")};
    const QStringList present{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString looseDir = fixtures.path() + QStringLiteral("/torn-legacy");
    if (!makeLegacyDir(looseDir, present)) {
        std::printf("FAIL: could not build the torn legacy fixture\n");
        return false;
    }
    QJsonObject root;
    root[id] = legacyRow(seriesId, seriesTitle, label, looseDir, listed);
    writeIndexFixture(root);

    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    const QByteArray p0Before = fileBytes(looseDir + QStringLiteral("/page_000.jpg"));

    ComicDownloader comics(nam);

    if (QFileInfo(canonical).exists()) {
        std::printf("FAIL: a canonical CBZ was written for a row with a missing page\n");
        return false;
    }
    const QJsonObject raw = readIndexRaw().value(id).toObject();
    if (!raw.value(QStringLiteral("archive")).toString().isEmpty()) {
        std::printf("FAIL: `archive` was set despite the missing page\n");
        return false;
    }
    if (raw.value(QStringLiteral("dir")).toString() != looseDir) {
        std::printf("FAIL: the untouched legacy row's `dir` was disturbed\n");
        return false;
    }
    if (!QDir(looseDir).exists()
        || fileBytes(looseDir + QStringLiteral("/page_000.jpg")) != p0Before) {
        std::printf("FAIL: the loose source was altered/deleted despite skipping migration\n");
        return false;
    }
    // The row still works off its dir (loadIndex kept it -- 2 of 3 files present
    // still passes the dir keep-condition of !files.isEmpty()).
    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: the un-migrated legacy row stopped being downloaded\n");
        return false;
    }

    std::printf("OK: a legacy row with a missing listed page is left untouched -- nothing packed, nothing deleted\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 3: crash recovery -- a valid canonical already exists -> ADOPT, no repack
// ─────────────────────────────────────────────────────────────────────────────
bool runAdoptExistingCanonicalScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-adopt");
    const QString seriesId = QStringLiteral("gcd_adopt");
    const QString seriesTitle = QStringLiteral("Adopt Series");
    const QString label = QStringLiteral("Issue Adopt");
    const QStringList files{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString looseDir = fixtures.path() + QStringLiteral("/adopt-legacy");
    if (!makeLegacyDir(looseDir, files)) {
        std::printf("FAIL: could not build the adopt legacy fixture\n");
        return false;
    }

    // Pre-create a VALID canonical from the SAME loose pages (an interrupted
    // prior migration that packed but never reached saveIndex()).
    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    {
        QString err;
        if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, looseDir, files, &err)) {
            std::printf("FAIL: could not pre-build the leftover canonical: %s\n", qPrintable(err));
            return false;
        }
    }
    const QByteArray canonBefore = fileBytes(canonical);

    QJsonObject root;
    root[id] = legacyRow(seriesId, seriesTitle, label, looseDir, files);
    writeIndexFixture(root);

    ComicDownloader comics(nam);

    // Adopted, not repacked: identical bytes prove writeImagesAtomic never ran again.
    if (fileBytes(canonical) != canonBefore) {
        std::printf("FAIL: the existing valid canonical was repacked instead of adopted (bytes changed)\n");
        return false;
    }
    const QJsonObject raw = readIndexRaw().value(id).toObject();
    if (raw.value(QStringLiteral("archive")).toString() != canonical) {
        std::printf("FAIL: adoption did not set `archive` to the existing canonical\n");
        return false;
    }
    if (raw.value(QStringLiteral("dir")).toString() != looseDir) {
        std::printf("FAIL: adoption cleared `dir` the same boot (Pass 1 must keep it one boot)\n");
        return false;
    }
    if (!comics.isDownloaded(id) || !localPagesResolveViaArchive(comics, id, files)) {
        std::printf("FAIL: the adopted row is not a readable archive row\n");
        return false;
    }

    std::printf("OK: an interrupted-prior valid canonical is adopted directly, no repack\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 4: crash recovery -- a stale/invalid canonical -> discard + repack
// ─────────────────────────────────────────────────────────────────────────────
bool runReplaceInvalidCanonicalScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-stale-canonical");
    const QString seriesId = QStringLiteral("gcd_stale");
    const QString seriesTitle = QStringLiteral("Stale Series");
    const QString label = QStringLiteral("Issue Stale");
    const QStringList files{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString looseDir = fixtures.path() + QStringLiteral("/stale-legacy");
    if (!makeLegacyDir(looseDir, files)) {
        std::printf("FAIL: could not build the stale legacy fixture\n");
        return false;
    }

    // Pre-create a canonical that is NOT a readable archive (garbage bytes) --
    // an interrupted attempt that never finished writing a valid file.
    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    { QFile f(canonical); if (f.open(QIODevice::WriteOnly)) f.write("not-a-zip-partial-write-garbage"); }
    {
        QString err;
        if (MangaTankoban::CbzArchive::probe(canonical, &err).nativelyReadable) {
            std::printf("FAIL: setup -- the garbage canonical unexpectedly probed readable\n");
            return false;
        }
    }

    QJsonObject root;
    root[id] = legacyRow(seriesId, seriesTitle, label, looseDir, files);
    writeIndexFixture(root);

    ComicDownloader comics(nam);

    // The garbage was discarded and a real CBZ repacked from the loose source.
    QString err;
    if (!MangaTankoban::CbzArchive::probe(canonical, &err).nativelyReadable) {
        std::printf("FAIL: the stale canonical was not replaced with a valid repacked CBZ: %s\n",
                    qPrintable(err));
        return false;
    }
    if (!archiveRoundTrips(canonical, files)) {
        std::printf("FAIL: the repacked canonical does not round-trip every page\n");
        return false;
    }
    const QJsonObject raw = readIndexRaw().value(id).toObject();
    if (raw.value(QStringLiteral("archive")).toString() != canonical
        || raw.value(QStringLiteral("dir")).toString() != looseDir) {
        std::printf("FAIL: repack did not persist archive+dir (Pass 1 shape)\n");
        return false;
    }
    if (!comics.isDownloaded(id) || !localPagesResolveViaArchive(comics, id, files)) {
        std::printf("FAIL: the repacked row is not a readable archive row\n");
        return false;
    }

    std::printf("OK: a stale/invalid leftover canonical is discarded and repacked from the loose source\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 5: an already-migrated row whose archive turns UNREADABLE -> Pass 2
// demotes it back to the dir, and a subsequent boot re-packs (never data loss)
// ─────────────────────────────────────────────────────────────────────────────
bool runDemoteThenRepackScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-demote");
    const QString seriesId = QStringLiteral("gcd_demote");
    const QString seriesTitle = QStringLiteral("Demote Series");
    const QString label = QStringLiteral("Issue Demote");
    const QStringList files{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString looseDir = fixtures.path() + QStringLiteral("/demote-legacy");
    if (!makeLegacyDir(looseDir, files)) {
        std::printf("FAIL: could not build the demote legacy fixture\n");
        return false;
    }

    // A row that LOOKS migrated (archive set + dir still present) but whose
    // archive file is present-yet-corrupt: isFile() true, probe() false. This
    // is the shape loadIndex() keeps (it only checks isFile), so Pass 2 must be
    // the one to catch the unreadability and fall back to the loose dir.
    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    { QFile f(canonical); if (f.open(QIODevice::WriteOnly)) f.write("corrupt-archive-that-isFile-but-not-a-zip"); }

    QJsonObject rowObj = legacyRow(seriesId, seriesTitle, label, looseDir, files);
    rowObj[QStringLiteral("archive")] = canonical;   // both archive AND dir set
    QJsonObject root;
    root[id] = rowObj;
    writeIndexFixture(root);

    // ── Boot A: Pass 2 sees archive unreadable + dir present -> demote (clear
    //    archive, keep dir), delete nothing. ──
    {
        ComicDownloader comics(nam);
        const QJsonObject raw = readIndexRaw().value(id).toObject();
        if (!raw.value(QStringLiteral("archive")).toString().isEmpty()) {
            std::printf("FAIL: Pass 2 did not demote an unreadable archive back to the dir\n");
            return false;
        }
        if (raw.value(QStringLiteral("dir")).toString() != looseDir || !QDir(looseDir).exists()) {
            std::printf("FAIL: demotion disturbed the loose dir (must be preserved for re-pack)\n");
            return false;
        }
        if (!comics.isDownloaded(id)) {
            std::printf("FAIL: demoted row stopped being downloaded (should survive off its dir)\n");
            return false;
        }
    }

    // ── Boot B: now a plain legacy dir row -> Pass 1 re-packs a VALID canonical. ──
    {
        ComicDownloader comics(nam);
        QString err;
        if (!MangaTankoban::CbzArchive::probe(canonical, &err).nativelyReadable) {
            std::printf("FAIL: the boot after demotion did not re-pack a valid archive: %s\n",
                        qPrintable(err));
            return false;
        }
        if (!comics.isDownloaded(id) || !localPagesResolveViaArchive(comics, id, files)) {
            std::printf("FAIL: the re-packed row is not a readable archive row\n");
            return false;
        }
        const QJsonObject raw = readIndexRaw().value(id).toObject();
        if (raw.value(QStringLiteral("archive")).toString() != canonical
            || raw.value(QStringLiteral("dir")).toString() != looseDir) {
            std::printf("FAIL: the re-pack did not restore the Pass 1 shape (archive+dir)\n");
            return false;
        }
    }

    std::printf("OK: an unreadable 'migrated' archive is demoted to its dir, then re-packed -- no data loss\n");
    return true;
}

// Build a dir of real, DISTINCT JPEGs -- dims[i] sets page i's dimensions so
// pages differ in byte size (needed to prove the per-page size verify), and a
// per-index fill colour keeps every page's bytes distinct (needed to prove a
// byte-exact readback isn't accidentally comparing identical blobs).
bool makeJpegDir(const QString& dir, const QStringList& names, const QList<int>& dims)
{
    QDir().mkpath(dir);
    for (int i = 0; i < names.size(); ++i) {
        const int d = dims.value(i, 48);
        QImage img(d, d, QImage::Format_ARGB32);
        img.fill(qRgb((13 * (i + 1)) % 256, (i * 7) % 256, 180));
        if (!img.save(dir + QChar('/') + names.at(i), "JPEG")) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 6: page ORDER is preserved as listed (not probe's collation sort)
// AND every migrated page reads back byte-for-byte identical to its loose source
// ─────────────────────────────────────────────────────────────────────────────
bool runOrderAndByteExactScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-order");
    const QString seriesId = QStringLiteral("gcd_order");
    const QString seriesTitle = QStringLiteral("Order Series");
    const QString label = QStringLiteral("Issue Order");
    // Names whose numeric-collation order (1,10,2,3,…) differs from the listed
    // reading order (1,2,3,…,10). Pass 1 must keep the LISTED order; if probe's
    // collator sort ever leaked into the page list, entry[1] would be "10.jpg".
    const QStringList files{
        QStringLiteral("1.jpg"), QStringLiteral("2.jpg"), QStringLiteral("3.jpg"),
        QStringLiteral("4.jpg"), QStringLiteral("10.jpg")};

    const QString looseDir = fixtures.path() + QStringLiteral("/order-legacy");
    if (!makeJpegDir(looseDir, files, {40, 52, 64, 48, 72})) {
        std::printf("FAIL: could not build the order fixture\n");
        return false;
    }
    // Capture the original loose bytes BEFORE migration (Pass 2 later deletes them).
    QList<QByteArray> original;
    for (const QString& n : files) original.append(fileBytes(looseDir + QChar('/') + n));

    QJsonObject root;
    root[id] = legacyRow(seriesId, seriesTitle, label, looseDir, files);
    writeIndexFixture(root);

    const QString canonical = issueArchivePathMirror(seriesId, label, id);

    {
        ComicDownloader comics(nam);   // Pass 1
        const QVariantList pages = comics.localPages(id);
        if (pages.size() != files.size()) {
            std::printf("FAIL: order scenario localPages size %d != %d\n",
                        (int)pages.size(), (int)files.size());
            return false;
        }
        for (int i = 0; i < files.size(); ++i) {
            const QVariantMap p = pages.at(i).toMap();
            if (p.value(QStringLiteral("entry")).toString() != files.at(i)) {
                std::printf("FAIL: page order scrambled at [%d]: entry \"%s\" != \"%s\" "
                            "(probe collation leaked into the page list)\n", i,
                            qPrintable(p.value(QStringLiteral("entry")).toString()),
                            qPrintable(files.at(i)));
                return false;
            }
            QString err;
            const QByteArray got =
                MangaTankoban::CbzArchive::readEntry(canonical, files.at(i), &err);
            if (got != original.at(i)) {
                std::printf("FAIL: page[%d] \"%s\" did not read back byte-for-byte "
                            "(migration altered the page bytes)\n", i, qPrintable(files.at(i)));
                return false;
            }
        }
    }

    std::printf("OK: page order is preserved as listed and every page reads back byte-exact\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 7: a canonical whose page OUTSIDE probe()'s {first,middle,last}
// sample window has the wrong size is rejected (probe alone would miss it),
// discarded, and correctly repacked from the true loose source
// ─────────────────────────────────────────────────────────────────────────────
bool runOffSampleCorruptionScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-offsample");
    const QString seriesId = QStringLiteral("gcd_offsample");
    const QString seriesTitle = QStringLiteral("OffSample Series");
    const QString label = QStringLiteral("Issue OffSample");
    // 5 pages -> probe samples indices {0, 2, 4}; index 3 is off-sample.
    const QStringList files{
        QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg"),
        QStringLiteral("page_002.jpg"), QStringLiteral("page_003.jpg"),
        QStringLiteral("page_004.jpg")};

    const QString trueDir = fixtures.path() + QStringLiteral("/offsample-true");
    if (!makeJpegDir(trueDir, files, {48, 48, 48, 48, 48})) {
        std::printf("FAIL: could not build the true off-sample fixture\n");
        return false;
    }
    // A tampered source identical EXCEPT page_003 (off-sample) is a different
    // size -- a valid, decodable image, so probe()'s sniff of the sampled pages
    // still passes; only the per-page size verify can catch it.
    const QString tamperedDir = fixtures.path() + QStringLiteral("/offsample-tampered");
    if (!makeJpegDir(tamperedDir, files, {48, 48, 48, 96, 48})) {
        std::printf("FAIL: could not build the tampered off-sample fixture\n");
        return false;
    }

    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    {
        QString err;
        if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, tamperedDir, files, &err)) {
            std::printf("FAIL: could not pre-build the tampered canonical: %s\n", qPrintable(err));
            return false;
        }
    }
    // Guard: the tampered canonical genuinely probes readable (so ONLY the size
    // verify, not probe, is what rejects it below).
    {
        QString err;
        if (!MangaTankoban::CbzArchive::probe(canonical, &err).nativelyReadable) {
            std::printf("FAIL: setup -- tampered canonical unexpectedly failed probe()\n");
            return false;
        }
    }

    QJsonObject root;
    root[id] = legacyRow(seriesId, seriesTitle, label, trueDir, files);
    writeIndexFixture(root);

    ComicDownloader comics(nam);   // Pass 1: adopt gate must REJECT the tampered canonical, repack

    // The repacked canonical must now match the TRUE loose source page-for-page.
    QString err;
    const MangaTankoban::CbzProbeResult probe = MangaTankoban::CbzArchive::probe(canonical, &err);
    if (!probe.nativelyReadable) {
        std::printf("FAIL: canonical not readable after repack: %s\n", qPrintable(err));
        return false;
    }
    const QByteArray migratedP3 =
        MangaTankoban::CbzArchive::readEntry(canonical, QStringLiteral("page_003.jpg"), &err);
    if (migratedP3 != fileBytes(trueDir + QStringLiteral("/page_003.jpg"))) {
        std::printf("FAIL: off-sample page_003 was NOT corrected -- the tampered canonical was "
                    "adopted despite a size mismatch probe() could not see\n");
        return false;
    }
    if (!comics.isDownloaded(id) || !localPagesResolveViaArchive(comics, id, files)) {
        std::printf("FAIL: the repacked off-sample row is not a readable archive row\n");
        return false;
    }

    std::printf("OK: an off-sample page-size mismatch is caught, the stale canonical discarded and repacked\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scenario 8: deleteIssue() on a fully-migrated archive row that still carries
// a stale (nonexistent) `dir` string must still succeed -- otherwise the comic
// would be undeletable from the UI (the §4 inert-string check)
// ─────────────────────────────────────────────────────────────────────────────
bool runDeleteStaleDirRowScenario(QNetworkAccessManager* nam, QTemporaryDir& fixtures)
{
    resetLibrary();
    const QString id = QStringLiteral("legacy-staledir-delete");
    const QString seriesId = QStringLiteral("gcd_staledir");
    const QString seriesTitle = QStringLiteral("StaleDir Series");
    const QString label = QStringLiteral("Issue StaleDir");
    const QStringList files{QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")};

    const QString pagesDir = fixtures.path() + QStringLiteral("/staledir-pages");
    if (!makeJpegDir(pagesDir, files, {48, 48})) {
        std::printf("FAIL: could not build the stale-dir fixture pages\n");
        return false;
    }
    const QString canonical = issueArchivePathMirror(seriesId, label, id);
    QDir().mkpath(QFileInfo(canonical).absolutePath());
    {
        QString err;
        if (!MangaTankoban::CbzArchive::writeImagesAtomic(canonical, pagesDir, files, &err)) {
            std::printf("FAIL: could not build the stale-dir canonical: %s\n", qPrintable(err));
            return false;
        }
    }
    // A valid archive row carrying a `dir` that does NOT exist (the shape a
    // crash between Pass 2's removal and the index commit leaves behind).
    QJsonObject rowObj = legacyRow(seriesId, seriesTitle, label,
                                   fixtures.path() + QStringLiteral("/staledir-does-not-exist"),
                                   files);
    rowObj[QStringLiteral("archive")] = canonical;
    QJsonObject root;
    root[id] = rowObj;
    writeIndexFixture(root);

    ComicDownloader comics(nam);   // migration leaves it inert (dir doesn't exist -> Pass 2 skips)

    if (!comics.isDownloaded(id)) {
        std::printf("FAIL: setup -- stale-dir archive row not recognized as downloaded\n");
        return false;
    }
    const QVariantMap result = comics.deleteIssue(id);
    if (!result.value(QStringLiteral("success")).toBool()) {
        std::printf("FAIL: deleteIssue() failed on an archive row with a stale nonexistent `dir` "
                    "-- the comic would be undeletable from the UI\n");
        return false;
    }
    if (comics.isDownloaded(id) || QFileInfo::exists(canonical)) {
        std::printf("FAIL: deleteIssue() did not fully remove the stale-dir archive row\n");
        return false;
    }

    std::printf("OK: deleteIssue() succeeds on an archive row carrying a stale nonexistent dir\n");
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("ComicDownloaderLegacyMigrationHarness"));

    QNetworkAccessManager nam;
    QTemporaryDir fixtures;
    if (!fixtures.isValid()) {
        std::printf("FAIL: could not create fixtures temp dir\n");
        return 1;
    }

    int failures = 0;
    if (!runTwoBootMigrationScenario(&nam, fixtures)) ++failures;
    if (!runMissingPageSkipsScenario(&nam, fixtures)) ++failures;
    if (!runAdoptExistingCanonicalScenario(&nam, fixtures)) ++failures;
    if (!runReplaceInvalidCanonicalScenario(&nam, fixtures)) ++failures;
    if (!runDemoteThenRepackScenario(&nam, fixtures)) ++failures;
    if (!runOrderAndByteExactScenario(&nam, fixtures)) ++failures;
    if (!runOffSampleCorruptionScenario(&nam, fixtures)) ++failures;
    if (!runDeleteStaleDirRowScenario(&nam, fixtures)) ++failures;

    Q_UNUSED(app);
    if (failures == 0)
        std::printf("comic_downloader_legacy_migration_harness OK\n");
    return failures == 0 ? 0 : 1;
}
