// tst_layout_verdict — Agent Visibility Phase 2, Slice L2. Pure data-in/verdict-out coverage
// for native/tools/LanistaLayoutVerdict.h: the three rule kinds (actionableNonzero, contained,
// noPeerOverlap) and the single-generation guarantee, all fed synthetic dump-ui-shaped JSON —
// no bridge, no app, no QGuiApplication. Compiles the header directly (APPLESS), exactly like
// tst_http_header_fields.cpp compiles native/player/http_header_fields.h.
//
// Non-vacuous by construction: each case's synthetic fixture is built so that flipping the
// evaluator's own logic (removing a guard, inverting a comparison) would flip exactly that
// case red — verified live per-case below in the case's own comment; the mandatory negative
// controls (one_generation_is_required's rejected-merge path, and the runtime harness/app_home
// tolerance-boundary swap performed separately by the runner) are recorded in the slice report.

#include "tools/LanistaLayoutVerdict.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtTest>

using lanista::CheckpointVerdict;
using lanista::LayoutItem;
using lanista::LayoutSnapshot;
using lanista::RuleVerdict;
using lanista::evaluateCheckpoint;
using lanista::rootWindowVirtualName;

namespace {

// Builds one dump-ui-shaped reply object: {generation, items:[...], rootWindow?}. Each entry
// in `items` is {name, x, y, w, h, visible, enabled}. Mirrors LanistaServer::cmdDumpUi's own
// reply shape exactly (handle is synthesized as "h<n>" — this evaluator never inspects it).
QJsonObject makeDumpReply(int generation,
                          const QVector<QVariantList>& items,
                          const QSize& rootWindowSize = QSize())
{
    QJsonArray rows;
    int n = 0;
    for (const QVariantList& it : items) {
        rows.append(QJsonObject{
            {QStringLiteral("objectName"), it.value(0).toString()},
            {QStringLiteral("handle"), QStringLiteral("h%1").arg(++n)},
            {QStringLiteral("sceneRect"), QJsonObject{
                {QStringLiteral("x"), it.value(1).toDouble()}, {QStringLiteral("y"), it.value(2).toDouble()},
                {QStringLiteral("width"), it.value(3).toDouble()}, {QStringLiteral("height"), it.value(4).toDouble()}}},
            {QStringLiteral("visible"), it.value(5, true).toBool()},
            {QStringLiteral("enabled"), it.value(6, true).toBool()},
            {QStringLiteral("opacity"), 1.0},
        });
    }
    QJsonObject reply{{QStringLiteral("type"), QStringLiteral("reply")},
                       {QStringLiteral("generation"), generation},
                       {QStringLiteral("items"), rows},
                       {QStringLiteral("truncated"), false}};
    if (rootWindowSize.isValid())
        reply.insert(QStringLiteral("rootWindow"),
                     QJsonObject{{QStringLiteral("width"), rootWindowSize.width()},
                                 {QStringLiteral("height"), rootWindowSize.height()}});
    return reply;
}

QJsonObject rule(const QString& kind, const QString& name, const QJsonObject& extra)
{
    QJsonObject r{{QStringLiteral("kind"), kind}, {QStringLiteral("name"), name}};
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) r.insert(it.key(), it.value());
    return r;
}

QJsonObject checkpointOf(const QString& name, const QJsonArray& rules)
{
    return QJsonObject{{QStringLiteral("checkpoint"), name}, {QStringLiteral("rules"), rules}};
}

// Finds one named rule's verdict out of a checkpoint result — every case below asserts on
// exactly the rule it set up, never "did the whole checkpoint pass", so an unrelated rule
// (or a rule this case didn't even define) can never launder a wrong answer.
RuleVerdict ruleNamed(const CheckpointVerdict& cv, const QString& name)
{
    for (const RuleVerdict& r : cv.rules)
        if (r.name == name) return r;
    return RuleVerdict{};
}

} // namespace

