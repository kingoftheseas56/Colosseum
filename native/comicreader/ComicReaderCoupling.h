// native/comicreader/ComicReaderCoupling.h
//
// Auto-coupling probe for the Comic Reader (Agent 1, plan 2026-07-23, Task 5).
// Pure scoring + decision: given a pair of already-decoded page images, score
// how visually continuous their touching inner edges are; given the per-pair
// costs measured under each CouplingPhase (Task 2's buildUnits), decide which
// phase to adopt by comparing their MEANS. No Qt GUI, no decode, no file I/O,
// no global state — depends only on QImage (Gui) for pixel sampling and
// Task 1's CouplingPhase enum.
//
// Behavioral reference: TankobanQTGroundWork app_qt/ui/readers/comic_reader.py
// `_edge_continuity_cost` (edge sampling + luminance cost) and
// `_choose_auto_coupling_phase` (confidence + floor/tie decision) — ported to
// clean C++, not copied. The reference's `_score_auto_coupling_phase` returns
// the MEAN of each phase's per-pair costs, and `_choose_auto_coupling_phase`
// compares those two scalar means — NOT sums. Mean is the faithful aggregation
// boundary here because the two phases' sample sets routinely have DIFFERENT
// lengths (`_auto_phase_sample_indexes` derives indices from each phase's own
// pairing units), so summing would disagree with the lineage whenever the
// vectors are unequal length. The backend (Task 7) decodes candidate pairs
// under both phases, collects edgeContinuityCost per pair into
// normalCosts/shiftedCosts (NOT pre-aggregated), and calls chooseCouplingPhase
// once per entry-open.
#pragma once

#include "comicreader/ComicReaderTypes.h"

#include <QImage>
#include <QVector>

namespace comicreader {

// Mean absolute luminance difference across the touching edges of a displayed
// pair, in [0, 1]. Each image is scaled (whole image, ignoring aspect ratio,
// fast transform) to an 8-wide x 96-tall sample; the cost is the mean over the
// 96 sampled rows of |lum(left column x=7) - lum(right column x=0)| / 255,
// where lum = 0.299*R + 0.587*G + 0.114*B. 0 = seamless continuity, 1 = no
// continuity (including either image being null).
double edgeContinuityCost(const QImage& left, const QImage& right);

// confidence in [0, 1]: how clearly the two phases' mean costs separate.
struct CouplingVerdict {
    CouplingPhase phase;
    double confidence = 0.0;
};

// Given the per-sample-pair costs measured under each phase (one entry per
// probed pair, NOT pre-aggregated), takes the MEAN of each vector and chooses
// the phase. Adopts Shifted only when its mean cost is clearly lower than
// Normal's AND the resulting confidence clears the 0.12 floor; otherwise (or
// on a tie) stays on Normal. If EITHER vector is empty (a phase had no
// decodable samples), returns {Normal, 0.0} without deciding — mirroring the
// reference's `if normal_samples<=0 or shifted_samples<=0` bail-to-retry,
// which never picks a phase from a one-sided probe.
CouplingVerdict chooseCouplingPhase(const QVector<double>& normalCosts,
                                     const QVector<double>& shiftedCosts);

} // namespace comicreader
