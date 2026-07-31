// Pure-parse contract for ComickCatalogClient: the published volume-DB record and
// the live Comick chapters payload. No network, no event loop — only the two free
// functions the client's network steps hand their bytes to.
//
// Two things are being defended here:
//
//   * THE RE-GATE. A published record carries its own `qualified` verdict, and the
//     client does not take it on trust: a stale or hand-edited record must not be
//     able to smuggle a broken shelf into the app. Cases 2-4 are records that LIE
//     — they claim qualified while carrying a volume gap, overlapping spans, or a
//     numbering quirk — and the client has to reject each one on its own reading.
//     The re-gate can only run checks 1-5; see the note on case 2.
//
//   * THE MIRROR. Case 11 pushes the real recorded My Hero Academia chapter pull
//     through the live path (parseChapterRows -> groupVolumes -> gateVolumes) and
//     asserts it lands on the exact shelf the Python batch job published for the
//     same series. If those two ever disagree, one series renders as a volume shelf
//     from the database and as a flat chapter list from a live scrape — the whole
//     reason the C++ grouper is a line-for-line port.
#include "engine/ComickCatalogClient.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

using namespace tankoban::manga::comick;

namespace {

int g_contracts = 0;
int g_failures = 0;
// A missing fixture is not a pass. The two recorded payloads carry THE MIRROR — the
// most valuable assertion in this file — so a machine without them must say so in the
// verdict line rather than printing a clean PASS for the contracts that did run.
int g_fixtureSkips = 0;
bool g_mirrorVerified = false;

// Every case prints its own verdict and a failure does NOT stop the run: the blocks
// below are independent, so one broken rule should still report every other rule's
// state in the same pass.
void require(bool condition, const QString& message)
{
    ++g_contracts;
    if (condition) {
        std::cout << "PASS: " << message.toStdString() << '\n';
    } else {
        ++g_failures;
        std::cout << "FAIL: " << message.toStdString() << '\n';
        std::cerr << "FAIL: " << message.toStdString() << '\n';
    }
}

// Same as require(), but a mismatch prints both sides — a 42-volume shelf is not
// legible as a bare boolean, and "which boundary moved" is the whole question.
void requireEqual(const QString& actual, const QString& expected, const QString& message)
{
    require(actual == expected, message);
    if (actual != expected) {
        const QString detail = QStringLiteral("  got:      %1\n  expected: %2\n")
                                   .arg(actual, expected);
        std::cout << detail.toStdString();
        std::cerr << detail.toStdString();
    }
}

void note(const QString& message)
{
    std::cout << "  note: " << message.toStdString() << '\n';
}

void skipFixture(const QString& message)
{
    ++g_fixtureSkips;
    std::cout << "SKIP: " << message.toStdString() << '\n';
    std::cerr << "SKIP: " << message.toStdString() << '\n';
}

// "1:1-7.5,2:8-17.5,..." — one comparable string for a whole shelf, whichever side
// of the pipeline produced it.
QString renderRanges(const QList<VolumeRange>& vols)
{
    QStringList parts;
    parts.reserve(vols.size());
    for (const VolumeRange& vol : vols)
        parts << QStringLiteral("%1:%2-%3").arg(vol.number).arg(vol.chapterStart, vol.chapterEnd);
    return parts.join(QLatin1Char(','));
}

QString renderVariants(const QVariantList& vols)
{
    QStringList parts;
    parts.reserve(vols.size());
    for (const QVariant& entry : vols) {
        const QVariantMap map = entry.toMap();
        parts << QStringLiteral("%1:%2-%3")
                     .arg(static_cast<int>(map.value(QStringLiteral("number")).toDouble()))
                     .arg(map.value(QStringLiteral("chapterStart")).toString(),
                          map.value(QStringLiteral("chapterEnd")).toString());
    }
    return parts.join(QLatin1Char(','));
}

// Recorded payloads live outside the repo (they are multi-MB pulls and a sibling
// checkout), so a machine without them SKIPS those cases loudly rather than failing
// — the same rule comick_volume_grouper_harness follows for its raw MHA pull.
bool readFirst(const QStringList& candidates, QByteArray* out, QString* found)
{
    for (const QString& candidate : candidates) {
        QFile file(candidate);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        *out = file.readAll();
        *found = candidate;
        return true;
    }
    return false;
}

// ── record fixtures ────────────────────────────────────────────────────────────
// Shaped exactly like a published record, including the dead `complete` field the
// batch job still writes (hardcoded true, carries no information — the client reads
// `qualified` and nothing else).

const char* const kQualifiedRecord = R"json(
{
  "seriesTitle": "Fixture Series",
  "comickHid": "aBcD1234",
  "comickSlug": "fixture-series",
  "volumes": [
    { "number": 1, "chapterStart": "1",  "chapterEnd": "7.5" },
    { "number": 2, "chapterStart": "8",  "chapterEnd": "17.5" },
    { "number": 3, "chapterStart": "18", "chapterEnd": "26.5" }
  ],
  "weebCentral": { "seriesId": "01JQH0FBS5BGDMBDC0BJW034N2" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": true,
  "gateReason": "",
  "scrapedAt": "2026-07-29T12:28:18Z"
}
)json";

// Claims qualified, but volume 3 is missing: 1, 2, 4.
const char* const kGapRecord = R"json(
{
  "seriesTitle": "Gap Series",
  "volumes": [
    { "number": 1, "chapterStart": "1",  "chapterEnd": "10" },
    { "number": 2, "chapterStart": "11", "chapterEnd": "20" },
    { "number": 4, "chapterStart": "31", "chapterEnd": "40" }
  ],
  "weebCentral": { "seriesId": "01FAKEGAP" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": true,
  "gateReason": ""
}
)json";

// Claims qualified, but volume 2 starts inside volume 1's span.
const char* const kOverlapRecord = R"json(
{
  "seriesTitle": "Overlap Series",
  "volumes": [
    { "number": 1, "chapterStart": "1", "chapterEnd": "10" },
    { "number": 2, "chapterStart": "5", "chapterEnd": "20" }
  ],
  "weebCentral": { "seriesId": "01FAKEOVERLAP" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": true,
  "gateReason": ""
}
)json";

// Claims qualified while admitting the numbering quirk that disqualifies it.
const char* const kQuirkRecord = R"json(
{
  "seriesTitle": "Quirk Series",
  "volumes": [
    { "number": 1, "chapterStart": "0.01", "chapterEnd": "9" },
    { "number": 2, "chapterStart": "10",   "chapterEnd": "19" }
  ],
  "weebCentral": { "seriesId": "01FAKEQUIRK" },
  "numberingQuirk": true,
  "complete": true,
  "qualified": true,
  "gateReason": ""
}
)json";