class tst_layout_verdict : public QObject
{
    Q_OBJECT

private slots:
    void actionable_zero_size_fails();
    void hidden_actionable_fails();
    void disabled_actionable_fails();
    void contained_inside_passes();
    void contained_outside_tolerance_fails();
    void touching_edges_do_not_overlap();
    void named_peer_overlap_fails();
    void unnamed_peers_are_not_global_rules();
    void one_generation_is_required();
    void logical_units_ignore_device_pixel_ratio();
    // Beyond the plan's 10 named cases: locks in a real bug found and fixed live against
    // the assembled app during this slice's runtime replay (see the L2 report).
    void duplicate_names_resolve_dfs_first();
};

// A control with a real (nonzero) declared size still fails actionableNonzero once its scene
// rect collapses to 0x0 — the exact "actionable" that visually vanished but is still
// visible/enabled, the case the plan calls out by name ("a zero-size ... 'actionable' fails").
void tst_layout_verdict::actionable_zero_size_fails()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"zeroBtn", 100.0, 100.0, 0.0, 0.0, true, true},
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("actionableNonzero", "zeroBtnActionable",
            QJsonObject{{"target", "zeroBtn"}})}));
    const RuleVerdict r = ruleNamed(cv, "zeroBtnActionable");
    QVERIFY(!r.pass);
    QVERIFY(r.detail.contains("zero"));
    QVERIFY(!cv.allPass);
}

// Same real nonzero size, but visible:false — actionableNonzero must fail even though the
// size check alone would pass, proving the visibility guard is genuinely ANDed in, not
// skipped once size is satisfied.
void tst_layout_verdict::hidden_actionable_fails()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"hiddenBtn", 100.0, 100.0, 40.0, 20.0, false, true},
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("actionableNonzero", "hiddenBtnActionable",
            QJsonObject{{"target", "hiddenBtn"}})}));
    const RuleVerdict r = ruleNamed(cv, "hiddenBtnActionable");
    QVERIFY(!r.pass);
    QVERIFY(r.detail.contains("not visible"));
}

// Nonzero size, visible — but enabled:false. Proves the enabled guard is independent of both
// the size and visibility guards, not folded into either.
void tst_layout_verdict::disabled_actionable_fails()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"disabledBtn", 100.0, 100.0, 40.0, 20.0, true, false},
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("actionableNonzero", "disabledBtnActionable",
            QJsonObject{{"target", "disabledBtn"}})}));
    const RuleVerdict r = ruleNamed(cv, "disabledBtnActionable");
    QVERIFY(!r.pass);
    QVERIFY(r.detail.contains("not enabled"));
}

// A target rect fully inside a named viewport, zero tolerance, passes — the baseline the
// tolerance/boundary cases below are measured against.
void tst_layout_verdict::contained_inside_passes()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"viewport", 0.0, 0.0, 200.0, 200.0, true, true},
        {"child", 10.0, 10.0, 50.0, 50.0, true, true},
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("contained", "childInViewport",
            QJsonObject{{"target", "child"}, {"viewport", "viewport"}, {"toleranceLogicalPx", 0}})}));
    const RuleVerdict r = ruleNamed(cv, "childInViewport");
    QVERIFY(r.pass);
    QCOMPARE(cv.allPass, true);
}

// The target overflows the viewport's right edge by 5 logical px; a tolerance of 2 is not
// enough to cover it, so the rule fails — and a LARGER tolerance (5, exactly the overflow)
// passes, pinning the boundary arithmetic rather than just "some tolerance value fails".
void tst_layout_verdict::contained_outside_tolerance_fails()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"viewport", 0.0, 0.0, 200.0, 200.0, true, true},
        {"overflow", 100.0, 10.0, 105.0, 50.0, true, true},   // right edge = 205, 5px past 200
    })));
    const CheckpointVerdict tooTight = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("contained", "overflowTight",
            QJsonObject{{"target", "overflow"}, {"viewport", "viewport"}, {"toleranceLogicalPx", 2}})}));
    const RuleVerdict rTight = ruleNamed(tooTight, "overflowTight");
    QVERIFY(!rTight.pass);
    QVERIFY(rTight.detail.contains("exceeds"));

    const CheckpointVerdict exact = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("contained", "overflowExact",
            QJsonObject{{"target", "overflow"}, {"viewport", "viewport"}, {"toleranceLogicalPx", 5}})}));
    QVERIFY(ruleNamed(exact, "overflowExact").pass);
}

