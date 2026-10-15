#include "engine/ComicEditionAssembler.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

using ComicEditionFileSelector::ComicPayloadKind;
using ComicEditionFileSelector::ComicSelectedFile;

// ── Extraction discovery, lifted verbatim from ComicDownloader's policy ──────
// (native/engine/ComicDownloader.cpp: sevenZipPath/bsdtarPath) — bsdtar reads
// BOTH rar and zip; an installed 7-Zip is the fallback. No vendored tool.

QString sevenZipPath()
{
    const QString p = QStringLiteral("C:/Program Files/7-Zip/7z.exe");
    return QFileInfo::exists(p) ? p : QString();
}

QString bsdtarPath()
{
#ifdef Q_OS_WIN
    const QString sys = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sys)) return sys;
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
#else
    // Comic archives require libarchive semantics. GNU tar may be the default
    // `tar` on Linux, but it cannot extract ZIP/CBZ/RAR payloads.
    return QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
#endif
}

bool isImageFile(const QString& name)
{
    static const QSet<QString> kExts = { QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("gif"),
        QStringLiteral("avif"), QStringLiteral("bmp") };
    return kExts.contains(QFileInfo(name).suffix().toLower());
}

// A cheap "is this a real image" gate: sniff the leading magic bytes. Mirrors
// MangaTankoban::MangaVolumeArchiveIngestor::looksDecodable (native/engine/
// MangaVolumeArchiveIngestor.cpp) — full pixel decoding would pull in
// Qt::Gui; the payload is trusted archive/torrent content, so a magic-byte
// check is enough to reject an empty/HTML/garbage extraction while keeping
// this assembler Qt::Core-only.
bool looksDecodable(const QString& absPath)
{
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray h = f.read(16);
    if (h.size() < 3) return false;
    const auto u = [&](int i) { return static_cast<unsigned char>(h.at(i)); };
    if (h.size() >= 8 && u(0) == 0x89 && u(1) == 0x50 && u(2) == 0x4E && u(3) == 0x47
        && u(4) == 0x0D && u(5) == 0x0A && u(6) == 0x1A && u(7) == 0x0A)
        return true;                                        // PNG
    if (u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) return true;   // JPEG
    if (h.startsWith("GIF8")) return true;                  // GIF
    if (h.startsWith("BM")) return true;                    // BMP
    if (h.size() >= 12 && h.startsWith("RIFF") && h.mid(8, 4) == "WEBP")
        return true;                                        // WebP
    return false;
}

bool runExtractorSync(const QString& exe, const QStringList& args)
{
    if (exe.isEmpty()) return false;
    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) return false;
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        proc.waitForFinished(2000);
        return false;
    }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

// bsdtar first, 7-Zip fallback — the EXACT policy ComicDownloader::
// runExtractor/onExtractDone uses, just run synchronously here.
bool extractArchive(const QString& archivePath, const QString& extractTmp, QString* errorOut)
{
    const QString bsdtar = bsdtarPath();
    if (!bsdtar.isEmpty()) {
        const QStringList args = { QStringLiteral("-xf"), QDir::toNativeSeparators(archivePath),
                                    QStringLiteral("-C"), QDir::toNativeSeparators(extractTmp) };
        if (runExtractorSync(bsdtar, args)) return true;
    }
    const QString sevenZip = sevenZipPath();
    if (!sevenZip.isEmpty()) {
        QDir(extractTmp).removeRecursively();
        QDir().mkpath(extractTmp);
        const QStringList args = { QStringLiteral("x"), QStringLiteral("-y"),
                                    QStringLiteral("-o") + QDir::toNativeSeparators(extractTmp),
                                    QDir::toNativeSeparators(archivePath) };
        if (runExtractorSync(sevenZip, args)) return true;
    }
    if (errorOut) {
        *errorOut = (bsdtar.isEmpty() && sevenZip.isEmpty())
            ? QStringLiteral("no archive extractor available (tar/7z)")
            : QStringLiteral("archive extraction failed (not a cbr/cbz?)");
    }
    return false;
}

// Recursively collects image-suffixed files under `dir`, requires at least
// one to pass the magic-byte gate (non-image junk mixed in is tolerated, an
// all-junk payload is not), and returns them natural-sorted by relative path
// so "…-0002" follows "…-0001" and 10 follows 9.
QStringList collectValidatedImages(const QString& dir, QString* errorOut)
{
    QStringList rel;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    const int prefixLen = dir.length() + 1;
    while (it.hasNext()) {
        const QString abs = it.next();
        if (isImageFile(abs)) rel.append(abs.mid(prefixLen));
    }
    if (rel.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("archive contained no pages");
        return {};
    }
    bool anyDecodable = false;
    for (const QString& r : rel) {
        if (looksDecodable(dir + QChar('/') + r)) { anyDecodable = true; break; }
    }
    if (!anyDecodable) {
        if (errorOut) *errorOut = QStringLiteral("no decodable image in payload");
        return {};
    }
    QCollator coll;
    // Archive page order must not depend on the process locale. QCollator's
    // numeric mode is ineffective in the C locale used by headless Linux.
    coll.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(rel.begin(), rel.end(), [&coll](const QString& a, const QString& b) {
        return coll.compare(a, b) < 0;
    });
    return rel;
}