// Claims qualified, and the volume NUMBERS read perfectly — but the last two spans are
// blank, the shape an interrupted batch write leaves behind. The gate skips a span it
// cannot parse rather than judging it, so checks 1-5 pass and this would ship a shelf
// with two tiles that have no chapters behind them.
const char* const kBlankTailRecord = R"json(
{
  "seriesTitle": "Blank Tail Series",
  "volumes": [
    { "number": 1, "chapterStart": "1",  "chapterEnd": "10" },
    { "number": 2, "chapterStart": "11", "chapterEnd": "20" },
    { "number": 3, "chapterStart": "",   "chapterEnd": "" },
    { "number": 4, "chapterStart": "",   "chapterEnd": "" }
  ],
  "weebCentral": { "seriesId": "01FAKEBLANKTAIL" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": true,
  "gateReason": ""
}
)json";

// The degenerate form: nothing parseable at all. gateVolumes answers {true, ""} for
// this, because every span is skipped and it never reaches an overlap comparison.
const char* const kAllBlankRecord = R"json(
{
  "seriesTitle": "All Blank Series",
  "volumes": [
    { "number": 1, "chapterStart": "", "chapterEnd": "" },
    { "number": 2, "chapterStart": "", "chapterEnd": "" }
  ],
  "weebCentral": { "seriesId": "01FAKEALLBLANK" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": true,
  "gateReason": ""
}
)json";

// Claims qualified, one volume, and no `number` key at all. toInt() would land it on 0
// — a LEGAL first volume — so a single-entry run would qualify on a number the record
// never carried.
const char* const kNoNumberRecord = R"json(
{
  "seriesTitle": "No Number Series",
  "volumes": [
    { "chapterStart": "1", "chapterEnd": "10" }
  ],
  "weebCentral": { "seriesId": "01FAKENONUMBER" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": true,
  "gateReason": ""
}
)json";

