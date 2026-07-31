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

// requestRange's retention margins, in pages either side of the visible run.
//
// The decode margin is ±2 because the Long Strip decode window asks for less
// than that, so the sweep can never fight the strip's own prefetch: the strip
// loads 1.5 screens past each edge of the viewport, and at the default 78%
// portrait width a PAGE is roughly two screens tall — so 1.5 screens is well
// under one page either side. ±2 is therefore comfortably conservative; a
// looser margin would stop bounding anything.
//
// The scaled margins are deliberately ASYMMETRIC — one behind, two ahead. A
// scale is cheap to redo from a page that is still decoded, and the reader is
// travelling forward, so spending the tier's entry budget behind the viewport
// buys less than spending it in front.
constexpr int kDecodeKeepMargin = 2;
constexpr int kScaleKeepBehind  = 1;
constexpr int kScaleKeepAhead   = 2;

// Scaled entries to allow per retained PAGE. The scaled tier is keyed finer than
// a page — target width and tier are part of the key — so one page can hold more
// than one entry at once, and sizing the capacity at the page count alone would
// make the tier evict a live in-window scale every time a second one appeared.
// Two, because both ways that happens are the same shape, "the live scale plus
// the one replacing it": the double-page surface changes srcCapW with zoom
// (1400/2048/2800), and Task 8 stacks a preview under its hq at the same width.
// Anything beyond that pair is genuinely stale and LRU is the right answer.
constexpr int kScaledEntriesPerPage = 2;

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
    // Both tiers publish their resident-entry high-water mark into the one
    // metrics struct. Wired here, before anything can reach either cache, so no
    // worker ever races the sink being installed.
    m_cache.setResidentHighWaterSink(&m_delivery.maxDecodedResident);
    m_scaleCache.setMetricsSink(&m_delivery);

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
    m_persistedDetectedSpreads.clear();
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
        const QString archive = pm.value(QStringLiteral("archive")).toString();
        const QString entry = pm.value(QStringLiteral("entry")).toString();
        if (!archive.isEmpty() && !entry.isEmpty()) {
            if (QFileInfo::exists(archive)) {
                meta.sourceKind = PageSourceKind::CbzEntry;
                meta.archivePath = archive;
                meta.archiveEntry = entry;
            } else {
                meta.error = PageError::MissingFile;
            }
            m_pages.append(meta);
            continue;
        }
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

    // The decoder's learned spreads from the last session (E6). Replayed so pairing is correct on
    // the FIRST paint instead of re-derived as decodes trickle in.
    if (persisted.contains(QStringLiteral("detectedSpreads"))) {
        m_persistedDetectedSpreads.clear();
        const QVariantList ds = persisted.value(QStringLiteral("detectedSpreads")).toList();
        for (const QVariant& v : ds) {
            bool ok = false;
            const int idx = v.toInt(&ok);
            if (ok && idx >= 0)
                m_persistedDetectedSpreads.append(idx);
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

    // Fold the decoder's REMEMBERED spreads on first, so pairing is shaped correctly before a
    // single page has decoded. Done before the override fold below, because the user's explicit
    // verdict must still win over anything the machine merely observed.
    for (const int idx : m_persistedDetectedSpreads) {
        if (idx >= 0 && idx < m_pages.size())
            m_pages[idx].detectedSpread = true;
    }

    // Fold persisted spread overrides onto the page metas so pairing + strip see them.
    for (auto it = m_spreadOverrides.constBegin(); it != m_spreadOverrides.constEnd(); ++it) {
        const int idx = it.key();
        if (idx >= 0 && idx < m_pages.size())
            m_pages[idx].spreadOverride = it.value();
    }

    m_cacheBudget = m_memorySaver ? kBudgetSaver : kBudgetNormal;
    m_cache.setBudget(m_cacheBudget);
    m_scaleCache.setHardCeiling(m_memorySaver ? ComicReaderScaleCache::kHardCeilingSaver
                                              : ComicReaderScaleCache::kHardCeilingNormal);
    // Every scale in the tier belongs to a generation that is now dead — only
    // one is ever live — so the whole tier goes rather than being swept page by
    // page. (The decoded tier's equivalent is openGeneration's clearGeneration.)
    m_scaleCache.clear();
    // The retained window belongs to the book that just closed; the next
    // requestRange must actually run even if the new book opens on the same
    // page numbers.
    m_rangeFirst = -1;
    m_rangeLast = -1;
    // These describe the delivery of ONE volume. Carrying a cold-open outlier
    // across an entry-open would leave maxResponseMs reporting a number from a
    // book nobody is reading any more.
    m_delivery.reset();

    // Becomes the live generation; drops the previous generation's cached pages.
    m_decode->openGeneration(m_generation, m_pages);

    ComicReaderStripModel::Options opt;
    opt.viewportWidth = m_stripViewportWidth;
    opt.portraitWidthPct = m_portraitWidthPct;
    opt.gap = m_stripGap;
    // The render profile SURVIVES an entry crossing (it is a property of how you
    // want to read this series), so the next volume must open already wearing the
    // quarter turn rather than laying out square bands and correcting itself the
    // first time anything else touches the profile.
    opt.rotationDegrees = m_renderProfile.load().rotation;
    m_strip->rebuild(m_pages, opt);

    rebuildUnits();

    m_analyzable = false;
    for (const PageMeta& m : m_pages) {
        const bool hasSource =
            (m.sourceKind == PageSourceKind::LocalFile && !m.localPath.isEmpty())
            || (m.sourceKind == PageSourceKind::CbzEntry
                && !m.archivePath.isEmpty() && !m.archiveEntry.isEmpty());
        if (m.error == PageError::None && hasSource) {
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
    m_scaleCache.setHardCeiling(ComicReaderScaleCache::kHardCeilingNormal);
    m_scaleCache.setCapacity(ComicReaderScaleCache::kDefaultCapacity);
    m_scaleCache.clear();
    m_rangeFirst = -1;
    m_rangeLast = -1;
    m_decode->openGeneration(m_generation, {});

    ComicReaderStripModel::Options opt;
    opt.viewportWidth = m_stripViewportWidth;
    opt.portraitWidthPct = m_portraitWidthPct;
    opt.gap = m_stripGap;
    opt.rotationDegrees = m_renderProfile.load().rotation;
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

QVariantMap ComicReaderCore::presentationForPage(int page) const {
    // Guard the EMPTY case before anything else. comicreader::unitForPage answers
    // "which unit", and for an empty walk that answer is 0 — a value, not an index —
    // so indexing straight off it reads past the end of an empty vector.
    //
    // Three reachable ways to get here with no units: no entry has been opened yet
    // (T29), the entry was CLOSED (closeEntry -> resetEntryState clears m_units, and
    // the shell calls it on close), and a zero-page entry (buildUnits returns empty
    // for n == 0). NOT "between openEntry and the first rebuildUnits" — an earlier
    // draft of this comment claimed that window and it does not exist: openEntry
    // calls rebuildUnits() synchronously before it returns, and openEntry is what
    // QML invokes. (Pinned in comicreader_pairing_harness's clamping block;
    // unitForPage() above guards it the same way.)
    if (m_units.isEmpty())
        return {};
    const int unitIndex = comicreader::unitForPage(m_units, page);
    if (unitIndex < 0 || unitIndex >= m_units.size())
        return {};
    const PairUnit unit = m_units[unitIndex];

    // The members are READ off the canonical unit — this function has no opinion
    // about who pairs with whom. A -1 is "absent", never a page, and m_pages[-1]
    // would be a read off the front of the vector.
    //
    // Only LEFTINDEX actually reaches that: it is -1 on every cover-alone, spread
    // and odd-tail unit, which is most of a book. rightIndex is set on every unit
    // buildUnits appends and m_units is populated from nowhere else, so its guard
    // is symmetry hardening against a future producer, not a bug that was found —
    // stating it as a second crash-class defect would be an overclaim. Both bounds
    // stay; the upper bound is the one that would matter if a unit ever outlived
    // the page vector it indexes.
    QVector<int> members;
    if (unit.rightIndex >= 0 && unit.rightIndex < m_pages.size())
        members.append(unit.rightIndex);
    if (unit.leftIndex >= 0 && unit.leftIndex < m_pages.size())
        members.append(unit.leftIndex);

    QVariantMap out = unit.toVariantMap();
    // Always present, so QML reads .state/.errorCode without first testing for the key.
    out.insert(QStringLiteral("errorCode"), pageErrorToString(PageError::None));

    // A unit with nothing in it has nothing to show — waiting, never "ready with
    // no pages" (which would tell the surface to paint an empty spread).
    if (members.isEmpty()) {
        out.insert(QStringLiteral("state"), QStringLiteral("waiting"));
        return out;
    }

    // ERROR FIRST. A member that can never arrive must stop the unit waiting for
    // it, even if the other half is sitting there decoded: the reader gets one
    // deliberate "this page is broken" instead of a half-painted spread that never
    // completes. (A page that heals clears its error in onMetaReady before
    // onPageReady lands, so a healed page passes this loop, not gets stuck in it.)
    for (int p : members) {
        if (m_pages[p].error != PageError::None) {
            out.insert(QStringLiteral("state"), QStringLiteral("error"));
            out.insert(QStringLiteral("errorCode"), pageErrorToString(m_pages[p].error));
            return out;
        }
    }
    // ...then WAITING while any member still has no pixels. m_readyPages is
    // "has decoded at least once", not "is resident", so a cache eviction does
    // not throw a read spread back to a placeholder mid-page-turn.
    for (int p : members) {
        if (!m_readyPages.contains(p)) {
            out.insert(QStringLiteral("state"), QStringLiteral("waiting"));
            return out;
        }
    }
    out.insert(QStringLiteral("state"), QStringLiteral("ready"));
    return out;
}

QString ComicReaderCore::imageUrl(int page, QString tier) const {
    // Normalised, not passed through: two different misspellings must not become
    // two different scaled-cache entries for the same picture.
    const QString normalised = tierToString(tierFromString(tier));
    // `rr` is the RENDER revision, and it is here for one reason: QML's own
    // pixmap cache is keyed by url. Without it, adjusting brightness produces a
    // byte-identical url, QQuickPixmapCache serves the pre-adjustment page from
    // its own store, and the provider is never asked — the reader would move a
    // slider and watch nothing happen. It plays no part in the request itself
    // (the worker reads the profile and its revision from the store, coherently);
    // it exists purely to make the url change when the picture does.
    return QStringLiteral("image://comicreader/%1/%2?rev=%3&tier=%4&rr=%5")
        .arg(m_generation)
        .arg(page)
        .arg(m_pageRev.value(page, 0))
        .arg(normalised)
        .arg(m_renderProfile.revision());
}

void ComicReaderCore::setRenderProfile(QVariantMap profile) {
    // THE validation boundary — see ComicReaderRenderProfile.h. Everything past
    // this line is in range by construction.
    const RenderProfile next = normalizeRenderProfile(profile);
    const RenderProfileStore::Change change = m_renderProfile.store(next);
    if (change == RenderProfileStore::Change::None)
        return;
    if (change == RenderProfileStore::Change::Pixel) {
        // Every scaled entry was computed under the PREVIOUS revision, so every
        // one of them is stale — clearing the tier IS "invalidate the entries for
        // the old render revision", with no second bookkeeping to keep in step.
        //
        // The DECODED tier is untouched, deliberately and load-bearingly: the
        // adjustments are applied to pixels we already hold, so a slider drag
        // costs a rescale, never a trip back to disk.
        m_scaleCache.clear();
    }
    // LONG STRIP BAND GEOMETRY FOLLOWS THE TURN (Task 8). The strip is
    // model-authoritative — its delegates take their height from this model, never
    // from the loaded Image's implicit size — so it is the ONE surface that cannot
    // discover a rotation for itself. Left unwired, a page turned 90 degrees was
    // delivered landscape and drawn PreserveAspectFit inside a portrait-shaped
    // band: correct pixels, big dead margins above and below every page. Single
    // and Pair self-correct off the delivered pixmap and need nothing here.
    //
    // Unconditional rather than gated on `change`: setRotation compares quarter
    // turns itself and returns without touching the column when nothing turned,
    // so there is no second, weaker copy of that test to fall out of step.
    m_strip->setRotation(next.rotation);
    emit renderProfileChanged();
}

QVariantMap ComicReaderCore::renderProfile() const {
    return renderProfileToVariantMap(m_renderProfile.load());
}

QVariantMap ComicReaderCore::deliveryMetrics() const {
    return {
        {QStringLiteral("sourceHits"), qulonglong(m_delivery.sourceHits.load())},
        {QStringLiteral("scaledHits"), qulonglong(m_delivery.scaledHits.load())},
        {QStringLiteral("scaleJobs"), qulonglong(m_delivery.scaleJobs.load())},
        {QStringLiteral("cancelledJobs"), qulonglong(m_delivery.cancelledJobs.load())},
        {QStringLiteral("staleDrops"), qulonglong(m_delivery.staleDrops.load())},
        {QStringLiteral("maxDispatchUs"), qulonglong(m_delivery.maxDispatchUs.load())},
        {QStringLiteral("maxResponseMs"), qulonglong(m_delivery.maxResponseMs.load())},
        {QStringLiteral("maxDecodedResident"), qulonglong(m_delivery.maxDecodedResident.load())},
        {QStringLiteral("maxScaledResident"), qulonglong(m_delivery.maxScaledResident.load())},
        {QStringLiteral("scaledBytesUsed"), qulonglong(m_delivery.scaledBytesUsed.load())},
        {QStringLiteral("scaledEvictions"), qulonglong(m_delivery.scaledEvictions.load())},
        // Not counters — the two bounds the scaled tier is working to right now,
        // reported because a bound nobody can read is a bound nobody can size,
        // and Task 12 has to size this one. Read them WITH scaledBytesUsed and
        // scaledEvictions: evictions climbing while bytes sit far under the
        // ceiling means the capacity is the thing that is too small, and the
        // other way round means the ceiling is.
        {QStringLiteral("scaledCapacity"), m_scaleCache.capacity()},
        {QStringLiteral("scaledCeilingBytes"), qulonglong(m_scaleCache.hardCeiling())},
    };
}

QVariantMap ComicReaderCore::persistedState() const {
    QVariantMap m;
    QVariantMap so;
    for (auto it = m_spreadOverrides.constBegin(); it != m_spreadOverrides.constEnd(); ++it)
        so.insert(QString::number(it.key()), it.value());
    m.insert(QStringLiteral("spreadOverrides"), so);
    // The decoder's learned spreads, emitted ONLY when non-empty so a book with none round-trips as
    // absence exactly as before (T12's byte-identical round-trip stays green).
    QVariantList ds;
    for (const PageMeta& pm : m_pages)
        if (pm.detectedSpread)
            ds.append(pm.index);
    if (!ds.isEmpty())
        m.insert(QStringLiteral("detectedSpreads"), ds);
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

int ComicReaderCore::stripPageAtCenter(double top, double viewportHeight) const {
    return m_pages.isEmpty() ? -1 : m_strip->pageAtCenter(top, viewportHeight);
}

double ComicReaderCore::stripPageHeight(int page) const {
    // Same guard shape as stripPageTop above, and the same reason it exists: the drawn
    // column cannot be asked about a page it has not realized yet.
    if (page < 0 || page >= m_pages.size())
        return 0.0;
    return m_strip->pageHeight(page);
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

void ComicReaderCore::requestRange(int first, int last) {
    // No book, no range to own. qBound would be handed min > max here, which is
    // undefined — the guard is the reason there is one.
    if (m_pages.isEmpty())
        return;

    const int lastPage = m_pages.size() - 1;
    first = qBound(0, first, lastPage);
    last = qBound(first, last, lastPage);

    // Change detection, and it is not an optimisation — it is what makes this
    // safe to wire to a scroll signal. Every real call walks two hashes under
    // two mutexes and bulk-frees QImages on the GUI thread, and the decoded walk
    // takes the very mutex every provider worker blocks on for every page fetch.
    // Task 8 drives this from the strip viewport, which moves per frame; without
    // this line that is a per-frame GUI-thread sweep, the same shape of
    // steady-state cost that turned out to be the player's stutter this month.
    // Post-clamp so the many raw ranges that clamp to the same window collapse
    // into one.
    //
    // What the early-out gives up, stated so it is a decision and not a
    // surprise: the decoded sweep also consumes m_lastPinned, so if the pin set
    // changes while the range does not, a page that has just been UNPINNED
    // outside the window keeps its decoded entry until the next range change.
    // That is retention only — the tier's budget/LRU still bounds it — so the
    // cost is one page held a little longer, against a per-frame sweep.
    if (first == m_rangeFirst && last == m_rangeLast)
        return;
    m_rangeFirst = first;
    m_rangeLast = last;

    const int decodeFirst = qMax(0, first - kDecodeKeepMargin);
    const int decodeLast = qMin(lastPage, last + kDecodeKeepMargin);
    const int scaleFirst = qMax(0, first - kScaleKeepBehind);
    const int scaleLast = qMin(lastPage, last + kScaleKeepAhead);

    // The window IS what bounds the scaled tier: the reader keeps the scales for
    // the pages it just said it wants, instead of however many a fixed byte
    // guess happens to allow. See ComicReaderScaleCache.h — a fixed 64 MiB
    // budget was the first cut's mistake, and it held two entries where it
    // claimed eight.
    m_scaleCache.setCapacity((scaleLast - scaleFirst + 1) * kScaledEntriesPerPage);

    // Retention only. Nothing is requested and no priority moves — setVisible
    // and setStripViewport own that half, and this owns what may stay.
    m_cache.retainRange(m_generation, decodeFirst, decodeLast, m_lastPinned);
    m_scaleCache.retainRange(m_generation, scaleFirst, scaleLast);
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
    // The scaled tier's safety net halves with the decoded tier's budget — a
    // "memory saver" that saved on one of the two image caches would be telling
    // half the truth. The window still governs; this only lowers the stop.
    m_scaleCache.setHardCeiling(on ? ComicReaderScaleCache::kHardCeilingSaver
                                   : ComicReaderScaleCache::kHardCeilingNormal);
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
    DeliveryContext ctx;
    ctx.pageCache = &m_cache;
    ctx.scaleCache = &m_scaleCache;
    ctx.liveGeneration = &m_liveGeneration;
    ctx.renderRevision = m_renderProfile.revisionAtomic();
    ctx.renderProfile = &m_renderProfile;
    ctx.metrics = &m_delivery;
    return new ComicReaderProvider(ctx);
}

} // namespace comicreader
