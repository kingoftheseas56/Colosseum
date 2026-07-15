// ComicEditionAssembler contract: given a selected payload (one archive, an
// ordered issue-archive set, a loose-image subtree, or a confirmed combined
// archive) plus the torrent job-root directory, produce ONE complete,
// validated page staging directory WITHOUT ever publishing partial output
// (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-
// volume-mode-design.md, "Assembly and publication"). Real file I/O: real
// bsdtar extraction of real CBZ fixtures, real magic-byte image validation.
#include "engine/ComicEditionAssembler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using ComicEditionFileSelector::ComicPayloadKind;
using ComicEditionFileSelector::ComicSelectedFile;

QString fixturesDir()
{
    return QStringLiteral(COMICS_PACK_FIXTURES_DIR);
}

bool copyFixture(const QString& name, const QString& destDir)
{
    QDir().mkpath(destDir);
    const QString src = fixturesDir() + QChar('/') + name;
    const QString dst = destDir + QChar('/') + name;
    if (QFile::exists(dst)) QFile::remove(dst);
    return QFile::copy(src, dst);
}

ComicSelectedFile sf(int index, const QString& path, int order = -1)
{
    ComicSelectedFile f;
    f.index = index;
    f.path = path;
    f.bytes = 0;
    f.order = order;
    return f;
}

