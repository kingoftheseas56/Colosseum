// native/comicreader/ComicReaderCore.cpp
#include "comicreader/ComicReaderCore.h"

#include "comicreader/ComicReaderDecode.h"
#include "comicreader/ComicReaderPairing.h"
#include "comicreader/ComicReaderProvider.h"
#include "comicreader/ComicReaderStripModel.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace comicreader {

namespace {
// LATEST-WINS wave priorities. Each setVisible wave sits kVisibleWaveStep above the last, and the
// offsets below rank the requests WITHIN one wave. The whole scheme is correct only while every
// offset is smaller than the step — otherwise a wave's prefetch would sink beneath the PREVIOUS
// wave's, silently undoing the newest-first ordering with no test to catch it (the ordering tests
// space their waves further apart than these offsets). Pinned rather than trusted.
constexpr int kVisibleWaveStep = 8;
constexpr int kOffNext1        = 3;    // the page after the visible run
constexpr int kOffNext2        = 4;    // one further ahead
constexpr int kOffPrevUnit     = 6;    // both halves of the previous unit
static_assert(kOffNext1 < kVisibleWaveStep && kOffNext2 < kVisibleWaveStep
                  && kOffPrevUnit < kVisibleWaveStep,
              "a within-wave offset must stay under the wave step, or a new wave's prefetch "
              "would rank below the previous wave's and latest-wins would silently break");

constexpr qint64 kBudgetNormal = 512LL * 1024 * 1024;
constexpr qint64 kBudgetSaver  = 256LL * 1024 * 1024;

// Decode priorities (higher runs sooner in QThreadPool). kPrioVisible and kPrioStripBase live
// in the header (not here) so stripDecodePriority() is directly unit-testable. The visible
// band's forward/backward prefetch priorities are no longer fixed constants: they are offsets
// below the CURRENT wave's priority, which climbs per setVisible() — see setVisible().
constexpr int kPrioProbe       = 10;   // auto-coupling probe: LOW, so visible pages win
} // namespace

int stripDecodePriority(int page, int centrePage) {
    if (centrePage < 0)
        return kPrioStripBase;
    return qMax(1, kPrioStripBase - qAbs(page - centrePage));
}

ComicReaderCore::ComicReaderCore(QObject* parent) : QObject(parent) {
    // m_cache is declared before m_decode, so &m_cache is fully constructed here.
    // The decode coordinator is a child of this object and lives on this thread —
    // satisfying its construct-and-destroy-on-owning-thread affinity invariant.
    m_decode = new ComicReaderDecode(&m_cache, this);
    m_strip = new ComicReaderStripModel(this);

    connect(m_decode, &ComicReaderDecode::metaReady, this, &ComicReaderCore::onMetaReady);
    connect(m_decode, &ComicReaderDecode::pageReady, this, &ComicReaderCore::onPageReady);
    connect(m_decode, &ComicReaderDecode::pageFailed, this, &ComicReaderCore::onPageFailed);

    m_cache.setBudget(m_cacheBudget);
}

ComicReaderCore::~ComicReaderCore() = default;

void ComicReaderCore::setDecodeWorkerHooksForTest(std::function<void(quint64, int)> onEnter,
                                                  std::function<void(quint64, int)> onExit) {
    m_decode->setWorkerHooksForTest(std::move(onEnter), std::move(onExit));
}

QString ComicReaderCore::couplingState() const {
    const QString mode = (m_couplingMode == CouplingMode::Manual)
                             ? QStringLiteral("manual") : QStringLiteral("auto");
    const QString phase = (m_couplingPhase == CouplingPhase::Shifted)
                              ? QStringLiteral("shifted") : QStringLiteral("normal");
    return mode + QLatin1Char(':') + phase + QLatin1Char(':')
           + QString::number(m_couplingConfidence, 'f', 2);
}

QAbstractListModel* ComicReaderCore::stripModel() const {
    return m_strip;
}

// ── entry lifecycle ─────────────────────────────────────────────────────────

