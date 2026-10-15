#pragma once
// LanistaLayoutVerdict — pure, header-only geometry evaluator for Lanista's runner-owned
// `layout_verdict` scenario step (Agent Visibility Phase 2, Slice L2).
//
// WHAT THIS IS. Turns "this control is cut off / sitting on top of its peer" into a
// deterministic red without a screenshot or a global overlap heuristic. It consumes the
// L1-Bridge structural-dump vocabulary VERBATIM (dump-ui's own reply shape: generation,
// items[] each carrying objectName/sceneRect/visible/enabled, plus rootWindow) and never
// invents a second geometry source. Header-only and free of QCoreApplication/QObject so the
// SAME translation unit is #include'd directly by both `native/tools/lanista.cpp` (the
// runner) and `tests/auto/lanista/tst_layout_verdict.cpp` (the Qt Test) — one algorithm, two
// callers, no native/CMakeLists.txt edit (that file carries another lane's uncommitted hunk;
// the Qt Test target compiles just its own .cpp, exactly like tst_http_header_fields.cpp
// compiles native/player/http_header_fields.h).
//
// THE THREE RULE KINDS (deliberately narrow — no others exist):
//   actionableNonzero — a named control has nonzero WIDTH and HEIGHT, is VISIBLE, and is
//                        ENABLED. A zero-size, hidden, or disabled "actionable" fails.
//   contained          — a named target rect sits within a named viewport rect, inclusive of
//                        an explicit tolerance in LOGICAL pixels (never device pixels — see
//                        below). Touching an edge exactly at the tolerance boundary passes.
//   noPeerOverlap      — an EXPLICIT list of named peers (2+) must not pairwise overlap.
//                        Only the named peers are compared — this is NEVER a global sweep
//                        over every item in the snapshot, by construction: the rule's only
//                        input is the `peers` array the checkpoint JSON names.
//
// THE SINGLE-GENERATION GUARANTEE. A layout verdict must never resolve its rules against rows
// captured at different moments — a delegate that moved between two dump-ui calls could
// otherwise manufacture a false pass or fail. LayoutSnapshot enforces this structurally: it
// accumulates rows from one or more dump-ui REPLIES (a checkpoint may need more rows than one
// bounded reply's byte/item budget allows, so the runner pages via `continuation`), but the
// FIRST reply's `generation` field pins the snapshot, and every LATER reply must carry the
// SAME `generation` or its rows are rejected and `hasMismatch()` latches true. A mismatched
// snapshot never produces a passing verdict — evaluateCheckpoint() fails the whole checkpoint
// with a named `oneGenerationIsRequired` rule rather than silently evaluating on whatever
// partial data merged cleanly. This is exactly the guarantee L1-Bridge's own `dump-ui`
// continuation contract already provides (a continuation is honored ONLY when the caller
// echoes back the generation its cursor was minted against) — LayoutSnapshot simply refuses
// to paper over a caller that violates it.
//
// LOGICAL UNITS, NEVER DEVICE PIXELS. Every rect this evaluator touches is already in
// scene/logical units — the SAME space dump-ui and ui-query report (mapRectToScene, never a
// grab's device-pixel PNG). This header performs NO devicePixelRatio scaling anywhere; it
// consumes the numbers it is given and nothing else. A caller that fed it device-pixel numbers
// would get a wrong answer, but that is a caller bug, not this evaluator inventing DPR math.
//
// THE SYNTHETIC "$rootWindow" VIEWPORT. dump-ui's reply carries a top-level `rootWindow`
// field (the window's own width/height) that names no QML objectName — there is no item to
// address it by. LayoutSnapshot exposes it under the reserved name kRootWindowName so a
// checkpoint's `contained` rule can use the whole window as a viewport ("target": "someCard",
// "viewport": "$rootWindow") without a fabricated production object. `$` cannot appear in a
// real QML objectName (QML identifiers are ASCII letters/digits/underscore), so the name never
// collides with a real item.
//
// WHAT THIS FILE DOES NOT DO: it never calls the bridge, never owns a QLocalSocket, never
// takes a grab, and never performs a global no-overlap sweep. All bridge I/O and paging live
// in native/tools/lanista.cpp's `layout_verdict` scenario step; this header is pure data in,
// pure verdict out.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lanista {