// An honest record: the batch job saw the raw rows, found a hole, and said so.
const char* const kHonestlyUnqualifiedRecord = R"json(
{
  "seriesTitle": "Vinland Fixture",
  "volumes": [
    { "number": 1, "chapterStart": "1",  "chapterEnd": "10" },
    { "number": 2, "chapterStart": "11", "chapterEnd": "20" }
  ],
  "weebCentral": { "seriesId": "01FAKEHOLE" },
  "numberingQuirk": false,
  "complete": true,
  "qualified": false,
  "gateReason": "9 chapter(s) in no volume (first: 210)"
}
)json";

// ── chapters fixtures ──────────────────────────────────────────────────────────

// Comick's real shape: JSON strings for chap/vol, all languages in one array.
const char* const kChaptersPayload = R"json(
{
  "chapters": [
    { "chap": "7",      "vol": "1", "lang": "en" },
    { "chap": "110.30", "vol": "12", "lang": "pt-br" },
    { "chap": "25.02",  "vol": "3", "lang": "es" }
  ],
  "total": 3
}
)json";

// A null chap, a null vol, and rows that simply omit the fields.
const char* const kSparseChaptersPayload = R"json(
{
  "chapters": [
    { "chap": null, "vol": "1",  "lang": "en" },
    { "chap": "12", "vol": null, "lang": "en" },
    { "lang": "en" },
    { "chap": "13", "vol": "2",  "lang": "ja" }
  ],
  "total": 4
}
)json";

// The unobserved schema change: chap arrives as a JSON NUMBER.
const char* const kNumericChaptersPayload = R"json(
{
  "chapters": [
    { "chap": 3,   "vol": "1", "lang": "en" },
    { "chap": 3.5, "vol": "1", "lang": "en" },
    { "chap": 4,   "vol": 2,   "lang": "en" }
  ],
  "total": 3
}
)json";

} // namespace