void ComicReaderCore::resetEntryState() {
    m_entryId.clear();
    m_direction = Direction::Ltr;
    m_pages.clear();
    m_units.clear();
    m_analyzable = false;

    m_couplingMode = CouplingMode::Auto;
    m_couplingPhase = CouplingPhase::Normal;
    m_couplingResolved = false;
    m_couplingConfidence = 0.0;

    m_spreadOverrides.clear();
    m_bookmarks.clear();
    m_memorySaver = false;

    m_readyCount = 0;
    m_readyPages.clear();
    m_pageRev.clear();
    m_lastPinned.clear();
    m_visibleBoost = 0;
    m_stripViewportTop = 0.0;
    m_stripViewportHeight = 0.0;

    m_probeActive = false;
    m_probeNormalPairs.clear();
    m_probeShiftedPairs.clear();
    m_probePendingPages.clear();
    m_probeCalledChoose = false;
    m_probeNormalSamples = 0;
    m_probeShiftedSamples = 0;
}

void ComicReaderCore::parsePages(const QVariantList& pages) {
    m_pages.clear();
    m_pages.reserve(pages.size());
    for (int i = 0; i < pages.size(); ++i) {
        const QVariantMap pm = pages[i].toMap();
        PageMeta meta;
        meta.index = i;   // authoritative dense index (the strip model asserts pages[i].index==i)
        const QString url = pm.value(QStringLiteral("url")).toString();
        const QUrl u(url);
        if (u.isLocalFile()) {
            const QString local = u.toLocalFile();
            if (QFileInfo::exists(local))
                meta.localPath = local;
            else
                meta.error = PageError::MissingFile;   // downloaded-but-vanished
        } else if (!url.isEmpty() && u.scheme().isEmpty() && QFileInfo::exists(url)) {
            meta.localPath = url;   // tolerate a bare existing local path
        } else {
            meta.error = PageError::MissingFile;   // remote / non-file:// / undownloaded
        }
        m_pages.append(meta);
    }
}

void ComicReaderCore::applyPersisted(const QVariantMap& persisted) {
    if (persisted.contains(QStringLiteral("memorySaver")))
        m_memorySaver = persisted.value(QStringLiteral("memorySaver")).toBool();

    if (persisted.contains(QStringLiteral("bookmarks"))) {
        m_bookmarks.clear();
        const QVariantList bm = persisted.value(QStringLiteral("bookmarks")).toList();
        for (const QVariant& v : bm)
            m_bookmarks.append(v.toInt());
    }

    if (persisted.contains(QStringLiteral("spreadOverrides"))) {
        m_spreadOverrides.clear();
        const QVariantMap so = persisted.value(QStringLiteral("spreadOverrides")).toMap();
        for (auto it = so.constBegin(); it != so.constEnd(); ++it) {
            bool ok = false;
            const int idx = it.key().toInt(&ok);
            if (ok)
                m_spreadOverrides.insert(idx, it.value().toBool());
        }
    }

    if (persisted.contains(QStringLiteral("couplingMode"))) {
        const QString mode = persisted.value(QStringLiteral("couplingMode")).toString();
        if (mode == QLatin1String("manual")) {
            m_couplingMode = CouplingMode::Manual;
            m_couplingResolved = true;   // a manual choice is a resolved state
        } else {
            m_couplingMode = CouplingMode::Auto;
            // A persisted Auto is resolved only if the shell saved it that way;
            // an unresolved Auto (first open) re-runs the probe.
            m_couplingResolved =
                persisted.value(QStringLiteral("couplingResolved"), false).toBool();
        }
    }
    if (persisted.contains(QStringLiteral("couplingPhase")))
        m_couplingPhase =
            (persisted.value(QStringLiteral("couplingPhase")).toString() == QLatin1String("shifted"))
                ? CouplingPhase::Shifted : CouplingPhase::Normal;
    if (persisted.contains(QStringLiteral("couplingConfidence")))
        m_couplingConfidence = persisted.value(QStringLiteral("couplingConfidence")).toDouble();
}

