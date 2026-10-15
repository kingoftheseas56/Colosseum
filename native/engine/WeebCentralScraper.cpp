#include "WeebCentralScraper.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

static const QString BASE = QStringLiteral("https://weebcentral.com");
static const QString USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");

static QNetworkRequest makeRequest(const QUrl& url, bool isHtmx = false)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", BASE.toUtf8());
    req.setRawHeader("Accept", "text/html,*/*");
    // Long-running series (One Piece, 1100+ chapters) exceed Qt's default
    // 10MB decompressed safety cap. Disable the cap for manga scrape responses.
    req.setDecompressedSafetyCheckThreshold(-1);
    if (isHtmx) {
        req.setRawHeader("HX-Request", "true");
        req.setRawHeader("HX-Target", "search-results");
    }
    return req;
}

// ── Search ──────────────────────────────────────────────────────────────────
void WeebCentralScraper::search(const QString& query, int /*limit*/)
{
    QUrl url(BASE + "/search/data");
    QUrlQuery q;
    q.addQueryItem("text", query);
    q.addQueryItem("sort", "Best Match");
    q.addQueryItem("order", "Descending");
    q.addQueryItem("official", "Any");
    q.addQueryItem("display_mode", "Full Display");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url, true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("WeebCentral search failed: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit searchFinished(parseSearchHtml(html));
    });
}