// The reserved name for the synthetic whole-window viewport (see header note above). Not a
// real QML objectName — `$` never appears in one — so it can never collide with a real item.
inline const QString& rootWindowVirtualName()
{
    static const QString kName = QStringLiteral("$rootWindow");
    return kName;
}

// One structural row's layout-relevant facts, narrowed to exactly what a layout rule can ever
// need. Built from a dump-ui item row (or the reply's own rootWindow field for the synthetic
// viewport) — never from a grab, never with any unit conversion.
struct LayoutItem {
    QString objectName;
    QString handle;
    QRectF sceneRect;      // logical/scene units — same space as ui-query / dump-ui
    bool visible = false;
    bool enabled = false;
    double opacity = 0.0;
    bool found = false;    // false = the default-constructed "missing" sentinel
};

inline QRectF rectFromJson(const QJsonObject& r)
{
    return QRectF(r.value(QStringLiteral("x")).toDouble(),
                  r.value(QStringLiteral("y")).toDouble(),
                  r.value(QStringLiteral("width")).toDouble(),
                  r.value(QStringLiteral("height")).toDouble());
}

inline QJsonObject rectToJson(const QRectF& r)
{
    return QJsonObject{{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                        {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
}

inline LayoutItem layoutItemFromDumpRow(const QJsonObject& row)
{
    LayoutItem it;
    it.found = true;
    it.objectName = row.value(QStringLiteral("objectName")).toString();
    it.handle = row.value(QStringLiteral("handle")).toString();
    it.sceneRect = rectFromJson(row.value(QStringLiteral("sceneRect")).toObject());
    it.visible = row.value(QStringLiteral("visible")).toBool();
    it.enabled = row.value(QStringLiteral("enabled")).toBool();
    it.opacity = row.value(QStringLiteral("opacity")).toDouble();
    return it;
}

// A bounded set of structural rows, all belonging to ONE L1 structural-dump generation — see
// the header's "single-generation guarantee" note. Rows are indexed by objectName (the
// checkpoint JSON's own addressing scheme) and by handle (kept for completeness/diagnosis;
// no rule kind below currently addresses by handle).
class LayoutSnapshot
{
public:
    // Merges one dump-ui REPLY (the verbatim JSON object the bridge returned: top-level
    // "generation", "items"[], optional "rootWindow"). Returns false — and merges NOTHING
    // from `reply` — when a generation has already been pinned by an earlier merge and this
    // reply's own "generation" disagrees; hasMismatch() latches true in that case and stays
    // true for the life of the snapshot. The FIRST successful merge pins m_generation.
    bool mergeDumpReply(const QJsonObject& reply)
    {
        const int gen = reply.value(QStringLiteral("generation")).toInt(-1);
        if (m_generation == -1) {
            m_generation = gen;
        } else if (gen != m_generation) {
            m_mismatch = true;
            return false;
        }

        for (const QJsonValue& v : reply.value(QStringLiteral("items")).toArray()) {
            const QJsonObject row = v.toObject();
            const QString name = row.value(QStringLiteral("objectName")).toString();
            if (name.isEmpty())
                continue;   // unnamed rows carry no address a checkpoint rule could name
            const LayoutItem item = layoutItemFromDumpRow(row);
            // FIRST occurrence wins, matching the rest of the bridge's own documented
            // name-collision law ("Name collisions resolve DFS-FIRST — including into
            // HIDDEN worlds", colosseum-lanista-verification.md). dump-ui walks in DFS
            // pre-order and pages strictly in that order, so "first seen while merging
            // pages in order" IS "DFS-first" — this matters for real: production
            // pre-warms other worlds' pages in the background, and each carries its own
            // TopBar with the SAME modePill_* objectNames, invisible/deeper in the tree.
            // A last-write-wins map would silently let a later, hidden, unrelated
            // duplicate overwrite the real on-screen row's geometry — proven live
            // 2026-08-13 against the real app_home checkpoint before this guard existed.
            if (!m_byName.contains(name))
                m_byName.insert(name, item);
            if (!item.handle.isEmpty() && !m_byHandle.contains(item.handle))
                m_byHandle.insert(item.handle, item);
        }

        if (reply.contains(QStringLiteral("rootWindow")) && !reply.value(QStringLiteral("rootWindow")).isNull()) {
            const QJsonObject rw = reply.value(QStringLiteral("rootWindow")).toObject();
            LayoutItem win;
            win.found = true;
            win.objectName = rootWindowVirtualName();
            win.sceneRect = QRectF(0.0, 0.0, rw.value(QStringLiteral("width")).toDouble(),
                                    rw.value(QStringLiteral("height")).toDouble());
            win.visible = true;
            win.enabled = true;
            win.opacity = 1.0;
            m_byName.insert(rootWindowVirtualName(), win);
        }
        return true;
    }

    int generation() const { return m_generation; }
    bool hasMismatch() const { return m_mismatch; }

    LayoutItem byName(const QString& name) const { return m_byName.value(name, LayoutItem{}); }
    LayoutItem byHandle(const QString& handle) const { return m_byHandle.value(handle, LayoutItem{}); }

private:
    int m_generation = -1;
    bool m_mismatch = false;
    QHash<QString, LayoutItem> m_byName;
    QHash<QString, LayoutItem> m_byHandle;
};

// One rule's outcome: which kind, the checkpoint's own name for it, pass/fail, a human
// detail, and the measured rectangles/intersection an evidence file can preserve verbatim.
struct RuleVerdict {
    QString kind;
    QString name;
    bool pass = false;
    QString detail;
    QJsonObject measured;
};

inline QJsonObject ruleVerdictToJson(const RuleVerdict& v)
{
    return QJsonObject{{QStringLiteral("kind"), v.kind}, {QStringLiteral("name"), v.name},
                        {QStringLiteral("pass"), v.pass}, {QStringLiteral("detail"), v.detail},
                        {QStringLiteral("measured"), v.measured}};
}

struct CheckpointVerdict {
    QString checkpoint;
    int generation = -1;
    bool allPass = false;
    QVector<RuleVerdict> rules;
};

inline QJsonObject checkpointVerdictToJson(const CheckpointVerdict& cv)
{
    QJsonArray rules;
    for (const RuleVerdict& r : cv.rules) rules.append(ruleVerdictToJson(r));
    return QJsonObject{{QStringLiteral("checkpoint"), cv.checkpoint},
                        {QStringLiteral("generation"), cv.generation},
                        {QStringLiteral("allPass"), cv.allPass},
                        {QStringLiteral("rules"), rules}};
}

// ── rule kind: actionableNonzero ────────────────────────────────────────────────────────────
inline RuleVerdict evaluateActionableNonzero(const LayoutSnapshot& snap, const QJsonObject& rule)
{
    RuleVerdict v;
    v.kind = QStringLiteral("actionableNonzero");
    v.name = rule.value(QStringLiteral("name")).toString();
    const QString target = rule.value(QStringLiteral("target")).toString();
    const LayoutItem item = snap.byName(target);

    if (!item.found) {
        v.pass = false;
        v.detail = QStringLiteral("target '%1' is not present in this generation's dump").arg(target);
        v.measured = QJsonObject{{QStringLiteral("target"), target}, {QStringLiteral("found"), false}};
        return v;
    }

    v.measured = QJsonObject{{QStringLiteral("target"), target},
                              {QStringLiteral("found"), true},
                              {QStringLiteral("rect"), rectToJson(item.sceneRect)},
                              {QStringLiteral("visible"), item.visible},
                              {QStringLiteral("enabled"), item.enabled}};

    if (item.sceneRect.width() <= 0.0 || item.sceneRect.height() <= 0.0) {
        v.pass = false;
        v.detail = QStringLiteral("'%1' has zero/negative actionable size (%2 x %3)")
                       .arg(target).arg(item.sceneRect.width()).arg(item.sceneRect.height());
        return v;
    }
    if (!item.visible) {
        v.pass = false;
        v.detail = QStringLiteral("'%1' is not visible").arg(target);
        return v;
    }
    if (!item.enabled) {
        v.pass = false;
        v.detail = QStringLiteral("'%1' is not enabled").arg(target);
        return v;
    }
    v.pass = true;
    v.detail = QStringLiteral("'%1' is actionable: %2 x %3, visible, enabled")
                   .arg(target).arg(item.sceneRect.width()).arg(item.sceneRect.height());
    return v;
}

// ── rule kind: contained ────────────────────────────────────────────────────────────────────
inline RuleVerdict evaluateContained(const LayoutSnapshot& snap, const QJsonObject& rule)
{
    RuleVerdict v;
    v.kind = QStringLiteral("contained");
    v.name = rule.value(QStringLiteral("name")).toString();
    const QString target = rule.value(QStringLiteral("target")).toString();
    const QString viewport = rule.value(QStringLiteral("viewport")).toString();
    const double tol = rule.value(QStringLiteral("toleranceLogicalPx")).toDouble(0.0);

    const LayoutItem t = snap.byName(target);
    const LayoutItem vp = snap.byName(viewport);
    if (!t.found || !vp.found) {
        v.pass = false;
        v.detail = QStringLiteral("missing target/viewport ('%1' found=%2, '%3' found=%4)")
                       .arg(target).arg(t.found).arg(viewport).arg(vp.found);
        v.measured = QJsonObject{{QStringLiteral("target"), target}, {QStringLiteral("targetFound"), t.found},
                                  {QStringLiteral("viewport"), viewport}, {QStringLiteral("viewportFound"), vp.found}};
        return v;
    }

    const QRectF tr = t.sceneRect;
    const QRectF tolerated = vp.sceneRect.adjusted(-tol, -tol, tol, tol);
    v.measured = QJsonObject{{QStringLiteral("target"), target}, {QStringLiteral("targetRect"), rectToJson(tr)},
                              {QStringLiteral("viewport"), viewport}, {QStringLiteral("viewportRect"), rectToJson(vp.sceneRect)},
                              {QStringLiteral("toleranceLogicalPx"), tol},
                              {QStringLiteral("toleratedRect"), rectToJson(tolerated)}};

    const bool inside = tr.left() >= tolerated.left() && tr.top() >= tolerated.top()
                         && tr.right() <= tolerated.right() && tr.bottom() <= tolerated.bottom();
    v.pass = inside;
    v.detail = inside
        ? QStringLiteral("'%1' is contained within '%2' (tolerance %3 logical px)").arg(target, viewport).arg(tol)
        : QStringLiteral("'%1' rect %2 exceeds '%3' bounds %4 beyond %5 logical px tolerance")
              .arg(target, QStringLiteral("%1,%2 %3x%4").arg(tr.x()).arg(tr.y()).arg(tr.width()).arg(tr.height()),
                   viewport, QStringLiteral("%1,%2 %3x%4").arg(vp.sceneRect.x()).arg(vp.sceneRect.y())
                                 .arg(vp.sceneRect.width()).arg(vp.sceneRect.height()))
              .arg(tol);
    return v;
}

// ── rule kind: noPeerOverlap ────────────────────────────────────────────────────────────────
// EXPLICIT peer list only — this function never iterates the snapshot's own item map, only
// the names the checkpoint rule itself supplies. That is the whole "never a global sweep"
// guarantee, enforced by construction rather than by a runtime flag.
inline RuleVerdict evaluateNoPeerOverlap(const LayoutSnapshot& snap, const QJsonObject& rule)
{
    RuleVerdict v;
    v.kind = QStringLiteral("noPeerOverlap");
    v.name = rule.value(QStringLiteral("name")).toString();
    const QJsonArray peerNames = rule.value(QStringLiteral("peers")).toArray();

    QVector<QPair<QString, LayoutItem>> resolved;
    for (const QJsonValue& pv : peerNames) {
        const QString name = pv.toString();
        const LayoutItem item = snap.byName(name);
        if (!item.found) {
            v.pass = false;
            v.detail = QStringLiteral("named peer '%1' is not present in this generation's dump").arg(name);
            v.measured = QJsonObject{{QStringLiteral("missingPeer"), name}};
            return v;
        }
        resolved.append({name, item});
    }

    QJsonArray measuredPeers;
    for (const auto& p : resolved)
        measuredPeers.append(QJsonObject{{QStringLiteral("name"), p.first}, {QStringLiteral("rect"), rectToJson(p.second.sceneRect)}});

    if (resolved.size() < 2) {
        v.pass = false;
        v.detail = QStringLiteral("noPeerOverlap needs at least 2 named peers, got %1").arg(resolved.size());
        v.measured = QJsonObject{{QStringLiteral("peers"), measuredPeers}};
        return v;
    }

    for (int i = 0; i < resolved.size(); ++i) {
        for (int j = i + 1; j < resolved.size(); ++j) {
            const QRectF a = resolved[i].second.sceneRect;
            const QRectF b = resolved[j].second.sceneRect;
            const QRectF inter = a.intersected(b);
            // A shared edge (zero-area intersection — either dimension is exactly 0) is NOT
            // an overlap: QRectF's right()/bottom() are x+width/y+height, so two rects that
            // exactly touch produce a 0-width or 0-height intersection here, never a false red.
            if (inter.width() > 0.0 && inter.height() > 0.0) {
                v.pass = false;
                v.detail = QStringLiteral("'%1' and '%2' overlap: intersection %3 x %4 at (%5,%6)")
                               .arg(resolved[i].first, resolved[j].first)
                               .arg(inter.width()).arg(inter.height()).arg(inter.x()).arg(inter.y());
                v.measured = QJsonObject{{QStringLiteral("peers"), measuredPeers},
                                          {QStringLiteral("overlapping"), QJsonArray{resolved[i].first, resolved[j].first}},
                                          {QStringLiteral("intersection"), rectToJson(inter)}};
                return v;
            }
        }
    }

    v.pass = true;
    v.detail = QStringLiteral("no overlap among %1 named peers").arg(resolved.size());
    v.measured = QJsonObject{{QStringLiteral("peers"), measuredPeers}};
    return v;
}

// ── the checkpoint: every named rule resolved against ONE snapshot ─────────────────────────
inline CheckpointVerdict evaluateCheckpoint(const LayoutSnapshot& snap, const QJsonObject& checkpoint)
{
    CheckpointVerdict cv;
    cv.checkpoint = checkpoint.value(QStringLiteral("checkpoint")).toString();
    cv.generation = snap.generation();

    // The single-generation guarantee's enforcement point: a snapshot that ever saw a page
    // disagree on generation NEVER produces a passing verdict, no matter what the individual
    // rules would otherwise say — moving delegates cannot manufacture a verdict from
    // mismatched moments.
    if (snap.hasMismatch()) {
        RuleVerdict v;
        v.kind = QStringLiteral("generation");
        v.name = QStringLiteral("oneGenerationIsRequired");
        v.pass = false;
        v.detail = QStringLiteral("structural rows came from more than one dump-ui generation; "
                                   "a layout verdict must resolve every rule against rows from "
                                   "exactly ONE generation");
        cv.rules.append(v);
        cv.allPass = false;
        return cv;
    }

    bool allPass = true;
    for (const QJsonValue& rv : checkpoint.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject rule = rv.toObject();
        const QString kind = rule.value(QStringLiteral("kind")).toString();
        RuleVerdict result;
        if (kind == QStringLiteral("actionableNonzero"))
            result = evaluateActionableNonzero(snap, rule);
        else if (kind == QStringLiteral("contained"))
            result = evaluateContained(snap, rule);
        else if (kind == QStringLiteral("noPeerOverlap"))
            result = evaluateNoPeerOverlap(snap, rule);
        else {
            result.kind = kind;
            result.name = rule.value(QStringLiteral("name")).toString();
            result.pass = false;
            result.detail = QStringLiteral("unknown layout-verdict rule kind: '%1'").arg(kind);
        }
        if (!result.pass) allPass = false;
        cv.rules.append(result);
    }
    cv.allPass = allPass;
    return cv;
}

} // namespace lanista