void ComicReaderCore::openEntry(QString entryId, QVariantList pages, QString direction,
                                QVariantMap persisted) {
    resetEntryState();

    ++m_generation;
    m_liveGeneration.store(m_generation);
    m_entryId = entryId;
    m_direction = (direction.trimmed().toLower() == QLatin1String("rtl"))
                      ? Direction::Rtl : Direction::Ltr;

    parsePages(pages);
    applyPersisted(persisted);

    // Fold persisted spread overrides onto the page metas so pairing + strip see them.
    for (auto it = m_spreadOverrides.constBegin(); it != m_spreadOverrides.constEnd(); ++it) {
        const int idx = it.key();
        if (idx >= 0 && idx < m_pages.size())
            m_pages[idx].spreadOverride = it.value();
    }

    m_cacheBudget = m_memorySaver ? kBudgetSaver : kBudgetNormal;
    m_cache.setBudget(m_cacheBudget);

    // Becomes the live generation; drops the previous generation's cached pages.
    m_decode->openGeneration(m_generation, m_pages);

    ComicReaderStripModel::Options opt;
    opt.viewportWidth = m_stripViewportWidth;
    opt.portraitWidthPct = m_portraitWidthPct;
    opt.gap = m_stripGap;
    m_strip->rebuild(m_pages, opt);

    rebuildUnits();

    m_analyzable = false;
    for (const PageMeta& m : m_pages) {
        if (m.error == PageError::None && !m.localPath.isEmpty()) {
            m_analyzable = true;
            break;
        }
    }

    emit entryChanged();
    emit pairingChanged();
    emit progressChanged();
    emit cacheChanged();

    // The auto-coupling probe runs at most once per entry, only for an unresolved
    // Auto coupling over an analyzable (local, decodable) entry.
    if (m_analyzable && m_couplingMode == CouplingMode::Auto && !m_couplingResolved)
        startAutoCouplingProbe();
}

void ComicReaderCore::closeEntry() {
    resetEntryState();

    // Retire the generation so any provider request still bound to the closed
    // entry returns null, and drop that generation's cached pages.
    ++m_generation;
    m_liveGeneration.store(m_generation);
    m_cacheBudget = kBudgetNormal;
    m_cache.setBudget(m_cacheBudget);
    m_decode->openGeneration(m_generation, {});

    ComicReaderStripModel::Options opt;
    opt.viewportWidth = m_stripViewportWidth;
    opt.portraitWidthPct = m_portraitWidthPct;
    opt.gap = m_stripGap;
    m_strip->rebuild({}, opt);
    m_units.clear();

    emit entryChanged();
    emit pairingChanged();
    emit progressChanged();
    emit cacheChanged();
}

// ── read-only queries ───────────────────────────────────────────────────────

QVariantMap ComicReaderCore::pageInfo(int page) const {
    if (page < 0 || page >= m_pages.size())
        return {};
    QVariantMap m = m_pages[page].toVariantMap();
    m.insert(QStringLiteral("rev"), m_pageRev.value(page, 0));
    m.insert(QStringLiteral("imageUrl"), imageUrl(page));
    return m;
}

QVariantMap ComicReaderCore::unitForPage(int page) const {
    if (m_units.isEmpty())
        return {};
    const int u = comicreader::unitForPage(m_units, page);
    if (u < 0 || u >= m_units.size())
        return {};
    return m_units[u].toVariantMap();
}

QString ComicReaderCore::imageUrl(int page) const {
    return QStringLiteral("image://comicreader/%1/%2?rev=%3")
        .arg(m_generation)
        .arg(page)
        .arg(m_pageRev.value(page, 0));
}

QVariantMap ComicReaderCore::persistedState() const {
    QVariantMap m;
    QVariantMap so;
    for (auto it = m_spreadOverrides.constBegin(); it != m_spreadOverrides.constEnd(); ++it)
        so.insert(QString::number(it.key()), it.value());
    m.insert(QStringLiteral("spreadOverrides"), so);
    m.insert(QStringLiteral("couplingMode"),
             m_couplingMode == CouplingMode::Manual ? QStringLiteral("manual")
                                                    : QStringLiteral("auto"));
    m.insert(QStringLiteral("couplingPhase"),
             m_couplingPhase == CouplingPhase::Shifted ? QStringLiteral("shifted")
                                                       : QStringLiteral("normal"));
    m.insert(QStringLiteral("couplingResolved"), m_couplingResolved);
    m.insert(QStringLiteral("couplingConfidence"), m_couplingConfidence);
    QVariantList bm;
    for (int b : m_bookmarks)
        bm.append(b);
    m.insert(QStringLiteral("bookmarks"), bm);
    m.insert(QStringLiteral("memorySaver"), m_memorySaver);
    return m;
}

