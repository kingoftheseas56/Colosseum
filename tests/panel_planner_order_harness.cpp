// tests/panel_planner_order_harness.cpp
//
// Contract + determinism gate for guided::PanelPlanner (the pure, deterministic
// guided-reading planner). Two modes:
//   (default)  load the geometry fixtures, run each case through plan(), and
//              assert its expected outcome / panel order / fallback code; plus a
//              couple of rebuild() override checks.
//   --digest   SHA-256 over the concatenated serializePath(plan(case)) for every
//              fixture case in file order -> a byte-stable digest that MUST be
//              identical on every run (same inputs -> byte-identical output).
#include "guided/GuidedTypes.h"
#include "guided/PanelPlanner.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QVector>

#include <cstdio>
#include <cstring>

using namespace guided;

#define CHECK(x, m) do { if (!(x)) { std::fprintf(stderr, "FAIL: %s\n", m); return 1; } } while (0)

namespace {

struct GeometryCase {
    QString name;
    CanvasSpec canvas;
    QVector<Detection> dets;
    bool expectFallback = false;
    QString fallbackCode;   // when expectFallback
    QVector<int> order;     // when trusted: expected Panel-step sourcePanelIds
};

// Load tests/fixtures/guided/geometry_cases.json into cases (file order preserved).
bool loadCases(QVector<GeometryCase>& out) {
    QFile file(QStringLiteral(GUIDED_FIXTURES_DIR "/geometry_cases.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonArray cases = doc.object().value(QStringLiteral("cases")).toArray();
    for (const QJsonValue& cv : cases) {
        const QJsonObject obj = cv.toObject();
        GeometryCase c;
        c.name = obj.value(QStringLiteral("name")).toString();
        const QString dir = obj.value(QStringLiteral("direction")).toString();
        c.canvas.direction = (dir == QLatin1String("rtl"))
            ? ReadingDirection::Rtl : ReadingDirection::Ltr;
        c.canvas.fingerprint = QStringLiteral("sha256:") + c.name;

        const QJsonArray panels = obj.value(QStringLiteral("panels")).toArray();
        int id = 0;
        for (const QJsonValue& pv : panels) {
            const QJsonArray p = pv.toArray();
            Detection d;
            d.id = id++;
            d.kind = DetectionKind::Panel;
            d.box.x = p.at(0).toDouble();
            d.box.y = p.at(1).toDouble();
            d.box.width = p.at(2).toDouble();
            d.box.height = p.at(3).toDouble();
            d.confidence = p.at(4).toDouble();
            c.dets.append(d);
        }

        if (obj.contains(QStringLiteral("fallback"))) {
            c.expectFallback = true;
            c.fallbackCode = obj.value(QStringLiteral("fallback")).toString();
        } else {
            const QJsonArray ord = obj.value(QStringLiteral("order")).toArray();
            for (const QJsonValue& iv : ord)
                c.order.append(iv.toInt());
        }
        out.append(c);
    }
    return true;
}

int runDefault() {
    QVector<GeometryCase> cases;
    CHECK(loadCases(cases), "load geometry_cases.json");
    CHECK(!cases.isEmpty(), "fixture has cases");

    const PanelPlanner planner;
    bool haveLtrGrid = false;
    bool haveAmbiguous = false;
    CanvasSpec ltrGridCanvas;
    QVector<Detection> ltrGridDets;
    CanvasSpec ambiguousCanvas;
    QVector<Detection> ambiguousDets;

    for (const GeometryCase& c : cases) {
        const GuidedPath path = planner.plan(c.canvas, c.dets);

        if (c.expectFallback) {
            CHECK(path.outcome == PlanOutcome::Fallback,
                  qPrintable(QStringLiteral("expected fallback: ") + c.name));
            CHECK(toCode(path.reason) == c.fallbackCode,
                  qPrintable(QStringLiteral("fallback code: ") + c.name));
            // Exactly one step, and it is the whole-page Overview.
            CHECK(path.steps.size() == 1,
                  qPrintable(QStringLiteral("fallback single step: ") + c.name));
            CHECK(path.steps.first().kind == StepKind::Overview,
                  qPrintable(QStringLiteral("fallback overview: ") + c.name));
        } else {
            CHECK(path.outcome == PlanOutcome::Trusted,
                  qPrintable(QStringLiteral("expected trusted: ") + c.name));
            // Trusted paths begin and end with a DISTINCT Overview bookend.
            CHECK(path.steps.size() >= 2,
                  qPrintable(QStringLiteral("trusted has bookends: ") + c.name));
            CHECK(path.steps.first().kind == StepKind::Overview,
                  qPrintable(QStringLiteral("begins with overview: ") + c.name));
            CHECK(path.steps.last().kind == StepKind::Overview,
                  qPrintable(QStringLiteral("ends with overview: ") + c.name));
            // The only Overview steps are the two bookends; the middle is panels.
            QVector<int> gotOrder;
            for (const PathStep& s : path.steps)
                if (s.kind == StepKind::Panel)
                    gotOrder.append(s.sourcePanelId);
            CHECK(gotOrder.size() == path.steps.size() - 2,
                  qPrintable(QStringLiteral("only two overview bookends: ") + c.name));
            CHECK(gotOrder == c.order,
                  qPrintable(QStringLiteral("panel order: ") + c.name));
        }

        if (c.name == QLatin1String("ltr_grid")) {
            ltrGridCanvas = c.canvas;
            ltrGridDets = c.dets;
            haveLtrGrid = true;
        }
        if (c.name == QLatin1String("ambiguous_overlap")) {
            ambiguousCanvas = c.canvas;
            ambiguousDets = c.dets;
            haveAmbiguous = true;
        }
    }

    CHECK(haveLtrGrid, "fixture contains ltr_grid");
    CHECK(haveAmbiguous, "fixture contains ambiguous_overlap");

    // rebuild(): WholePage override => a single deliberate Overview step.
    const GuidedPath wholePage =
        planner.rebuild(ltrGridCanvas, ltrGridDets, OverrideKind::WholePage);
    CHECK(wholePage.steps.size() == 1, "rebuild WholePage yields a single step");

    // rebuild(): DetectedPanels forces the ordered-panel path even past the
    // ambiguity guard that plan() would have tripped -> not a fallback.
    const GuidedPath forced =
        planner.rebuild(ambiguousCanvas, ambiguousDets, OverrideKind::DetectedPanels);
    CHECK(forced.outcome != PlanOutcome::Fallback,
          "rebuild DetectedPanels forces panels past the ambiguity guard");

    std::puts("PANEL_PLANNER_ORDER_OK");
    return 0;
}

int runDigest() {
    QVector<GeometryCase> cases;
    CHECK(loadCases(cases), "load geometry_cases.json");

    const PanelPlanner planner;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const GeometryCase& c : cases) {
        const GuidedPath path = planner.plan(c.canvas, c.dets);
        hash.addData(serializePath(path));
    }
    const QByteArray hex = hash.result().toHex();   // lowercase hex
    std::printf("%s\n", hex.constData());
    std::puts("PANEL_PLANNER_ORDER_OK");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--digest") == 0)
        return runDigest();
    return runDefault();
}