QList<MangaResult> WeebCentralScraper::parseSearchHtml(const QString& html)
{
    QList<MangaResult> results;

    // All <a> tags within a block
    static QRegularExpression anyLinkRe(
        R"re(<a\b([^>]*)>(.*?)</a>)re",
        QRegularExpression::DotMatchesEverythingOption);

    // Series href: /series/{ULID}/{slug} — accept either relative or absolute
    // (WeebCentral started emitting absolute URLs circa 2026-04).
    static QRegularExpression hrefRe(
        R"re(href="(?:https?://[^/"]+)?(/series/([^/"]+)/[^"]*)")re");

    // class="..." extractor (used on both article attrs and link attrs)
    static QRegularExpression classAttrRe(
        R"re(class="([^"]*)")re");

    static QRegularExpression imgSrcRe(
        R"re(<img\b[^>]*src="([^"]+)"[^>]*>)re");

    static QRegularExpression spanRe(
        R"re(<span\b[^>]*>(.*?)</span>)re",
        QRegularExpression::DotMatchesEverythingOption);

    static QRegularExpression stripTagsRe(R"re(<[^>]*>)re");

    // Depth-aware walk over <article> tags. WeebCentral nests inner <article>
    // tags (cover-lg + cover-mobile) inside each result card, so a non-greedy
    // regex would truncate the block at the first inner </article>. Walk
    // manually: at each <article, track depth until the matching close.
    int pos = 0;
    while (pos < html.length()) {
        int articleStart = html.indexOf(QLatin1String("<article"), pos);
        if (articleStart < 0) break;

        int attrEnd = html.indexOf(QLatin1Char('>'), articleStart);
        if (attrEnd < 0) break;

        QString articleAttrs = html.mid(articleStart + 8, attrEnd - (articleStart + 8));

        // Walk forward tracking depth to find the matching </article>.
        int depth = 1;
        int scan = attrEnd + 1;
        int blockEnd = -1;
        while (scan < html.length()) {
            int nextOpen  = html.indexOf(QLatin1String("<article"),  scan);
            int nextClose = html.indexOf(QLatin1String("</article>"), scan);
            if (nextClose < 0) break;
            if (nextOpen >= 0 && nextOpen < nextClose) {
                ++depth;
                scan = nextOpen + 8;
            } else {
                --depth;
                if (depth == 0) { blockEnd = nextClose; break; }
                scan = nextClose + 10;
            }
        }
        if (blockEnd < 0) break;

        int nextPos = blockEnd + 10;

        // Filter: must have both "flex" and "gap-4" classes in the opening tag
        auto ac = classAttrRe.match(articleAttrs);
        QString articleCls = ac.hasMatch() ? ac.captured(1) : QString();
        if (!articleCls.contains(QLatin1String("flex")) ||
            !articleCls.contains(QLatin1String("gap-4"))) {
            pos = nextPos;
            continue;
        }

        QString block = html.mid(attrEnd + 1, blockEnd - (attrEnd + 1));

        MangaResult r;
        r.source = "weebcentral";
        r.type = "manga";

        // Cover image
        auto im = imgSrcRe.match(block);
        if (im.hasMatch())
            r.thumbnailUrl = im.captured(1);

        // Scan all <a> links in this block
        auto linkMatches = anyLinkRe.globalMatch(block);
        while (linkMatches.hasNext()) {
            auto lm = linkMatches.next();
            QString attrs = lm.captured(1);
            QString inner = lm.captured(2);
            inner.remove(stripTagsRe);
            inner = inner.trimmed();

            // Series ID from first /series/{ULID}/... href
            auto hm = hrefRe.match(attrs);
            if (hm.hasMatch() && r.id.isEmpty()) {
                r.id  = hm.captured(2);
                r.url = hm.captured(1);
            }

            if (inner.isEmpty()) continue;

            auto lc = classAttrRe.match(attrs);
            QString linkCls = lc.hasMatch() ? lc.captured(1) : QString();

            // Title: link-hover WITHOUT link-info
            // Mirrors Groundwork: "link-hover" in cls and "link-info" not in cls
            if (linkCls.contains(QLatin1String("link-hover")) &&
                !linkCls.contains(QLatin1String("link-info")) &&
                r.title.isEmpty()) {
                r.title = inner;
            }

            // Author: BOTH link-info AND link-hover
            // Mirrors Groundwork: "link-info" in cls and "link-hover" in cls
            if (linkCls.contains(QLatin1String("link-info")) &&
                linkCls.contains(QLatin1String("link-hover"))) {
                r.author = inner;
            }
        }

        // Status from span text
        auto spanMatches = spanRe.globalMatch(block);
        while (spanMatches.hasNext()) {
            QString text = spanMatches.next().captured(1);
            text.remove(stripTagsRe);
            text = text.trimmed();
            if (text == QLatin1String("Ongoing") || text == QLatin1String("Completed") ||
                text == QLatin1String("Hiatus")  || text == QLatin1String("Cancelled")) {
                r.status = text;
                break;
            }
        }

        if (!r.title.isEmpty() && !r.id.isEmpty())
            results.append(r);

        pos = nextPos;
    }

    return results;
}

// ── Chapters ────────────────────────────────────────────────────────────────
void WeebCentralScraper::fetchChapters(const QString& seriesId)
{
    QUrl url(BASE + "/series/" + seriesId + "/full-chapter-list");

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch chapters: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit chaptersReady(parseChaptersHtml(html, "weebcentral"));
    });
}

QList<ChapterInfo> WeebCentralScraper::parseChaptersHtml(const QString& html, const QString& source)
{
    QList<ChapterInfo> chapters;

    // Pattern: <a href="/chapters/{ULID}">Chapter N</a>
    // Accept either relative or absolute URL (WeebCentral started emitting
    // absolute URLs circa 2026-04).
    static QRegularExpression chapterRe(
        R"RE(<a\s+href="(?:https?://[^/"]+)?/chapters/([^"]+)"[^>]*>\s*(.*?)\s*</a>)RE",
        QRegularExpression::DotMatchesEverythingOption);

    static QRegularExpression numRe(R"((\d+(?:\.\d+)?))", QRegularExpression::NoPatternOption);

    // Smoke 2026-05-15 P0-S2a: pre-fix the strip-tags regex `<[^>]*>` removed
    // tag delimiters but left ALL text nodes between tags intact. The chapter
    // anchor on weebcentral contains nested <span>name</span>, "Last Read"
    // badge text, an <svg> icon with embedded <style> CSS, and a <time> ISO
    // timestamp. All of those concatenated into ch.name, then leaked into
    // MangaDownloader's filename construction and produced the 0-byte
    // unreadable `Prologue 1\n...{ fill_ #d3d629; }...2024-09-07T17_04_15Z.cbz`
    // observed during smoke. Post-fix order-of-ops (per dotAll chapterRe):
    // (1) capture <time>...</time> ISO date into ch.dateUpload as ms-epoch;
    // (2) strip <svg> + <style> + <time> blocks entirely (kill the CSS + date
    // sources); (3) drop "Last Read" badge literal; (4) strip remaining tag
    // delimiters; (5) QString::simplified() to collapse whitespace + trim.
    static QRegularExpression svgBlockRe(
        QStringLiteral(R"RX(<svg\b[^>]*>.*?</svg>)RX"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression styleBlockRe(
        QStringLiteral(R"RX(<style\b[^>]*>.*?</style>)RX"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    // Capture group 1 = the `datetime` attribute value (always 3-digit
    // millisecond precision on weebcentral, e.g. "2025-09-11T15:08:10.666Z")
    // rather than the visible inner text (which is 6-digit microsecond
    // precision e.g. "2025-09-11T15:08:10.666085Z" — exceeds Qt's
    // Qt::ISODateWithMs parser's 3-digit ceiling and would silently parse
    // as invalid, leaving dateUpload=0 for every chapter). The full
    // <time>...</time> match still gets removed wholesale from the chapter
    // name by `rawInner.remove(timeBlockRe)` since remove() takes the
    // entire regex match, not just the capture group.
    static QRegularExpression timeBlockRe(
        QStringLiteral(R"RX(<time\b[^>]*\bdatetime="([^"]+)"[^>]*>.*?</time>)RX"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression chapterTagStripRe(QStringLiteral(R"RX(<[^>]*>)RX"));

    auto matches = chapterRe.globalMatch(html);
    while (matches.hasNext()) {
        auto m = matches.next();
        ChapterInfo ch;
        ch.id     = m.captured(1);
        ch.url    = "/chapters/" + ch.id;
        ch.source = source;

        QString rawInner = m.captured(2);

        // VOLUME_X_QUALITY 2026-05-28 (Agent 1). Violet tick = volume-scanned;
        // gray = magazine. Read BEFORE the <svg> block is stripped below.
        // Discriminator confirmed in agents/audits/weebcentral_volume_tick_markup_2026-05-28.md.
        ch.isVolumeScanned = rawInner.contains(QStringLiteral("stroke=\"#d8b4fe\""));

        // (1) Pull ISO date from <time>...</time> inner text into dateUpload.
        const auto timeMatch = timeBlockRe.match(rawInner);
        if (timeMatch.hasMatch()) {
            const QString isoText = timeMatch.captured(1).trimmed();
            const QDateTime dt = QDateTime::fromString(isoText, Qt::ISODateWithMs);
            if (dt.isValid()) ch.dateUpload = dt.toMSecsSinceEpoch();
        }

        // (2) Kill <svg> / <style> / <time> blocks with their inner content.
        rawInner.remove(svgBlockRe);
        rawInner.remove(styleBlockRe);
        rawInner.remove(timeBlockRe);

        // (3) Drop the "Last Read" badge label.
        rawInner.remove(QLatin1String("Last Read"));

        // (4) Strip remaining tag delimiters.
        rawInner.remove(chapterTagStripRe);

        // (5) Collapse whitespace + trim in one shot.
        ch.name = rawInner.simplified();

        // Extract chapter number from name
        auto nm = numRe.match(ch.name);
        if (nm.hasMatch())
            ch.chapterNumber = nm.captured(1).toDouble();

        if (!ch.id.isEmpty())
            chapters.append(ch);
    }

    // Sort ascending by chapter number
    std::sort(chapters.begin(), chapters.end(), [](const ChapterInfo& a, const ChapterInfo& b) {
        return a.chapterNumber < b.chapterNumber;
    });

    return chapters;
}

// ── Pages ───────────────────────────────────────────────────────────────────
void WeebCentralScraper::fetchPages(const QString& chapterId)
{
    QUrl url(BASE + "/chapters/" + chapterId + "/images");
    QUrlQuery q;
    q.addQueryItem("is_prev", "False");
    q.addQueryItem("current_page", "1");
    q.addQueryItem("reading_style", "long_strip");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch pages: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit pagesReady(parsePagesHtml(html));
    });
}

QList<PageInfo> WeebCentralScraper::parsePagesHtml(const QString& html)
{
    QList<PageInfo> pages;

    // The images endpoint returns ONLY reader pages (an htmx fragment, no header/footer chrome).
    // Modern WeebCentral binds each page via Alpine (`:src` / x-data), NOT a plain <img src>, so the
    // old `<img src="...">` regex matched nothing. Match any http(s) image URL directly instead —
    // also future-proof against CDN-host drift (planeptune, lowee, …). Dedup, preserve document order.
    static QRegularExpression imgRe(
        R"RE(https?://[^"'\s<>]+?\.(?:png|jpe?g|webp)(?:\?[^"'\s<>]*)?)RE",
        QRegularExpression::CaseInsensitiveOption);

    QSet<QString> seen;
    auto matches = imgRe.globalMatch(html);
    int idx = 0;
    while (matches.hasNext()) {
        const QString url = matches.next().captured(0);
        if (url.contains(QLatin1String("/broken_image."))) continue;
        if (seen.contains(url)) continue;
        seen.insert(url);
        PageInfo p;
        p.index    = idx++;
        p.imageUrl = url;
        pages.append(p);
    }

    return pages;
}

// ── Pages (MangaPlus paired) ──────────────────────────────────────────────
void WeebCentralScraper::fetchPagesPaired(const QString& chapterId)
{
    QUrl url(BASE + "/chapters/" + chapterId + "/images");
    QUrlQuery q;
    q.addQueryItem("is_prev", "False");
    q.addQueryItem("current_page", "1");
    // double_page_v2 = "Double Page (MangaPlus)": cover-alone + correct
    // right-to-left facing pairs, verified 2026-05-29.
    q.addQueryItem("reading_style", "double_page_v2");
    url.setQuery(q);

    auto* reply = m_nam->get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("Failed to fetch paired pages: " + reply->errorString());
            return;
        }
        auto html = QString::fromUtf8(reply->readAll());
        emit pagesReady(parsePagesPairedHtml(html));
    });
}

QList<PageInfo> WeebCentralScraper::parsePagesPairedHtml(const QString& html)
{
    QList<PageInfo> pages;

    // double_page_v2 markup: repeated `... page === N ...> <img src="...">`.
    // Each match is one image tagged with its facing-pair group N. Document
    // order is the visual left-to-right order WeebCentral renders.
    static const QRegularExpression groupImgRe(
        QStringLiteral(R"RE(page === (\d+)[^>]*>\s*<img\b[^>]*\bsrc="(https?://[^"]+\.(?:png|jpe?g|webp)(?:\?[^"]*)?)")RE"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    auto it = groupImgRe.globalMatch(html);
    int idx = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        const QString url = m.captured(2);
        if (url.contains(QLatin1String("/broken_image.")))
            continue;
        PageInfo p;
        p.index     = idx++;
        p.pageGroup = m.captured(1).toInt();
        p.imageUrl  = url;
        pages.append(p);
    }
    return pages;
}

// ── Detail (v1 merger) ──────────────────────────────────────────────────────
void WeebCentralScraper::fetchDetail(const MangaResult& preview)
{
    // preview.url is the relative "/series/{ULID}/{slug}" path captured by
    // parseSearchHtml's hrefRe (capture group 1 strips any absolute prefix).
    // Resolve against BASE so QNetworkRequest sees a fully-qualified https
    // URL — pre-fix this passed a schemeless QUrl and tripped Qt's
    // "Protocol \"\" is unknown" error on every detail open
    // (smoke 2026-05-15 P0-S1).
    const QUrl full = preview.url.startsWith(QLatin1String("http"))
                          ? QUrl(preview.url)
                          : QUrl(BASE).resolved(QUrl(preview.url));
    QNetworkRequest req = makeRequest(full, /*isHtmx=*/false);
    auto* reply = m_nam->get(req);
    QPointer<WeebCentralScraper> self(this);
    connect(reply, &QNetworkReply::finished, this, [reply, self, preview]() {
        reply->deleteLater();
        if (!self) return;
        if (reply->error() != QNetworkReply::NoError) {
            emit self->errorOccurred(QString("weebcentral fetchDetail: %1")
                                     .arg(reply->errorString()));
            return;
        }
        const QString html = QString::fromUtf8(reply->readAll());

        static QRegularExpression stripTagsRe(R"re(<[^>]*>)re");
        auto stripTags = [](QString s) {
            s.remove(stripTagsRe);
            return s.trimmed();
        };

        MangaSeriesDetail detail;
        detail.preview     = preview;
        detail.sourceUrl   = preview.url;

        // Live-HTML-validated selectors (Playwright pre-flight 2026-05-15
        // ~10:48am against https://weebcentral.com/series/01J76XY7EF75DJNQCV04HTPDZK/Berserk).
        // The prior pinned-at-plan-author-time selectors (which the file
        // formerly flagged as TODO(smoke-verify)) all missed: synopsis used
        // a non-existent `<li class="description">`, genres used a
        // non-existent `/genres/` href shape (WeebCentral uses Tags(s) via
        // `?included_tag=NAME`), Year used a non-existent "Year" label
        // (WeebCentral uses "Released:"), Hero used a non-existent `hero`
        // class. Only Status happened to match by structural coincidence.
        //
        // Synopsis: <strong>Description</strong><p class="...">TEXT</p>
        static QRegularExpression kSynopsis(
            R"RX(<strong>Description</strong>\s*<p[^>]*>([\s\S]*?)</p>)RX");
        auto sm = kSynopsis.match(html);
        if (sm.hasMatch()) detail.synopsis = stripTags(sm.captured(1));

        // Genres: WeebCentral exposes them as "Tags" via <a href="...?included_tag=NAME">NAME</a>.
        // Project them into MangaSeriesDetail::genres so downstream UI doesn't
        // need to know the source's local naming.
        static QRegularExpression kGenre(
            R"RX(<a[^>]+href="[^"]*\?included_tag=[^"]*"[^>]*>([^<]+)</a>)RX");
        auto gi = kGenre.globalMatch(html);
        while (gi.hasNext()) {
            const auto m = gi.next();
            detail.genres.append(m.captured(1).trimmed());
        }

        // Year: <strong>Released:</strong> <span>YYYY</span>. The "Released"
        // label is WeebCentral's term for first-publication year.
        static QRegularExpression kYear(
            R"RX(Released:[^<]*</strong>\s*<span>(\d{4})</span>)RX");
        auto ym = kYear.match(html);
        if (ym.hasMatch()) detail.year = ym.captured(1);

        // Status: <strong>Status:</strong> <a ...>Ongoing</a> — the existing
        // loose `Status[^<]*<[^>]*>\s*<[^>]*>([^<]+)<` shape coincidentally
        // walks `Status: ` → `</strong>` → whitespace → `<a ...>` → `Ongoing<`
        // correctly. Kept as-is per Playwright validation.
        static QRegularExpression kStatus(
            R"RX(Status[^<]*<[^>]*>\s*<[^>]*>([^<]+)<)RX");
        auto stm = kStatus.match(html);
        if (stm.hasMatch()) detail.status = stm.captured(1).trimmed();

        // Hero cover: first <img src="...temp.compsci88.com/cover/..."> on
        // the page is the series cover by document order (the page renders
        // its own cover first, then thumbnail strips for related series).
        // No `hero` class exists in WeebCentral's markup, so anchor on the
        // host+path pattern. Fall back to preview.thumbnailUrl on miss.
        static QRegularExpression kHero(
            R"RX(<img[^>]+src="([^"]*compsci88\.com/cover/[^"]+)")RX");
        auto hm = kHero.match(html);
        detail.heroCoverUrl = hm.hasMatch() ? hm.captured(1) : preview.thumbnailUrl;

        // cachedChapters NOT populated here — WeebCentral chapter list is its own
        // endpoint (the existing fetchChapters call). Leave empty.

        emit self->detailReady(detail);
    });
}