int main()
{
    // ── 1. A qualified record parses to emit-ready volumes ─────────────────────
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kQualifiedRecord));
        require(rec.ok, "a well-formed record parses (ok)");
        require(rec.qualified, QStringLiteral("a qualified record stays qualified: %1")
                                   .arg(rec.gateReason));
        require(rec.volumes.size() == 3,
                QStringLiteral("the record's 3 volumes come through, got %1")
                    .arg(rec.volumes.size()));
        requireEqual(renderVariants(rec.volumes),
                     QStringLiteral("1:1-7.5,2:8-17.5,3:18-26.5"),
                     "emitted volumes are ascending with the record's own boundaries");

        bool coversEmpty = true;
        bool numbersAreDoubles = true;
        bool boundariesAreStrings = true;
        for (const QVariant& entry : rec.volumes) {
            const QVariantMap map = entry.toMap();
            const QVariant cover = map.value(QStringLiteral("cover"));
            if (!cover.isValid() || cover.typeId() != QMetaType::QString
                || !cover.toString().isEmpty())
                coversEmpty = false;
            if (map.value(QStringLiteral("number")).typeId() != QMetaType::Double)
                numbersAreDoubles = false;
            if (map.value(QStringLiteral("chapterStart")).typeId() != QMetaType::QString
                || map.value(QStringLiteral("chapterEnd")).typeId() != QMetaType::QString)
                boundariesAreStrings = false;
        }
        require(coversEmpty,
                "every emitted volume carries an EMPTY cover (the shelf draws its own "
                "numbered placeholder; a downloaded volume uses its own first page)");
        require(numbersAreDoubles, "`number` is a double, as QML expects");
        require(boundariesAreStrings, "`chapterStart`/`chapterEnd` are strings");

        const QVariantMap second = rec.volumes.at(1).toMap();
        requireEqual(second.value(QStringLiteral("chapterEnd")).toString(),
                     QStringLiteral("17.5"),
                     "a fractional boundary survives byte-for-byte (17.5, not 17.5000001)");
    }

    // ── 2. A record that CLAIMS qualified but has a volume gap is refused ──────
    //
    // The honest limit of this re-gate, stated once here because it governs cases
    // 2-4: a published record carries its volume RANGES but not the raw chapter
    // rows they were derived from, and the gate's coverage check (check 6) needs
    // those rows. So the client can only re-verify checks 1-5 — quirk, non-empty,
    // first volume 0/1, unbroken volume numbers, non-overlapping spans. Coverage
    // stays the batch job's word, because the batch job is the only side that ever
    // held the evidence. Fabricating rows to feed the gate would manufacture a
    // guarantee we do not have.
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kGapRecord));
        require(rec.ok, "the gap record is well-formed JSON (ok)");
        require(!rec.qualified,
                "a record claiming qualified with a VOLUME GAP is refused by the re-gate");
        require(rec.gateReason.contains(QStringLiteral("gap after volume 2")),
                QStringLiteral("the refusal names the gap: %1").arg(rec.gateReason));
    }

    // ── 3. Overlapping spans are refused ───────────────────────────────────────
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kOverlapRecord));
        require(rec.ok, "the overlap record is well-formed JSON (ok)");
        require(!rec.qualified,
                "a record claiming qualified with OVERLAPPING spans is refused");
        require(rec.gateReason.contains(QStringLiteral("overlaps")),
                QStringLiteral("the refusal names the overlap: %1").arg(rec.gateReason));
    }

    // ── 4. A declared numbering quirk is ACCEPTED if structurally sound ────────
    // SUPERSEDED 2026-07-31. This used to refuse. It no longer does, mirroring
    // colosseum-volume-db aa18444 which removed the same blanket check from
    // volume_builder.gate(). Berserk opens on chapters 0.001-0.03 — a real published
    // numbering scheme — and the structural checks catch genuinely broken numbering
    // on its own evidence. THIS CONTRACT IS LOAD-BEARING: while the two copies
    // disagreed, this local re-gate silently overruled a record the batch job had
    // published as qualified, and Berserk's shelf vanished with no error anywhere.
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kQuirkRecord));
        require(rec.ok, "the quirk record is well-formed JSON (ok)");
        require(rec.qualified,
                "a record with numberingQuirk:true is accepted when structurally sound");
        require(rec.gateReason.isEmpty(),
                QStringLiteral("an accepted quirk record carries no refusal reason: %1")
                    .arg(rec.gateReason));
    }

    // ── 4b. Blank chapter spans are refused BEFORE the gate sees them ──────────
    // The gate skips an unparseable span instead of judging it, and answers
    // {true, ""} when every span is unparseable — safe on the live path, where the
    // only spans it ever sees are formatChapterKey's own output, but a published
    // record is the first thing to hand it strings written somewhere else. Caught in
    // parseDbRecord, deliberately NOT in ComickVolumeGrouper, which is mirrored
    // line-for-line against the Python and must not drift.
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kBlankTailRecord));
        require(rec.ok, "the blank-tail record is well-formed JSON (ok)");
        require(!rec.qualified,
                "a record whose last volumes carry BLANK spans is refused — an "
                "interrupted batch write must not ship tiles with no chapters behind them");
        require(rec.volumes.isEmpty(), "the blank-tail record emits nothing");
        require(rec.gateReason.contains(QStringLiteral("volume 3 carries no usable chapter range")),
                QStringLiteral("the refusal names the first bad volume: %1").arg(rec.gateReason));
    }
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kAllBlankRecord));
        require(rec.ok, "the all-blank record is well-formed JSON (ok)");
        require(!rec.qualified,
                "a record with NO parseable span at all is refused (the gate alone says "
                "qualified here, because it skips every span and compares nothing)");
        require(rec.volumes.isEmpty(), "the all-blank record emits nothing");
    }
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kNoNumberRecord));
        require(rec.ok, "the no-number record is well-formed JSON (ok)");
        require(!rec.qualified,
                "a volume entry with no `number` is refused — it would fall to 0, and 0 "
                "is a legal first volume, so a one-entry record would qualify on nothing");
        require(rec.gateReason.contains(QStringLiteral("no whole `number`")),
                QStringLiteral("the refusal names the missing number: %1").arg(rec.gateReason));
    }

    // ── 5. An honestly unqualified record keeps its own reason ─────────────────
    {
        const ParsedRecord rec = parseDbRecord(QByteArray(kHonestlyUnqualifiedRecord));
        require(rec.ok, "an unqualified record is still well-formed JSON (ok)");
        require(!rec.qualified, "an unqualified record stays unqualified");
        requireEqual(rec.gateReason,
                     QStringLiteral("9 chapter(s) in no volume (first: 210)"),
                     "the batch job's gateReason is carried through verbatim — it saw the "
                     "raw rows and knows more than the client can");
    }

    // ── 6. Malformed JSON ──────────────────────────────────────────────────────
    {
        require(!parseDbRecord(QByteArray("{ not json at all")).ok,
                "truncated JSON -> ok == false");
        require(!parseDbRecord(QByteArray()).ok, "empty bytes -> ok == false");
        require(!parseDbRecord(QByteArray("[1,2,3]")).ok,
                "a JSON array is not a record -> ok == false");
        require(!parseDbRecord(QByteArray(R"json({"qualified": true})json")).ok,
                "an object with no `volumes` array is not a record -> ok == false");
    }

    // ── 7. An unqualified record emits NOTHING ─────────────────────────────────
    {
        const ParsedRecord honest = parseDbRecord(QByteArray(kHonestlyUnqualifiedRecord));
        require(honest.volumes.isEmpty(),
                "an unqualified record yields an EMPTY volume list — the app must fall "
                "back to the flat chapter list, never a half shelf");
        const ParsedRecord lying = parseDbRecord(QByteArray(kGapRecord));
        require(lying.volumes.isEmpty(),
                "a record refused by the re-gate also yields an empty volume list");
    }

    // ── 8. A live chapters payload keeps its labels byte-for-byte ──────────────
    {
        const QList<ChapterRow> rows = parseChapterRows(QByteArray(kChaptersPayload));
        require(rows.size() == 3,
                QStringLiteral("3 chapter rows parse, got %1").arg(rows.size()));
        if (rows.size() == 3) {
            requireEqual(rows.at(0).chap, QStringLiteral("7"), "row 0 chap is '7'");
            requireEqual(rows.at(0).vol, QStringLiteral("1"), "row 0 vol is '1'");
            requireEqual(rows.at(1).chap, QStringLiteral("110.30"),
                         "row 1 chap is '110.30' — the 30th side chapter, NOT '110.3'");
            requireEqual(rows.at(1).vol, QStringLiteral("12"), "row 1 vol is '12'");
            requireEqual(rows.at(2).chap, QStringLiteral("25.02"),
                         "row 2 chap keeps its zero padding ('25.02')");
        }
        require(parseChapterRows(QByteArray("{ broken")).isEmpty(),
                "a malformed chapters payload yields no rows");
    }

    // ── 9. Missing / null fields become empty strings, and the rows survive ────
    {
        const QList<ChapterRow> rows = parseChapterRows(QByteArray(kSparseChaptersPayload));
        require(rows.size() == 4,
                QStringLiteral("every row survives, including the empty ones, got %1")
                    .arg(rows.size()));
        if (rows.size() == 4) {
            require(rows.at(0).chap.isEmpty() && rows.at(0).vol == QStringLiteral("1"),
                    "a null `chap` becomes an empty string");
            require(rows.at(1).chap == QStringLiteral("12") && rows.at(1).vol.isEmpty(),
                    "a null `vol` becomes an empty string");
            require(rows.at(2).chap.isEmpty() && rows.at(2).vol.isEmpty(),
                    "an absent chap/vol becomes an empty string");
            require(rows.at(3).chap == QStringLiteral("13"),
                    "a normal row after the sparse ones is unaffected");
        }
    }

    // ── 10. A JSON-number chap renders without a spurious decimal ──────────────
    // Comick sends strings today. If a number ever arrives it means the schema
    // changed, so the client warns and renders integrally: 3 -> "3", 3.5 -> "3.5".
    // Note this DELIBERATELY differs from the Python batch path's str(), which would
    // render a JSON 3.0 as "3.0" — a side-chapter key. See the client's comment.
    {
        const QList<ChapterRow> rows = parseChapterRows(QByteArray(kNumericChaptersPayload));
        require(rows.size() == 3,
                QStringLiteral("3 numeric rows parse, got %1").arg(rows.size()));
        if (rows.size() == 3) {
            requireEqual(rows.at(0).chap, QStringLiteral("3"),
                         "a JSON-number chap of 3 renders as '3', not '3.0'");
            requireEqual(rows.at(1).chap, QStringLiteral("3.5"),
                         "a non-integral JSON-number chap keeps its decimal ('3.5')");
            requireEqual(rows.at(2).vol, QStringLiteral("2"),
                         "a JSON-number vol renders integrally too ('2')");
        }
        // And the rendered labels still group: the whole point of rendering "3"
        // rather than "3.0" is that "3.0" would be chapter 3's 0th SIDE chapter.
        ChapterKey key;
        require(parseChapterKey(QStringLiteral("3"), &key) && !key.isSideChapter(),
                "the rendered '3' parses as a WHOLE chapter, not a side chapter");
    }

    // ── 11. THE MIRROR: the live path lands on the published shelf ─────────────
    QString publishedRender;
    {
        const QStringList candidates{
            QStringLiteral("C:/Users/Suprabha/Desktop/colosseum-volume-db/db/"
                           "01JQH0FBS5BGDMBDC0BJW034N2.json"),
            QStringLiteral("../../../colosseum-volume-db/db/01JQH0FBS5BGDMBDC0BJW034N2.json")};
        QByteArray bytes;
        QString found;
        if (!readFirst(candidates, &bytes, &found)) {
            skipFixture(QStringLiteral("published MHA record not on this machine — case 12 "
                                       "did NOT run"));
        } else {
            // ── 12. The real published record parses to 42 qualified volumes ───
            const ParsedRecord rec = parseDbRecord(bytes);
            require(rec.ok, QStringLiteral("the real published record parses (%1)").arg(found));
            require(rec.qualified,
                    QStringLiteral("the real My Hero Academia record is qualified: %1")
                        .arg(rec.gateReason));
            require(rec.volumes.size() == 42,
                    QStringLiteral("the real record yields 42 volumes, got %1")
                        .arg(rec.volumes.size()));
            publishedRender = renderVariants(rec.volumes);
            require(publishedRender.startsWith(QStringLiteral("1:1-7.5,")),
                    QStringLiteral("the real record opens on volume 1 = 1-7.5"));
            require(publishedRender.endsWith(QStringLiteral(",42:423-431")),
                    QStringLiteral("the real record closes on volume 42 = 423-431"));
        }
    }
    {
        const QStringList candidates{
            QStringLiteral("C:/Users/Suprabha/Desktop/Brotherhood/scripts/mha_all.json"),
            QStringLiteral("../../scripts/mha_all.json"),
            QStringLiteral("scripts/mha_all.json")};
        QByteArray bytes;
        QString found;
        if (!readFirst(candidates, &bytes, &found)) {
            skipFixture(QStringLiteral("raw MHA chapter pull not on this machine — case 11 "
                                       "did NOT run"));
        } else {
            const QList<ChapterRow> rows = parseChapterRows(bytes);
            require(rows.size() == 2714,
                    QStringLiteral("%1 parses to 2714 chapter rows, got %2")
                        .arg(found).arg(rows.size()));
            const QList<VolumeRange> vols = groupVolumes(rows);
            const bool quirk = numberingIsOddball(rows);
            const GateVerdict verdict = gateVolumes(vols, quirk, rows);
            require(vols.size() == 42,
                    QStringLiteral("the live path groups MHA into 42 volumes, got %1")
                        .arg(vols.size()));
            require(!quirk, "the live path sees no numbering quirk for MHA");
            require(verdict.qualified,
                    QStringLiteral("the live path qualifies MHA for tankoban mode: %1")
                        .arg(verdict.reason));
            if (publishedRender.isEmpty()) {
                note(QStringLiteral("live-vs-published comparison did NOT run — the published "
                                    "record is not on this machine"));
            } else {
                requireEqual(renderRanges(vols), publishedRender,
                             "THE MIRROR: the live scrape and the published record agree on "
                             "all 42 volumes, every boundary label");
                g_mirrorVerified = (renderRanges(vols) == publishedRender);
                note(QStringLiteral("live path: %1").arg(renderRanges(vols)));
            }
        }
    }

    // The verdict has to carry the skips: a run that never reached THE MIRROR is not
    // the same result as one that did, and printing a bare PASS for it would hide the
    // single assertion this file exists for.
    QString qualifier;
    if (g_fixtureSkips > 0) {
        qualifier = QStringLiteral(", %1 fixture(s) SKIPPED").arg(g_fixtureSkips);
        if (!g_mirrorVerified)
            qualifier += QStringLiteral(" — mirror NOT verified");
    }

    if (g_failures > 0) {
        const QString line = QStringLiteral("comick_catalog_parse_harness: FAIL (%1 of %2 "
                                            "contracts%3)")
                                 .arg(g_failures).arg(g_contracts).arg(qualifier);
        std::cout << line.toStdString() << '\n';
        return 1;
    }
    const QString line = QStringLiteral("comick_catalog_parse_harness: PASS (%1 contracts%2)")
                             .arg(g_contracts).arg(qualifier);
    std::cout << line.toStdString() << '\n';
    return 0;
}