QVariantList ComicReaderCore::pinnedPages() const {
    QVariantList out;
    for (int p : m_lastPinned)
        out.append(p);
    return out;
}

QVariantMap ComicReaderCore::couplingProbeDebug() const {
    QVariantMap m;
    m.insert(QStringLiteral("called"), m_probeCalledChoose);
    m.insert(QStringLiteral("normalSamples"), m_probeNormalSamples);
    m.insert(QStringLiteral("shiftedSamples"), m_probeShiftedSamples);
    m.insert(QStringLiteral("resolved"), m_couplingResolved);
    m.insert(QStringLiteral("active"), m_probeActive);
    return m;
}

// ── mutations ───────────────────────────────────────────────────────────────

void ComicReaderCore::setSpreadOverride(int page, QString state) {
    if (page < 0 || page >= m_pages.size())
        return;
    const QString s = state.trimmed().toLower();
    if (s == QLatin1String("spread")) {
        m_spreadOverrides.insert(page, true);
        m_pages[page].spreadOverride = true;
    } else if (s == QLatin1String("single")) {
        m_spreadOverrides.insert(page, false);
        m_pages[page].spreadOverride = false;
    } else {   // "clear" (or anything else) defers back to detection
        m_spreadOverrides.remove(page);
        m_pages[page].spreadOverride.reset();
    }
    m_strip->updatePage(m_pages[page]);
    flushStripCompensation();
    rebuildUnits();
    emit pairingChanged();
}

void ComicReaderCore::nudgeCoupling() {
    m_couplingMode = CouplingMode::Manual;
    m_couplingResolved = true;
    m_couplingPhase = (m_couplingPhase == CouplingPhase::Normal)
                          ? CouplingPhase::Shifted : CouplingPhase::Normal;
    // A deliberate manual nudge aborts any pending auto-coupling probe.
    m_probeActive = false;
    m_probePendingPages.clear();
    rebuildUnits();
    emit pairingChanged();
}

void ComicReaderCore::resetCoupling() {
    m_couplingMode = CouplingMode::Auto;
    m_couplingPhase = CouplingPhase::Normal;
    m_couplingResolved = false;
    m_couplingConfidence = 0.0;

    // Drop anything a previous probe left mid-flight so the fresh one owns the
    // verdict alone (a stale in-flight probe would finalize against the old
    // sample set and re-resolve behind this one).
    m_probeActive = false;
    m_probePendingPages.clear();
    m_probeNormalPairs.clear();
    m_probeShiftedPairs.clear();
    m_probeCalledChoose = false;
    m_probeNormalSamples = 0;
    m_probeShiftedSamples = 0;

    rebuildUnits();
    emit pairingChanged();

    // Re-decide, same gate as openEntry: only an analyzable entry can be probed.
    if (m_analyzable)
        startAutoCouplingProbe();
}

double ComicReaderCore::stripPageTop(int page) const {
    if (page < 0 || page >= m_pages.size())
        return 0.0;
    return m_strip->pageTop(page);
}

double ComicReaderCore::setStripLayout(int portraitWidthPct, int gap,
                                       double viewportTop, double viewportHeight) {
    const int wpct = qBound(40, portraitWidthPct, 100);
    const int g = qBound(0, gap, 80);
    if (wpct == m_portraitWidthPct && g == m_stripGap)
        return viewportTop;   // nothing moved, so the reader must not move either

    // Capture the anchor BEFORE the geometry changes: which page the viewport centre sits in, and
    // how far down that page it is. Both are read from the pre-change column.
    const bool anchoring = viewportHeight > 0.0 && !m_pages.isEmpty();
    int anchorPage = -1;
    double anchorFrac = 0.0;
    if (anchoring) {
        anchorPage = m_strip->pageAtCenter(viewportTop, viewportHeight);
        if (anchorPage >= 0) {
            const double top = m_strip->pageTop(anchorPage);
            const double h = m_strip->pageHeight(anchorPage);
            if (h > 0.0)
                anchorFrac = qBound(0.0, (viewportTop + viewportHeight / 2.0 - top) / h, 1.0);
        }
    }

    m_portraitWidthPct = wpct;
    m_stripGap = g;
    // In place: the strip reflows without a model reset (a rebuild() here would tear down every
    // delegate and snap the reader back to page 1).
    m_strip->setLayout(m_portraitWidthPct, m_stripGap);
    emit stripLayoutChanged();

    if (!anchoring || anchorPage < 0)
        return viewportTop;

    // Put the same point of the same page back under the viewport centre.
    const double newTop = m_strip->pageTop(anchorPage)
                          + anchorFrac * m_strip->pageHeight(anchorPage)
                          - viewportHeight / 2.0;
    const double maxTop = qMax(0.0, m_strip->contentHeight() - viewportHeight);
    return qBound(0.0, newTop, maxTop);
}

