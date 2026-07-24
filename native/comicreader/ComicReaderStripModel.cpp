// native/comicreader/ComicReaderStripModel.cpp
#include "comicreader/ComicReaderStripModel.h"

#include <algorithm>

namespace comicreader {

namespace {
// House number (matches QTGW's DEFAULT_DIMENSIONS): the estimated portrait
// source size used for a page whose real dimensions have never been learned.
constexpr int kEstimateSourceWidth = 1600;
constexpr int kEstimateSourceHeight = 2400;

// Same rule as ComicReaderPairing::isSpread — a manual override always beats
// the decoder's own geometry verdict.
bool effectiveSpread(const PageMeta& meta) {
    return meta.spreadOverride.has_value() ? *meta.spreadOverride : meta.detectedSpread;
}
} // namespace

ComicReaderStripModel::ComicReaderStripModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

// Mirrors ScrollStripCanvas::targetPageWidth + rebuildYOffsets' per-page ratio
// math: spread pages take the full viewport width, portrait pages take
// portraitWidthPct% of it; height follows from the (possibly estimated)
// source aspect ratio. TB2's no-upscale clamp (never scale a page past its
// own native resolution) is deliberately NOT ported: this is long-strip
// fits-to-width layout, so a tiny source image is upscaled to fill its
// column exactly like every other page. If TB2-exact no-upscale is ever
// wanted, it must live IN this model, not upstream — the clamp needs the
// displayWidth THIS model computes (viewportWidth × frac), and shrinking or
// pre-clamping sourceSize before feeding it here cannot reproduce that: the
// ratio-based math above never reads sourceWidth to size the column, only
// to size the height, so a caller-side clamp on sourceSize is inert.
void ComicReaderStripModel::recomputeGeometry(Entry& e) const
{
    bool spread;
    QSize src;
    if (e.sizeKnown) {
        spread = e.knownSpread;
        src = e.knownSize;
    } else {
        spread = effectiveSpread(e.meta);
        src = QSize(kEstimateSourceWidth, kEstimateSourceHeight);
    }

    const double frac = spread ? 1.0 : (m_options.portraitWidthPct / 100.0);
    const double dw = m_options.viewportWidth * frac;
    const double ratio = src.width() > 0
        ? (static_cast<double>(src.height()) / static_cast<double>(src.width()))
        : 0.0;

    e.displayWidth = dw;
    e.displayHeight = dw * ratio;
}

// Mirrors ScrollStripCanvas::rebuildYOffsets' running sum, generalized with a
// caller-supplied gap (TB2's SPACING was a hardcoded 0). contentHeight omits
// the trailing gap after the last page, matching ScrollStripCanvas::
// totalHeight() (last.yOffset + last.height, not the accumulator that also
// carries one more SPACING past the end).
void ComicReaderStripModel::recomputeTops()
{
    double y = 0.0;
    const int n = m_entries.size();
    for (int i = 0; i < n; ++i) {
        m_entries[i].top = y;
        y += m_entries[i].displayHeight + m_options.gap;
    }
    m_contentHeight = (n > 0) ? (y - m_options.gap) : 0.0;
}

void ComicReaderStripModel::rebuild(const QVector<PageMeta>& pages, const Options& opt)
{
    // Precondition: pages[i].index == i for every i — see the header comment.
    // Q_ASSERT catches a violation loudly in a debug build; it compiles out
    // under NDEBUG/Release, so the loop below ALSO hard-early-outs to an
    // empty model rather than trusting a bad index to key the right row.
    bool indicesValid = true;
    for (int i = 0; i < pages.size(); ++i) {
        Q_ASSERT(pages[i].index == i);
        if (pages[i].index != i) {
            indicesValid = false;
            break;
        }
    }

    beginResetModel();
    m_options = opt;
    m_entries.clear();
    if (indicesValid) {
        m_entries.resize(pages.size());
        for (int i = 0; i < pages.size(); ++i) {
            Entry& e = m_entries[i];
            e.meta = pages[i];
            if (pages[i].decoded && pages[i].sourceSize.width() > 0 && pages[i].sourceSize.height() > 0) {
                e.sizeKnown = true;
                e.knownSize = pages[i].sourceSize;
                e.knownSpread = effectiveSpread(pages[i]);
            }
            recomputeGeometry(e);
        }
        recomputeTops();
    } else {
        m_contentHeight = 0.0;
    }

    m_pendingCompensations.clear();
    endResetModel();
}

void ComicReaderStripModel::setViewportWidth(int width)
{
    if (width <= 0 || width == m_options.viewportWidth)
        return;

    m_options.viewportWidth = width;

    // In-place rescale: same rows, new sizes. recomputeGeometry re-fits each page to
    // the new viewport width (keeping any locked-in real source size); recomputeTops
    // re-sums the column. dataChanged — NOT beginResetModel — so the bound ListView
    // keeps its delegates AND its contentY, and simply reflows.
    for (int i = 0; i < m_entries.size(); ++i)
        recomputeGeometry(m_entries[i]);
    recomputeTops();

    if (!m_entries.isEmpty())
        emit dataChanged(index(0, 0), index(m_entries.size() - 1, 0),
                         {TopRole, DisplayWidthRole, DisplayHeightRole});
}

void ComicReaderStripModel::updatePage(const PageMeta& meta)
{
    const int idx = meta.index;
    if (idx < 0 || idx >= m_entries.size())
        return;

    Entry& e = m_entries[idx];
    e.meta = meta; // ReadyRole/ErrorCodeRole/PageIndexRole always track the latest meta

    const double oldTop = e.top;
    const double oldHeight = e.displayHeight;

    // Lock in a real size the first time it arrives (or if a later decode
    // reports a genuinely different real size/spread verdict). A page that
    // reports decoded=false here — an eviction, not a fresh decode — leaves
    // an already-locked size untouched: see the class comment.
    const bool hasRealSize = meta.decoded && meta.sourceSize.width() > 0 && meta.sourceSize.height() > 0;
    if (hasRealSize) {
        const bool spreadNow = effectiveSpread(meta);
        if (!e.sizeKnown || e.knownSize != meta.sourceSize || e.knownSpread != spreadNow) {
            e.sizeKnown = true;
            e.knownSize = meta.sourceSize;
            e.knownSpread = spreadNow;
        }
    }

    recomputeGeometry(e);

    const double delta = e.displayHeight - oldHeight;
    if (delta != 0.0) {
        // Changing page idx's OWN height never moves its OWN top (a widget's
        // position depends only on everything BEFORE it) — only pages after
        // idx shift. recomputeTops() re-sums from scratch using each entry's
        // already-computed displayHeight, so this stays correct however many
        // pages have changed since the last call.
        recomputeTops();
        const int lastRow = m_entries.size() - 1;
        emit dataChanged(index(idx, 0), index(lastRow, 0),
                          {TopRole, DisplayWidthRole, DisplayHeightRole, ReadyRole, ErrorCodeRole});
    } else {
        emit dataChanged(index(idx, 0), index(idx, 0),
                          {DisplayWidthRole, ReadyRole, ErrorCodeRole});
    }

    if (delta != 0.0)
        m_pendingCompensations.append({oldTop, delta});
}

// Mirrors ScrollStripCanvas::firstVisiblePage: the least index whose band
// bottom is strictly greater than y, i.e. the first page whose [top, bottom)
// half-open band could contain y or anything after it.
int ComicReaderStripModel::firstBandStart(double y) const
{
    const int n = m_entries.size();
    if (n == 0)
        return 0;
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        const double bottom = m_entries[mid].top + m_entries[mid].displayHeight;
        if (bottom <= y)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// Mirrors ScrollStripCanvas::pagesNeedingDecode: binary-search the first page
// that could intersect the expanded band, then walk forward until a page's
// top passes the band's bottom edge.
QVector<int> ComicReaderStripModel::window(double top, double vpHeight, double marginScreens) const
{
    QVector<int> result;
    const int n = m_entries.size();
    if (n == 0)
        return result;

    const double margin = marginScreens * vpHeight;
    const double loadTop = top - margin;
    const double loadBot = top + vpHeight + margin;

    const int first = firstBandStart(std::max(0.0, loadTop));
    for (int i = first; i < n; ++i) {
        if (m_entries[i].top > loadBot)
            break;
        result.append(i);
    }
    return result;
}

// Mirrors ScrollStripCanvas::pageAtCenter: binary-search the band containing
// the viewport's vertical midpoint, clamped into range. -1 on an empty model
// — 0 would look like a real page to a careless caller.
int ComicReaderStripModel::pageAtCenter(double top, double vpHeight) const
{
    const int n = m_entries.size();
    if (n == 0)
        return -1;
    const double centerY = top + vpHeight / 2.0;
    const int idx = firstBandStart(centerY);
    return std::min(std::max(idx, 0), n - 1);
}

double ComicReaderStripModel::pageTop(int page) const
{
    if (page < 0 || page >= m_entries.size())
        return 0.0;
    return m_entries[page].top;
}

double ComicReaderStripModel::contentHeight() const
{
    return m_contentHeight;
}

// Accumulate-and-clear: mirrors QTGW comic_reader.py on_page_loaded's
// compensation gate (apply a page's delta only when its old top was strictly
// above the caller's scroll position), generalized to sum over every
// updatePage() call recorded since the last drain — a batch of several
// finished decodes above the fold all contribute, not just the last one.
double ComicReaderStripModel::takePendingCompensation(double viewportTop)
{
    double sum = 0.0;
    for (const auto& pc : m_pendingCompensations) {
        if (pc.oldTop < viewportTop)
            sum += pc.delta;
    }
    m_pendingCompensations.clear();
    return sum;
}

int ComicReaderStripModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant ComicReaderStripModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry& e = m_entries[index.row()];
    switch (role) {
    case PageIndexRole:     return e.meta.index;
    case TopRole:           return e.top;
    case DisplayWidthRole:  return e.displayWidth;
    case DisplayHeightRole: return e.displayHeight;
    case ReadyRole:         return e.meta.decoded;
    case ErrorCodeRole:     return static_cast<int>(e.meta.error);
    default:                return {};
    }
}

QHash<int, QByteArray> ComicReaderStripModel::roleNames() const
{
    return {
        {PageIndexRole,     "pageIndex"},
        {TopRole,           "top"},
        {DisplayWidthRole,  "displayWidth"},
        {DisplayHeightRole, "displayHeight"},
        {ReadyRole,         "ready"},
        {ErrorCodeRole,     "errorCode"},
    };
}

} // namespace comicreader
