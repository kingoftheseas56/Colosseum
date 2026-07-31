// native/comicreader/ComicReaderRenderProfile.h
//
// THE IMAGE ADJUSTMENTS, as a pure function (Agent 1, overhaul plan 2026-07-28,
// Task 7). Plainly: this is "what the reader did to the picture" — a small
// validated struct, and one function that turns a decoded page into the page you
// actually see. Nothing here knows about caches, threads, QML or the reader's
// state; ComicReaderImageResponse calls it on the provider's worker pool and
// ComicReaderCore owns the one live copy.
//
// ── The contract, and why every field is clamped ─────────────────────────────
// The profile is PERSISTED (per series, through the shell's series record), so
// the bytes that reach normalizeRenderProfile() may have been written by a
// future version, hand-edited, or corrupted. Every value is therefore clamped or
// snapped on the way IN, exactly once, at this boundary:
//
//   brightness   -100..100      clamped
//   contrast     -100..100      clamped
//   gamma          10..300      clamped   (hundredths; 100 = 1.0 = untouched)
//   rotation    0/90/180/270    snapped to the NEAREST quarter turn, mod 360
//   autoCrop       bool
//   nightFilter    bool         (see the note below — NOT a pixel operation)
//   quality      fast|balanced|best   unknown -> balanced
//
// A missing key keeps the DEFAULT, and a present-but-unparseable value keeps the
// default too — never 0, which for gamma would mean a black page. That
// distinction is the whole reason this is a function and not a QVariantMap the
// rest of the code reads directly.
//
// ── Identity is byte-stable, and that is load bearing ────────────────────────
// applyRenderProfile(image, RenderProfile{}) returns THE SAME IMAGE — not a
// visually identical copy, the same implicitly-shared object. Everything
// downstream assumes a no-op profile costs nothing: the delivery worker's
// "no scale to do, the source IS the answer" path stays byte-for-byte what it
// was before this file existed, and a reader who never opens the Image panel
// pays not one instruction for it.
//
// ── nightFilter is deliberately NOT applied here ─────────────────────────────
// It rides in the profile because that is where the panel's controls are
// persisted and validated, but applyRenderProfile IGNORES it and it is excluded
// from samePixelsAs(). The reader's night filter is a composited veil over the
// surfaces (ComicReaderShell's nightVeil rectangle, opacity from
// ComicReaderState.nightVeilOpacity) — a control you toggle WHILE looking at a
// page, so it has to be free. Baking it into the pixels would bump the render
// revision, drop every scaled entry and re-scale the visible pages just to dim
// them, and it would leave the reader with two night controls that could
// disagree. One veil, one painter. See the shell for the wiring.
//
// ── The quality dial, and what it actually buys ──────────────────────────────
// The plan sketched `best` as "smooth scaling and the target DPR". There is no
// DPR to use: ComicReaderCore::imageUrl() never emits one, so ScaleKey::dpr100
// is 100 for every request in the app today (Task 2 left that seam inert and
// this task does not build a DPR pipeline). Rather than ship a third segment
// that is a byte-for-byte copy of the second, the three qualities differ by
// something real and free — WHEN the tonal maths runs relative to the resample:
//
//   fast      geometry -> FastTransformation downscale -> tone on the SMALL image
//   balanced  geometry -> SmoothTransformation downscale -> tone on the small image
//   best      geometry -> tone on the FULL-RESOLUTION page -> SmoothTransformation
//
// Resampling and a non-linear tone curve do not commute: adjust-then-resample
// and resample-then-adjust genuinely differ wherever gamma or contrast clips,
// and adjusting first is the more correct order. It is also the expensive one —
// a 2400x3600 page is ~8.6M pixels of LUT work against ~1.5M for the scaled
// copy — so the names describe the real cost. With a default profile all three
// collapse to today's behaviour and `fast` is the only one that changes a pixel
// (its resampler), which is exactly what a quality dial should do.
//
// RenderStage is what lets the delivery worker split that pipeline without a
// second entry point: Geometry is the half that changes DIMENSIONS (and so must
// always run before a width-driven scale), Tone is the half that only changes
// values.
#pragma once

#include <QImage>
#include <QMutex>
#include <QString>
#include <QStringView>
#include <QVariantMap>

#include <atomic>

#include <QtGlobal>

