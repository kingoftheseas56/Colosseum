// native/comicreader/ComicReaderImageResponse.cpp
#include "comicreader/ComicReaderImageResponse.h"

#include "comicreader/ComicReaderPageCache.h"
#include "comicreader/ComicReaderRenderProfile.h"

#include <QMetaObject>
#include <QStringView>

#include <optional>

namespace comicreader {
namespace {

// "<generation>/<page>", ignoring any query the QML side appended purely to
// bust its own image cache ("?rev=3"). Leaves the outputs untouched and returns
// false for anything that is not a well-formed, non-negative pair.
bool parseKey(const QString& id, quint64& generation, int& page) {
    QStringView key(id);
    const qsizetype query = key.indexOf(QLatin1Char('?'));
    if (query >= 0)
        key = key.left(query);

    const qsizetype slash = key.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return false;

    bool okGeneration = false;
    bool okPage = false;
    const quint64 parsedGeneration = key.left(slash).toULongLong(&okGeneration);
    const int parsedPage = key.mid(slash + 1).toInt(&okPage);
    // toInt() accepts "-1" happily. A negative page is never a real request, so
    // reject it here rather than letting a later cache miss hide it.
    if (!okGeneration || !okPage || parsedPage < 0)
        return false;

    generation = parsedGeneration;
    page = parsedPage;
    return true;
}

// The raw value of `key` in the id's "?a=1&b=2" query, or a null view when the
// key is absent. Deliberately tolerant: an unknown key is ignored and a missing
// one falls back to the caller's default, which is what lets a pre-Task-2 url
// ("<gen>/<page>?rev=1") stay valid.
QStringView queryValue(const QString& id, QLatin1String key) {
    const qsizetype start = id.indexOf(QLatin1Char('?'));
    if (start < 0)
        return {};
    QStringView query = QStringView(id).mid(start + 1);
    while (!query.isEmpty()) {
        const qsizetype amp = query.indexOf(QLatin1Char('&'));
        const QStringView pair = amp >= 0 ? query.left(amp) : query;
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        if (eq > 0 && pair.left(eq) == key)
            return pair.mid(eq + 1);
        if (amp < 0)
            break;
        query = query.mid(amp + 1);
    }
    return {};
}

// Absent, "hq", or anything unrecognised all mean hq — tierFromString owns that
// rule so the writer and the reader of the url cannot drift apart.
ScaleTier parseTier(const QString& id) {
    return tierFromString(queryValue(id, QLatin1String("tier")));
}

// Device pixel ratio as hundredths, so it can key exactly. 100 (1.0x) whenever
// the caller says nothing or says something unparseable.
int parseDpr100(const QString& id) {
    const QStringView value = queryValue(id, QLatin1String("dpr"));
    if (value.isEmpty())
        return 100;
    bool ok = false;
    const double dpr = value.toDouble(&ok);
    if (!ok || dpr <= 0.0)
        return 100;
    return qRound(dpr * 100.0);
}

// The width this tier will actually scale to, given what the caller asked for.
// Only the thumbnail tier overrides the caller, and only downward — the cap is
// a ceiling, not a size, so a smaller request is honoured as asked. A thumbnail
// asked for with NO width still gets the cap, which is the request shape a plain
// `Image { source: ... }` produces.
int targetWidthFor(ScaleTier tier, int requestedWidth) {
    if (tier != ScaleTier::Thumbnail)
        return requestedWidth;
    return requestedWidth > 0
               ? qMin(requestedWidth, ComicReaderImageResponse::kThumbnailMaxWidth)
               : ComicReaderImageResponse::kThumbnailMaxWidth;
}

Qt::TransformationMode transformFor(ScaleTier tier, RenderQuality quality) {
    // Preview trades quality for speed on purpose — it exists to put pixels on
    // screen and be replaced. Thumbnails are smooth: scaling a 2400px page down
    // to 240 with a fast transform aliases badly, and a thumbnail is looked at,
    // not glanced past.
    //
    // Task 7: the reader's quality dial governs the HQ tier and only the HQ tier,
    // because hq IS the page the setting is about. Letting "fast" alias the
    // filmstrip would trade quality where none of the cost is — a 240px
    // thumbnail's scale is already nearly free — and letting "best" un-fast the
    // preview would defeat the one job the preview tier has.
    if (tier == ScaleTier::Preview)
        return Qt::FastTransformation;
    if (tier == ScaleTier::Thumbnail)
        return Qt::SmoothTransformation;
    return quality == RenderQuality::Fast ? Qt::FastTransformation : Qt::SmoothTransformation;
}

} // namespace

ComicReaderImageResponse::ComicReaderImageResponse(const DeliveryContext& ctx,
                                                   const QString& id,
                                                   const QSize& requestedSize)
    : m_ctx(ctx),
      m_requestedWidth(requestedSize.width()) {
    // The pool must not delete this — the owner does, after finished(). Safe to
    // set here: the pool reads the flag before dispatch and never revisits the
    // object once run() returns (see the header's lifetime note).
    setAutoDelete(false);

    m_idParsed = parseKey(id, m_generation, m_page);
    m_tier = parseTier(id);
    m_dpr100 = parseDpr100(id);
    m_age.start();
}

void ComicReaderImageResponse::cancel() {
    m_cancelled.store(true, std::memory_order_release);
}

bool ComicReaderImageResponse::wasCancelled() const {
    return m_cancelled.load(std::memory_order_acquire);
}

QQuickTextureFactory* ComicReaderImageResponse::textureFactory() const {
    return m_result.isNull() ? nullptr
                             : QQuickTextureFactory::textureFactoryForImage(m_result);
}

QThread* ComicReaderImageResponse::servedOn() const {
    return m_servedOn.load(std::memory_order_relaxed);
}

void ComicReaderImageResponse::run() {
    m_servedOn.store(QThread::currentThread(), std::memory_order_relaxed);

    QImage served;

    // An unparseable id, a provider wired to nothing, or a cancel that beat the
    // worker to the start: all exit without ever touching a cache.
    if (m_idParsed && m_ctx.pageCache && m_ctx.liveGeneration && !wasCancelled()) {
        // Stale guard: anything but the live generation resolves to nothing, so
        // a QML Image still bound to a retired entry never repaints old pixels.
        if (m_generation != m_ctx.liveGeneration->load()) {
            if (m_ctx.metrics)
                m_ctx.metrics->staleDrops.fetch_add(1, std::memory_order_relaxed);
        } else {
            // The profile and the revision it BELONGS TO, together. Reading them
            // separately can tear, and a tear here files new-profile pixels under
            // the old revision's key — a scaled entry whose contents disagree with
            // its own identity, served to every later request that matches it.
            // RenderProfileStore::load closes that window; the bare-atomic path is
            // the pre-Task-7 fallback for a context with no profile at all.
            RenderProfile profile;
            quint64 renderRevision = 0;
            if (m_ctx.renderProfile)
                profile = m_ctx.renderProfile->load(&renderRevision);
            else if (m_ctx.renderRevision)
                renderRevision = m_ctx.renderRevision->load();

            const int targetWidth = targetWidthFor(m_tier, m_requestedWidth);
            const ScaleKey key{m_generation, m_page, QSize(targetWidth, 0),
                               m_dpr100, renderRevision, m_tier};

            // The scaled tier FIRST. On a hit this is the whole request: no
            // full-resolution read, no scale, no allocation beyond the implicit
            // share — which is the entire point of Task 2.
            std::optional<QImage> reused;
            if (m_ctx.scaleCache)
                reused = m_ctx.scaleCache->get(key);

            if (reused.has_value() && !reused->isNull()) {
                served = *reused;
                if (m_ctx.metrics)
                    m_ctx.metrics->scaledHits.fetch_add(1, std::memory_order_relaxed);
            } else {
                const std::optional<QImage> source = m_ctx.pageCache->get(m_generation, m_page);
                if (source.has_value() && !source->isNull()) {
                    if (m_ctx.metrics)
                        m_ctx.metrics->sourceHits.fetch_add(1, std::memory_order_relaxed);

                    // Cancelled while the source lookup ran — skip the scale,
                    // which is the expensive half and the whole reason this is
                    // off-thread.
                    if (!wasCancelled()) {
                        // GEOMETRY FIRST, ALWAYS. Auto-crop and rotation change
                        // the image's dimensions, and `targetWidth` names the
                        // FINAL width — scaling a portrait page to 2048 and then
                        // turning it 90 degrees would deliver a 2048-TALL image
                        // to a caller that asked for 2048 wide. With an identity
                        // profile this returns the source itself, so the path
                        // below is byte-for-byte what it was before Task 7.
                        QImage rendered =
                            applyRenderProfile(*source, profile, RenderStage::Geometry);

                        // `best` pays the tonal maths at FULL resolution, before
                        // the resample; `fast` and `balanced` pay it on the
                        // scaled copy. See ComicReaderRenderProfile.h for why the
                        // two orders differ at all — resampling and a non-linear
                        // tone curve do not commute.
                        //
                        // HQ ONLY, on the same rule transformFor() follows: the
                        // full-resolution pass is by far the expensive half (a
                        // 2400x3600 page is ~8.6M LUT samples), and spending it on
                        // the PREVIEW tier would destroy the one job preview has —
                        // first pixels on screen — while spending it on a 240px
                        // thumbnail buys nothing anybody can see.
                        const bool toneFirst = profile.quality == RenderQuality::Best
                                               && m_tier == ScaleTier::Hq;
                        if (toneFirst)
                            rendered = applyRenderProfile(rendered, profile, RenderStage::Tone);

                        const bool scaling = targetWidth > 0 && targetWidth < rendered.width();
                        if (scaling) {
                            if (m_ctx.metrics)
                                m_ctx.metrics->scaleJobs.fetch_add(1, std::memory_order_relaxed);
                            rendered = rendered.scaledToWidth(
                                targetWidth, transformFor(m_tier, profile.quality));
                        }
                        if (!toneFirst)
                            rendered = applyRenderProfile(rendered, profile, RenderStage::Tone);
                        served = rendered;

                        // Seat it in the scaled tier when it COST something —
                        // a scale, or any pixel work the profile asked for.
                        // Without the profile arm a rotated-but-not-downscaled
                        // page would re-run its transform on every single
                        // request. The pre-Task-7 rule survives inside this one:
                        // with an identity profile and no scale, `served` is
                        // byte-identical to the source and seating it would
                        // spend a slot to save no work.
                        //
                        // Publish AFTER a fresh generation check: a volume that
                        // closed while this ran must not seed the tier for the
                        // volume that replaced it.
                        if (m_ctx.scaleCache && (scaling || !profile.isIdentity())
                            && m_generation == m_ctx.liveGeneration->load())
                            m_ctx.scaleCache->insert(key, served);
                    }
                }
            }
        }
    }

    // Publish on the response's own thread. Queued, so a cancel() issued from
    // that thread is ordered ahead of this and simply wins.
    QMetaObject::invokeMethod(
        this, [this, served]() { publish(served); }, Qt::QueuedConnection);
}

void ComicReaderImageResponse::publish(const QImage& image) {
    const bool cancelled = wasCancelled();
    if (!cancelled)
        m_result = image;

    if (m_ctx.metrics) {
        // Counted HERE, not in run(): this is the one place that sees every
        // cancellation, including one that lands after the work finished (the
        // checkpoint F8 pins). Counting it in the worker would miss those and
        // double-count nothing.
        if (cancelled) {
            m_ctx.metrics->cancelledJobs.fetch_add(1, std::memory_order_relaxed);
        } else if (!image.isNull()) {
            // Only a request that actually delivered a page contributes to the
            // latency figure. A cancelled or stale response resolves to nothing,
            // and folding its lifetime in would make maxResponseMs report how
            // long a cancel took rather than how long a page took.
            raiseMax(m_ctx.metrics->maxResponseMs, static_cast<quint64>(m_age.elapsed()));
        }
    }

    emit finished();
}

} // namespace comicreader
