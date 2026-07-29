// Comick grouping + tankoban completeness-gate contract.
//
// This mirrors colosseum-volume-db/comick_volume_db/tests/test_volume_builder.py
// case-for-case. The two implementations are the SAME algorithm running in two
// places — the Python batch pipeline that publishes the volume DB, and this C++
// live-scrape path — so a divergence means one series renders as a shelf from the
// database and as a flat chapter list from a live scrape. Every case below is a
// defect that was actually found against live data during the Python review:
//
//   * sub-chapter labels are ORDINALS, not decimals ("315.9" < "315.10")
//   * labels round-trip byte-for-byte ("110.30" is not "110.3")
//   * a dead tie leaves the chapter UNASSIGNED rather than guessed
//   * stray-tag repair is POSITIONAL, not vote-weighted, in both directions
//   * the gate's coverage check counts CHAPTERS, not ROWS
//
// The last section is the one that matters most: real My Hero Academia rows
// (all languages, 2714 source rows) grouped into the exact 42-volume record the
// Python published. It runs from an embedded compact fixture so it works on any
// machine, and additionally re-runs against the raw JSON pull when that file is
// present, which proves the compact fixture is faithful to the real corpus.
#include "engine/ComickVolumeGrouper.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <iostream>

using namespace tankoban::manga::comick;

namespace {

int g_contracts = 0;
int g_failures = 0;

// A sanity check on the harness's own scaffolding (a test label that must parse, a
// fixture line that must be well formed). Fails just as hard, but is NOT counted as
// a contract — otherwise the 885-line embedded fixture would inflate the tally into
// meaninglessness. A broken fixture invalidates everything after it, so unlike a
// failed contract this one does stop the run.
void precondition(bool condition, const QString& message)
{
    if (!condition) {
        std::cerr << "HARNESS FAULT: " << message.toStdString() << '\n';
        std::exit(2);
    }
}

// Every case prints its own verdict, and a failure does NOT stop the run: the blocks
// below are independent, so one broken rule should report every other rule's state in
// the same pass rather than hiding them behind an early exit.
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

// Same as require(), but a mismatch prints both sides: a 42-volume shelf is not
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

// ── row builders (mirrors of the Python test helpers) ──────────────────────────

struct Rows {
    QList<ChapterRow> list;

