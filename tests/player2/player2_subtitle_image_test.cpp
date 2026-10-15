// Pins the byte order of the PGS/bitmap subtitle image conversion. The pipeline packs each pixel as a
// native-endian 0xAARRGGBB quint32 (FFmpeg RGB32 palette), so the QImage must be ARGB32 — a channel
// swap would be invisible on white subtitles (the common case) but wrong on any coloured cue, which is
// exactly why eyes-on can't be trusted here and this test exists. Also proves the image is deep-copied
// (the source buffer is transient) so it stays valid after the cue's bytes are freed.
#include "player2/core/Player2Session.h"

#include <QtGui/QImage>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace Colosseum::Player2;

int main()
{
    QByteArray rgba;
    rgba.resize(2 * 1 * 4);
    auto *px = reinterpret_cast<quint32 *>(rgba.data());
    px[0] = 0xFFFF0000u; // opaque red   (A=FF R=FF G=00 B=00)
    px[1] = 0xFF0000FFu; // opaque blue  (A=FF R=00 G=00 B=FF)

    try {
        QImage img = subtitleImageFromRgba(rgba, 2, 1);
        if (img.width() != 2 || img.height() != 1)
            throw std::runtime_error("image dimensions wrong");
        if (img.pixel(0, 0) != 0xFFFF0000u)
            throw std::runtime_error("pixel 0 is not red — RGBA byte order is wrong");
        if (img.pixel(1, 0) != 0xFF0000FFu)
            throw std::runtime_error("pixel 1 is not blue — RGBA byte order is wrong");
        // The image must own its pixels: mutating the source after conversion must not change it.
        px[0] = 0xFF00FF00u;
        if (img.pixel(0, 0) != 0xFFFF0000u)
            throw std::runtime_error("image was not deep-copied from the transient cue buffer");
        // A degenerate cue converts to a null image, never a crash.
        if (!subtitleImageFromRgba(QByteArray(), 0, 0).isNull())
            throw std::runtime_error("an empty cue must yield a null image");
    } catch (const std::exception &error) {
        std::cerr << "player2_subtitle_image_test: FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "player2_subtitle_image_test: PASS\n";
    return EXIT_SUCCESS;
}
