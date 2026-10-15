// native/comicreader/ComicReaderRenderProfile.cpp
#include "comicreader/ComicReaderRenderProfile.h"

#include <QMutexLocker>
#include <QRect>
#include <QTransform>
#include <QVariant>

#include <cmath>

namespace comicreader {
namespace {

// ── normalisation helpers ────────────────────────────────────────────────────
// Each one takes the CURRENT default and returns it untouched when the map has
// nothing usable to say. "Absent" and "garbage" deliberately land in the same
// place: a settings blob written by a future version must degrade to today's
// defaults, never to zero (gamma 0 is a black page, and a black page is a bug
// report about the reader, not about the blob).

int clampedInt(const QVariantMap& raw, const char* key, int fallback, int lo, int hi) {
    const auto it = raw.constFind(QLatin1String(key));
    if (it == raw.constEnd())
        return fallback;
    bool ok = false;
    const int value = it.value().toInt(&ok);
    if (!ok)
        return fallback;
    return qBound(lo, value, hi);
}

bool boolAt(const QVariantMap& raw, const char* key, bool fallback) {
    const auto it = raw.constFind(QLatin1String(key));
    if (it == raw.constEnd())
        return fallback;
    // QVariant::toBool is total and does the sane thing for the shapes JSON can
    // produce (bool, 0/1, "true"/"false" via QVariant's own conversion).
    return it.value().toBool();
}

// 0 / 90 / 180 / 270 and nothing else. Anything off-grid snaps to the NEAREST
// quarter turn rather than being rejected: rejecting would silently un-rotate a
// book the reader had deliberately turned, and every real value round-trips
// exactly. Negative and >360 both fold, so -90 is 270 and 450 is 90.
int snappedRotation(const QVariantMap& raw, int fallback) {
    const auto it = raw.constFind(QLatin1String("rotation"));
    if (it == raw.constEnd())
        return fallback;
    bool ok = false;
    const double value = it.value().toDouble(&ok);
    if (!ok)
        return fallback;
    const int quarters = ((static_cast<int>(std::lround(value / 90.0)) % 4) + 4) % 4;
    return quarters * 90;
}

// ── the tone curve ───────────────────────────────────────────────────────────
// ONE 256-entry lookup, applied identically to R, G and B (alpha is carried
// through untouched). The order is gamma -> contrast -> brightness, and it is
// documented rather than incidental: gamma is the shape of the curve, contrast
// stretches it about mid-grey, brightness slides the result.
//
//   gamma      out = in ^ (100 / gamma)   -> 100 is the identity, higher is brighter
//   contrast   out = (in - 0.5) * 2^(c/100) + 0.5  -> +-100 is a 2x / 0.5x slope
//   brightness out = in + b/200           -> +-100 is +-0.5 of full scale
void buildToneLut(const RenderProfile& profile, quint8 lut[256]) {
    const double exponent = 100.0 / static_cast<double>(qBound(10, profile.gamma, 300));
    const double slope = std::pow(2.0, qBound(-100, profile.contrast, 100) / 100.0);
    const double offset = qBound(-100, profile.brightness, 100) / 200.0;
    const bool doGamma = profile.gamma != 100;
    const bool doContrast = profile.contrast != 0;

    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0;
        if (doGamma)
            v = std::pow(v, exponent);
        if (doContrast)
            v = (v - 0.5) * slope + 0.5;
        v += offset;
        lut[i] = static_cast<quint8>(qBound(0, static_cast<int>(std::lround(v * 255.0)), 255));
    }
}

QImage applyTone(const QImage& source, const RenderProfile& profile) {
    if (source.isNull() || !profile.changesTone())
        return source;

    quint8 lut[256];
    buildToneLut(profile, lut);

    // NON-premultiplied on purpose: a premultiplied buffer stores colour already
    // scaled by alpha, so a LUT over those bytes would brighten transparent
    // pixels differently from opaque ones. Comic pages are opaque in practice —
    // this is about never being wrong on the page that is not.
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    if (out.isNull())
        return source;

    const int h = out.height();
    const int w = out.width();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)], lut[qBlue(p)], qAlpha(p));
        }
    }
    return out;
}