QString stagingPathFor(const QTemporaryDir& stagingRoot, const QString& editionId)
{
    return stagingRoot.path() + QChar('/') + editionId + QStringLiteral(".staging");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    // ── (a) SingleArchive: edition-one.cbz → staging has 2 page_NNN files, ok ──
    {
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (a)");
        require(copyFixture(QStringLiteral("edition-one.cbz"), job.path()), "copy edition-one.cbz (a)");

        ComicEditionAssembler assembler;
        bool sawFinished = false;
        QObject::connect(&assembler, &ComicEditionAssembler::finished, &assembler,
            [&](const QString&, const QString&, const QStringList&, const QList<int>&) { sawFinished = true; });

        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-single");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::SingleArchive;
        req.files = { sf(0, QStringLiteral("edition-one.cbz")) };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(result.ok, "single archive assembles ok (a)");
        require(result.error.isEmpty(), "single archive reports no error (a)");
        require(sawFinished, "finished signal emitted (a)");
        require(result.orderedFiles.size() == 2, "single archive yields 2 pages (a)");
        require(result.groups.size() == 2 && result.groups[0] == -1 && result.groups[1] == -1,
                "single archive pages carry group -1 (a)");
        const QString stagingDir = stagingPathFor(staging, QStringLiteral("ed-single"));
        require(result.stagingDir == stagingDir, "staging dir path matches <id>.staging convention (a)");
        require(QDir(stagingDir).exists(), "staging dir exists on disk (a)");
        require(QFileInfo::exists(stagingDir + QStringLiteral("/page_000.png")), "page_000.png present (a)");
        require(QFileInfo::exists(stagingDir + QStringLiteral("/page_001.png")), "page_001.png present (a)");
    }

    // ── (b) IssueArchiveSet: [issue-001, issue-002] → 4 pages, groups [0,0,1,1] ──
    {
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (b)");
        require(copyFixture(QStringLiteral("issue-001.cbz"), job.path()), "copy issue-001.cbz (b)");
        require(copyFixture(QStringLiteral("issue-002.cbz"), job.path()), "copy issue-002.cbz (b)");

        ComicEditionAssembler assembler;
        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-issues");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::IssueArchiveSet;
        req.files = {
            sf(0, QStringLiteral("issue-001.cbz"), 0),
            sf(1, QStringLiteral("issue-002.cbz"), 1),
        };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(result.ok, "issue set assembles ok (b)");
        require(result.orderedFiles.size() == 4, "issue set yields 4 pages (b)");
        require(result.groups.size() == 4, "issue set groups sized 4 (b)");
        require(result.groups[0] == 0 && result.groups[1] == 0, "issue 1 pages group 0 (b)");
        require(result.groups[2] == 1 && result.groups[3] == 1, "issue 2 pages group 1 (b)");
        require(result.orderedFiles == QStringList({
                    QStringLiteral("page_000.png"), QStringLiteral("page_001.png"),
                    QStringLiteral("page_002.png"), QStringLiteral("page_003.png")}),
                "issue set pages are sequentially numbered across issues, in order (b)");
        const QString stagingDir = stagingPathFor(staging, QStringLiteral("ed-issues"));
        require(QDir(stagingDir).exists(), "issue set staging dir exists (b)");
    }

    // ── (c) LooseImageSubtree: natural relative-path order, regardless of input order ──
    {
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (c)");

        // Pull two REAL, distinct, valid PNGs out of a fixture archive so the
        // magic-byte gate sees real image bytes (page_001=red, page_002=green).
        const QString extractDir = job.path() + QStringLiteral("/.src-extract");
        require(QDir().mkpath(extractDir), "mkpath extract dir (c)");
        const int extractCode = QProcess::execute(QStringLiteral("C:/Windows/System32/tar.exe"),
            {QStringLiteral("-xf"), fixturesDir() + QStringLiteral("/edition-one.cbz"),
             QStringLiteral("-C"), extractDir});
        require(extractCode == 0, "extract edition-one.cbz to seed loose pages (c)");

        const QString looseDir = job.path() + QStringLiteral("/Loose Pages");
        require(QDir().mkpath(looseDir), "mkpath loose pages dir (c)");
        // Numeric-natural-sort trap: ASCII order would put "page10" before
        // "page2"; natural order must put "page2" first.
        require(QFile::copy(extractDir + QStringLiteral("/page_001.png"), looseDir + QStringLiteral("/page10.png")),
                "seed page10.png (c)");
        require(QFile::copy(extractDir + QStringLiteral("/page_002.png"), looseDir + QStringLiteral("/page2.png")),
                "seed page2.png (c)");

        ComicEditionAssembler assembler;
        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-loose");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::LooseImageSubtree;
        // Deliberately listed out of natural order.
        req.files = {
            sf(0, QStringLiteral("Loose Pages/page10.png")),
            sf(1, QStringLiteral("Loose Pages/page2.png")),
        };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(result.ok, "loose image subtree assembles ok (c)");
        require(result.orderedFiles.size() == 2, "loose subtree yields 2 pages (c)");
        require(result.groups.size() == 2 && result.groups[0] == 0 && result.groups[1] == 0,
                "loose subtree pages carry group 0 (c)");

        QFile expectedFirst(looseDir + QStringLiteral("/page2.png"));
        QFile actualFirst(result.stagingDir + QChar('/') + result.orderedFiles[0]);
        require(expectedFirst.open(QIODevice::ReadOnly) && actualFirst.open(QIODevice::ReadOnly),
                "open page2.png + staged page_000 for byte comparison (c)");
        require(expectedFirst.readAll() == actualFirst.readAll(),
                "natural sort places page2.png before page10.png, bytes never recompressed (c)");
    }

    // ── (d) Corrupt/truncated archive → failed, NO staging dir left ─────────
    {
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (d)");
        QFile broken(job.path() + QStringLiteral("/broken.cbz"));
        require(broken.open(QIODevice::WriteOnly), "create broken.cbz (d)");
        broken.write("this is not a real archive, just garbage bytes");
        broken.close();

        ComicEditionAssembler assembler;
        bool sawFailed = false;
        QObject::connect(&assembler, &ComicEditionAssembler::failed, &assembler,
            [&](const QString&, const QString&) { sawFailed = true; });

        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-corrupt");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::SingleArchive;
        req.files = { sf(0, QStringLiteral("broken.cbz")) };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(!result.ok, "corrupt archive fails (d)");
        require(!result.error.isEmpty(), "corrupt archive reports a reason (d)");
        require(sawFailed, "failed signal emitted for corrupt archive (d)");
        require(!QDir(stagingPathFor(staging, QStringLiteral("ed-corrupt"))).exists(),
                "no staging dir left after corrupt archive (d)");
    }

    // ── (e) Traversal path (..) → rejected/failed, NO staging dir left ──────
    {
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (e)");

        ComicEditionAssembler assembler;
        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-traversal");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::SingleArchive;
        req.files = { sf(0, QStringLiteral("../escaped.cbz")) };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(!result.ok, "traversal path is rejected (e)");
        require(!QDir(stagingPathFor(staging, QStringLiteral("ed-traversal"))).exists(),
                "no staging dir left after traversal reject (e)");
    }

    // ── (f) cancel(): before assemble() aborts immediately; after a success cleans up ──
    {
        // (f1) cancel arrives before the matching assemble() call.
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (f1)");
        require(copyFixture(QStringLiteral("edition-one.cbz"), job.path()), "copy edition-one.cbz (f1)");

        ComicEditionAssembler assembler;
        assembler.cancel(QStringLiteral("ed-cancel-before"));

        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-cancel-before");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::SingleArchive;
        req.files = { sf(0, QStringLiteral("edition-one.cbz")) };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(!result.ok, "pre-cancelled request fails immediately (f1)");
        require(!QDir(stagingPathFor(staging, QStringLiteral("ed-cancel-before"))).exists(),
                "no staging dir for a pre-cancelled request (f1)");
    }
    {
        // (f2) cancel arrives after a successful assemble() — cleans the staging.
        QTemporaryDir job, staging;
        require(job.isValid() && staging.isValid(), "temp dirs valid (f2)");
        require(copyFixture(QStringLiteral("edition-one.cbz"), job.path()), "copy edition-one.cbz (f2)");

        ComicEditionAssembler assembler;
        ComicAssembleRequest req;
        req.editionId = QStringLiteral("ed-cancel-after");
        req.jobRoot = job.path();
        req.kind = ComicPayloadKind::SingleArchive;
        req.files = { sf(0, QStringLiteral("edition-one.cbz")) };
        req.stagingRoot = staging.path();

        const auto result = assembler.assemble(req);
        require(result.ok, "assembly succeeds before cancel (f2)");
        const QString stagingDir = stagingPathFor(staging, QStringLiteral("ed-cancel-after"));
        require(QDir(stagingDir).exists(), "staging exists before cancel (f2)");

        assembler.cancel(QStringLiteral("ed-cancel-after"));
        require(!QDir(stagingDir).exists(), "cancel after success removes the staging dir (f2)");
    }

    std::cout << "COMIC_EDITION_ASSEMBLER_OK\n";
    return 0;
}
