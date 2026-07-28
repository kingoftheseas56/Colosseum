// Atomic local volume index + archive ingestion contract.
//
// A REAL cbz (tests/fixtures/tankoban/tiny-volume.cbz, three valid 1x1 PNGs)
// enters MangaVolumeArchiveIngestor, is extracted by the OS archiver, naturally
// ordered and atomically published into MangaVolumeIndex. The index survives a
// simulated restart (reload) and self-heals a missing payload down to "none".
// The WeebCentral packer's no-archive publish() path yields the same ready
// shape, and its natural-sort ordering is proven with 1/2/10-named inputs whose
// lexical order (1,10,2) differs from the natural order (1,2,10).
//
// Async note: ingestArchive runs the extractor via QProcess, so the harness
// owns a QCoreApplication and a waitFor() event-loop pump.
#include "engine/CbzArchive.h"
#include "engine/MangaTankobanLogic.h"
#include "engine/MangaVolumeArchiveIngestor.h"
#include "engine/MangaVolumeIndex.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QString fixturePath(const QString& name)
{
    return QStringLiteral(TANKOBAN_FIXTURES_DIR) + QLatin1Char('/') + name;
}

// Spin a QEventLoop until `signal` on `obj` fires, or timeoutMs elapses.
// Returns true iff the signal fired first.
template <typename Obj, typename Signal>
bool waitFor(Obj* obj, Signal signal, int timeoutMs)
{
    QEventLoop loop;
    bool fired = false;
    const QMetaObject::Connection c =
        QObject::connect(obj, signal, &loop, [&]() { fired = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, [&]() { loop.quit(); });
    loop.exec();
    QObject::disconnect(c);
    return fired;
}

// Minimal valid 1x1 truecolour PNG (matches the generator behind tiny-volume.cbz):
// correct magic + a real zlib IDAT so a magic-byte "decodable image" check passes.
const unsigned char kPng[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xde,0x00,0x00,0x00,0x0c,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0x00,
    0x00,0x03,0x01,0x01,0x00,0xc9,0xfe,0x92,0xef,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
    0x44,0xae,0x42,0x60,0x82
};

// Write a valid PNG plus `pad` trailing bytes (ignored by decoders, magic intact)
// so files can be told apart by size after being renamed to page_NNN.
bool writePaddedPng(const QString& path, int pad)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
    if (pad > 0) f.write(QByteArray(pad, '\0'));
    return true;
}

