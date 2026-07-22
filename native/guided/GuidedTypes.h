// native/guided/GuidedTypes.h
//
// Typed boundary for the Panel-Aware Guided Comic Reader (Agent 1).
// Pure value types + strict JSON (de)serialization for the immutable GuidedPath
// that Panel Step and Auto Read both consume. No Qt GUI, no ONNX — this is the
// foundation every guided/ unit builds on.
#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace guided {

enum class ReadingDirection { Ltr, Rtl };
enum class CanvasKind { SinglePage, Spread };
enum class DetectionKind { Panel, Text };
enum class StepKind { Overview, Panel, InternalStop };
enum class PlanOutcome { Trusted, Fallback, Failed };
enum class CanvasStage { Waiting, Decoding, Detecting, Planning, Ready, Fallback, Failed };
enum class FallbackCode { None, NoPanels, LayoutAmbiguous, SpreadUncertain,
                          ImageDecodeFailed, ModelMissing, ModelChecksumFailed,
                          InferenceFailed, StoreFailed };

// How a canvas's effective guided path is chosen when a user overrides the machine.
// Shared by the planner's rebuild() and the store's per-canvas override column.
enum class OverrideKind { None, WholePage, DetectedPanels };

struct NormalizedPoint { double x = 0; double y = 0; };

// All geometry is normalized to the combined canvas: [0,1] in both axes.
struct NormalizedRect {
    double x = 0; double y = 0; double width = 0; double height = 0;
    bool isValid() const;
    bool contains(NormalizedPoint point) const;
    NormalizedPoint center() const;
    double area() const;
};

struct Detection {
    int id = -1;
    DetectionKind kind = DetectionKind::Panel;
    NormalizedRect box;
    double confidence = 0;
};

struct CanvasSpec {
    QString entryId;
    int canvasIndex = 0;
    CanvasKind kind = CanvasKind::SinglePage;
    QStringList localFiles;
    QVector<int> sourcePageIndices;
    QString fingerprint;
    QSize sourceSize;
    ReadingDirection direction = ReadingDirection::Rtl;
};

struct PathStep {
    StepKind kind = StepKind::Overview;
    int sourcePanelId = -1;
    NormalizedRect camera;
    double holdSecondsAt1x = 0;
    double transitionSecondsAt1x = 0;
    double plannerConfidence = 0;
};

struct GuidedPath {
    QString canvasFingerprint;
    QString modelVersion;
    QString plannerVersion;
    PlanOutcome outcome = PlanOutcome::Failed;
    FallbackCode reason = FallbackCode::None;
    QVector<PathStep> steps;
};

// Compact JSON. deserializePath returns nullopt on malformed input.
QByteArray serializePath(const GuidedPath& path);
std::optional<GuidedPath> deserializePath(const QByteArray& bytes);

// Stable snake_case wire code, e.g. FallbackCode::LayoutAmbiguous -> "layout_ambiguous".
QString toCode(FallbackCode code);

} // namespace guided
