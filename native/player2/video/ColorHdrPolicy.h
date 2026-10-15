#pragma once

#include <QtCore/QString>

namespace Colosseum::Player2 {

// Player 2's colour and HDR decision, kept as a pure function so it is testable without a GPU and
// so the honest support level is stated in one place. The engine's VideoProcessor output is 8-bit
// RGBA (SDR); there is NO true HDR passthrough. HDR sources (PQ/HLG transfer or BT.2020 primaries)
// are tone-mapped down to SDR rather than silently mishandled, and that fact is reported.
//
// Inputs are FFmpeg AVCOL_* integer values, passed as ints so this header needs no FFmpeg include.
// The relevant constants (documented, not redefined): AVCOL_SPC_BT709=1, UNSPECIFIED=2,
// BT2020_NCL=9, BT2020_CL=10; AVCOL_RANGE_JPEG=2 (full) else studio; AVCOL_TRC_SMPTE2084=16 (PQ),
// ARIB_STD_B67=18 (HLG); AVCOL_PRI_BT2020=9.

enum class ColorMatrix { Bt601, Bt709, Bt2020 };
enum class ColorRange { Studio, Full };

// How the engine treats the source's dynamic range. Passthrough is intentionally absent — the
// engine cannot output HDR yet, so it is never a supported outcome.
enum class HdrHandling { Sdr, TonemapToSdr };

struct ColorConversion
{
    ColorMatrix matrix = ColorMatrix::Bt709;
    ColorRange range = ColorRange::Studio;
    bool hdrSource = false;                 // the source declares an HDR transfer or BT.2020 primaries
    HdrHandling handling = HdrHandling::Sdr; // TonemapToSdr when hdrSource, else Sdr
    bool untaggedHdFallback = false;         // matrix inferred from height because colorspace was unset

    // D3D11 legacy VideoProcessor values. The legacy colour-space struct only expresses BT.601 (0)
    // and BT.709 (1); BT.2020 is approximated as BT.709 (documented in the HDR policy).
    int inputYCbCrMatrix() const { return matrix == ColorMatrix::Bt601 ? 0 : 1; }
    int inputNominalRange() const { return range == ColorRange::Full ? 2 : 1; }

    QString describe() const;
};

// Resolve the conversion from a decoded frame's colour metadata. `height` drives the untagged-HD
// fallback (unset colourspace + height >= 720 => BT.709), preserving the prototype/production rule.
ColorConversion resolveColorConversion(int colorspace, int colorRange, int height, int transfer,
                                       int primaries, int bitDepth);

} // namespace Colosseum::Player2
