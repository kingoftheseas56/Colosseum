// native/comicreader/ComicReaderCoupling.cpp
#include "comicreader/ComicReaderCoupling.h"

#include <QColor>
#include <Qt>
#include <algorithm>
#include <cmath>

namespace comicreader {

namespace {
constexpr int kSampleW = 8;
constexpr int kSampleH = 96;
constexpr double kMinConfidence = 0.12;

double luminance(const QColor& c) {
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
}
} // namespace

double edgeContinuityCost(const QImage& left, const QImage& right) {
    if (left.isNull() || right.isNull())
        return 1.0;

    const QImage li = left.scaled(kSampleW, kSampleH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    const QImage ri = right.scaled(kSampleW, kSampleH, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (li.isNull() || ri.isNull())
        return 1.0;

    double acc = 0.0;
    int cnt = 0;
    for (int y = 0; y < kSampleH; ++y) {
        const double ll = luminance(li.pixelColor(kSampleW - 1, y));
        const double rl = luminance(ri.pixelColor(0, y));
        acc += std::abs(ll - rl) / 255.0;
        ++cnt;
    }
    if (cnt <= 0)
        return 1.0;
    return acc / static_cast<double>(cnt);
}

CouplingVerdict chooseCouplingPhase(const QVector<double>& normalCosts,
                                     const QVector<double>& shiftedCosts) {
    // A one-sided (or empty/empty) probe never decides — mirrors the
    // reference's normal_samples<=0 / shifted_samples<=0 bail-to-Normal-retry.
    if (normalCosts.isEmpty() || shiftedCosts.isEmpty())
        return {CouplingPhase::Normal, 0.0};

    double nSum = 0.0;
    for (double c : normalCosts)
        nSum += std::max(0.0, c);
    double sSum = 0.0;
    for (double c : shiftedCosts)
        sSum += std::max(0.0, c);

    const double nMean = nSum / static_cast<double>(normalCosts.size());
    const double sMean = sSum / static_cast<double>(shiftedCosts.size());

    const double base = std::max(0.001, nMean + sMean);
    const double confidence = std::clamp(std::abs(nMean - sMean) / base, 0.0, 1.0);

    if (sMean < nMean && confidence >= kMinConfidence)
        return {CouplingPhase::Shifted, confidence};
    return {CouplingPhase::Normal, confidence};
}

} // namespace comicreader