void ComicReaderCore::setVisible(QVariantList pageIndices) {
    QVector<int> visible;
    for (const QVariant& v : pageIndices) {
        const int i = v.toInt();
        if (i >= 0 && i < m_pages.size())
            visible.append(i);
    }
    if (visible.isEmpty())
        return;

    // Pin the visible pages plus their immediate neighbors so the on-screen frame
    // (and the pages a flip lands on) never blank under memory pressure.
    QSet<int> pinSet;
    int minV = visible.first();
    int maxV = visible.first();
    for (int v : visible) {
        pinSet.insert(v);
        if (v - 1 >= 0) pinSet.insert(v - 1);
        if (v + 1 < m_pages.size()) pinSet.insert(v + 1);
        minV = qMin(minV, v);
        maxV = qMax(maxV, v);
    }
    QVector<int> pinned(pinSet.begin(), pinSet.end());
    std::sort(pinned.begin(), pinned.end());
    m_lastPinned = pinned;
    m_cache.setPinned(m_generation, pinned);

    // LATEST-WINS. Each visible wave outranks every earlier one still queued, so a held
    // page-turn decodes the page you LAND on first instead of last (equal priority is FIFO,
    // which put the newest — and only interesting — page at the back). Monotonic within an
    // entry; always above the strip band, which tops out at kPrioStripBase.
    //
    // A wave's LOWEST priority still beats the previous wave's HIGHEST, because every
    // within-wave offset is smaller than kVisibleWaveStep — pinned by the static_assert up
    // top rather than restated here in numbers that can drift out of step with it. That
    // ordering has to hold at the request, not later — a page already in the pool queue can
    // never be re-prioritized (QThreadPool has no such call, and request() dedups it away),
    // so out-ranking the stale work is the only lever there is.
    m_visibleBoost += kVisibleWaveStep;
    const int prioVisible = kPrioVisible + m_visibleBoost;

    for (int v : visible)
        m_decode->request(v, prioVisible);
    if (maxV + 1 < m_pages.size()) m_decode->request(maxV + 1, prioVisible - kOffNext1);
    if (maxV + 2 < m_pages.size()) m_decode->request(maxV + 2, prioVisible - kOffNext2);
    if (minV - 1 >= 0) {
        // Flipping BACK lands on a UNIT, not a page — prefetch both halves or the second one
        // pops in late (most visible re-reading backwards in RTL manga).
        const QVariantMap prevUnit = unitForPage(minV - 1);
        const int pr = prevUnit.value(QStringLiteral("rightIndex"), -1).toInt();
        const int pl = prevUnit.value(QStringLiteral("leftIndex"), -1).toInt();
        if (pr >= 0) m_decode->request(pr, prioVisible - kOffPrevUnit);
        if (pl >= 0) m_decode->request(pl, prioVisible - kOffPrevUnit);
    }
}

void ComicReaderCore::setStripViewport(double top, double height) {
    m_stripViewportTop = top;
    m_stripViewportHeight = height;
    const QVector<int> w = m_strip->window(top, height, 1.5);
    // Priority peaks AT the viewport centre and falls off symmetrically. The window starts
    // 1.5 screens ABOVE the fold, so ordering by window position handed the best priority to
    // pages the reader had already finished.
    const int centre = m_strip->pageAtCenter(top, height);
    for (int p : w)
        m_decode->request(p, stripDecodePriority(p, centre));
    flushStripCompensation();
}

void ComicReaderCore::setStripViewportWidth(int width) {
    if (width <= 0 || width == m_stripViewportWidth)
        return;
    m_stripViewportWidth = width;
    // In-place geometry rescale (dataChanged, NOT a model reset): the ListView keeps its
    // delegates and its scroll and simply reflows — no blink, no jump to page 1 — matching
    // how the rest of the app resizes. Locked-in real page sizes are preserved. The QML
    // strip surface scales its own scroll by the width ratio to hold the read position.
    m_strip->setViewportWidth(width);
}