// Two peers that share an edge exactly (b starts exactly where a ends) must NOT be reported as
// overlapping — QRectF's right()/bottom() are x+width/y+height, so a naive `<=`/`>=` compare
// (instead of a real positive-area intersection) would wrongly flag this as overlap.
void tst_layout_verdict::touching_edges_do_not_overlap()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"a", 0.0, 0.0, 10.0, 10.0, true, true},
        {"b", 10.0, 0.0, 10.0, 10.0, true, true},   // a's right edge == b's left edge
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("noPeerOverlap", "abTouch",
            QJsonObject{{"peers", QJsonArray{"a", "b"}}})}));
    const RuleVerdict r = ruleNamed(cv, "abTouch");
    QVERIFY(r.pass);
}

// Two named peers with a genuine positive-area overlap fail, and the measured intersection
// rectangle is reported (evidence, not just a boolean).
void tst_layout_verdict::named_peer_overlap_fails()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"a", 0.0, 0.0, 10.0, 10.0, true, true},
        {"b", 5.0, 5.0, 10.0, 10.0, true, true},   // 5x5 overlap at (5,5)
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("noPeerOverlap", "abOverlap",
            QJsonObject{{"peers", QJsonArray{"a", "b"}}})}));
    const RuleVerdict r = ruleNamed(cv, "abOverlap");
    QVERIFY(!r.pass);
    QVERIFY(r.detail.contains("overlap"));
    const QJsonObject inter = r.measured.value("intersection").toObject();
    QCOMPARE(inter.value("width").toDouble(), 5.0);
    QCOMPARE(inter.value("height").toDouble(), 5.0);
}

// THE "never a global sweep" proof: item 'a' genuinely overlaps BOTH 'b' and 'c', but the
// checkpoint's noPeerOverlap rule names only ['b','c'] as peers — and b/c do NOT overlap each
// other. The rule must PASS: a global sweep over every item in the snapshot would instead see
// a-vs-b and a-vs-c overlapping and wrongly fail. 'a' is deliberately not named in ANY rule.
void tst_layout_verdict::unnamed_peers_are_not_global_rules()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"a", 0.0, 0.0, 20.0, 20.0, true, true},     // overlaps both b and c
        {"b", 10.0, 0.0, 20.0, 20.0, true, true},    // overlaps a (10..20 x 0..20)
        {"c", 0.0, 10.0, 5.0, 5.0, true, true},      // overlaps a, but NOT b (b starts x=10)
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("noPeerOverlap", "bcOnly",
            QJsonObject{{"peers", QJsonArray{"b", "c"}}})}));
    const RuleVerdict r = ruleNamed(cv, "bcOnly");
    QVERIFY2(r.pass, qUtf8Printable(QStringLiteral("expected pass (a's overlap with b/c must be "
        "ignored — it is not a named peer): ") + r.detail));
}

// A checkpoint whose structural rows were merged from TWO dump-ui replies reporting DIFFERENT
// generations must never produce a passing verdict — the single-generation guarantee's own
// enforcement point. The second reply's rows are also proven NOT to have entered the snapshot
// at all (a stricter proof than "the checkpoint failed somehow").
void tst_layout_verdict::one_generation_is_required()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"stableWidget", 0.0, 0.0, 40.0, 40.0, true, true},
    })));
    QCOMPARE(snap.generation(), 1);
    QVERIFY(!snap.hasMismatch());

    // A second page claiming to continue the SAME walk, but reporting generation 2 — exactly
    // what a moving delegate between two independent dump-ui calls would produce.
    const bool merged = snap.mergeDumpReply(makeDumpReply(2, {
        {"movedWidget", 999.0, 999.0, 40.0, 40.0, true, true},
    }));
    QVERIFY(!merged);
    QVERIFY(snap.hasMismatch());
    // The rejected page's own row must NOT be reachable — a mismatched merge contributes
    // nothing, it does not "mostly" merge.
    QVERIFY(!snap.byName("movedWidget").found);
    QVERIFY(snap.byName("stableWidget").found);

    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("actionableNonzero", "stableWidgetActionable",
            QJsonObject{{"target", "stableWidget"}})}));
    QVERIFY(!cv.allPass);
    const RuleVerdict genRule = ruleNamed(cv, "oneGenerationIsRequired");
    QVERIFY(genRule.kind == QStringLiteral("generation"));
    QVERIFY(!genRule.pass);
    // The whole-checkpoint short-circuit means the ordinary rule never even reports a pass —
    // it must be absent, not silently green, once a mismatch is latched.
    QVERIFY(ruleNamed(cv, "stableWidgetActionable").name.isEmpty());
}