    // `times` = how many language rows carry this exact claim, i.e. its vote weight.
    Rows& add(const QString& chap, const QString& vol, int times = 1)
    {
        for (int i = 0; i < times; ++i)
            list.append(ChapterRow{chap, vol});
        return *this;
    }
    Rows& span(int fromChapter, int toChapterInclusive, const QString& vol, int times = 1)
    {
        for (int c = fromChapter; c <= toChapterInclusive; ++c)
            add(QString::number(c), vol, times);
        return *this;
    }
    Rows& append(const QList<ChapterRow>& other)
    {
        list.append(other);
        return *this;
    }
};

// Python `_span_volumes`: volumes each holding `per` consecutive chapters, spans
// ascending and non-overlapping. Volume n covers n*per .. n*per+per-1.
QList<VolumeRange> spanVolumes(int firstNumber, int lastNumberInclusive, int per = 10)
{
    QList<VolumeRange> vols;
    for (int n = firstNumber; n <= lastNumberInclusive; ++n) {
        vols.append(VolumeRange{n, QString::number(n * per),
                                QString::number(n * per + per - 1)});
    }
    return vols;
}

// Python `_rows_for`: source rows that exactly fill the given spans — nothing
// unmapped, nothing missing.
QList<ChapterRow> rowsFor(const QList<VolumeRange>& vols)
{
    QList<ChapterRow> rows;
    for (const VolumeRange& vol : vols) {
        for (int c = vol.chapterStart.toInt(); c <= vol.chapterEnd.toInt(); ++c)
            rows.append(ChapterRow{QString::number(c), QString::number(vol.number)});
    }
    return rows;
}

// majorityAssign's keys are sort keys, not labels — read them back as the source
// wrote them (Python `_labels`).
QMap<QString, int> labelled(const QMap<ChapterKey, int>& assign)
{
    QMap<QString, int> out;
    for (auto it = assign.constBegin(); it != assign.constEnd(); ++it)
        out.insert(formatChapterKey(it.key()), it.value());
    return out;
}

// "1:1-7.5,2:8-17.5,..." — one comparable string for a whole shelf.
QString render(const QList<VolumeRange>& vols)
{
    QStringList parts;
    parts.reserve(vols.size());
    for (const VolumeRange& vol : vols) {
        parts << QStringLiteral("%1:%2-%3").arg(vol.number)
                     .arg(vol.chapterStart, vol.chapterEnd);
    }
    return parts.join(QLatin1Char(','));
}

ChapterKey key(const QString& label)
{
    ChapterKey out;
    precondition(parseChapterKey(label, &out), QStringLiteral("test label %1 parses").arg(label));
    return out;
}

// ── the real My Hero Academia corpus, compacted ────────────────────────────────
//
// Generated from scripts/mha_all.json (Comick's all-language chapter pull for
// My Hero Academia: 2714 rows, the same data as the Python's
// tests/fixtures/mha_all_lang_pairs.json — the chap/vol multisets are identical).
// Format: "chap|vol[|rowCount];..." with an empty vol meaning the source left the
// chapter untagged, and rowCount omitted when it is 1.
//
// Deduplicating identical rows into a count is lossless for every function under
// test: majorityAssign counts rows per (chapter, volume), and coverage/oddball are
// set- and minimum-based, so N identical rows and one row with count N give byte-
// identical output. Rows whose `chap` is null are dropped, because every function
// discards them before doing anything.
const char* const kMhaCompact =
    "1||2;1|1|13;10||2;10|2|5;10.5|2;100||2;100|12|4;101||2;101|12|4;102||2;102|12|4;103||2;103|12|4;104|"
    "|2;104|12|4;105||2;105|12|4;106||2;106|12|4;107||2;107|12|4;108||2;108|12|4;109||2;109|13|4;11||2;11"
    "|2|5;110||2;110|13|4;111||2;111|13|4;112||2;112|13|4;113||2;113|13|4;114||2;114|13|3;115||2;115|13|4"
    ";116||2;116|13|4;117||2;117|13|4;118||2;118|13|4;119||2;119|14|4;12||2;12|2|5;120||2;120|14|4;121||2"
    ";121|14|4;122||2;122|14|4;123||2;123|14|4;124||2;124|14|4;125||2;125|14|4;126||2;126|14|4;127||2;127"
    "|14|4;128||2;128|14|4;128|15;129||2;129|15|5;13||2;13|2|5;130||2;130|15|5;131||2;131|15|5;132||2;132"
    "|15|5;133||2;133|15|5;134||2;134|15|5;135||2;135|15|5;136||2;136|15|4;136|16;137||2;137|15|4;137|16;"
    "138||2;138|16|5;139||2;139|16|5;14||2;14|2|5;14.5|2;140||2;140|16|5;141||2;141|16|5;142||2;142|16|5;"
    "143||2;143|16|5;144||2;144|16|5;145||2;145|16|5;146||2;146|16|5;146|17;147||2;147|16|5;147|17;148||2"
    ";148|17|6;149||2;149|17|6;15||2;15|2|4;150||2;150|17|6;151||2;151|17|5;152||2;152|17|5;153||2;153|17"
    "|5;154||2;154|17|5;155||2;155|17|5;156||2;156|17|5;157||2;157|17|5;158||2;158|18|5;159||2;159|18|5;1"
    "6||2;16|2|4;160||2;160|18|5;161||2;161|18|6;162||2;162|18|6;163||2;163|18|6;164||2;164|18|6;165||2;1"
    "65|18|6;166||2;166|18|6;167||2;167|18|6;168||2;168|19|6;169||2;169|19|8;17||2;17|2|4;17.5|2;170||2;1"
    "70|19|7;171||2;171|19|7;172||2;172|19|7;173||2;173|19|8;174||2;174|19|8;175||2;175|19|8;176||2;176|1"
    "9|8;177||2;177|19|8;178||2;178|20|8;179||2;179|20|9;18||2;18|3|4;180||2;180|20|8;181||2;181|20|8;182"
    "||2;182|20|7;183||2;183|20|7;184||2;184|20|8;185||2;185|20|8;186||2;186|20|7;187||2;187|20|6;188||2;"
    "188|20|6;189||2;189|21|6;19||2;19|3|4;190||2;190|21|5;191||2;191|21|5;192||2;192|21|5;192.1|21;192.2"
    "|21;193||2;193|21|5;194||2;194|21|4;195||2;195|21|4;196||2;196|21|5;197||2;197|21|5;198||2;198|21|4;"
    "199||2;199|21|4;2||2;2|1|10;20||2;20|3|4;200||2;200|21|5;201||2;201|22|4;202||2;202|22|4;203||2;203|"
    "22|4;204||2;204|22|3;205||2;205|22|4;206||2;206|22|5;207||2;207|22|4;208||2;208|22|3;209||2;209|22|3"
    ";21||2;21|3|4;210||2;210|22|3;211||2;211|22|3;212||2;212|22|4;213||2;213|23|5;214||2;214|23|4;215||2"
    ";215|23|4;216||2;216|23|4;217||3;217|23|4;218||3;218|23|4;219||2;219|23|4;22||2;22|3|4;220||2;220|23"
    "|4;221||2;221|23|3;222||2;222|23|3;223||2;223|23|3;224||2;224|23|3;225||2;225|24|3;226||2;226|24|3;2"
    "27||2;227|24|3;228||2;228|24|3;229||2;229|24|3;23||2;23|3|4;230||2;230|24|3;231||2;231|24|2;232||2;2"
    "32|24|2;233||2;233|24|2;234||2;234|24|4;235||2;235|24|2;236||2;236|25|3;237||2;237|25|4;238||2;238|2"
    "5|4;239||2;239|25|4;24||2;24|3|4;240||2;240|25|3;241||2;241|25|3;242||2;242|25|3;243||2;243|25|3;244"
    "||2;244|25|3;245||2;245|25|3;246||2;246|25|3;247||2;247|26|3;248||2;248|26|3;249||2;249|26|3;25||2;2"
    "5|3|4;250||2;250|26|3;251||2;251|26|3;252||2;252|26|3;253||2;253|26|3;254||2;254|26|3;255||2;255|26|"
    "3;256||2;256|26|3;257||2;257|26|5;258||2;258|26|5;259||2;259|27|7;26||2;26|3|3;26.5|3;260||2;260|27|"
    "6;261||2;261|27|6;262||2;262|27|6;263||2;263|27|6;264|;264|27|6;265||2;265|27|6;266||2;266|27|5;267|"
    "|2;267|27|5;268||2;268|28|5;269||2;269|28|5;27||2;27|4|3;270||2;270|28|5;271||2;271|28|5;272||2;272|"
    "28|5;273||2;273|28|5;274||2;274|28|5;275||2;275|28|5;276||2;276|28|5;277|;277|29|5;278||2;278|29|4;2"
    "79|;279|29|4;28||2;28|4|3;280|;280|29|4;281|;281|29|4;282|;282|29|3;283|;283|29|3;284|;284|29|4;285|"
    ";285|29|4;286||2;286|30|4;287||2;287|30|4;288||2;288|30|4;289||2;289|30|3;29||2;29|4|3;290||2;290|30"
    "|3;291||2;291|30|3;292||2;292|30|3;293||2;293|30|3;294||2;294|30|3;295||2;295|30|3;296||2;296|31|3;2"
    "97||2;297|31|3;298||2;298|31|3;299||2;299|31|3;3||2;3|1|10;30||2;30|4|3;300||2;300|31|3;301||2;301|3"
    "1|3;302||2;302|31|3;303||2;303|31|3;304||2;304|31|3;305||2;305|31|3;306||2;306|31|3;307||2;307|32|3;"
    "308||2;308|32|3;309||2;309|32|3;31||2;31|4|3;310||2;310|32|3;311||2;311|32|3;312||2;312|32|3;313||2;"
    "313|32|3;314||2;314|32|3;315||2;315|32|3;316||2;316|32|3;317||2;317|32|4;318||2;318|32|4;319||2;319|"
    "33|4;32||2;32|4|3;320||2;320|33|4;321||2;321|33|4;321.5|;321.5|33;322||2;322|33|4;323||2;323|33|4;32"
    "4||2;324|33|4;325||2;325|33|4;326||2;326|33|4;327||2;327|33|3;328||2;328|33|4;329||2;329|34|4;33||2;"
    "33|4|3;330||2;330|34|4;331||2;331|34|4;332||2;332|34|4;333||2;333|34|4;334||2;334|34|4;335||2;335|34"
    "|4;336||2;336|34|4;337||2;337|34|4;338||2;338|34|4;339||2;339|34|4;34||2;34|4|4;34.5|4;340||2;340|35"
    "|4;341||2;341|30;341|35|4;342||2;342|35|4;343||2;343|35|4;344||2;344|35|4;345||2;345|35|4;346||2;346"
    "|35|4;347||2;347|35|4;348||2;348|35|4;349||2;349|35|4;35||2;35|4|3;350||2;350|35|5;351||2;351|36|5;3"
    "52||2;352|36|6;353||2;353|36|6;354||2;354|36|5;355||2;355|36|4;356||2;356|36|5;357||2;357|36|4;358||"
    "2;358|36|4;359||2;359|36|4;36||2;36|5|3;360||2;360|36|4;361||2;361|36|4;362||2;362|36|4;363||2;363|3"
    "7|4;364||2;364|37|4;365||2;365|37|4;366||2;366|37|4;367||2;367|37|4;368||2;368|37|5;369||2;369|37|5;"
    "37||2;37|5|3;370||2;370|37|5;371||2;371|37|5;372||2;372|37|5;373||2;373|37|4;374||2;374|37|4;375||2;"
    "375|38|5;376|;376|38|5;377|;377|38|5;378|;378|38|5;379|;379|38|6;38||2;38|5|3;380|;380|38|5;381|;381"
    "|38|6;382|;382|38|6;383|;383|38|5;384|;384|38|5;385|;385|38|4;386|;386|38|4;387||2;387|39|3;388||2;3"
    "88|39|2;389||2;389|39|2;39||2;39|5|3;390||2;390|39|2;391||2;391|39|2;392||2;392|39|2;393||2;393|39|2"
    ";394||2;394|39|2;395||2;395|39|2;396||2;396|39|3;397||2;397|39|2;398||2;398|39|2;399||2;399|40|2;4||"
    "2;4|1|6;40||2;40|5|3;400||2;400|40|3;401||2;401|40|3;402||2;402|40|3;403||2;403|40|3;404||2;404|40|3"
    ";405||2;405|40|3;406||2;406|40|3;407||2;407|40|3;408||2;408|40|3;409||2;409|40|3;41||2;41|5|3;410||2"
    ";410|40|3;411||3;411|41|3;412||2;412|41|3;413||2;413|41|3;414||2;414|41|3;415||2;415|41|3;416||2;416"
    "|41|3;417||2;417|41|3;418||2;418|41|3;419||3;419|41|3;42||2;42|5|3;420||3;420|41|3;421||3;421|41|3;4"
    "22||2;422|41|3;423||2;423|42|3;424||2;424|42|3;425||3;425|42|3;426||3;426|42|3;427||2;427|42|3;428||"
    "3;428|42|3;429||2;429|42|3;43||2;43|5|3;430||2;430|42|3;430.5|;430.6|;431|42|3;44||2;44|5|3;45||2;45"
    "|6|3;46||2;46|6|3;47||2;47|6|3;48||2;48|6|3;49||2;49|6|3;49.5|6;5||2;5|1|6;50||2;50|6|3;51||2;51|6|3"
    ";52||2;52|6|3;53||2;53|6|3;54||2;54|7|3;55||2;55|7|3;56||2;56|7|3;57||2;57|7|3;58||2;58|7|3;59||2;59"
    "|7|3;6||2;6|1|6;6.5|1;60||2;60|7|3;61||2;61|7|3;62||2;62|7|3;63||2;63|8|3;64||2;64|8|3;65||2;65|8|3;"
    "66||2;66|8|3;67||2;67|8|3;68||2;68|8|3;68.5|8;69||2;69|8|3;7||2;7|1|4;7|2;7.5|1|2;70||2;70|8|3;71||2"
    ";71|8|3;72||2;72|9|3;73||2;73|9|3;74||2;74|9|3;75||2;75|9|3;76||2;76|9|3;77||2;77|9|3;78||2;78|9|3;7"
    "9||2;79|9|3;8||2;8|2|5;80||2;80|9|4;80.5|9;81||2;81|10|4;82||2;82|10|4;83||2;83|10|4;84||2;84|10|4;8"
    "5||2;85|10|4;86||2;86|10|4;87||2;87|10|4;88||2;88|10|4;89||2;89|10|4;89.5|10;9||2;9|2|5;90||2;90|11|"
    "4;91||2;91|11|4;92||2;92|11|4;93||2;93|11|4;94||2;94|11|4;95||2;95|11|4;96||2;96|11|4;97||2;97|11|4;"
    "98||2;98|11|4;99||2;99|11|4"
    ;

// The record the Python published for this series, verbatim.
const char* const kMhaExpected =
    "1:1-7.5,2:8-17.5,3:18-26.5,4:27-35,5:36-44,6:45-53,7:54-62,8:63-71,9:72-80.5,"
    "10:81-89.5,11:90-99,12:100-108,13:109-118,14:119-128,15:129-137,16:138-147,"
    "17:148-157,18:158-167,19:168-177,20:178-188,21:189-200,22:201-212,23:213-224,"
    "24:225-235,25:236-246,26:247-258,27:259-267,28:268-276,29:277-285,30:286-295,"
    "31:296-306,32:307-318,33:319-328,34:329-339,35:340-350,36:351-362,37:363-374,"
    "38:375-386,39:387-398,40:399-410,41:411-422,42:423-431";

QList<ChapterRow> expandCompact(const QString& compact)
{
    Rows rows;
    const QStringList entries = compact.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        const QStringList fields = entry.split(QLatin1Char('|'));
        precondition(fields.size() == 2 || fields.size() == 3,
                     QStringLiteral("compact fixture entry '%1' is well formed").arg(entry));
        const int times = fields.size() == 3 ? fields.at(2).toInt() : 1;
        precondition(times >= 1,
                     QStringLiteral("compact fixture entry '%1' has a positive count").arg(entry));
        rows.add(fields.at(0), fields.at(1), times);
    }
    return rows.list;
}