// Cleans `relPath` against `cleanJobRoot`, rejects any ".." segment outright,
// and verifies the resolved absolute path stays inside the job root — every
// filesystem path derived from torrent metadata must be cleaned and verified
// BEFORE it is ever opened (design safety contract).
bool safeJobPath(const QString& cleanJobRoot, const QString& relPath, QString* absOut)
{
    if (relPath.trimmed().isEmpty()) return false;
    const QStringList segs = relPath.split(QChar('/'), Qt::SkipEmptyParts);
    for (const QString& s : segs) {
        if (s == QStringLiteral("..")) return false;
    }
    const QString candidate = QDir::cleanPath(cleanJobRoot + QChar('/') + relPath);
    if (candidate != cleanJobRoot && !candidate.startsWith(cleanJobRoot + QChar('/')))
        return false;
    if (absOut) *absOut = candidate;
    return true;
}

// Natural sort of selected files by their manifest path — LooseImageSubtree
// pages must land in natural relative-path order regardless of the order the
// caller happened to list them in.
QList<ComicSelectedFile> naturalSortedByPath(QList<ComicSelectedFile> files)
{
    QCollator coll;
    // Archive page order must not depend on the process locale. QCollator's
    // numeric mode is ineffective in the C locale used by headless Linux.
    coll.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&coll](const ComicSelectedFile& a, const ComicSelectedFile& b) {
        return coll.compare(a.path, b.path) < 0;
    });
    return files;
}

} // namespace

ComicEditionAssembler::ComicEditionAssembler(QObject* parent) : QObject(parent)
{
}

ComicEditionAssembler::Result ComicEditionAssembler::assembleDetached(
    const ComicAssembleRequest& req,
    const std::shared_ptr<std::atomic_bool>& cancelFlag)
{
    ComicAssembleRequest workerRequest = req;
    workerRequest.cancelFlag = cancelFlag;
    ComicEditionAssembler worker;
    return worker.assemble(workerRequest);
}

void ComicEditionAssembler::cancel(const QString& editionId)
{
    const QString id = editionId.trimmed();
    if (id.isEmpty()) return;
    m_cancelRequested.insert(id);
    const QString dir = m_stagingDirFor.value(id);
    if (!dir.isEmpty()) QDir(dir).removeRecursively();
}