// ── auto-crop ────────────────────────────────────────────────────────────────
// The content box of a scan, found on a THUMBNAIL of the page rather than the
// page itself. A 2400x3600 scan is 8.6M pixels; the probe below is at most
// 256 wide (~98k), which is two orders of magnitude less work for a decision
// that only needs to be accurate to a few source pixels — and this runs on the
// provider's worker pool for every scaled-tier miss, so the difference is the
// difference between a usable setting and a stutter.
//
// Fails CLOSED, in both directions: a page with no detectable margin, a page
// that is almost entirely margin (a blank or near-blank leaf), or anything too
// small to probe returns a null rect, and the caller leaves the image alone.
// Cropping a blank page to a speck of dust would be a garbage image, which is
// exactly what the validation contract exists to prevent.
QRect autoCropRect(const QImage& source) {
    if (source.width() < 16 || source.height() < 16)
        return QRect();

    const int probeW = qMin(256, source.width());
    const QImage probe = source.scaledToWidth(probeW, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_Grayscale8);
    if (probe.isNull() || probe.width() < 4 || probe.height() < 4)
        return QRect();

    const int pw = probe.width();
    const int ph = probe.height();

    // The margin's own colour, taken from the four corners. A mean (not a single
    // corner) so one speck of dither in a corner cannot define the page.
    const int bg = (int(probe.constScanLine(0)[0]) + int(probe.constScanLine(0)[pw - 1])
                    + int(probe.constScanLine(ph - 1)[0]) + int(probe.constScanLine(ph - 1)[pw - 1]))
                   / 4;
    constexpr int kThreshold = 24;   // ~9% of full scale: past scanner noise, under real ink

    int minX = pw;
    int minY = ph;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < ph; ++y) {
        const uchar* line = probe.constScanLine(y);
        for (int x = 0; x < pw; ++x) {
            if (qAbs(int(line[x]) - bg) <= kThreshold)
                continue;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
    }
    if (maxX < 0)
        return QRect();   // uniform page: nothing to crop TO

    // One probe pixel of slack either side, so the trim never bites into ink.
    minX = qMax(0, minX - 1);
    minY = qMax(0, minY - 1);
    maxX = qMin(pw - 1, maxX + 1);
    maxY = qMin(ph - 1, maxY + 1);

    const double sx = double(source.width()) / double(pw);
    const double sy = double(source.height()) / double(ph);
    QRect box(int(std::floor(minX * sx)), int(std::floor(minY * sy)),
              int(std::ceil((maxX - minX + 1) * sx)), int(std::ceil((maxY - minY + 1) * sy)));
    box = box.intersected(QRect(0, 0, source.width(), source.height()));
    if (box.isEmpty())
        return QRect();

    const double sourceArea = double(source.width()) * double(source.height());
    const double boxArea = double(box.width()) * double(box.height());
    // Nothing worth doing (a full-bleed page): leave it alone rather than spend a
    // copy and a fresh scaled-cache entry on a 1-pixel trim.
    if (boxArea >= sourceArea * 0.98)
        return QRect();
    // Almost nothing left: this is a blank leaf with a smudge on it, not a page
    // with margins.
    if (boxArea <= sourceArea * 0.05)
        return QRect();
    return box;
}

QImage applyGeometry(const QImage& source, const RenderProfile& profile) {
    if (source.isNull() || !profile.changesGeometry())
        return source;

    QImage out = source;
    if (profile.autoCrop) {
        const QRect box = autoCropRect(out);
        if (!box.isNull())
            out = out.copy(box);
    }
    if (profile.rotation != 0) {
        // Right angles only, so this is an exact block move — no interpolation,
        // no quality question, and the same for every quality setting.
        out = out.transformed(QTransform().rotate(profile.rotation));
    }
    return out;
}

} // namespace

RenderQuality renderQualityFromString(QStringView value) {
    if (value.compare(QLatin1String("fast"), Qt::CaseInsensitive) == 0)
        return RenderQuality::Fast;
    if (value.compare(QLatin1String("best"), Qt::CaseInsensitive) == 0)
        return RenderQuality::Best;
    return RenderQuality::Balanced;
}

QString renderQualityToString(RenderQuality quality) {
    switch (quality) {
    case RenderQuality::Fast: return QStringLiteral("fast");
    case RenderQuality::Best: return QStringLiteral("best");
    case RenderQuality::Balanced: break;
    }
    return QStringLiteral("balanced");
}

RenderProfile normalizeRenderProfile(const QVariantMap& raw) {
    RenderProfile out;   // the defaults ARE the fallbacks
    out.brightness = clampedInt(raw, "brightness", out.brightness, -100, 100);
    out.contrast = clampedInt(raw, "contrast", out.contrast, -100, 100);
    out.gamma = clampedInt(raw, "gamma", out.gamma, 10, 300);
    out.rotation = snappedRotation(raw, out.rotation);
    out.autoCrop = boolAt(raw, "autoCrop", out.autoCrop);
    out.nightFilter = boolAt(raw, "nightFilter", out.nightFilter);
    const auto quality = raw.constFind(QLatin1String("quality"));
    if (quality != raw.constEnd())
        out.quality = renderQualityFromString(quality.value().toString());
    return out;
}

QVariantMap renderProfileToVariantMap(const RenderProfile& profile) {
    return {
        {QStringLiteral("brightness"), profile.brightness},
        {QStringLiteral("contrast"), profile.contrast},
        {QStringLiteral("gamma"), profile.gamma},
        {QStringLiteral("rotation"), profile.rotation},
        {QStringLiteral("autoCrop"), profile.autoCrop},
        {QStringLiteral("nightFilter"), profile.nightFilter},
        {QStringLiteral("quality"), renderQualityToString(profile.quality)},
    };
}

QImage applyRenderProfile(const QImage& source, const RenderProfile& profile, RenderStage stage) {
    // THE identity fast path. Returning `source` itself (not a copy, not a
    // converted copy) is what makes a default profile byte-stable AND free — the
    // delivery worker's untouched path depends on it.
    if (source.isNull() || profile.isIdentity())
        return source;

    QImage out = source;
    if (stage != RenderStage::Tone)
        out = applyGeometry(out, profile);
    if (stage != RenderStage::Geometry)
        out = applyTone(out, profile);
    return out;
}

// ── RenderProfileStore ───────────────────────────────────────────────────────

RenderProfile RenderProfileStore::load(quint64* revisionOut) const {
    QMutexLocker lock(&m_mutex);
    if (revisionOut)
        *revisionOut = m_revision.load(std::memory_order_relaxed);
    return m_profile;
}

RenderProfileStore::Change RenderProfileStore::store(const RenderProfile& next) {
    QMutexLocker lock(&m_mutex);
    if (m_profile == next)
        return Change::None;
    const bool pixels = !m_profile.samePixelsAs(next);
    m_profile = next;
    if (!pixels)
        return Change::NonPixel;
    // Monotonic, and written under the same lock as the profile so a worker's
    // load() can never pair new pixels with an old identity.
    m_revision.fetch_add(1, std::memory_order_release);
    return Change::Pixel;
}

} // namespace comicreader