// Reads a raw Comick chapter pull: {"chapters":[{"chap":"7","vol":"1",...},...]}.
// QJsonDocument lives in Qt6::Core, so this needs no extra dependency.
//
// `chap`/`vol` come off the wire as JSON strings or null. Anything else would be
// silently dropped by toString(), so this refuses to guess and reports instead —
// a quietly shrinking corpus would turn the parity test green for the wrong reason.
bool loadComickJson(const QString& path, QList<ChapterRow>* out, QString* why)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *why = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        *why = QStringLiteral("%1 is not a JSON object (%2)").arg(path, error.errorString());
        return false;
    }
    const QJsonValue chapters = doc.object().value(QStringLiteral("chapters"));
    if (!chapters.isArray()) {
        *why = QStringLiteral("%1 has no 'chapters' array").arg(path);
        return false;
    }
    out->clear();
    const QJsonArray array = chapters.toArray();
    for (const QJsonValue& item : array) {
        if (!item.isObject()) {
            *why = QStringLiteral("%1 has a non-object chapter row").arg(path);
            return false;
        }
        const QJsonObject row = item.toObject();
        const QJsonValue chap = row.value(QStringLiteral("chap"));
        const QJsonValue vol = row.value(QStringLiteral("vol"));
        for (const QJsonValue& field : {chap, vol}) {
            if (!field.isString() && !field.isNull() && !field.isUndefined()) {
                *why = QStringLiteral("%1 has a chap/vol that is neither string nor null")
                           .arg(path);
                return false;
            }
        }
        out->append(ChapterRow{chap.toString(), vol.toString()});
    }
    return true;
}