ComicEditionAssembler::Result ComicEditionAssembler::assemble(const ComicAssembleRequest& req)
{
    Result out;
    const QString id = req.editionId.trimmed();
    if (id.isEmpty()) {
        out.error = QStringLiteral("empty edition id");
        emit failed(id, out.error);
        return out;
    }

    const QString cleanJobRoot = QDir::cleanPath(QDir(req.jobRoot).absolutePath());
    const QString cleanStagingRoot = QDir::cleanPath(req.stagingRoot);
    const QString stagingDir = cleanStagingRoot + QChar('/') + id + QStringLiteral(".staging");
    m_stagingDirFor.insert(id, stagingDir);

    auto fail = [&](const QString& reason) -> Result {
        QDir(stagingDir).removeRecursively();
        out.ok = false;
        out.error = reason;
        emit failed(id, reason);
        return out;
    };

    auto cancelled = [&req, this, &id]() {
        return m_cancelRequested.contains(id)
            || (req.cancelFlag && req.cancelFlag->load(std::memory_order_acquire));
    };

    // A cancel() that arrived before this call reached the heavy work aborts
    // immediately — no extraction, no staging left behind.
    if (m_cancelRequested.remove(id) || cancelled()) return fail(QStringLiteral("cancelled"));
    if (req.files.isEmpty()) return fail(QStringLiteral("no files selected for assembly"));
    if (req.kind == ComicPayloadKind::None) return fail(QStringLiteral("no payload kind selected"));

    QDir(stagingDir).removeRecursively();
    if (!QDir().mkpath(stagingDir)) return fail(QStringLiteral("cannot create staging dir"));

    QStringList orderedFiles;
    QList<int> groups;
    int globalIndex = 0;

    auto appendFromExtracted = [&](const QString& extractTmp, const QStringList& relSorted, int group) -> bool {
        for (const QString& r : relSorted) {
            const QString ext = QFileInfo(r).suffix().toLower();
            const QString name = QStringLiteral("page_%1.%2").arg(globalIndex, 3, 10, QChar('0')).arg(ext);
            const QString dst = stagingDir + QChar('/') + name;
            const QString src = extractTmp + QChar('/') + r;
            if (!QFile::rename(src, dst)) return false;
            orderedFiles.append(name);
            groups.append(group);
            ++globalIndex;
        }
        return true;
    };

    if (req.kind == ComicPayloadKind::SingleArchive || req.kind == ComicPayloadKind::CombinedWholeArchive) {
        if (req.files.size() != 1)
            return fail(QStringLiteral("expected exactly one archive for this payload"));
        QString abs;
        if (!safeJobPath(cleanJobRoot, req.files.first().path, &abs))
            return fail(QStringLiteral("selected file path escapes the job root"));
        if (!QFileInfo::exists(abs))
            return fail(QStringLiteral("selected archive missing on disk"));

        const QString extractTmp = stagingDir + QStringLiteral(".x0");
        QDir(extractTmp).removeRecursively();
        if (!QDir().mkpath(extractTmp)) return fail(QStringLiteral("cannot create extract dir"));
        QString err;
        if (!extractArchive(abs, extractTmp, &err)) {
            QDir(extractTmp).removeRecursively();
            return fail(err);
        }
        if (cancelled()) return fail(QStringLiteral("cancelled"));
        QString collectErr;
        const QStringList rel = collectValidatedImages(extractTmp, &collectErr);
        if (rel.isEmpty()) {
            QDir(extractTmp).removeRecursively();
            return fail(collectErr);
        }
        // Combined archives group -1 (design). SingleArchive has no source-issue
        // grouping either, so it shares the same "no grouping" sentinel.
        if (!appendFromExtracted(extractTmp, rel, -1)) {
            QDir(extractTmp).removeRecursively();
            return fail(QStringLiteral("failed placing a page"));
        }
        QDir(extractTmp).removeRecursively();
        emit progress(id, 1, 1);

    } else if (req.kind == ComicPayloadKind::IssueArchiveSet) {
        const int total = req.files.size();
        int done = 0;
        for (const ComicSelectedFile& sf : req.files) {
            if (cancelled()) return fail(QStringLiteral("cancelled"));
            QString abs;
            if (!safeJobPath(cleanJobRoot, sf.path, &abs))
                return fail(QStringLiteral("selected file path escapes the job root"));
            if (!QFileInfo::exists(abs))
                return fail(QStringLiteral("selected issue archive missing on disk"));

            // Each issue archive extracts into its OWN isolated child dir.
            const QString extractTmp = stagingDir + QStringLiteral(".x%1").arg(done);
            QDir(extractTmp).removeRecursively();
            if (!QDir().mkpath(extractTmp)) return fail(QStringLiteral("cannot create extract dir"));
            QString err;
            if (!extractArchive(abs, extractTmp, &err)) {
                QDir(extractTmp).removeRecursively();
                return fail(err);
            }
            QString collectErr;
            const QStringList rel = collectValidatedImages(extractTmp, &collectErr);
            if (rel.isEmpty()) {
                QDir(extractTmp).removeRecursively();
                return fail(collectErr);
            }
            // The selected file's `order` is the source-issue index (issue 1 -> 0,
            // issue 2 -> 1, ...); fall back to encounter order if unset.
            const int group = sf.order >= 0 ? sf.order : done;
            if (!appendFromExtracted(extractTmp, rel, group)) {
                QDir(extractTmp).removeRecursively();
                return fail(QStringLiteral("failed placing a page"));
            }
            QDir(extractTmp).removeRecursively();
            ++done;
            emit progress(id, done, total);
        }

    } else if (req.kind == ComicPayloadKind::LooseImageSubtree) {
        const QList<ComicSelectedFile> sorted = naturalSortedByPath(req.files);
        for (const ComicSelectedFile& sf : sorted) {
            if (cancelled()) return fail(QStringLiteral("cancelled"));
            QString abs;
            if (!safeJobPath(cleanJobRoot, sf.path, &abs))
                return fail(QStringLiteral("selected file path escapes the job root"));
            if (!QFileInfo::exists(abs))
                return fail(QStringLiteral("selected image missing on disk"));
            if (!looksDecodable(abs)) continue;   // tolerate non-image junk mixed into the subtree
            const QString ext = QFileInfo(abs).suffix().toLower();
            const QString name = QStringLiteral("page_%1.%2").arg(globalIndex, 3, 10, QChar('0')).arg(ext);
            // A loose page lives directly under the shared torrent job root —
            // COPY, never move/rename/delete it: sibling edition intents may
            // still depend on the same payload.
            if (!QFile::copy(abs, stagingDir + QChar('/') + name))
                return fail(QStringLiteral("failed copying a page"));
            orderedFiles.append(name);
            groups.append(0);
            ++globalIndex;
        }
        if (orderedFiles.isEmpty()) return fail(QStringLiteral("no decodable image in payload"));
        emit progress(id, orderedFiles.size(), req.files.size());

    } else {
        return fail(QStringLiteral("unsupported payload kind"));
    }

    if (orderedFiles.isEmpty()) return fail(QStringLiteral("assembly produced no pages"));

    out.ok = true;
    out.stagingDir = stagingDir;
    out.orderedFiles = orderedFiles;
    out.groups = groups;
    emit finished(id, stagingDir, orderedFiles, groups);
    return out;
}
