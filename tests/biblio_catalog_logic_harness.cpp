// tests/biblio_catalog_logic_harness.cpp
//
// Pure-logic oracle for the BiblioCatalog foundation (Discover/Explore Task 1):
// the controlled taxonomy/facet mapper (spec 6.3 / 4.3) and the deterministic
// ranking functions (spec 7). No networking, no SQL, no QML — every function
// under test is a pure function of its inputs, so this harness is a reliable
// RED/GREEN oracle. Prints BIBLIO_CATALOG_LOGIC_OK and returns 0 only when every
// require() passed.
#include "engine/BiblioCatalogTypes.h"
#include "engine/BiblioTaxonomy.h"
#include "engine/BiblioRanking.h"
#include "engine/BiblioProviders.h"
#include "engine/BiblioCanonicalizer.h"
#include "engine/BiblioArtworkUrl.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <QVector>

#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QStringList ids(const QVector<BiblioWork> &works)
{
    QStringList out;
    for (const BiblioWork &w : works)
        out << w.canonicalId;
    return out;
}

QStringList facetKeys(const QString &axis)
{
    for (const BiblioFilterGroup &g : BiblioTaxonomy::filterGroups()) {
        if (g.axis == axis) {
            QStringList keys;
            for (const BiblioFacet &f : g.facets)
                keys << f.key;
            return keys;
        }
    }
    return {};
}

bool hasAxis(const QString &axis)
{
    for (const BiblioFilterGroup &g : BiblioTaxonomy::filterGroups())
        if (g.axis == axis)
            return true;
    return false;
}

QDateTime utc(int y, int m, int d)
{
    return QDateTime(QDate(y, m, d), QTime(12, 0, 0), QTimeZone::utc());
}

BiblioWork work(const QString &id)
{
    BiblioWork w;
    w.canonicalId = id;
    return w;
}

BiblioRankSnapshot snap(const QString &id, const QDateTime &at, double demand)
{
    BiblioRankSnapshot s;
    s.canonicalId = id;
    s.capturedAt = at;
    s.demandScore = demand;
    return s;
}

// ── Task 2 helpers ─────────────────────────────────────────────────────────

// Read a Task-2 provider fixture as raw bytes. BIBLIO_FIXTURES_DIR is injected by
// CMake so the harness finds the file regardless of the working directory.
QByteArray fixture(const QString &name)
{
    const QString path = QStringLiteral(BIBLIO_FIXTURES_DIR) + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: cannot open fixture " << path.toStdString() << '\n';
        std::exit(1);
    }
    return f.readAll();
}

const BiblioCanonicalWork *findById(const QList<BiblioCanonicalWork> &works, const QString &id)
{
    for (const BiblioCanonicalWork &cw : works)
        if (cw.work.canonicalId == id)
            return &cw;
    return nullptr;
}

// Read a canonical work by its canonical id and hand back the BiblioWork so the
// `.editions` / `.originalLanguage` assertions read cleanly (the plan's
// `find(works, "<label>")`).
const BiblioWork &find(const QList<BiblioCanonicalWork> &works, const QString &id)
{
    const BiblioCanonicalWork *cw = findById(works, id);
    require(cw != nullptr, "expected a canonical work with the given id");
    return cw->work;
}

QStringList editionFormats(const BiblioWork &w)
{
    QStringList f;
    for (const BiblioEdition &e : w.editions)
        f << e.format;
    f.sort();
    return f;
}

bool hasEdition(const BiblioWork &w, const QString &format, const QString &language)
{
    for (const BiblioEdition &e : w.editions)
        if (e.format == format && e.language == language)
            return true;
    return false;
}

