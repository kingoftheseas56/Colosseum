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

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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

    std::cout << "BIBLIO_CATALOG_LOGIC_OK\n";
    return 0;
}