void ComicReaderCore::setMemorySaver(bool on) {
    m_memorySaver = on;
    m_cacheBudget = on ? kBudgetSaver : kBudgetNormal;
    m_cache.setBudget(m_cacheBudget);
    emit cacheChanged();
}

void ComicReaderCore::toggleBookmark(int page) {
    if (page < 0 || page >= m_pages.size())
        return;
    const int i = m_bookmarks.indexOf(page);
    if (i >= 0)
        m_bookmarks.removeAt(i);
    else {
        m_bookmarks.append(page);
        std::sort(m_bookmarks.begin(), m_bookmarks.end());
    }
    emit bookmarksChanged();
}

QVariantList ComicReaderCore::bookmarks() const {
    QVariantList out;
    for (int b : m_bookmarks)
        out.append(b);
    return out;
}

void ComicReaderCore::rebuildUnits() {
    m_units = buildUnits(m_pages, m_couplingPhase);
}

void ComicReaderCore::flushStripCompensation() {
    const double delta = m_strip->takePendingCompensation(m_stripViewportTop);
    if (delta != 0.0)
        emit stripCompensation(delta);
}

// ── decode coordinator callbacks (owning thread) ────────────────────────────

void ComicReaderCore::onMetaReady(quint64 gen, const PageMeta& meta) {
    if (gen != m_generation)
        return;
    const int p = meta.index;
    if (p < 0 || p >= m_pages.size())
        return;

    const bool wasSpread = isSpread(m_pages[p]);
    m_pages[p].sourceSize = meta.sourceSize;
    m_pages[p].detectedSpread = meta.detectedSpread;
    // metaReady arrives TWICE per page now: once as the coordinator's
    // header-only dimension hint (real geometry, decoded=false), then again
    // when the full decode lands (decoded=true). Only the second one means
    // "there are pixels" — forcing decoded=true here would make the hint claim
    // a readiness it does not have, and would clear a real error on a page that
    // never actually decoded. Geometry is taken from both; readiness only from
    // the decode.
    if (meta.decoded) {
        m_pages[p].decoded = true;
        m_pages[p].error = PageError::None;
    }

    m_strip->updatePage(m_pages[p]);
    flushStripCompensation();

    // Metadata-driven spread discovery: a newly-learned spread (with no manual
    // override) restructures the double-page units.
    const bool nowSpread = isSpread(m_pages[p]);
    if (nowSpread != wasSpread && !m_pages[p].spreadOverride.has_value()) {
        rebuildUnits();
        emit pairingChanged();
    }
}

void ComicReaderCore::onPageReady(quint64 gen, int page) {
    if (gen != m_generation)
        return;
    // Bump the page's imageUrl rev so a bound QML Image re-requests the fresh pixels.
    m_pageRev.insert(page, m_pageRev.value(page, 0) + 1);
    if (!m_readyPages.contains(page)) {
        m_readyPages.insert(page);
        ++m_readyCount;
        emit progressChanged();
    }
    emit pageReady(page);

    if (m_probeActive && m_probePendingPages.contains(page))
        onProbePageResolved(page);
}

void ComicReaderCore::onPageFailed(quint64 gen, int page, PageError error) {
    if (gen != m_generation)
        return;
    if (page >= 0 && page < m_pages.size()) {
        m_pages[page].error = error;
        m_pages[page].decoded = false;
        m_strip->updatePage(m_pages[page]);
    }
    emit pageFailed(page, pageErrorToString(error));

    if (m_probeActive && m_probePendingPages.contains(page))
        onProbePageResolved(page);
}

// ── auto-coupling probe ─────────────────────────────────────────────────────