namespace comicreader {

// How hard the delivery worker should work for one page. Carried in the profile
// rather than the url because it is a reader PREFERENCE, not a per-request shape
// (the tier query is the per-request shape).
enum class RenderQuality { Fast, Balanced, Best };

// The wire spellings, owned in one place so the persisted record and the code
// can never drift. Anything unrecognised — including empty — is Balanced.
RenderQuality renderQualityFromString(QStringView value);
QString renderQualityToString(RenderQuality quality);

struct RenderProfile {
    int  brightness  = 0;      // -100..100, additive; 0 = untouched
    int  contrast    = 0;      // -100..100, multiplicative about mid-grey; 0 = untouched
    int  gamma       = 100;    // 10..300 hundredths; 100 = 1.0 = untouched, higher = brighter
    int  rotation    = 0;      // 0 | 90 | 180 | 270, clockwise
    bool autoCrop    = false;  // trim uniform scan margins
    bool nightFilter = false;  // the shell's veil — NOT applied here, see the header note
    RenderQuality quality = RenderQuality::Balanced;

    // Does this profile change the image's DIMENSIONS? Geometry must run before a
    // width-driven scale, because the target width names the FINAL image.
    bool changesGeometry() const { return rotation != 0 || autoCrop; }
    // ...and does it change pixel VALUES?
    bool changesTone() const { return brightness != 0 || contrast != 0 || gamma != 100; }
    // The no-op. applyRenderProfile short-circuits on this, which is what makes
    // identity byte-stable rather than merely correct.
    bool isIdentity() const { return !changesGeometry() && !changesTone(); }

    // Every field that can change a delivered pixel — quality included (it picks
    // the resampler and the pipeline order), nightFilter deliberately excluded.
    // THIS is what decides whether the render revision moves, so it is also what
    // decides whether the scaled tier is thrown away.
    bool samePixelsAs(const RenderProfile& other) const {
        return brightness == other.brightness && contrast == other.contrast
               && gamma == other.gamma && rotation == other.rotation
               && autoCrop == other.autoCrop && quality == other.quality;
    }

    bool operator==(const RenderProfile& other) const {
        return samePixelsAs(other) && nightFilter == other.nightFilter;
    }
    bool operator!=(const RenderProfile& other) const { return !(*this == other); }
};

// THE boundary. Total: any map at all yields a valid profile, and an absent or
// unparseable key keeps that field's default rather than collapsing to zero.
RenderProfile normalizeRenderProfile(const QVariantMap& raw);
// The canonical map — every key present, canonical types. What renderProfile()
// hands QML and what the shell persists, so a stored record is always already
// normalised and re-reading it is a fixed point.
QVariantMap renderProfileToVariantMap(const RenderProfile& profile);

// Which half of the pipeline to run. See the header note on the quality dial.
enum class RenderStage {
    Geometry,  // auto-crop + rotation: changes dimensions
    Tone,      // brightness/contrast/gamma: changes values
    All,       // both, geometry first — the plan's named entry point
};

// The one transform. Returns `source` UNCHANGED whenever the requested stage has
// nothing to do, which is what keeps the identity profile free.
QImage applyRenderProfile(const QImage& source, const RenderProfile& profile,
                          RenderStage stage = RenderStage::All);

// The live profile, readable from the provider's worker threads.
//
// Why a store rather than a bare member plus the existing revision atomic: the
// worker needs the profile and the revision that profile BELONGS TO, and reading
// them separately can tear — it would compute new-profile pixels and file them
// under the old revision's key, poisoning the scaled tier with an entry whose
// pixels do not match its identity. load() takes both under one lock so that
// window does not exist.
class RenderProfileStore {
public:
    enum class Change {
        None,      // the stored profile already said exactly this
        NonPixel,  // something changed, but nothing that changes a delivered pixel
        Pixel,     // a pixel-affecting change: the revision moved
    };

    // The profile, and (optionally) the revision it belongs to, coherently.
    RenderProfile load(quint64* revisionOut = nullptr) const;

    // Lock-free reads for callers that only want the identity. The atomic is
    // handed out so DeliveryContext can keep its existing plain-revision seam
    // for harnesses that have no profile at all.
    quint64 revision() const { return m_revision.load(std::memory_order_acquire); }
    const std::atomic<quint64>* revisionAtomic() const { return &m_revision; }

    // Replace the stored profile. The revision is bumped ONLY for a
    // pixel-affecting change, so toggling the night filter never invalidates a
    // single scaled entry.
    Change store(const RenderProfile& next);

private:
    mutable QMutex m_mutex;
    RenderProfile m_profile;
    // Written under m_mutex (so load() sees a coherent pair) and read without it
    // by the workers, which only ever want the identity.
    std::atomic<quint64> m_revision{0};
};

} // namespace comicreader
