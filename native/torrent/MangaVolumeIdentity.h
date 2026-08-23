#pragma once

// Arc 18 M1 — the ONE shared volume-identity grammar (contract:
// Preflight-Architect arcs/18-manga-torrentio-volume-sync, VOLUME-IDENTITY-AND-
// INDEX-CONTRACT §1-2). Before this, release-title parsing lived in
// MangaNyaaSource.cpp and file/path parsing in MangaVolumeFilePicker.cpp as two
// private integer-centric regex grammars that could drift; both now call here.
//
// Rules that are law, not style:
//   * Numeric volume identity is a CANONICAL DECIMAL STRING ("1", "1.5", "10.5").
//     Equality/ordering never round-trips through int or double — the old picker
//     could not even isolate volume "10.5" from a file that named it correctly.
//   * A bare number is NEVER volume evidence; an explicit v / vol / vol. /
//     volume / volumes marker is required ("Chapter 2" stays a chapter).
//   * Named/special labels stay textual and fail closed: they match only by
//     exact canonical text equality, never by coercion or guessing.
//   * Ranges are inclusive and may repeat the marker on the second bound
//     ("v01-v12", "Vol 1 - Vol 12", "Volumes 1-3").

#include <QString>

namespace MangaVolumeIdentity {

enum class LabelKind { Numeric, Named, Unknown };
enum class CoverageKind { None, Single, Range };

// Where a coverage claim came from. Filename evidence outranks Directory which
// outranks a ReleaseTitle claim (the last is discovery evidence only — it can
// never prove an isolable file by itself).
enum class EvidenceSource { None, Filename, Directory, ReleaseTitle };

struct VolumeLabel {
    LabelKind kind = LabelKind::Unknown;
    QString canonical; // Numeric: canonical decimal string. Named: folded text.

    bool isNumeric() const { return kind == LabelKind::Numeric; }
    bool isNamed() const { return kind == LabelKind::Named; }
};

struct VolumeCoverage {
    CoverageKind kind = CoverageKind::None;
    VolumeLabel lo;
    VolumeLabel hi;
    EvidenceSource source = EvidenceSource::None;

    bool has() const { return kind != CoverageKind::None; }
    bool isSingle() const { return kind == CoverageKind::Single; }
    bool isRange() const { return kind == CoverageKind::Range; }
};

// Normalize a numeric token to its canonical decimal-string form:
// "01" -> "1", "001.5" -> "1.5", "10.50" -> "10.5", "0.5" -> "0.5".
// Returns an empty string when the token is not unambiguously numeric.
QString canonicalizeNumber(const QString& token);

// True when the token is unambiguously numeric (digits with optional single
// fractional part).
bool isNumericToken(const QString& token);

// Fold a named label to its comparison form (trimmed, case-folded, simplified).
QString foldNamed(const QString& token);

// Build a label from a raw token: Numeric when numeric, Named when a non-empty
// word, Unknown otherwise.
VolumeLabel makeLabel(const QString& token);

// Exact decimal-string comparison for canonical numeric strings: -1 / 0 / 1.
// Returns 0 (equal) when either side is not numeric — callers must check
// isNumericToken first when that matters.
int numericCompare(const QString& a, const QString& b);

// String-safe equality: numeric tokens compare by canonical value ("1.50" ==
// "1.5"); non-numeric tokens compare by folded text.
bool labelsEqual(const QString& a, const QString& b);

// Parse explicit-marker volume coverage from one piece of text (a release
// title, a file base name, or a directory segment). `source` is stamped on the
// result. Bare numbers produce no coverage. Range wins over single.
VolumeCoverage detectCoverage(const QString& text, EvidenceSource source);

// Resolve a torrent file path's coverage: the base FILE NAME wins; only when it
// carries no explicit marker does the deepest parent-directory segment count.
// Backslashes are treated as separators (libtorrent emits them on Windows).
// Returns kind None when neither filename nor directory names a volume.
VolumeCoverage coverageForPath(const QString& filePath);

// Does the coverage honestly contain the canonical target? Single: exact label
// equality (numeric by value, named by folded text). Range: numeric-only
// inclusive bounds. None / mismatched kinds: false, always fail closed.
bool coversTarget(const VolumeCoverage& coverage, const QString& target);

} // namespace MangaVolumeIdentity