QByteArray pageBytes(const QVariantMap& page)
{
    const QString archive = page.value(QStringLiteral("archive")).toString();
    if (!archive.isEmpty())
        return CbzArchive::readEntry(
            archive, page.value(QStringLiteral("entry")).toString());
    QFile file(page.value(QStringLiteral("url")).toUrl().toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool startsWithPngMagic(const QVariantMap& page)
{
    return pageBytes(page).startsWith(
        QByteArray(reinterpret_cast<const char*>(kPng), 8));
}

bool writeJson(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) > 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Brotherhood"));
    QCoreApplication::setApplicationName(QStringLiteral("MangaVolumeIndexHarness"));

    QTemporaryDir root;
    require(root.isValid(), "temp index root created");

    MangaVolumeIndex index(root.path());
    MangaVolumeArchiveIngestor ingestor(&index);

    // ── Legacy repair-before-prune: manifest beats a corrupt global row ────
    {
        QTemporaryDir legacyRoot;
        require(legacyRoot.isValid(), "legacy repair root created");

        VolumeProvenance legacy;
        legacy.id = volumeId(QStringLiteral("tankoban:one-piece"), QStringLiteral("1"));
        legacy.seriesId = QStringLiteral("tankoban:one-piece");
        legacy.seriesTitle = QStringLiteral("One Piece");
        legacy.volumeNumber = QStringLiteral("1");
        legacy.sourceKind = QStringLiteral("nyaa");

        MangaVolumeIndex layout(legacyRoot.path());
        const QString legacyDir = layout.pagesDirFor(legacy);
        require(QDir().mkpath(legacyDir), "legacy loose directory created");
        require(writePaddedPng(legacyDir + QStringLiteral("/page_000.png"), 1),
                "legacy first page written");
        require(writePaddedPng(legacyDir + QStringLiteral("/page_001.png"), 2),
                "legacy second page written");

        QJsonObject manifest{
            {QStringLiteral("volumeId"), legacy.id},
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("bytes"), 0.0},
            {QStringLiteral("files"), QJsonArray{
                QStringLiteral("page_000.png"), QStringLiteral("page_001.png")}},
            {QStringLiteral("groups"), QJsonArray{0, 1}}
        };
        require(writeJson(legacyDir + QStringLiteral("/index.json"), manifest),
                "legacy per-volume manifest written");

        // The global row deliberately records wrong .jpg suffixes. Current
        // prune-first heal() deletes this good directory; the required behavior
        // reconciles from index.json, creates CBZ, saves, verifies, then retires it.
        QJsonObject corruptRow{
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("dir"), legacyDir},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("files"), QJsonArray{
                QStringLiteral("page_000.jpg"), QStringLiteral("page_001.jpg")}},
            {QStringLiteral("groups"), QJsonArray{0, 1}}
        };
        require(QDir().mkpath(layout.baseDir()), "legacy ledger parent created");
        require(writeJson(layout.baseDir() + QStringLiteral("/volume-index.json"),
                          QJsonObject{{legacy.id, corruptRow}}),
                "corrupt global ledger written");

        MangaVolumeIndex repairing(legacyRoot.path());
        require(repairing.statusOf(legacy.id).value(QStringLiteral("state")).toString()
                    == QStringLiteral("none"),
                "corrupt global row is not falsely ready before heal");
        repairing.heal();

        const QVariantList repairedPages = repairing.localPages(legacy.id);
        require(repairedPages.size() == 2,
                "heal recovers both pages instead of pruning them");
        const QVariantMap repairedFirst = repairedPages.first().toMap();
        require(repairedFirst.value(QStringLiteral("entry")).toString()
                    == QStringLiteral("page_000.png"),
                "per-volume manifest repairs the wrong global suffix");
        const QString repairedArchive =
            repairedFirst.value(QStringLiteral("archive")).toString();
        require(repairedArchive.endsWith(QStringLiteral(".cbz"))
                    && QFileInfo::exists(repairedArchive),
                "legacy pages migrate to canonical CBZ");
        require(!QDir(legacyDir).exists(),
                "loose directory retires only after successful CBZ migration");
        QString archiveError;
        require(CbzArchive::readEntry(repairedArchive, QStringLiteral("page_000.png"),
                                      &archiveError).startsWith(
                    QByteArray(reinterpret_cast<const char*>(kPng), 8)),
                "migrated CBZ contains the original first page bytes");
        require(repairedPages[1].toMap().value(QStringLiteral("group")).toInt() == 1,
                "legacy chapter groups survive migration");
    }

    // ── Intact legacy rows also migrate: CBZ-only is a storage invariant ─────
    {
        QTemporaryDir legacyRoot;
        require(legacyRoot.isValid(), "intact legacy root created");

        VolumeProvenance legacy;
        legacy.id = volumeId(QStringLiteral("tankoban:one-piece"),
                             QStringLiteral("70"));
        legacy.seriesId = QStringLiteral("tankoban:one-piece");
        legacy.seriesTitle = QStringLiteral("One Piece");
        legacy.volumeNumber = QStringLiteral("70");
        legacy.sourceKind = QStringLiteral("nyaa");

        MangaVolumeIndex layout(legacyRoot.path());
        const QString legacyDir = layout.pagesDirFor(legacy);
        require(QDir().mkpath(legacyDir), "intact legacy directory created");
        require(writePaddedPng(legacyDir + QStringLiteral("/page_000.png"), 1),
                "intact legacy page written");

        const QJsonArray files{QStringLiteral("page_000.png")};
        const QJsonArray groups{0};
        const QJsonObject manifest{
            {QStringLiteral("volumeId"), legacy.id},
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("files"), files},
            {QStringLiteral("groups"), groups}
        };
        require(writeJson(legacyDir + QStringLiteral("/index.json"), manifest),
                "intact legacy manifest written");

        const QJsonObject row{
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("dir"), legacyDir},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("files"), files},
            {QStringLiteral("groups"), groups}
        };
        require(QDir().mkpath(layout.baseDir()),
                "intact legacy ledger parent created");
        require(writeJson(layout.baseDir() + QStringLiteral("/volume-index.json"),
                          QJsonObject{{legacy.id, row}}),
                "intact legacy ledger written");

        MangaVolumeIndex repairing(legacyRoot.path());
        require(repairing.statusOf(legacy.id).value(QStringLiteral("state")).toString()
                    == QStringLiteral("ready"),
                "intact legacy row starts ready");
        repairing.heal();

        const QVariantList pages = repairing.localPages(legacy.id);
        require(pages.size() == 1
                    && pages.first().toMap().value(QStringLiteral("archive"))
                        .toString().endsWith(QStringLiteral(".cbz")),
                "heal migrates an intact loose-page row to CBZ");
        require(!QDir(legacyDir).exists(),
                "intact legacy loose directory retires after verified migration");
    }

    // Failed migration retains a still-valid loose payload for retry.
    {
        QTemporaryDir legacyRoot;
        require(legacyRoot.isValid(), "failed migration root created");

        VolumeProvenance legacy;
        legacy.id = volumeId(QStringLiteral("tankoban:one-piece"),
                             QStringLiteral("71"));
        legacy.seriesId = QStringLiteral("tankoban:one-piece");
        legacy.seriesTitle = QStringLiteral("One Piece");
        legacy.volumeNumber = QStringLiteral("71");
        legacy.sourceKind = QStringLiteral("nyaa");

        MangaVolumeIndex layout(legacyRoot.path());
        const QString legacyDir = layout.pagesDirFor(legacy);
        require(QDir().mkpath(legacyDir), "failed migration directory created");
        require(writePaddedPng(legacyDir + QStringLiteral("/page_000.png"), 1),
                "failed migration page written");
        const QJsonArray files{QStringLiteral("page_000.png")};
        const QJsonObject manifest{
            {QStringLiteral("volumeId"), legacy.id},
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("files"), files},
            {QStringLiteral("groups"), QJsonArray{0}}
        };
        require(writeJson(legacyDir + QStringLiteral("/index.json"), manifest),
                "failed migration manifest written");
        const QJsonObject row{
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("dir"), legacyDir},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("files"), files},
            {QStringLiteral("groups"), QJsonArray{0}}
        };
        require(QDir().mkpath(layout.baseDir()),
                "failed migration ledger parent created");
        require(writeJson(layout.baseDir() + QStringLiteral("/volume-index.json"),
                          QJsonObject{{legacy.id, row}}),
                "failed migration ledger written");

        const QString blockedArchive = layout.archivePathFor(legacy);
        require(QDir().mkpath(QFileInfo(blockedArchive).absolutePath()),
                "failed migration archive parent created");
        QFile invalid(blockedArchive);
        require(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "failed migration blocker opened");
        invalid.write("not a zip");
        invalid.close();

        MangaVolumeIndex repairing(legacyRoot.path());
        repairing.heal();
        require(repairing.statusOf(legacy.id).value(QStringLiteral("state")).toString()
                    == QStringLiteral("ready"),
                "failed CBZ migration preserves the valid legacy ledger row");
        require(QFileInfo::exists(legacyDir + QStringLiteral("/page_000.png")),
                "failed CBZ migration preserves loose payload bytes");
    }

    // Unrecoverable lookup rows are pruned without deleting payload bytes.
    {
        QTemporaryDir legacyRoot;
        require(legacyRoot.isValid(), "unrecoverable row root created");

        VolumeProvenance legacy;
        legacy.id = volumeId(QStringLiteral("tankoban:one-piece"),
                             QStringLiteral("72"));
        legacy.seriesId = QStringLiteral("tankoban:one-piece");
        legacy.seriesTitle = QStringLiteral("One Piece");
        legacy.volumeNumber = QStringLiteral("72");
        legacy.sourceKind = QStringLiteral("nyaa");

        MangaVolumeIndex layout(legacyRoot.path());
        const QString legacyDir = layout.pagesDirFor(legacy);
        require(QDir().mkpath(legacyDir), "unrecoverable payload directory created");
        const QString orphan = legacyDir + QStringLiteral("/orphan.png");
        require(writePaddedPng(orphan, 1), "unrecoverable payload byte written");
        const QJsonObject brokenRow{
            {QStringLiteral("seriesId"), legacy.seriesId},
            {QStringLiteral("seriesTitle"), legacy.seriesTitle},
            {QStringLiteral("volumeNumber"), legacy.volumeNumber},
            {QStringLiteral("dir"), legacyDir},
            {QStringLiteral("sourceKind"), legacy.sourceKind},
            {QStringLiteral("files"), QJsonArray{QStringLiteral("missing.png")}},
            {QStringLiteral("groups"), QJsonArray{0}}
        };
        require(QDir().mkpath(layout.baseDir()),
                "unrecoverable ledger parent created");
        require(writeJson(layout.baseDir() + QStringLiteral("/volume-index.json"),
                          QJsonObject{{legacy.id, brokenRow}}),
                "unrecoverable ledger written");

        MangaVolumeIndex repairing(legacyRoot.path());
        repairing.heal();
        require(repairing.statusOf(legacy.id).value(QStringLiteral("state")).toString()
                    == QStringLiteral("none"),
                "unrecoverable row is pruned from lookup state");
        require(QFileInfo::exists(orphan),
                "pruning an unrecoverable row never deletes unvalidated payload");
    }

    // A hard failure aborts immediately with its reason rather than idling to timeout.
    QObject::connect(&ingestor, &MangaVolumeArchiveIngestor::failed, &app,
        [](const QString& id, const QString& reason) {
            std::cerr << "FAIL: ingest failed " << id.toStdString()
                      << ": " << reason.toStdString() << '\n';
            std::exit(1);
        });

    // ── Nyaa archive ingestion ────────────────────────────────────────────────
    VolumeProvenance record;
    record.id           = volumeId(QStringLiteral("mangadex:berserk"), QStringLiteral("2"));
    record.seriesId     = QStringLiteral("mangadex:berserk");
    record.seriesTitle  = QStringLiteral("Berserk");
    record.volumeNumber = QStringLiteral("2");
    record.sourceKind   = QStringLiteral("nyaa");
    record.releaseTitle = QStringLiteral("Berserk v02 (Digital) (danke)");
    record.uploader     = QStringLiteral("danke");
    record.infoHash     = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

    // Copy the committed fixture: the ingestor takes ownership and DELETES the
    // source archive, so it must never touch the repo's checked-in fixture.
    const QString inputCbz = root.path() + QStringLiteral("/ingest-input.cbz");
    require(QFile::copy(fixturePath(QStringLiteral("tiny-volume.cbz")), inputCbz),
            "fixture copied to a disposable ingest input");

    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "unknown volume reports state none before ingest");

    ingestor.ingestArchive(record, inputCbz);
    require(waitFor(&ingestor, &MangaVolumeArchiveIngestor::finished, 30000),
            "ingestArchive emitted finished before timeout");

    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("ready"),
            "published only after extraction");
    QVariantList pages = index.localPages(record.id);
    require(pages.size() == 3, "three naturally ordered pages");

    // Reader shape: index ascending 0..2, page_000 first, page_002 last.
    require(pages[0].toMap().value(QStringLiteral("index")).toInt() == 0
                && pages[1].toMap().value(QStringLiteral("index")).toInt() == 1
                && pages[2].toMap().value(QStringLiteral("index")).toInt() == 2,
            "page index runs ascending from zero");
    require(pages[0].toMap().value(QStringLiteral("group")).toInt() == 0,
            "single-source volume pages carry chapter-group 0");
    require(pages[0].toMap().value(QStringLiteral("archive")).toString()
                    .endsWith(QStringLiteral(".cbz"))
                && pages[0].toMap().value(QStringLiteral("entry")).toString()
                    == QStringLiteral("001.png")
                && pages[2].toMap().value(QStringLiteral("entry")).toString()
                    == QStringLiteral("003.png"),
            "Nyaa volume stays in CBZ and exposes naturally ordered entries");

    // Anti-stub: the archive-backed page is the REAL PNG, not a placeholder.
    require(startsWithPngMagic(pages[0].toMap()),
            "published CBZ entry is a real image (PNG magic)");
    // Ownership transfer: the disposable source archive is gone.
    require(!QFile::exists(inputCbz), "source archive deleted after publish");

    // ── Atomic index survives a restart ───────────────────────────────────────
    index.reload();
    require(index.localPages(record.id).size() == 3, "atomic index survives restart");
    // Independent proof: a fresh index over the same root reads the same payload.
    {
        MangaVolumeIndex reopened(root.path());
        require(reopened.localPages(record.id).size() == 3,
                "fresh index instance reads the persisted volume");
    }

    // Recovery sidecar repairs a corrupt archive-backed ledger row.
    {
        const QString ledgerPath =
            index.baseDir() + QStringLiteral("/volume-index.json");
        QFile ledgerFile(ledgerPath);
        require(ledgerFile.open(QIODevice::ReadOnly),
                "sidecar repair ledger opened");
        QJsonObject ledgerRoot =
            QJsonDocument::fromJson(ledgerFile.readAll()).object();
        ledgerFile.close();
        QJsonObject corrupt = ledgerRoot.value(record.id).toObject();
        corrupt[QStringLiteral("files")] =
            QJsonArray{QStringLiteral("wrong-entry.png")};
        ledgerRoot[record.id] = corrupt;
        require(writeJson(ledgerPath, ledgerRoot),
                "sidecar repair corrupt ledger written");

        index.reload();
        require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                    == QStringLiteral("none"),
                "corrupt archive ledger is not falsely ready");
        index.heal();
        const QVariantList repaired = index.localPages(record.id);
        require(repaired.size() == 3
                    && repaired.first().toMap().value(QStringLiteral("entry")).toString()
                        == QStringLiteral("001.png"),
                "sidecar reconciles archive entries back into the ledger");
    }

    // ── Self-heal prunes a missing payload ────────────────────────────────────
    const QString archivePath =
        index.localPages(record.id).at(1).toMap().value(QStringLiteral("archive")).toString();
    require(QFile::remove(archivePath), "removed published CBZ from disk");
    // I1: statusOf is file-aware. A surviving dir with one page gone is NOT ready
    // even BEFORE heal() runs — statusOf agrees with localPages, no pre-heal needed.
    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "missing page reports state none without heal()");
    require(index.localPages(record.id).isEmpty(),
            "localPages rejects a missing CBZ (statusOf agrees)");
    index.heal();
    require(index.statusOf(record.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "missing payload is pruned");
    // remove() is idempotent on an already-pruned id.
    require(!index.remove(record.id), "remove of an already-pruned id is a no-op");

    // ── WeebCentral publish: no archive, same ready shape, natural sort ────────
    VolumeProvenance wc;
    wc.id           = volumeId(QStringLiteral("weebcentral:vinland-saga"), QStringLiteral("2"));
    wc.seriesId     = QStringLiteral("weebcentral:vinland-saga");
    wc.seriesTitle  = QStringLiteral("Vinland Saga");
    wc.volumeNumber = QStringLiteral("2");
    wc.sourceKind   = QStringLiteral("weebcentral");
    wc.releaseTitle = QStringLiteral("Vinland Saga Vol. 2");
    wc.chapterIds   = QStringList{QStringLiteral("ch-6"), QStringLiteral("ch-7")};

    QTemporaryDir prepared;
    require(prepared.isValid(), "prepared page dir created");
    // Lexical order would be 1,10,2 — natural order must be 1,2,10. Pad by ordinal
    // so page byte-sizes rise strictly in the CORRECT (natural) order.
    require(writePaddedPng(prepared.path() + QStringLiteral("/img1.png"), 1), "wrote img1");
    require(writePaddedPng(prepared.path() + QStringLiteral("/img2.png"), 2), "wrote img2");
    require(writePaddedPng(prepared.path() + QStringLiteral("/img10.png"), 10), "wrote img10");

    require(ingestor.publish(wc, prepared.path()), "publish of a prepared page dir succeeds");
    require(index.statusOf(wc.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("ready"),
            "prepared-dir publish yields the same ready shape");
    QVariantList wcPages = index.localPages(wc.id);
    require(wcPages.size() == 3, "prepared-dir publish yields three pages");
    require(wcPages[0].toMap().value(QStringLiteral("archive")).toString()
                    .endsWith(QStringLiteral(".cbz"))
                && wcPages[0].toMap().value(QStringLiteral("url")).toUrl().isEmpty(),
            "WeebCentral publish has CBZ-only reader descriptors");
    require(startsWithPngMagic(wcPages[0].toMap()),
            "published prepared CBZ entry is a real image");
    // Natural sort proof: sizes strictly ascend (img1 < img2 < img10). Lexical
    // sort (1,10,2) would put the +10 pad in the middle and break this.
    const qint64 s0 = pageBytes(wcPages[0].toMap()).size();
    const qint64 s1 = pageBytes(wcPages[1].toMap()).size();
    const qint64 s2 = pageBytes(wcPages[2].toMap()).size();
    require(s0 < s1 && s1 < s2, "pages ordered by natural (numeric) sort, not lexical");

    // ── remove() deletes CBZ + entry, and is idempotent ───────────────────────
    const QString wcArchive =
        wcPages[0].toMap().value(QStringLiteral("archive")).toString();
    require(QFileInfo::exists(wcArchive), "published volume has an on-disk CBZ");

    require(index.remove(wc.id), "remove() reports it removed the volume");
    require(!QFileInfo::exists(wcArchive), "remove() deleted the CBZ");
    require(index.statusOf(wc.id).value(QStringLiteral("state")).toString()
                == QStringLiteral("none"),
            "removed volume reports state none");
    require(!index.remove(wc.id), "second remove() of the same id is a no-op");

    // ── I2: publish carries per-page chapter groups (WeebCentral multi-chapter) ─
    VolumeProvenance grp;
    grp.id           = volumeId(QStringLiteral("weebcentral:berserk"), QStringLiteral("3"));
    grp.seriesId     = QStringLiteral("weebcentral:berserk");
    grp.seriesTitle  = QStringLiteral("Berserk");
    grp.volumeNumber = QStringLiteral("3");
    grp.sourceKind   = QStringLiteral("weebcentral");
    grp.chapterIds   = QStringList{QStringLiteral("ch-8"), QStringLiteral("ch-9")};

    QTemporaryDir grpDir;
    require(grpDir.isValid(), "grouped prepared dir created");
    require(writePaddedPng(grpDir.path() + QStringLiteral("/img1.png"), 0), "wrote grouped img1");
    require(writePaddedPng(grpDir.path() + QStringLiteral("/img2.png"), 0), "wrote grouped img2");
    require(writePaddedPng(grpDir.path() + QStringLiteral("/img3.png"), 0), "wrote grouped img3");

    // Two chapters packed into one volume: pages 1-2 are chapter group 0, page 3
    // is group 1. Groups are given in natural-sorted page order (1,2,3).
    require(ingestor.publish(grp, grpDir.path(), QVector<int>{0, 0, 1}),
            "publish honours a per-page chapter-group list");
    QVariantList grpPages = index.localPages(grp.id);
    require(grpPages.size() == 3, "grouped publish yields three pages");
    require(grpPages[0].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[1].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[2].toMap().value(QStringLiteral("group")).toInt() == 1,
            "chapter groups stored in natural page order (0,0,1)");
    // Groups survive the atomic ledger round-trip.
    index.reload();
    grpPages = index.localPages(grp.id);
    require(grpPages.size() == 3, "grouped volume survives reload");
    require(grpPages[0].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[1].toMap().value(QStringLiteral("group")).toInt() == 0
                && grpPages[2].toMap().value(QStringLiteral("group")).toInt() == 1,
            "chapter groups preserved across reload");

    std::cout << "MANGA_VOLUME_INDEX_OK\n";
    return 0;
}