// ── inspect mode ───────────────────────────────────────────────────────────────
//
// `comick_volume_grouper_harness <payload.json>` runs the real pipeline over a real
// Comick pull and PRINTS what it got — the volume list and the gate verdict — rather
// than asserting against a baked-in expectation. That is what makes the mirror
// checkable end-to-end: the same payload goes through the Python and through this,
// and the two outputs are diffed. A unit test can only prove this code agrees with
// itself.
int inspect(const QString& path)
{
    QList<ChapterRow> rows;
    QString why;
    if (!loadComickJson(path, &rows, &why)) {
        std::cerr << "ERROR: " << why.toStdString() << '\n';
        return 1;
    }
    const QList<VolumeRange> vols = groupVolumes(rows);
    const bool quirk = numberingIsOddball(rows);
    const GateVerdict verdict = gateVolumes(vols, quirk, rows);

    std::cout << "payload: " << path.toStdString() << '\n';
    std::cout << "rows: " << rows.size() << '\n';
    std::cout << "numberingQuirk: " << (quirk ? "true" : "false") << '\n';
    std::cout << "volumes: " << vols.size() << '\n';
    for (const VolumeRange& vol : vols) {
        std::cout << "  " << vol.number << '\t' << vol.chapterStart.toStdString() << '\t'
                  << vol.chapterEnd.toStdString() << '\n';
    }
    std::cout << "render: " << render(vols).toStdString() << '\n';
    std::cout << "qualified: " << (verdict.qualified ? "true" : "false") << '\n';
    std::cout << "gateReason: " << verdict.reason.toStdString() << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    // A payload path means inspect mode; no arguments means the contract suite.
    if (argc > 1)
        return inspect(QString::fromLocal8Bit(argv[1]));


    // ── 1. Label parsing and byte-exact round-trip ─────────────────────────────
    // The app joins these labels against WeebCentral's chapter labels, so drift is
    // a broken join, not a cosmetic difference.
    {
        require(formatChapterKey(key(QStringLiteral("7"))) == QStringLiteral("7"),
                "whole chapter round-trips");
        require(formatChapterKey(key(QStringLiteral("110.5"))) == QStringLiteral("110.5"),
                "side chapter round-trips");
        require(formatChapterKey(key(QStringLiteral("110.30"))) == QStringLiteral("110.30"),
                "'110.30' does not decay to '110.3'");
        require(formatChapterKey(key(QStringLiteral("25.02"))) == QStringLiteral("25.02"),
                "zero-padded '25.02' keeps its padding");
        require(formatChapterKey(key(QStringLiteral("0.01"))) == QStringLiteral("0.01"),
                "Berserk's '0.01' prologue keeps its padding");
        require(formatChapterKey(key(QStringLiteral("5.0"))) == QStringLiteral("5.0"),
                "'5.0' is a side chapter with ordinal 0, not a whole chapter");

        require(key(QStringLiteral("7")).subOrdinal == -1
                    && !key(QStringLiteral("7")).isSideChapter(),
                "a whole chapter carries ordinal -1");
        require(key(QStringLiteral("315.10")).subOrdinal == 10
                    && key(QStringLiteral("315.10")).isSideChapter(),
                "'315.10' is the TENTH side chapter, not 315.1");

        // Whitespace is stripped; a leading '-' is legal; leading zeros are numeric,
        // so "007" normalises to "7" exactly as Python's int() does.
        require(formatChapterKey(key(QStringLiteral(" 7 "))) == QStringLiteral("7"),
                "labels are stripped before parsing");
        require(key(QStringLiteral("-3")).whole == -3, "a negative whole label parses");
        require(formatChapterKey(key(QStringLiteral("007"))) == QStringLiteral("7"),
                "leading zeros in the whole part are numeric, like Python's int()");

        const char* const rejects[] = {"", "abc", "7.", ".5", "1.2.3", "1,5", "+3", "1e3", "-"};
        for (const char* bad : rejects) {
            require(!parseChapterKey(QString::fromLatin1(bad), nullptr),
                    QStringLiteral("'%1' is not a plain number").arg(QString::fromLatin1(bad)));
        }
    }

    // ── 2. Sub-chapters are ORDINALS, not fractions ────────────────────────────
    // Real Bleach rows: 315.1-315.9 are volume 36, 315.10-315.12 are volume 37.
    // Read as floats, "315.10" == "315.1" — the two pool into one vote and one of
    // the chapters vanishes from the shelf entirely.
    {
        require(key(QStringLiteral("315")) < key(QStringLiteral("315.1")),
                "a whole chapter sorts before its side chapters");
        require(key(QStringLiteral("315.1")) < key(QStringLiteral("315.9")),
                "315.1 < 315.9");
        require(key(QStringLiteral("315.9")) < key(QStringLiteral("315.10")),
                "315.9 < 315.10 — the tenth side chapter, not the first");
        require(key(QStringLiteral("315.10")) < key(QStringLiteral("315.11")),
                "315.10 < 315.11");
        require(!(key(QStringLiteral("315.1")) == key(QStringLiteral("315.10"))),
                "315.1 and 315.10 are two different chapters");

        // Both survive the vote: neither is dropped by a key collision.
        Rows rows;
        rows.add(QStringLiteral("315.1"), QStringLiteral("36"))
            .add(QStringLiteral("315.9"), QStringLiteral("36"))
            .add(QStringLiteral("315.10"), QStringLiteral("37"))
            .add(QStringLiteral("315.12"), QStringLiteral("37"))
            .add(QStringLiteral("322"), QStringLiteral("37"));
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(assign.size() == 5, "no source chapter is dropped by a key collision");
        require(assign.value(QStringLiteral("315.9")) == 36
                    && assign.value(QStringLiteral("315.10")) == 37,
                "315.9 and 315.10 land in different volumes, as the source says");
        require(render(groupVolumes(rows.list))
                    == QStringLiteral("36:315.1-315.9,37:315.10-322"),
                "the Bleach 36/37 seam groups exactly as the source labels it");
    }

    // ── 2b. subDigits is part of key identity, because it is in the Python tuple ─
    // "315.09" and "315.9" are equal on (whole, ordinal) but Python's key is the
    // 3-tuple (whole, ordinal, digits), so they are two DISTINCT entries ordered by
    // string compare. Not observed in today's corpus (measured 2026-07-29 over the
    // four Comick fixtures plus the MHA pull: zero such pairs) — this is mirrored so
    // a future corpus cannot make the batch and live paths disagree.
    {
        require(key(QStringLiteral("315.09")) < key(QStringLiteral("315.9")),
                "'315.09' sorts before '315.9' by digit-string compare, as in Python");
        require(key(QStringLiteral("315.1")) < key(QStringLiteral("315.09")),
                "the ordinal still dominates the digit string");
        Rows rows;
        rows.add(QStringLiteral("315.09"), QStringLiteral("36"))
            .add(QStringLiteral("315.9"), QStringLiteral("36"));
        require(majorityAssign(rows.list).size() == 2,
                "two spellings of the same ordinal stay two chapters, as in Python");
        require(render(groupVolumes(rows.list)) == QStringLiteral("36:315.09-315.9"),
                "both spellings survive and each is handed back byte-for-byte");
    }

    // ── 3. Majority vote ───────────────────────────────────────────────────────
    {
        // Two rows say volume 1, one stray row says volume 2 -> majority wins.
        Rows rows;
        rows.add(QStringLiteral("7"), QStringLiteral("1"), 2)
            .add(QStringLiteral("7"), QStringLiteral("2"))
            .add(QStringLiteral("8"), QStringLiteral("2"));
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(assign.value(QStringLiteral("7")) == 1, "the majority volume wins");
        require(assign.value(QStringLiteral("8")) == 2, "an uncontested chapter keeps its tag");
    }
    {
        // A dead tie means the sources genuinely contradict each other. Guessing a
        // winner would invent a book boundary; leaving the chapter out leaves a hole
        // the gate can see.
        Rows rows;
        rows.add(QStringLiteral("7"), QStringLiteral("1"))
            .add(QStringLiteral("7"), QStringLiteral("2"))
            .add(QStringLiteral("8"), QStringLiteral("2"));
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(!assign.contains(QStringLiteral("7")),
                "a dead tie leaves the chapter UNASSIGNED rather than guessed");
        require(assign.value(QStringLiteral("8")) == 2, "the tie does not disturb its neighbour");
    }
    {
        // Rows missing chap or vol do not vote at all.
        Rows rows;
        rows.add(QStringLiteral("7"), QString())
            .add(QString(), QStringLiteral("1"))
            .add(QStringLiteral("8"), QStringLiteral("1"));
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(assign.size() == 1 && assign.value(QStringLiteral("8")) == 1,
                "rows missing chap or vol do not vote");
    }
    {
        // A fractional VOLUME tag votes for its whole book: "1.5" is a vote for 1.
        Rows rows;
        rows.add(QStringLiteral("7"), QStringLiteral("1.5"));
        require(labelled(majorityAssign(rows.list)).value(QStringLiteral("7")) == 1,
                "only the volume's whole part votes");
    }

    // ── 4. Stray-tag repair, both directions ───────────────────────────────────
    {
        // Naruto shape: one uploader put 459.3 in volume 50 while its neighbours are
        // all volume 49. Physical volumes are sequential, so a later chapter is never
        // bound into an earlier book.
        Rows rows;
        rows.span(454, 463, QStringLiteral("49"), 4)
            .add(QStringLiteral("459.3"), QStringLiteral("50"))
            .span(464, 473, QStringLiteral("50"), 4);
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(assign.value(QStringLiteral("459.3")) == 49,
                "Naruto 459.3 joins the volume-49 run it sits inside");
        require(assign.value(QStringLiteral("463")) == 49
                    && assign.value(QStringLiteral("464")) == 50,
                "the real volume 49/50 seam is left exactly where the sources put it");
    }
    {
        // Berserk shape, the mirror image: chapter 106.5 carries ONE row tagging it
        // volume 13 while 106 and 107 carry eleven rows each for volume 15. The stray
        // chapter joins its neighbours — the sixteen well-attested chapters around it
        // must not budge. (Capping by the lowest volume claimed later would be
        // monotonic too, and would drag all of them down into volume 13.)
        Rows rows;
        rows.span(100, 110, QStringLiteral("15"), 11)
            .add(QStringLiteral("106.5"), QStringLiteral("13"));
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(assign.value(QStringLiteral("106.5")) == 15,
                "Berserk 106.5 is pulled FORWARD to volume 15");
        bool consensusHeld = true;
        for (auto it = assign.constBegin(); it != assign.constEnd(); ++it) {
            if (it.key() != QStringLiteral("106.5") && it.value() != 15)
                consensusHeld = false;
        }
        require(consensusHeld, "the stray moves and the consensus never does");
    }
    {
        // The repair is POSITIONAL, not vote-weighted, and the code should not be
        // read as if it were weighing evidence: a 9-vote chapter flanked by two
        // 1-vote neighbours that agree with each other is still the one that moves.
        // This has never fired in the corpus; the test pins the documented behaviour
        // so nobody "fixes" it into a vote-weighted rule by accident.
        Rows rows;
        rows.add(QStringLiteral("1"), QStringLiteral("2"))
            .add(QStringLiteral("2"), QStringLiteral("1"), 9)
            .add(QStringLiteral("3"), QStringLiteral("2"));
        require(labelled(majorityAssign(rows.list)).value(QStringLiteral("2")) == 2,
                "the middle chapter moves on position, not on vote weight");
    }
    {
        // Correcting a stray must never conjure an assignment for a chapter nobody
        // tagged: 8 is untagged by every source and stays out of the map.
        Rows rows;
        rows.add(QStringLiteral("7"), QStringLiteral("1"))
            .add(QStringLiteral("8"), QString())
            .add(QStringLiteral("9"), QStringLiteral("2"))
            .add(QStringLiteral("10"), QStringLiteral("1"), 2);
        const QMap<QString, int> assign = labelled(majorityAssign(rows.list));
        require(!assign.contains(QStringLiteral("8")),
                "repair never invents an assignment for an untagged chapter");
        require(assign.value(QStringLiteral("9")) == 1,
                "9 is pulled back to its two agreeing ASSIGNED neighbours");
    }

    // ── 5. Grouping ────────────────────────────────────────────────────────────
    {
        Rows rows;
        rows.add(QStringLiteral("1"), QStringLiteral("1"), 2)
            .add(QStringLiteral("2"), QStringLiteral("1"));
        require(render(groupVolumes(rows.list)) == QStringLiteral("1:1-2"),
                "duplicate scanlation rows collapse to one chapter");
    }
    {
        Rows rows;
        rows.add(QStringLiteral("1"), QStringLiteral("1"))
            .add(QStringLiteral("2"), QStringLiteral("1"))
            .add(QStringLiteral("2.5"), QStringLiteral("2"))
            .add(QStringLiteral("3"), QStringLiteral("2"));
        require(render(groupVolumes(rows.list)) == QStringLiteral("1:1-2,2:2.5-3"),
                "a volume that opens on a side chapter keeps that label as its start");
    }
    {
        Rows rows;
        rows.add(QStringLiteral("110.30"), QStringLiteral("9"))
            .add(QStringLiteral("110.5"), QStringLiteral("9"));
        require(render(groupVolumes(rows.list)) == QStringLiteral("9:110.5-110.30"),
                "'110.5' opens and '110.30' closes — ordinal order, exact labels");
        Rows single;
        single.add(QStringLiteral("25.02"), QStringLiteral("3"));
        require(render(groupVolumes(single.list)) == QStringLiteral("3:25.02-25.02"),
                "a padded label survives grouping unchanged");
    }
    {
        require(groupVolumes({}).isEmpty(), "no rows means no volumes");
    }

    // ── 6. Oddball numbering ───────────────────────────────────────────────────
    {
        Rows berserk;   // chapters 0.01, 0.02, ... — Comick's numbering is offset
        berserk.add(QStringLiteral("0.01"), QStringLiteral("1"))
               .add(QStringLiteral("0.02"), QStringLiteral("1"))
               .add(QStringLiteral("1"), QStringLiteral("1"));
        require(numberingIsOddball(berserk.list),
                "a fractional FIRST chapter is a numbering quirk");

        Rows bleach;
        bleach.add(QStringLiteral("1"), QStringLiteral("1"))
              .add(QStringLiteral("27.2"), QStringLiteral("3"))
              .add(QStringLiteral("28"), QStringLiteral("3"));
        require(!numberingIsOddball(bleach.list),
                "a mid-series sub-chapter is NOT a quirk — it buckets fine");

        Rows deathNote;
        deathNote.add(QStringLiteral("0"), QString())
                 .add(QStringLiteral("1"), QStringLiteral("1"));
        require(!numberingIsOddball(deathNote.list),
                "Death Note's chapter 0 start is a clean integer origin");

        require(numberingIsOddball({}), "nothing parseable is treated as a quirk");
    }

    // ── 7. The gate ────────────────────────────────────────────────────────────
    {
        const QList<VolumeRange> vols = spanVolumes(1, 42);
        const GateVerdict verdict = gateVolumes(vols, false, rowsFor(vols));
        require(verdict.qualified, QStringLiteral("contiguous run from 1 qualifies: %1")
                                       .arg(verdict.reason));
    }
    {
        const QList<VolumeRange> vols = spanVolumes(0, 12);   // Death Note: vol 0..12
        require(gateVolumes(vols, false, rowsFor(vols)).qualified,
                "a shelf that opens at volume 0 qualifies");
    }
    {
        require(!gateVolumes(spanVolumes(1, 1), true, {}).qualified,
                "a numbering quirk fails the gate outright");
        require(gateVolumes(spanVolumes(1, 1), true, {}).reason.contains(
                    QStringLiteral("numbering quirk")),
                "the quirk rejection says so");
        require(!gateVolumes({}, false, {}).qualified, "no mapped volumes fails the gate");
        require(gateVolumes({}, false, {}).reason == QStringLiteral("no mapped volumes"),
                "the empty rejection says so");
    }
    {
        QList<VolumeRange> vols;   // the en-only MHA shape: 1, 19, 38
        vols << VolumeRange{1, QStringLiteral("10"), QStringLiteral("19")}
             << VolumeRange{19, QStringLiteral("190"), QStringLiteral("199")}
             << VolumeRange{38, QStringLiteral("380"), QStringLiteral("389")};
        const GateVerdict verdict = gateVolumes(vols, false, rowsFor(vols));
        require(!verdict.qualified && verdict.reason.contains(QStringLiteral("gap")),
                "a mid-run volume gap fails the gate");
    }
    {
        const QList<VolumeRange> vols = spanVolumes(25, 38);
        const GateVerdict verdict = gateVolumes(vols, false, rowsFor(vols));
        require(!verdict.qualified && verdict.reason.contains(QStringLiteral("25")),
                "a shelf that opens at volume 25 is not a shelf");
    }
    {
        // Naruto shape: a stray row dragged volume 50's start back inside volume 49's
        // span. The volume numbers still read 1..50, so only a span check catches it.
        const QList<VolumeRange> clean = spanVolumes(1, 48);
        QList<VolumeRange> vols = clean;
        vols << VolumeRange{49, QStringLiteral("490"), QStringLiteral("499")}
             << VolumeRange{50, QStringLiteral("495"), QStringLiteral("509")};
        Rows rows;
        rows.append(rowsFor(clean)).span(490, 509, QStringLiteral("49"));
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(!verdict.qualified && verdict.reason.contains(QStringLiteral("overlap"))
                    && verdict.reason.contains(QStringLiteral("49")),
                "overlapping spans fail the gate");
    }
    {
        // Two adjacent mis-tags are not a lone stray, so nothing corrects them. The
        // volumes then overlap and the gate refuses — the series falls back to a
        // chapter list rather than having well-attested chapters silently reshaped.
        Rows rows;
        rows.span(1, 3, QStringLiteral("1"))
            .span(4, 5, QStringLiteral("3"))      // both mis-tagged
            .span(6, 7, QStringLiteral("2"));
        const QList<VolumeRange> vols = groupVolumes(rows.list);
        require(vols.size() == 3, "nothing is dropped or invented by an unfixable run");
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(!verdict.qualified && verdict.reason.contains(QStringLiteral("overlap"))
                    && verdict.reason.contains(QStringLiteral("2")),
                "a run that stays out of order never reaches the shelf");
    }
    {
        // Vinland Saga shape: nine whole chapters carry no volume tag in ANY language,
        // so the volume after them starts past the hole — inside an otherwise perfect
        // 1..29. Coverage is the only check that can see it.
        const QList<VolumeRange> clean = spanVolumes(1, 27, 7);   // chapters 7..195
        QList<VolumeRange> vols = clean;
        vols << VolumeRange{28, QStringLiteral("196"), QStringLiteral("203")}
             << VolumeRange{29, QStringLiteral("213"), QStringLiteral("214")};
        Rows rows;
        rows.append(rowsFor(clean))
            .span(196, 203, QStringLiteral("28"))
            .span(204, 212, QString(), 4)                          // untagged, 4 languages
            .span(213, 214, QStringLiteral("29"));
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(!verdict.qualified && verdict.reason.contains(QStringLiteral("no volume")),
                "chapters stranded at the seam between volumes fail the gate");
        require(verdict.reason.contains(QStringLiteral("9 chapter"))
                    && verdict.reason.contains(QStringLiteral("204")),
                "the count is CHAPTERS not ROWS (9, not 36) and names the first hole");
    }
    {
        // The sparse-anchor case, which is exactly the interpolation this gate exists
        // to refuse: volume 2 is tagged on chapters 11 and 20 ONLY, so its span
        // stretches across 12-19 — eight whole chapters nobody tagged, silently
        // absorbed. Numbers and seams both look perfect.
        Rows rows;
        rows.span(1, 10, QStringLiteral("1"))
            .add(QStringLiteral("11"), QStringLiteral("2"))
            .span(12, 19, QString(), 5)                            // 5 language rows each
            .add(QStringLiteral("20"), QStringLiteral("2"))
            .span(21, 30, QStringLiteral("3"));
        const QList<VolumeRange> vols = groupVolumes(rows.list);
        require(render(vols) == QStringLiteral("1:1-10,2:11-20,3:21-30"),
                "the sparse-anchor spans look flawless");
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(!verdict.qualified && verdict.reason.contains(QStringLiteral("no volume")),
                "chapters swallowed inside a stretched span fail the gate");
        require(verdict.reason.contains(QStringLiteral("8 chapter"))
                    && verdict.reason.contains(QStringLiteral("12")),
                "the count is CHAPTERS not ROWS (8, not 40)");
    }
    {
        // Real Bleach: volume 19 is 159-168 and volume 20 is 169-178 — back to back,
        // every whole chapter in a book. The only thing between them is one untagged
        // 168.5 extra, never bound into either volume. That is not a hole.
        const QList<VolumeRange> clean = spanVolumes(1, 18, 8);   // chapters 8..151
        QList<VolumeRange> vols = clean;
        vols << VolumeRange{19, QStringLiteral("159"), QStringLiteral("168")}
             << VolumeRange{20, QStringLiteral("169"), QStringLiteral("178")};
        Rows rows;
        rows.append(rowsFor(clean))
            .span(159, 168, QStringLiteral("19"))
            .add(QStringLiteral("168.5"), QString())               // exists, untagged, an extra
            .span(169, 178, QStringLiteral("20"));
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(verdict.qualified,
                QStringLiteral("an untagged SIDE chapter between volumes is not a hole: %1")
                    .arg(verdict.reason));
    }
    {
        // 499.5 sits after one volume ends and IS the next volume's first chapter —
        // a real Bleach shape, and not a hole.
        const QList<VolumeRange> clean = spanVolumes(1, 48);
        QList<VolumeRange> vols = clean;
        vols << VolumeRange{49, QStringLiteral("490"), QStringLiteral("499")}
             << VolumeRange{50, QStringLiteral("499.5"), QStringLiteral("509")};
        Rows rows;
        rows.append(rowsFor(clean))
            .span(490, 499, QStringLiteral("49"))
            .add(QStringLiteral("499.5"), QStringLiteral("50"))
            .span(500, 509, QStringLiteral("50"));
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(verdict.qualified,
                QStringLiteral("a side chapter that OPENS the next volume qualifies: %1")
                    .arg(verdict.reason));
    }
    {
        // Below the first volume: Bleach's untagged chapter 0 one-shot, genuinely in
        // no book. Above the last volume: the uncollected tail of an ongoing series,
        // which the app shows as "Latest chapters". Neither is a hole in the shelf.
        const QList<VolumeRange> vols = spanVolumes(1, 5);   // chapters 10..59
        Rows rows;
        rows.append(rowsFor(vols))
            .add(QStringLiteral("0"), QString())
            .add(QStringLiteral("5"), QString())              // before volume 1 starts
            .span(60, 69, QString());                         // after volume 5 ends
        const GateVerdict verdict = gateVolumes(vols, false, rows.list);
        require(verdict.qualified,
                QStringLiteral("chapters outside the shelf's range are ignored: %1")
                    .arg(verdict.reason));
    }

    // ── 8. Parity with the Python on real data ─────────────────────────────────
    // The single most valuable test here: real My Hero Academia rows through the C++
    // must produce the exact 42-volume record the Python published.
    {
        const QList<ChapterRow> rows = expandCompact(QString::fromLatin1(kMhaCompact));
        // 2636 of the raw pull's 2714 rows carry a chapter label; the other 78 have a
        // null `chap` and are discarded by every function before it does anything.
        require(rows.size() == 2636,
                QStringLiteral("embedded MHA fixture expands to 2636 chapter-bearing rows, got %1")
                    .arg(rows.size()));

        const QList<VolumeRange> vols = groupVolumes(rows);
        require(vols.size() == 42,
                QStringLiteral("MHA groups into 42 volumes, got %1").arg(vols.size()));
        bool unbroken = true;
        for (int i = 0; i < vols.size(); ++i) {
            if (vols.at(i).number != i + 1)
                unbroken = false;
        }
        require(unbroken, "MHA volume numbers are an unbroken 1..42");
        require(vols.first().chapterStart == QStringLiteral("1")
                    && vols.first().chapterEnd == QStringLiteral("7.5"),
                QStringLiteral("MHA volume 1 is 1-7.5, got %1-%2")
                    .arg(vols.first().chapterStart, vols.first().chapterEnd));
        requireEqual(render(vols), QString::fromLatin1(kMhaExpected),
                     QStringLiteral("MHA parity with the Python's published record "
                                    "(42 volumes, every boundary label)"));

        const bool quirk = numberingIsOddball(rows);
        require(!quirk, "MHA numbering is not a quirk (clean integer origin)");
        const GateVerdict verdict = gateVolumes(vols, quirk, rows);
        require(verdict.qualified,
                QStringLiteral("MHA qualifies for tankoban mode: %1").arg(verdict.reason));
        note(QStringLiteral("MHA parity (embedded corpus): 42 volumes, gate QUALIFIED"));
    }
    {
        // The same assertions against the raw JSON pull, when it is on this machine.
        // This is what proves the embedded fixture above is faithful to real data
        // rather than to itself.
        const QStringList candidates{
            QStringLiteral("C:/Users/Suprabha/Desktop/Brotherhood/scripts/mha_all.json"),
            QStringLiteral("../../scripts/mha_all.json"),
            QStringLiteral("scripts/mha_all.json")};

        QList<ChapterRow> rows;
        QString why;
        QString found;
        for (const QString& candidate : candidates) {
            if (loadComickJson(candidate, &rows, &why)) {
                found = candidate;
                break;
            }
        }
        if (found.isEmpty()) {
            note(QStringLiteral("MHA parity (raw JSON): SKIPPED — %1").arg(why));
            note(QStringLiteral("  run `comick_volume_grouper_harness <payload.json>` to inspect "
                                "one directly; the embedded corpus above already covered the "
                                "same assertions"));
        } else {
            require(rows.size() == 2714,
                    QStringLiteral("%1 holds 2714 chapter rows, got %2")
                        .arg(found).arg(rows.size()));
            const QList<VolumeRange> vols = groupVolumes(rows);
            requireEqual(render(vols), QString::fromLatin1(kMhaExpected),
                         QStringLiteral("raw-JSON MHA parity with the published record"));
            const bool quirk = numberingIsOddball(rows);
            require(!quirk, "raw-JSON MHA numbering is not a quirk");
            require(gateVolumes(vols, quirk, rows).qualified,
                    "raw-JSON MHA qualifies for tankoban mode");
            note(QStringLiteral("MHA parity (raw JSON %1): 42 volumes, gate QUALIFIED")
                     .arg(found));
        }
    }

    if (g_failures > 0) {
        std::cout << "comick_volume_grouper_harness: FAIL (" << g_failures << " of "
                  << g_contracts << " contracts)\n";
        return 1;
    }
    std::cout << "comick_volume_grouper_harness: PASS (" << g_contracts << " contracts)\n";
    return 0;
}