void ComicReaderCore::startAutoCouplingProbe() {
    m_probeActive = true;
    m_probeCalledChoose = false;
    m_probeNormalSamples = 0;
    m_probeShiftedSamples = 0;
    m_probeNormalPairs.clear();
    m_probeShiftedPairs.clear();
    m_probePendingPages.clear();

    const int limit = qMin(8, m_pages.size());   // sample only the first ~8 pages
    auto collect = [&](CouplingPhase ph) {
        QVector<ProbePair> out;
        const QVector<PairUnit> units = buildUnits(m_pages, ph);
        for (const PairUnit& pu : units) {
            if (pu.spread)
                continue;
            if (pu.leftIndex < 0 || pu.rightIndex < 0)
                continue;   // only true two-page pairs are continuity candidates
            const int a = qMin(pu.rightIndex, pu.leftIndex);
            const int b = qMax(pu.rightIndex, pu.leftIndex);
            if (b >= limit)
                continue;
            out.append({a, b});
            if (out.size() >= 4)
                break;
        }
        return out;
    };
    m_probeNormalPairs = collect(CouplingPhase::Normal);
    m_probeShiftedPairs = collect(CouplingPhase::Shifted);

    QSet<int> targets;
    for (const ProbePair& pr : m_probeNormalPairs) { targets.insert(pr.a); targets.insert(pr.b); }
    for (const ProbePair& pr : m_probeShiftedPairs) { targets.insert(pr.a); targets.insert(pr.b); }

    for (int p : targets) {
        if (p < 0 || p >= m_pages.size())
            continue;
        if (m_pages[p].error != PageError::None)
            continue;   // undecodable — leave it out, finalize will treat it as absent
        if (m_cache.get(m_generation, p).has_value())
            continue;   // already decoded (e.g. a visible page) — cost read at finalize
        m_probePendingPages.insert(p);
        m_decode->request(p, kPrioProbe);
    }

    if (m_probePendingPages.isEmpty())
        finalizeProbe();
}

void ComicReaderCore::onProbePageResolved(int page) {
    if (!m_probeActive)
        return;
    m_probePendingPages.remove(page);
    if (m_probePendingPages.isEmpty())
        finalizeProbe();
}

void ComicReaderCore::finalizeProbe() {
    m_probeActive = false;

    auto costFor = [&](const ProbePair& pr, double& out) -> bool {
        const std::optional<QImage> la = m_cache.get(m_generation, pr.a);
        const std::optional<QImage> lb = m_cache.get(m_generation, pr.b);
        if (!la.has_value() || !lb.has_value())
            return false;
        out = edgeContinuityCost(*la, *lb);
        return true;
    };

    QVector<double> normalCosts;
    QVector<double> shiftedCosts;
    for (const ProbePair& pr : m_probeNormalPairs) {
        double c = 0.0;
        if (costFor(pr, c))
            normalCosts.append(c);
    }
    for (const ProbePair& pr : m_probeShiftedPairs) {
        double c = 0.0;
        if (costFor(pr, c))
            shiftedCosts.append(c);
    }

    // Feed the FULL per-phase cost vectors (both non-empty); mean aggregation in
    // chooseCouplingPhase handles different sample counts — do NOT trim to equal
    // length. Each phase's pairing yields its OWN seams (normal collects (1,2),
    // (3,4),(5,6)...; shifted collects (2,3),(4,5)...), so the two vectors
    // routinely differ in length AND normalCosts[i]/shiftedCosts[i] are different
    // page seams — a positional min()-trim was never a principled paired compare,
    // and it would discard real evidence (e.g. normal's last high-cost seam, the
    // very proof that normal coupling is wrong), flipping the verdict. The MEAN
    // is the faithful aggregation boundary (Task 5 contract, commit f92491c).
    if (!normalCosts.isEmpty() && !shiftedCosts.isEmpty()) {
        m_probeNormalSamples = normalCosts.size();
        m_probeShiftedSamples = shiftedCosts.size();
        m_probeCalledChoose = true;
        const CouplingVerdict v = chooseCouplingPhase(normalCosts, shiftedCosts);
        m_couplingPhase = v.phase;
        m_couplingConfidence = v.confidence;
    } else {
        // A one-sided (or empty/empty) probe never decides — stay Normal.
        m_couplingPhase = CouplingPhase::Normal;
        m_couplingConfidence = 0.0;
        m_probeCalledChoose = false;
    }

    m_couplingResolved = true;
    rebuildUnits();
    emit pairingChanged();
}

// ── provider factory ────────────────────────────────────────────────────────

ComicReaderProvider* ComicReaderCore::createProvider() {
    return new ComicReaderProvider(&m_cache, &m_liveGeneration);
}

} // namespace comicreader
