#include "tools/LanistaHash.h"

namespace lanista {
// 9x8 grayscale difference hash: bit set where pixel(x) < pixel(x+1).
// Tolerant of scaling/compression noise, sensitive to layout shifts —
// exactly the regression class goldens exist to catch.
quint64 dhash(const QImage& img)
{
    if (img.isNull()) return 0;
    const QImage g = img.scaled(9, 8, Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_Grayscale8);
    quint64 h = 0;
    for (int y = 0; y < 8; ++y) {
        const uchar* line = g.constScanLine(y);
        for (int x = 0; x < 8; ++x) {
            h <<= 1;
            if (line[x] < line[x + 1]) h |= 1;
        }
    }
    return h;
}
int hamming(quint64 a, quint64 b) { return int(qPopulationCount(a ^ b)); }
}
