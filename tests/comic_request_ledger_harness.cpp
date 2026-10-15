// Comic collected-edition request ledger contract: one row per requested
// edition survives a process restart via an atomic, versioned JSON journal
// (design: docs/superpowers/specs/2026-07-15-colosseum-tankorent-comic-
// volume-mode-design.md, "Durable shared-infohash transport" ->
// ComicRequestLedger). Mirrors MangaVolumeRequestLedger's proven QSaveFile
// discipline (native/torrent/MangaVolumeRequestLedger.h) but ported to the
// comics identity/payload types (ComicEditionIdentity, ComicEditionFile
// Selector) and a versioned schema. Pure persistence: no network, no GUI.
#include "torrent/ComicRequestLedger.h"
#include "torrent/ComicEditionFileSelector.h"
#include "torrent/ComicEditionIdentity.h"

#include <QChar>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
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
using ComicEditionIdentity::ComicCollectionFormat;
using ComicEditionIdentity::ComicIssueRef;

// Exactly 40 repeated hex-safe characters — avoids miscounted literal hex
// strings in the fixtures below.
QString hex40(char c)
{
    return QString(40, QLatin1Char(c));
}

ComicEditionRequestRow fullRow(const QString& editionId, const QString& infoHash, const QString& state)
{
    ComicEditionRequestRow r;
    r.editionId = editionId;
    r.infoHash = infoHash;
    r.magnetUri = QStringLiteral("magnet:?xt=urn:btih:") + infoHash;
    r.seriesId = QStringLiteral("sid-invincible");
    r.seriesTitle = QStringLiteral("Invincible");
    r.editionTitle = QStringLiteral("Invincible Compendium #1");
    r.format = ComicCollectionFormat::Compendium;
    r.ordinal = 1;
    r.isbnDigits = QStringLiteral("9781632150363");
    r.collectedIssues = { ComicIssueRef{QStringLiteral("Invincible"), 1},
                           ComicIssueRef{QStringLiteral("Invincible"), 2},
                           ComicIssueRef{QStringLiteral("Invincible"), 3} };
    r.collectedIssuesComplete = true;    // distinctive: a fully-parsed set
    r.formatAmbiguous = false;           // distinctive: a persisted false must survive
    r.savePath = QStringLiteral("C:/torrents/invincible-compendium-1");
    r.pickedFileIndices = { 0, 2, 5 };
    r.payloadKind = ComicPayloadKind::IssueArchiveSet;
    r.state = state;
    return r;
}

void writeRawJournal(const QString& path, const QJsonObject& root)
{
    QFile f(path);
    require(f.open(QIODevice::WriteOnly), "raw fixture journal opens for write");
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();
}

} // namespace

