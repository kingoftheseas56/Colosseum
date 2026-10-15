#include "ColorHdrPolicy.h"

namespace Colosseum::Player2 {
namespace {

// AVCOL_* values we branch on, named locally so this stays FFmpeg-header-free.
constexpr int kSpcBt709 = 1;
constexpr int kSpcUnspecified = 2;
constexpr int kSpcBt2020Ncl = 9;
constexpr int kSpcBt2020Cl = 10;
constexpr int kRangeJpeg = 2;   // full range
constexpr int kTrcSmpte2084 = 16; // PQ
constexpr int kTrcAribB67 = 18;   // HLG
constexpr int kPriBt2020 = 9;

} // namespace

ColorConversion resolveColorConversion(int colorspace, int colorRange, int height, int transfer,
                                       int primaries, int bitDepth)
{
    ColorConversion result;

    // Matrix, with the untagged-HD fallback preserved from the prototype (shared_bridge / production).
    if (colorspace == kSpcBt2020Ncl || colorspace == kSpcBt2020Cl) {
        result.matrix = ColorMatrix::Bt2020;
    } else if (colorspace == kSpcBt709) {
        result.matrix = ColorMatrix::Bt709;
    } else if (colorspace == kSpcUnspecified && height >= 720) {
        result.matrix = ColorMatrix::Bt709;
        result.untaggedHdFallback = true;
    } else {
        result.matrix = ColorMatrix::Bt601;
    }

    result.range = colorRange == kRangeJpeg ? ColorRange::Full : ColorRange::Studio;

    // HDR is signalled by a PQ/HLG transfer function or BT.2020 primaries. The engine has no HDR
    // output path, so any HDR source is tone-mapped to SDR (identified, never a silent passthrough).
    result.hdrSource = transfer == kTrcSmpte2084 || transfer == kTrcAribB67 ||
                       primaries == kPriBt2020 || result.matrix == ColorMatrix::Bt2020;
    result.handling = result.hdrSource ? HdrHandling::TonemapToSdr : HdrHandling::Sdr;

    (void)bitDepth; // 10-bit (P010) is down-converted to 8-bit RGBA by the VideoProcessor; no branch.
    return result;
}

QString ColorConversion::describe() const
{
    QString matrixName;
    switch (matrix) {
    case ColorMatrix::Bt601: matrixName = QStringLiteral("BT.601"); break;
    case ColorMatrix::Bt709: matrixName = QStringLiteral("BT.709"); break;
    case ColorMatrix::Bt2020: matrixName = QStringLiteral("BT.2020"); break;
    }
    const QString rangeName = range == ColorRange::Full ? QStringLiteral("full")
                                                        : QStringLiteral("studio");
    QString out = QStringLiteral("%1 / %2 range").arg(matrixName, rangeName);
    if (hdrSource)
        out += QStringLiteral(" / HDR tone-mapped to SDR");
    if (untaggedHdFallback)
        out += QStringLiteral(" (untagged-HD fallback)");
    return out;
}

} // namespace Colosseum::Player2