// Proves the evaluator applies NO devicePixelRatio scaling anywhere: fractional logical-pixel
// rects (the kind Qt Quick's own anchor/layout math produces routinely) are compared exactly
// as given. If a DPR multiplier (e.g. 1.5x, the ledger's own documented value on this machine)
// were secretly applied, a target equal to its viewport would appear to overflow and this
// would go red.
void tst_layout_verdict::logical_units_ignore_device_pixel_ratio()
{
    LayoutSnapshot snap;
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"viewport", 0.0, 0.0, 100.5, 64.25, true, true},
        {"exact", 0.0, 0.0, 100.5, 64.25, true, true},   // byte-identical fractional rect
    })));
    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("contained", "exactFractional",
            QJsonObject{{"target", "exact"}, {"viewport", "viewport"}, {"toleranceLogicalPx", 0}})}));
    const RuleVerdict r = ruleNamed(cv, "exactFractional");
    QVERIFY2(r.pass, qUtf8Printable(r.detail));
    // The measured rect the evidence carries must equal the INPUT numbers exactly — no
    // rounding, no scaling by any ratio.
    const QJsonObject measuredRect = r.measured.value("targetRect").toObject();
    QCOMPARE(measuredRect.value("width").toDouble(), 100.5);
    QCOMPARE(measuredRect.value("height").toDouble(), 64.25);
}

// Real production shape (found live, 2026-08-13, driving the assembled app's home screen):
// Colosseum pre-warms other worlds' pages in the background, and TopBar ("ONE source for
// the top bar across the home AND every world page") is reused verbatim by each — so the
// SAME modePill_* objectName exists more than once in the full tree: once on the real,
// visible home page, and again inside each hidden pre-warmed world's own TopBar instance,
// deeper and invisible. A dump-ui walk that pages past the home row eventually reaches the
// hidden duplicate. This proves LayoutSnapshot keeps the FIRST (DFS) occurrence across
// merged pages, matching the bridge's own documented "name collisions resolve DFS-first"
// law — not a last-write-wins map that would let a later hidden duplicate silently
// overwrite the real on-screen row.
void tst_layout_verdict::duplicate_names_resolve_dfs_first()
{
    LayoutSnapshot snap;
    // Page 0: the real, visible, on-screen row appears FIRST (shallower depth), as it does
    // in production (home's TopBar sits nearer the tree root than a pre-warmed world's).
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"modePill_Tankoban", 460.0, 41.0, 93.0, 34.0, true, true},
    })));
    // Page 1 (continuation, SAME generation): a hidden pre-warmed world's own duplicate
    // TopBar instance, invisible, deeper in the tree — exactly the real shape found live.
    QVERIFY(snap.mergeDumpReply(makeDumpReply(1, {
        {"modePill_Tankoban", 460.0, 41.0, 93.0, 34.0, false, true},
    })));

    const LayoutItem resolved = snap.byName(QStringLiteral("modePill_Tankoban"));
    QVERIFY(resolved.found);
    QVERIFY2(resolved.visible, "the FIRST (real, on-screen) occurrence must win, not the "
                                "later hidden duplicate");

    const CheckpointVerdict cv = evaluateCheckpoint(snap,
        checkpointOf("t", QJsonArray{rule("actionableNonzero", "tankobanPillActionable",
            QJsonObject{{"target", "modePill_Tankoban"}})}));
    QVERIFY2(ruleNamed(cv, "tankobanPillActionable").pass,
              "a real, visible control must not read as not-actionable because an unrelated "
              "hidden duplicate shares its name");
}

QTEST_APPLESS_MAIN(tst_layout_verdict)
#include "tst_layout_verdict.moc"