QString provSource(const BiblioCanonicalWork &cw, const QString &field)
{
    for (const BiblioFieldSource &fs : cw.fieldSources)
        if (fs.field == field)
            return fs.source;
    return QString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── Apple artwork URL normalization (2026-08-06 shelf-quality fix) ──
    // Real RSS-feed URL from tonight's live cache (biblio-v1.sqlite): Apple's OWN feed
    // ships this exact "0x170bb.png" shape, and Apple's CDN rejects it live with
    // {"errorMessage":"Cannot produce 0x170 image with Resize Style: 'bb'"} — verified
    // by curl against production just now, not simulated. Every blank Discover cover
    // tonight had a coverUrl in this broken shape.
    require(normalizedAppleArtworkUrl(
                "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/39/64/45/"
                "3964459f-f5f3-d0b9-8623-d792b4ee584d/9781954118829.jpg/0x170bb.png")
            == "https://is1-ssl.mzstatic.com/image/thumb/Publication211/v4/39/64/45/"
               "3964459f-f5f3-d0b9-8623-d792b4ee584d/9781954118829.jpg/600x600bb.jpg",
            "broken RSS 0xNbb.png shape rewritten to a working 600x600bb.jpg");
    // Real Search-API URL (also from tonight's cache) — VALID but small (100px); rewritten
    // up to 600 so a real cover isn't stretched blurry in the gallery grid.
    require(normalizedAppleArtworkUrl(
                "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/e1/41/af/"
                "e141af03-f50a-dcd5-31f5-bc8fc4bb1db1/1026292563.jpg/100x100bb.jpg")
            == "https://is1-ssl.mzstatic.com/image/thumb/Publication221/v4/e1/41/af/"
               "e141af03-f50a-dcd5-31f5-bc8fc4bb1db1/1026292563.jpg/600x600bb.jpg",
            "valid-but-small 100x100bb.jpg upgraded to 600x600bb.jpg");
    require(normalizedAppleArtworkUrl(
                "https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/60x60bb.jpg")
            == "https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/600x600bb.jpg",
            "60x60bb.jpg (artworkUrl60 fallback shape) also upgraded to 600x600bb.jpg");
    // Fail-safe: anything that doesn't match the expected trailing shape is untouched —
    // Open Library covers, an already-600 URL, or a future unknown Apple CDN change.
    require(normalizedAppleArtworkUrl("https://covers.openlibrary.org/b/id/12345-L.jpg")
            == "https://covers.openlibrary.org/b/id/12345-L.jpg",
            "a non-Apple-thumb URL (Open Library) passes through untouched");
    require(normalizedAppleArtworkUrl(
                "https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/600x600bb.jpg")
            == "https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/600x600bb.jpg",
            "an already-600x600bb.jpg URL is idempotent (unchanged)");
    require(normalizedAppleArtworkUrl(QString()).isEmpty(),
            "an empty URL never crashes, passes through as empty");

    // ── Length buckets (spec 4.3): Short <200, Standard 200-499, Long 500-799, Epic 800+ ──
    require(BiblioTaxonomy::lengthKey(199) == "short", "199 pages is Short");
    require(BiblioTaxonomy::lengthKey(200) == "standard", "200 pages is Standard");
    require(BiblioTaxonomy::lengthKey(499) == "standard", "499 pages is Standard");
    require(BiblioTaxonomy::lengthKey(500) == "long", "500 pages is Long");
    require(BiblioTaxonomy::lengthKey(799) == "long", "799 pages is Long");
    require(BiblioTaxonomy::lengthKey(800) == "epic", "800 pages is Epic");
    require(BiblioTaxonomy::lengthKey(1) == "short", "1 page is Short");
    require(BiblioTaxonomy::lengthKey(1200) == "epic", "1200 pages is Epic");
    // Works without reliable pagination do NOT enter a Length result.
    require(BiblioTaxonomy::lengthKey(0).isEmpty(), "0 pages has no Length bucket");
    require(BiblioTaxonomy::lengthKey(-5).isEmpty(), "negative pages has no Length bucket");

    // ── Publication era (spec 4.3): all seven buckets, tested at every boundary ──
    require(BiblioTaxonomy::eraKey(1600) == "before-1900", "1600 is Before 1900");
    require(BiblioTaxonomy::eraKey(1899) == "before-1900", "1899 era boundary");
    require(BiblioTaxonomy::eraKey(1900) == "1900-1949", "1900 era boundary");
    require(BiblioTaxonomy::eraKey(1949) == "1900-1949", "1949 era boundary");
    require(BiblioTaxonomy::eraKey(1950) == "1950-1979", "1950 era boundary");
    require(BiblioTaxonomy::eraKey(1979) == "1950-1979", "1979 era boundary");
    require(BiblioTaxonomy::eraKey(1980) == "1980-1999", "1980 era boundary");
    require(BiblioTaxonomy::eraKey(1999) == "1980-1999", "1999 era boundary");
    require(BiblioTaxonomy::eraKey(2000) == "2000-2009", "2000 era boundary");
    require(BiblioTaxonomy::eraKey(2009) == "2000-2009", "2009 era boundary");
    require(BiblioTaxonomy::eraKey(2010) == "2010-2019", "2010 era boundary");
    require(BiblioTaxonomy::eraKey(2019) == "2010-2019", "2019 era boundary");
    require(BiblioTaxonomy::eraKey(2020) == "2020-present", "2020 era boundary");
    require(BiblioTaxonomy::eraKey(2026) == "2020-present", "2026 is 2020-Present");
    // Unknown/unreliable year does NOT enter an Era result.
    require(BiblioTaxonomy::eraKey(0).isEmpty(), "unknown year has no Era bucket");

    // ── Audience (spec 2.8 / 4.3): Adult is a READERSHIP category, not content rating ──
    require(BiblioTaxonomy::normalize("audience", "Adult") == "adult", "Adult is an audience key");
    require(BiblioTaxonomy::normalize("audience", "YA") == "young-adult", "YA collapses to young-adult");
    require(BiblioTaxonomy::normalize("audience", "Teens") == "young-adult", "Teens collapses to young-adult");
    require(BiblioTaxonomy::normalize("audience", "Middle Grade") == "middle-grade", "Middle Grade key");
    require(BiblioTaxonomy::normalize("audience", "Kids") == "children", "Kids collapses to children");

    // ── Original language (spec 4.3): English vs Translated ──
    require(BiblioTaxonomy::languageKey("English", true) == "english", "English original is english");
    require(BiblioTaxonomy::languageKey("English", false) == "english", "English original stays english");
    require(BiblioTaxonomy::languageKey("French", true) == "translated",
            "non-English original with an English edition is translated");
    require(BiblioTaxonomy::languageKey("Japanese", true) == "translated", "Japanese + English edition is translated");
    require(BiblioTaxonomy::languageKey("French", false).isEmpty(),
            "non-English original with no English edition is outside the English catalogue");

    // ── Controlled vocabulary (spec 6.3): synonym/case/plural collapse, unknown suppression ──
    require(BiblioTaxonomy::normalize("genre", "Sci-Fi") == "science-fiction", "genre alias Sci-Fi");
    require(BiblioTaxonomy::normalize("genre", "Science fiction") == "science-fiction",
            "genre alias 'Science fiction' collapses with 'Sci-Fi'");
    require(BiblioTaxonomy::normalize("genre", "  SCIENCE-FICTION  ") == "science-fiction",
            "genre normalize trims and lowercases");
    require(BiblioTaxonomy::normalize("genre", "Mysteries") == "mystery", "plural collapses to singular");
    require(BiblioTaxonomy::normalize("theme", "Unreviewed random tag").isEmpty(), "unknown tags stay hidden");
    require(BiblioTaxonomy::normalize("genre", "Totally Made Up Genre").isEmpty(),
            "unknown genre never becomes a filter");
    require(BiblioTaxonomy::normalize("planet", "Mars").isEmpty(), "unknown axis yields no key");

    // ── filterGroups() advertises exactly the keys the helpers/mapper produce ──
    require(facetKeys("length").contains(BiblioTaxonomy::lengthKey(199)), "length group advertises 'short'");
    require(facetKeys("length").contains(BiblioTaxonomy::lengthKey(800)), "length group advertises 'epic'");
    require(facetKeys("era").contains(BiblioTaxonomy::eraKey(2019)), "era group advertises '2010-2019'");
    require(facetKeys("era").contains(BiblioTaxonomy::eraKey(1899)), "era group advertises 'before-1900'");
    require(facetKeys("era").contains(BiblioTaxonomy::eraKey(2020)), "era group advertises '2020-present'");
    require(facetKeys("audience").contains(BiblioTaxonomy::normalize("audience", "Adult")),
            "audience group advertises 'adult'");
    require(facetKeys("language").contains(BiblioTaxonomy::languageKey("English", true)),
            "language group advertises 'english'");
    require(facetKeys("language").contains(BiblioTaxonomy::languageKey("French", true)),
            "language group advertises 'translated'");
    require(facetKeys("genre").contains(BiblioTaxonomy::normalize("genre", "Sci-Fi")),
            "genre group advertises 'science-fiction'");
    // Publisher is a real axis but its values are data-derived (curatedPublishers
    // applies the coverage floor), so filterGroups() advertises it present-but-empty.
    require(hasAxis("publisher"), "filterGroups advertises the publisher axis");
    require(facetKeys("publisher").isEmpty(), "publisher axis carries no static values (data-derived)");

    // ── Curated publisher floor (spec 4.3): imprints fold under parents; a publisher is
    //    admitted only after >= kPublisherCoverageFloor canonical works map to it ──
    require(BiblioTaxonomy::kPublisherCoverageFloor == 25, "coverage floor is 25");
    require(BiblioTaxonomy::normalize("publisher", "Vintage") == "penguin-random-house",
            "imprint Vintage folds under Penguin Random House");
    require(BiblioTaxonomy::normalize("publisher", "Tor Books") == "macmillan",
            "imprint Tor Books folds under Macmillan");
    {
        QList<BiblioWork> pub;
        const QStringList prhImprints{"Vintage", "Penguin Classics", "Alfred A. Knopf"};
        for (int i = 0; i < 25; ++i) {
            BiblioWork w = work(QStringLiteral("prh-%1").arg(i));
            w.publisher = prhImprints.at(i % prhImprints.size()); // 25 works -> penguin-random-house
            pub << w;
        }
        for (int i = 0; i < 24; ++i) {
            BiblioWork w = work(QStringLiteral("mac-%1").arg(i));
            w.publisher = QStringLiteral("Tor Books"); // 24 works -> macmillan (below floor)
            pub << w;
        }
        for (int i = 0; i < 5; ++i) {
            BiblioWork w = work(QStringLiteral("indie-%1").arg(i));
            w.publisher = QStringLiteral("Some Unknown Indie Press"); // unknown -> never counted
            pub << w;
        }
        const QStringList curated = BiblioTaxonomy::curatedPublishers(pub);
        require(curated.contains("penguin-random-house"), "25 mapped works admit Penguin Random House");
        require(!curated.contains("macmillan"), "24 mapped works fall below the coverage floor");
        require(curated.size() == 1, "only the publisher clearing the floor is admitted");
    }

    const QDateTime now = utc(2026, 7, 31);

    // ── rank() only serves the four known shelves; anything else is empty ──
    require(BiblioRanking::rank("wishlist", {}, {}, now).isEmpty(), "unknown shelf id returns empty");
    require(BiblioRanking::rank("", {}, {}, now).isEmpty(), "empty shelf id returns empty");

    // ── Top Rated (spec 7): Bayesian shrink toward a population prior so a handful of
    //    perfect ratings cannot dominate a broadly-loved 4.7. ──
    {
        BiblioWork broad = work(QStringLiteral("broad-4.7"));
        broad.rating = {4.7, 5000};
        BiblioWork tiny = work(QStringLiteral("tiny-5.0"));
        tiny.rating = {5.0, 3};
        BiblioWork weak = work(QStringLiteral("weak-3.6"));
        weak.rating = {3.6, 4000};
        BiblioWork unrated = work(QStringLiteral("unrated-0"));
        unrated.rating = {0.0, 0}; // no rating evidence -> excluded from Top Rated

        const QList<BiblioWork> works{tiny, weak, broad, unrated}; // deliberately unsorted input
        const QVector<BiblioWork> ranked = BiblioRanking::rank("top-rated", works, {}, now);
        require(ids(ranked) == (QStringList{"broad-4.7", "tiny-5.0", "weak-3.6"}),
                "confidence weighting: broad 4.7 outranks tiny 5.0; unrated excluded");
        require(ids(ranked).indexOf("broad-4.7") < ids(ranked).indexOf("tiny-5.0"),
                "a broadly-rated 4.7 beats a 5.0 with a tiny vote count");
        require(!ids(ranked).contains("unrated-0"), "a work with no ratings is not Top Rated");
    }

    // ── Popular (spec 7): blends Apple chart + rating volume + Open Library popularity;
    //    must NOT be a copy of the Apple Top 10, and missing signals drop only that signal. ──
    {
        BiblioWork appleDarling = work(QStringLiteral("apple-darling"));
        appleDarling.appleChartScore = 100.0; // Apple #1
        appleDarling.rating = {4.0, 50};
        appleDarling.openLibraryPopularity = 5.0;
        BiblioWork crowdFavorite = work(QStringLiteral("crowd-favorite"));
        crowdFavorite.appleChartScore = 60.0; // only #2 on Apple
        crowdFavorite.rating = {4.0, 100000};
        crowdFavorite.openLibraryPopularity = 100.0;
        BiblioWork olOnly = work(QStringLiteral("ol-only"));
        olOnly.appleChartScore = 0.0; // absent from Apple chart AND has zero ratings...
        olOnly.rating = {0.0, 0};
        olOnly.openLibraryPopularity = 80.0; // ...but strong Open Library evidence

        const QList<BiblioWork> works{appleDarling, crowdFavorite, olOnly};
        const QVector<BiblioWork> ranked = BiblioRanking::rank("popular", works, {}, now);
        require(ids(ranked) == (QStringList{"crowd-favorite", "apple-darling", "ol-only"}),
                "Popular blends signals rather than echoing the Apple chart");
        require(ranked.first().canonicalId != "apple-darling", "Popular is not a copy of the Apple Top 10");
        require(ids(ranked).contains("ol-only"),
                "a work missing Apple + rating signals is still ranked on its Open Library signal");
    }

    // ── New Releases (spec 7): canonical works first published in the trailing 12 months;
    //    reprints / new covers / ebook / audiobook releases do NOT reset eligibility. ──
    {
        BiblioWork fresh30 = work(QStringLiteral("fresh-30d"));
        fresh30.canonicalFirstPublished = QDate(2026, 7, 1);
        BiblioWork fresh2mo = work(QStringLiteral("fresh-2mo"));
        fresh2mo.canonicalFirstPublished = QDate(2026, 5, 31);
        BiblioWork edge = work(QStringLiteral("edge-1yr"));
        edge.canonicalFirstPublished = QDate(2025, 7, 31); // exactly the window start -> included
        BiblioWork justOver = work(QStringLiteral("just-over"));
        justOver.canonicalFirstPublished = QDate(2025, 7, 30); // one day too old -> excluded
        BiblioWork ancient = work(QStringLiteral("ancient"));
        ancient.canonicalFirstPublished = QDate(2020, 1, 1);
        BiblioWork future = work(QStringLiteral("future"));
        future.canonicalFirstPublished = QDate(2026, 8, 15); // not published yet -> excluded
        BiblioWork noDate = work(QStringLiteral("no-date"));
        noDate.canonicalFirstPublished = QDate(); // unreliable date -> excluded
        BiblioWork reprint = work(QStringLiteral("old-classic"));
        reprint.canonicalFirstPublished = QDate(1979, 4, 1); // first published long ago
        BiblioEdition audio; // a brand-new audiobook edition must NOT reset eligibility
        audio.editionId = QStringLiteral("old-classic-audio");
        audio.format = QStringLiteral("audiobook");
        audio.published = QDate(2026, 7, 20);
        reprint.editions << audio;

        const QList<BiblioWork> works{ancient, fresh2mo, future, edge, noDate, fresh30, justOver, reprint};
        const QVector<BiblioWork> ranked = BiblioRanking::rank("new-releases", works, {}, now);
        require(ids(ranked) == (QStringList{"fresh-30d", "fresh-2mo", "edge-1yr"}),
                "New Releases = canonical works first published in the trailing 12 months, newest first");
        require(!ids(ranked).contains("old-classic"), "a fresh audiobook edition does not reset first-publication");
        require(!ids(ranked).contains("just-over"), "one day past the 12-month window is excluded");
        require(!ids(ranked).contains("future"), "a not-yet-published work is not a New Release");
    }

    // ── Trending (spec 7): seven-day momentum from dated snapshots; needs two snapshots
    //    >= 6 days apart; empty when momentum cannot be honestly computed; never aliases Popular. ──
    {
        BiblioWork steady = work(QStringLiteral("steady-giant"));
        steady.appleChartScore = 100.0;
        steady.rating = {4.5, 100000};
        steady.openLibraryPopularity = 100.0;
        BiblioWork rising = work(QStringLiteral("rising-star"));
        rising.appleChartScore = 30.0;
        rising.rating = {4.6, 1000};
        rising.openLibraryPopularity = 20.0;
        const QList<BiblioWork> works{steady, rising};

        // Popular ranks the entrenched giant first.
        require(ids(BiblioRanking::rank("popular", works, {}, now))
                    == (QStringList{"steady-giant", "rising-star"}),
                "Popular ranks the entrenched giant first");

        // 7-day window: rising-star gains +50, steady-giant only +1 -> Trending flips the order.
        const QList<BiblioRankSnapshot> history{
            snap("steady-giant", utc(2026, 7, 24), 100.0),
            snap("steady-giant", now, 101.0),
            snap("rising-star", utc(2026, 7, 24), 10.0),
            snap("rising-star", now, 60.0),
        };
        require(ids(BiblioRanking::rank("trending", works, history, now))
                    == (QStringList{"rising-star", "steady-giant"}),
                "Trending orders by momentum, distinct from Popular");

        // No history at all -> empty, and crucially NOT the Popular ordering.
        const QVector<BiblioWork> noHistory = BiblioRanking::rank("trending", works, {}, now);
        require(noHistory.isEmpty(), "Trending is empty when there is no snapshot history");
        require(ids(noHistory) != ids(BiblioRanking::rank("popular", works, {}, now)),
                "insufficient history must not alias Popular");

        // Snapshots < 6 days apart -> cannot honestly compute momentum -> empty.
        const QList<BiblioRankSnapshot> tooClose{
            snap("rising-star", utc(2026, 7, 28), 10.0),
            snap("rising-star", now, 60.0), // 3 days apart
        };
        require(BiblioRanking::rank("trending", works, tooClose, now).isEmpty(),
                "snapshots closer than six days cannot compute momentum");

        // Exactly six days apart -> eligible.
        const QList<BiblioRankSnapshot> sixDays{
            snap("rising-star", utc(2026, 7, 25), 10.0),
            snap("rising-star", now, 40.0), // exactly 6 days apart
        };
        require(ids(BiblioRanking::rank("trending", works, sixDays, now)) == (QStringList{"rising-star"}),
                "exactly six days apart is enough to trend");

        // Spanning >= 6 days but flat demand -> not rising -> excluded.
        const QList<BiblioRankSnapshot> flat{
            snap("rising-star", utc(2026, 7, 24), 50.0),
            snap("rising-star", now, 50.0), // no movement
        };
        require(BiblioRanking::rank("trending", works, flat, now).isEmpty(),
                "a work with flat momentum is not Trending");
    }

    // ════════════════════════════════════════════════════════════════════════
    // Task 2 — provider parsing (spec 6.1) and canonical work identity (spec 6.2)
    // ════════════════════════════════════════════════════════════════════════
    {
        const QDateTime observed = utc(2026, 7, 30);

        // ── Apple RSS: array form, chart score, artwork, defensive gaps ──
        const QList<BiblioSourceRecord> rss =
            BiblioProviders::parseAppleRss(fixture("apple-rss.json"), observed);
        require(rss.size() == 2, "Apple RSS array form yields one record per entry");
        require(rss.first().source == "apple", "Apple RSS records are sourced to apple");
        require(rss.first().title == "Shared ISBN Trio", "Apple RSS reads im:name as the title");
        require(rss.first().author == "Nora Vance", "Apple RSS reads im:artist as the author");
        require(rss.first().format == "ebook", "top-ebooks RSS entries are ebook-format");
        require(!rss.first().artworkUrl.isEmpty(), "Apple RSS keeps the artwork url");
        require(rss.first().appleChartScore > rss.at(1).appleChartScore, "chart rank 1 outscores rank 2");
        require(rss.at(1).appleChartScore > 0.0, "every charting entry carries a positive chart score");
        require(rss.at(1).artworkUrl.isEmpty(), "a chart entry with no im:image parses with empty artwork");
        require(rss.first().observedAt == observed, "Apple RSS stamps the observedAt it was given");

        // ── Apple RSS singleton form: feed.entry is a single object, not an array ──
        const QByteArray singleton =
            QByteArrayLiteral("{\"feed\":{\"entry\":{\"im:name\":{\"label\":\"Solo Chart Book\"},"
                              "\"im:artist\":{\"label\":\"One Author\"},"
                              "\"id\":{\"attributes\":{\"im:id\":\"42\"}}}}}");
        require(BiblioProviders::parseAppleRss(singleton).size() == 1,
                "Apple RSS singleton entry object parses as one record");

        // ── Defensive: empty / garbage bytes never crash and yield nothing ──
        require(BiblioProviders::parseAppleRss(QByteArray()).isEmpty(), "empty RSS bytes yield no records");
        require(BiblioProviders::parseAppleRss("not json{").isEmpty(), "garbage RSS bytes yield no records");

        // ── Apple Search: rating, HTML strip, audiobook form, skips junk ──
        const QList<BiblioSourceRecord> search =
            BiblioProviders::parseAppleSearch(fixture("apple-search.json"), observed);
        require(search.size() == 4, "the malformed (title-less) Apple result is skipped");
        const BiblioSourceRecord *ebook1 = nullptr, *audio1 = nullptr, *frost = nullptr;
        for (const BiblioSourceRecord &r : search) {
            if (r.title == "Shared ISBN Trio" && r.format == "ebook") ebook1 = &r;
            if (r.title.startsWith("Shared ISBN Trio") && r.format == "audiobook") audio1 = &r;
            if (r.author == "Julian Frost") frost = &r;
        }
        require(ebook1 && ebook1->hasRating, "an Apple search ebook carries a storefront rating");
        require(qFuzzyCompare(ebook1->rating.average, 4.5) && ebook1->rating.count == 1200,
                "the rating value and count are preserved");
        require(!ebook1->description.contains('<') && !ebook1->description.contains('>'),
                "HTML tags are stripped from the description");
        require(ebook1->description.contains('&') && !ebook1->description.contains("&amp;"),
                "HTML entities are decoded (a bare & not &amp;)");
        require(audio1 && audio1->format == "audiobook", "an audiobook wrapper becomes an audiobook-format record");
        require(frost && !frost->hasRating, "a result with no rating fields is defensively rating-less");
        require(BiblioProviders::parseAppleSearch(QByteArray()).isEmpty(), "empty search bytes yield no records");

        // ── Open Library: work key, ISBNs, first-publish, pages, language ──
        const QList<BiblioSourceRecord> ol =
            BiblioProviders::parseOpenLibrarySearch(fixture("openlibrary-search.json"), observed);
        require(ol.size() == 6, "one Open Library record per search doc");
        const BiblioSourceRecord *olPrint = nullptr, *olFrench = nullptr;
        for (const BiblioSourceRecord &r : ol) {
            if (r.workKey == "/works/OL1001W") olPrint = &r;
            if (r.workKey == "/works/OL2002W" && r.language == "fr") olFrench = &r;
        }
        require(olPrint, "the Open Library work key is parsed");
        require(olPrint->source == "openlibrary", "Open Library records are sourced to openlibrary");
        require(olPrint->isbns.contains("9780000000101"), "Open Library ISBNs are parsed");
        require(olPrint->firstPublishYear == 2019, "Open Library first_publish_year is parsed");
        require(olPrint->pageCount == 352, "Open Library median page count is parsed");
        require(olPrint->language == "en", "Open Library 'eng' is normalized to 'en'");
        require(olPrint->format == "print", "Open Library records default to print editions");
        require(olPrint->englishReadable, "an English Open Library edition is English-readable");
        require(olPrint->openLibraryPopularity > 0.0, "readinglog_count becomes an Open Library popularity signal");
        require(olFrench && olFrench->language == "fr", "Open Library 'fre' is normalized to 'fr'");
        require(olFrench->description.indexOf('<') < 0, "HTML is stripped from an object-form OL description");
        require(BiblioProviders::parseOpenLibrarySearch("garbage").isEmpty(), "garbage OL bytes yield no records");

        // ── Keyless URL builders: right endpoints, encoded terms, never a key ──
        const QString rssUrl = BiblioProviders::appleTopEbooksRssUrl("us", 100, 0).toString();
        require(rssUrl.contains("itunes.apple.com") && rssUrl.contains("topebooks"),
                "the Apple RSS url targets the top-ebooks feed");
        const QString genreUrl = BiblioProviders::appleTopEbooksRssUrl("us", 50, 9031).toString();
        require(genreUrl.contains("genre=9031"), "a genre id selects the genre chart");
        const QString searchUrl = BiblioProviders::appleSearchUrl("dune & sand", "ebook").toString(QUrl::FullyEncoded);
        require(searchUrl.contains("itunes.apple.com/search") && searchUrl.contains("media=ebook"),
                "the Apple search url is a keyless media=ebook query");
        require(!searchUrl.contains(' '), "the Apple search term is URL-encoded");
        const QString olUrl = BiblioProviders::openLibrarySearchUrl("song of the deep", "camille rousseau").toString();
        require(olUrl.contains("openlibrary.org/search.json"), "the Open Library url targets search.json");
        require(!rssUrl.contains("key") && !searchUrl.contains("key") && !olUrl.contains("key")
                    && !searchUrl.contains("token"),
                "no provider url carries an api key or token");

        // ── Canonicalization (spec 6.2): merge every record into canonical works ──
        QList<BiblioSourceRecord> records;
        records += rss;
        records += search;
        records += ol;
        const QList<BiblioCanonicalWork> works = BiblioCanonicalizer::merge(records);

        require(works.size() == 4, "ordinary formats merge without title-only collisions");
        require(BiblioCanonicalizer::merge({}).isEmpty(), "merging nothing yields nothing");

        // Work 1 — a shared ISBN across sources nests ebook + print + audio as one work.
        const BiblioWork &sharedIsbn = find(works, "ol1001w");
        require(sharedIsbn.editions.size() == 3, "ebook, print, and audio nest");
        require(editionFormats(sharedIsbn) == (QStringList{"audiobook", "ebook", "print"}),
                "the three nested editions are audiobook, ebook and print");
        require(sharedIsbn.title == "Shared ISBN Trio", "canonical title from the representative English edition");
        require(sharedIsbn.author == "Nora Vance", "the card carries the representative primary author (author-at-rest)");
        require(!sharedIsbn.coverUrl.isEmpty(), "the card carries cover art");
        require(sharedIsbn.canonicalFirstPublished == QDate(2019, 1, 1),
                "first-publication is Open Library's, not Apple's later release date");
        require(qFuzzyCompare(sharedIsbn.rating.average, 4.5) && sharedIsbn.rating.count == 1200,
                "Apple owns the storefront rating on the merged work");
        require(sharedIsbn.appleChartScore > 0.0, "Apple owns the chart score on the merged work");
        require(sharedIsbn.openLibraryPopularity > 0.0, "Open Library owns the popularity signal on the merged work");
        require(sharedIsbn.originalLanguage == "en", "an English-native work has original language en");

        // Provenance: each field remembers who set it (spec 6.1 ownership).
        const BiblioCanonicalWork *sharedCw = findById(works, "ol1001w");
        require(provSource(*sharedCw, "rating") == "apple", "rating provenance is Apple");
        require(provSource(*sharedCw, "appleChartScore") == "apple", "chart provenance is Apple");
        require(provSource(*sharedCw, "firstPublished") == "openlibrary", "first-publish provenance is Open Library");
        require(provSource(*sharedCw, "title") == "openlibrary", "identity/title provenance is Open Library");
        require(provSource(*sharedCw, "coverUrl") == "apple", "cover art provenance is Apple (Apple owns artwork)");
        require(provSource(*sharedCw, "openLibraryPopularity") == "openlibrary", "popularity provenance is Open Library");
        for (const BiblioFieldSource &fs : sharedCw->fieldSources)
            require(fs.observedAt.isValid() && !fs.sourceId.isEmpty(),
                    "every field source keeps observedAt + sourceId");

        // Work 2 — a French original with an English edition keeps translation lineage.
        const BiblioWork &translated = find(works, "ol2002w");
        require(translated.originalLanguage == "fr", "translation lineage retained");
        require(translated.editions.size() == 2, "the French original and the English edition both nest");
        require(hasEdition(translated, "print", "en"), "an English-readable edition is present");
        require(translated.canonicalFirstPublished == QDate(2015, 1, 1),
                "first-publish is the earliest (French) edition");
        require(translated.title == "Song of the Deep", "the canonical title is the English edition's");
        require(BiblioTaxonomy::languageKey(translated.originalLanguage, true) == "translated",
                "the taxonomy classifies the retained lineage as translated");

        // Works 3 & 4 — same title, different authors: never merged on title alone.
        const BiblioWork &holt = find(works, "ol3003w");
        const BiblioWork &frostWork = find(works, "ol4004w");
        require(holt.canonicalId != frostWork.canonicalId, "same title by different authors stays two works");
        require(holt.title == "Echoes of the Deep" && frostWork.title == "Echoes of the Deep",
                "both collision works keep the shared title");
        require(holt.editions.size() == 2, "two ordinary formats of one work nest as ebook + print");
        require(editionFormats(holt) == (QStringList{"ebook", "print"}), "the ordinary formats are ebook and print");
        require(holt.appleChartScore > frostWork.appleChartScore,
                "the charting collision work carries the Apple chart score");

        // Publisher stays raw on the work; the taxonomy folds the imprint on demand.
        require(BiblioTaxonomy::normalize("publisher", holt.publisher) == "penguin-random-house",
                "the raw Vintage imprint folds under Penguin Random House via the taxonomy");
    }

    std::cout << "BIBLIO_CATALOG_LOGIC_OK\n";
    return 0;
}