int main()
{
    // ── Full round-trip of every field, including collectedIssues and
    //    several pickedFileIndices ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "round-trip temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        {
            ComicRequestLedger ledger(path);
            ledger.load();   // no file yet — must not crash, must start empty
            require(ledger.all().isEmpty(), "a fresh ledger with no journal file starts empty");

            ledger.upsert(fullRow(QStringLiteral("ed-1"), hex40('a'), QStringLiteral("downloading")));
        }

        {
            ComicRequestLedger reopened(path);
            reopened.load();
            const QList<ComicEditionRequestRow> rows = reopened.all();
            require(rows.size() == 1, "round-trip: exactly one row persisted");
            const ComicEditionRequestRow& r = rows.first();
            require(r.editionId == QStringLiteral("ed-1"), "round-trip: editionId");
            require(r.infoHash == hex40('a'), "round-trip: infoHash");
            require(r.magnetUri == QStringLiteral("magnet:?xt=urn:btih:") + hex40('a'), "round-trip: magnetUri");
            require(r.seriesId == QStringLiteral("sid-invincible"), "round-trip: seriesId");
            require(r.seriesTitle == QStringLiteral("Invincible"), "round-trip: seriesTitle");
            require(r.editionTitle == QStringLiteral("Invincible Compendium #1"), "round-trip: editionTitle");
            require(r.format == ComicCollectionFormat::Compendium, "round-trip: format enum");
            require(r.ordinal == 1, "round-trip: ordinal");
            require(r.isbnDigits == QStringLiteral("9781632150363"), "round-trip: isbnDigits");
            require(r.collectedIssues.size() == 3, "round-trip: collectedIssues count");
            require(r.collectedIssues[0].series == QStringLiteral("Invincible") && r.collectedIssues[0].number == 1,
                    "round-trip: collectedIssues[0]");
            require(r.collectedIssues[2].number == 3, "round-trip: collectedIssues[2] number");
            require(r.collectedIssuesComplete == true, "round-trip: collectedIssuesComplete (true survives)");
            require(r.formatAmbiguous == false, "round-trip: formatAmbiguous (persisted false survives)");
            require(r.savePath == QStringLiteral("C:/torrents/invincible-compendium-1"), "round-trip: savePath");
            require(r.pickedFileIndices == QList<int>({0, 2, 5}), "round-trip: pickedFileIndices (several)");
            require(r.payloadKind == ComicPayloadKind::IssueArchiveSet, "round-trip: payloadKind enum");
            require(r.state == QStringLiteral("downloading"), "round-trip: state");
        }
    }

    // ── Backward-compat safe defaults: a PRE-FIX journal row that predates the
    //    identity-safety keys must load on the SAFE side — a set is NOT complete
    //    (hold back auto-download) and format IS treated ambiguous (force a
    //    manual choice) rather than risk an unscoped auto-match ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "legacy-defaults temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        QJsonObject legacyRow;   // no collectedIssuesComplete / formatAmbiguous keys
        legacyRow[QStringLiteral("editionId")] = QStringLiteral("ed-legacy");
        legacyRow[QStringLiteral("infoHash")] = hex40('9');
        legacyRow[QStringLiteral("state")] = QStringLiteral("downloading");

        QJsonArray rows;
        rows.append(legacyRow);
        QJsonObject root;
        root[QStringLiteral("version")] = ComicRequestLedger::schemaVersion();
        root[QStringLiteral("rows")] = rows;
        writeRawJournal(path, root);

        ComicRequestLedger ledger(path);
        ledger.load();
        require(ledger.all().size() == 1, "legacy row loads (fields are additive, no version bump)");
        const ComicEditionRequestRow& r = ledger.all().first();
        require(r.collectedIssuesComplete == false,
                "legacy default: collectedIssuesComplete is false (hold back auto-download)");
        require(r.formatAmbiguous == true,
                "legacy default: formatAmbiguous is true (force a manual choice)");
    }

    // ── Atomic replacement: upsert same editionId twice -> one row, latest
    //    wins, and that survives a reload ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "replacement temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        ComicRequestLedger ledger(path);
        ledger.load();
        ledger.upsert(fullRow(QStringLiteral("ed-dup"), hex40('b'), QStringLiteral("awaiting_metadata")));
        ComicEditionRequestRow second = fullRow(QStringLiteral("ed-dup"), hex40('c'), QStringLiteral("downloading"));
        second.ordinal = 2;
        ledger.upsert(second);

        require(ledger.all().size() == 1, "upsert same editionId twice keeps exactly one row");
        require(ledger.all().first().infoHash == hex40('c'), "upsert same editionId twice: latest infoHash wins");
        require(ledger.all().first().state == QStringLiteral("downloading"),
                "upsert same editionId twice: latest state wins");

        ComicRequestLedger reopened(path);
        reopened.load();
        require(reopened.all().size() == 1, "atomic replacement persists as a single row across reload");
        require(reopened.all().first().ordinal == 2, "atomic replacement: latest ordinal wins across reload");
    }

    // ── Malformed-row quarantine: a hand-crafted journal with one good row
    //    and one row missing its editionId -> only the good row survives ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "malformed-row temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        QJsonObject goodRow;
        goodRow[QStringLiteral("editionId")] = QStringLiteral("ed-good");
        goodRow[QStringLiteral("infoHash")] = hex40('d');
        goodRow[QStringLiteral("state")] = QStringLiteral("downloading");

        QJsonObject brokenRow;   // no editionId at all -> malformed, must be quarantined whole
        brokenRow[QStringLiteral("infoHash")] = hex40('e');
        brokenRow[QStringLiteral("state")] = QStringLiteral("downloading");

        QJsonArray rows;
        rows.append(goodRow);
        rows.append(brokenRow);

        QJsonObject root;
        root[QStringLiteral("version")] = ComicRequestLedger::schemaVersion();
        root[QStringLiteral("rows")] = rows;
        writeRawJournal(path, root);

        ComicRequestLedger ledger(path);
        ledger.load();
        require(ledger.all().size() == 1, "malformed row is quarantined — only the good row survives");
        require(ledger.all().first().editionId == QStringLiteral("ed-good"), "the surviving row is the good one");
    }

    // ── Unknown schema version -> the whole journal is ignored, not
    //    partially applied ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "version-mismatch temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        QJsonObject row;
        row[QStringLiteral("editionId")] = QStringLiteral("ed-futureversion");
        row[QStringLiteral("infoHash")] = hex40('f');
        row[QStringLiteral("state")] = QStringLiteral("downloading");

        QJsonArray rows;
        rows.append(row);

        QJsonObject root;
        root[QStringLiteral("version")] = 999;
        root[QStringLiteral("rows")] = rows;
        writeRawJournal(path, root);

        ComicRequestLedger ledger(path);
        ledger.load();
        require(ledger.all().isEmpty(), "a version-999 journal is ignored entirely, not partially applied");
    }

    // ── active(): drops terminal states AND drops a malformed (non-40-hex)
    //    infoHash row, while all() still reports every row ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "active-filter temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        ComicRequestLedger ledger(path);
        ledger.load();
        ledger.upsert(fullRow(QStringLiteral("ed-active"), hex40('1'), QStringLiteral("downloading")));
        ledger.upsert(fullRow(QStringLiteral("ed-completed"), hex40('2'), QStringLiteral("completed")));
        ledger.upsert(fullRow(QStringLiteral("ed-failed"), hex40('3'), QStringLiteral("failed")));
        ledger.upsert(fullRow(QStringLiteral("ed-cancelled"), hex40('4'), QStringLiteral("cancelled")));
        ledger.upsert(fullRow(QStringLiteral("ed-badhash"), QStringLiteral("not-a-40-hex-hash"),
                               QStringLiteral("downloading")));

        require(ledger.all().size() == 5, "all() keeps every upserted row regardless of state/infoHash validity");

        const QList<ComicEditionRequestRow> live = ledger.active();
        require(live.size() == 1, "active() drops terminal states and the malformed-infoHash row");
        require(live.first().editionId == QStringLiteral("ed-active"),
                "the only surviving active row is the live, well-formed one");
    }

    // ── setState / setSelection / remove persist and survive a reload on a
    //    fresh ledger instance over the same path ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "mutation temp dir is valid");
        const QString path = dir.path() + QStringLiteral("/edition-requests.json");

        ComicRequestLedger ledger(path);
        ledger.load();
        ledger.upsert(fullRow(QStringLiteral("ed-mutate"), hex40('5'), QStringLiteral("awaiting_metadata")));
        ledger.upsert(fullRow(QStringLiteral("ed-doomed"), hex40('6'), QStringLiteral("awaiting_metadata")));

        ledger.setSelection(QStringLiteral("ed-mutate"), QList<int>{1, 3, 4}, ComicPayloadKind::LooseImageSubtree);
        ledger.setState(QStringLiteral("ed-mutate"), QStringLiteral("assembling"));
        ledger.remove(QStringLiteral("ed-doomed"));

        ComicRequestLedger reopened(path);
        reopened.load();
        require(reopened.all().size() == 1, "remove() persists: the removed row is gone across reload");
        const ComicEditionRequestRow r = reopened.all().first();
        require(r.editionId == QStringLiteral("ed-mutate"), "the surviving row after remove() is the mutated one");
        require(r.state == QStringLiteral("assembling"), "setState() persists across reload");
        require(r.pickedFileIndices == QList<int>({1, 3, 4}), "setSelection() pickedFileIndices persists across reload");
        require(r.payloadKind == ComicPayloadKind::LooseImageSubtree,
                "setSelection() payloadKind persists across reload");
    }

    std::cout << "COMIC_REQUEST_LEDGER_OK\n";
    return 0;
}
