// tests/biblio_catalog_store_harness.cpp
//
// SQLite oracle for the BiblioCatalog snapshot store (Discover/Explore Task 3,
// plan 2026-08-03): atomic publication of daily catalogue snapshots, stable
// paged reads, exact facet filtering, explicit gating, canonical uniqueness,
// seven-day ranking-history retention, cached Top 10, and rollback after an
// invalid staged snapshot (spec 6.3 / 7 / 8 / 10 / 12.1, DoD 9 / 14).
//
// Each test opens a FRESH temp database file (QStandardPaths::writableLocation
// under a unique subdir, or a process-unique temp path) so a real app DB is
// never clobbered. Prints BIBLIO_CATALOG_STORE_OK and returns 0 only when every
// require() passed.
#include "engine/BiblioCatalogStore.h"
#include "engine/BiblioCatalogTypes.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <QVariantList>
#include <QVariantMap>

#include <iostream>

namespace {

int g_failures = 0;

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

// --- fixture builders -----------------------------------------------------

QDateTime utc(int y, int m, int d)
{
    return QDateTime(QDate(y, m, d), QTime(9, 0, 0), QTimeZone::utc());
}

// A canonical work with a stable id, title/author, and controllable fields. The
// harness builds the full snapshot payload from these — the store's job is to
// persist and re-read them exactly.
BiblioWork makeWork(const QString &id, const QString &title, const QString &author)
{
    BiblioWork w;
    w.canonicalId = id;
    w.title = title;
    w.author = author;
    return w;
}

// One edition row beneath a work. Returns the value-type BiblioEdition so it can
// be nested on BiblioWork::editions (a QList<BiblioEdition>); the store persists
// those nested editions directly.
BiblioEdition makeEdition(const QString &editionId, const QString &format,
                          int pages, bool englishReadable)
{
    BiblioEdition e;
    e.editionId = editionId;
    e.language = englishReadable ? QStringLiteral("en") : QStringLiteral("fr");
    e.englishReadable = englishReadable;
    e.pageCount = pages;
    e.format = format;
    e.published = QDate(2020, 6, 1);
    return e;
}

// A controlled facet binding on a work (axis + stable key from the taxonomy).
BiblioCatalogFacet makeFacet(const QString &axis, const QString &key)
{
    BiblioCatalogFacet f;
    f.axis = axis;
    f.key = key;
    return f;
}

// A per-field provenance row (which source set which canonical field).
BiblioCatalogSource makeSource(const QString &field, const QString &source,
                               const QString &sourceId, const QDateTime &observedAt)
{
    BiblioCatalogSource s;
    s.field = field;
    s.source = source;
    s.sourceId = sourceId;
    s.observedAt = observedAt;
    return s;
}

// A computed ranking for one catalogue + work: score + 1-based rank position.
BiblioCatalogRanking makeRanking(const QString &catalog, const QString &canonicalId,
                                 double score, int rank)
{
    BiblioCatalogRanking r;
    r.catalogId = catalog;
    r.canonicalId = canonicalId;
    r.score = score;
    r.rank = rank;
    return r;
}

// One dated daily demand reading (Trending's seven-day delta source).
BiblioCatalogHistory makeHistory(const QString &canonicalId, const QDateTime &capturedAt,
                                 double demand)
{
    BiblioCatalogHistory h;
    h.canonicalId = canonicalId;
    h.capturedAt = capturedAt;
    h.demandScore = demand;
    return h;
}

// A minimal-but-complete snapshot for the paging/filter tests. Eight works,
// each with one edition, known facet bindings, four-catalogue rankings, no
// history, captured at a fixed time.
BiblioCatalogSnapshot eightWorkSnapshot(const QDateTime &capturedAt)
{
    BiblioCatalogSnapshot snap;
    snap.capturedAt = capturedAt;
    for (int i = 0; i < 8; ++i) {
        const QString id = QStringLiteral("work-%1").arg(i);
        BiblioWork w = makeWork(id, QStringLiteral("Title %1").arg(i),
                                QStringLiteral("Author %1").arg(i));
        w.publisher = QStringLiteral("Acme");
        w.editions.append(makeEdition(id + QStringLiteral("-ed"), QStringLiteral("print"),
                                      250, true));
        snap.works.append(w);

        snap.sources.append({id, makeSource(QStringLiteral("title"),
                                            QStringLiteral("openlibrary"),
                                            id + QStringLiteral("-ol"), capturedAt)});

        // Bind a genre facet only to even-indexed works so the filter test has a
        // clean split. The key is from the controlled taxonomy ("science-fiction").
        if (i % 2 == 0)
            snap.facets.append({id, makeFacet(QStringLiteral("genre"),
                                              QStringLiteral("science-fiction"))});

        // Ranking: Popular catalogue, in id order so paging is deterministic.
        snap.rankings.append(makeRanking(QStringLiteral("popular"), id,
                                         10.0 - double(i), i + 1));
    }
    return snap;
}

// --- per-test cases -------------------------------------------------------

void testOpenCreatesDbAndSchema()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir created");
    const QString path = dir.path() + QStringLiteral("/biblio-store.sqlite");

    {
        BiblioCatalogStore store;
        require(store.open(path), "open() creates the database");
        require(QFileInfo::exists(path), "database file exists on disk after open");
        require(!store.hasSnapshot(), "fresh db has no snapshot");
        require(!store.lastSuccessUtc().isValid(), "fresh db has no last-success");
    }
    // Reopen: schema must already exist (idempotent open), no user_version reset.
    BiblioCatalogStore store;
    require(store.open(path), "open() succeeds on an existing db");
    require(!store.hasSnapshot(), "existing db still has no snapshot until publish");
}

void testPublishEstablishesSnapshot()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir created");
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    const QDateTime at = utc(2026, 8, 1);
    require(store.publish(eightWorkSnapshot(at)), "publish succeeds for a valid snapshot");
    require(store.hasSnapshot(), "hasSnapshot true after a successful publish");
    require(store.lastSuccessUtc().isValid(), "lastSuccessUtc valid after publish");
    require(store.lastSuccessUtc() == at, "lastSuccessUtc reflects the snapshot's capturedAt");
}

void testStablePaging()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish eight works");

    const QString catalog = QStringLiteral("popular");
    const QString empty;
    // Page size 3: pages 1, 2, 3 must tile all 8 with no overlap or skip.
    const QVariantMap p1 = store.page(catalog, empty, empty, true, 0, 3);
    require(p1.value(QStringLiteral("items")).toList().size() == 3, "page 1 returns 3 items");
    require(p1.value(QStringLiteral("nextOffset")).toInt() == 3, "page 1 nextOffset == 3");
    require(!p1.value(QStringLiteral("exhausted")).toBool(), "page 1 not exhausted");

    const QVariantMap p2 = store.page(catalog, empty, empty, true, 3, 3);
    require(p2.value(QStringLiteral("items")).toList().size() == 3, "page 2 returns 3 items");
    require(p2.value(QStringLiteral("nextOffset")).toInt() == 6, "page 2 nextOffset == 6");
    require(!p2.value(QStringLiteral("exhausted")).toBool(), "page 2 not exhausted");

    const QVariantMap p3 = store.page(catalog, empty, empty, true, 6, 3);
    require(p3.value(QStringLiteral("items")).toList().size() == 2, "page 3 returns 2 items");
    require(p3.value(QStringLiteral("exhausted")).toBool(), "page 3 exhausted");

    // Collect canonicalIds across all pages; assert uniqueness and full coverage.
    QStringList seen;
    for (const QVariant &v : p1.value(QStringLiteral("items")).toList())
        seen << v.toMap().value(QStringLiteral("canonicalId")).toString();
    for (const QVariant &v : p2.value(QStringLiteral("items")).toList())
        seen << v.toMap().value(QStringLiteral("canonicalId")).toString();
    for (const QVariant &v : p3.value(QStringLiteral("items")).toList())
        seen << v.toMap().value(QStringLiteral("canonicalId")).toString();
    require(seen.size() == 8, "all 8 works returned across pages");
    require(QSet<QString>(seen.begin(), seen.end()).size() == 8, "no overlap/skip across pages");

    // Paging must be stable: the ordering is the catalogue's ranking, so page 2
    // is exactly ranks 4..6.
    require(seen.at(3) == QStringLiteral("work-3"), "page 2 first item is rank 4");
    require(seen.at(5) == QStringLiteral("work-5"), "page 2 last item is rank 6");
}

void testOffsetClampedNegativeLimitClampedHigh()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish");

    const QString catalog = QStringLiteral("popular");
    const QString empty;
    // Negative offset -> treated as 0; limit over 100 -> clamped to 100 (we have 8).
    const QVariantMap p = store.page(catalog, empty, empty, true, -50, 9999);
    require(p.value(QStringLiteral("nextOffset")).toInt() == 8, "negative offset clamps to 0, all 8 returned");
    require(p.value(QStringLiteral("exhausted")).toBool(), "over-limit clamps and exhausts");
    require(p.value(QStringLiteral("items")).toList().size() == 8, "all 8 returned with clamped limit");

    // limit below 1 -> clamped to 1.
    const QVariantMap pOne = store.page(catalog, empty, empty, true, 0, 0);
    require(pOne.value(QStringLiteral("items")).toList().size() == 1, "limit 0 clamps to 1");
}

void testExactFacetFiltering()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish");

    const QString catalog = QStringLiteral("popular");
    // Matched axis+key: only the four even-indexed works carry "genre/science-fiction".
    const QVariantMap matched = store.page(catalog, QStringLiteral("genre"),
                                           QStringLiteral("science-fiction"),
                                           true, 0, 100);
    const QVariantList items = matched.value(QStringLiteral("items")).toList();
    require(items.size() == 4, "matched facet returns the four bound works");
    for (const QVariant &v : items) {
        const QString id = v.toMap().value(QStringLiteral("canonicalId")).toString();
        const int idx = QStringView{id}.mid(QStringLiteral("work-").length()).toInt();
        require(idx % 2 == 0, "only even-indexed works carry the genre facet");
    }

    // Unmatched key: empty result, exhausted true, empty warning.
    const QVariantMap unmatched = store.page(catalog, QStringLiteral("genre"),
                                             QStringLiteral("nonexistent-key"),
                                             true, 0, 100);
    require(unmatched.value(QStringLiteral("items")).toList().empty(),
            "unmatched facet key returns no items");
    require(unmatched.value(QStringLiteral("exhausted")).toBool(),
            "unmatched facet key exhausts immediately");
}

void testExplicitGating()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    // Build a snapshot with one source-confirmed sexually-explicit work, one
    // Adult-audience work (NOT explicit), and one plain work (spec 14 / DoD 14).
    BiblioCatalogSnapshot snap;
    snap.capturedAt = utc(2026, 8, 1);

    BiblioWork plain = makeWork(QStringLiteral("plain"), QStringLiteral("Plain"),
                                QStringLiteral("A"));
    plain.editions.append(makeEdition(QStringLiteral("plain-ed"),
                                      QStringLiteral("print"), 300, true));
    snap.works.append(plain);

    BiblioWork adult = makeWork(QStringLiteral("adult-aud"), QStringLiteral("Adult Audience"),
                                QStringLiteral("B"));
    adult.editions.append(makeEdition(QStringLiteral("adult-ed"),
                                      QStringLiteral("print"), 300, true));
    snap.works.append(adult);
    snap.facets.append({QStringLiteral("adult-aud"),
                        makeFacet(QStringLiteral("audience"), QStringLiteral("adult"))});

    BiblioWork explicitW = makeWork(QStringLiteral("explicit-one"),
                                    QStringLiteral("Explicit"), QStringLiteral("C"));
    explicitW.editions.append(makeEdition(QStringLiteral("explicit-ed"),
                                          QStringLiteral("print"), 300, true));
    snap.works.append(explicitW);
    // Only source-confirmed explicit flag classifies a work as explicit.
    snap.explicitWorkIds.insert(QStringLiteral("explicit-one"));

    for (int i = 0; i < snap.works.size(); ++i) {
        const QString &id = snap.works[i].canonicalId;
        snap.rankings.append(makeRanking(QStringLiteral("popular"), id,
                                         10.0 - i, i + 1));
    }

    require(store.publish(snap), "publish explicit-gating fixture");

    const QString catalog = QStringLiteral("popular");
    const QString empty;

    // includeExplicit=false: the explicit work is hidden; Adult audience stays.
    const QVariantMap hidden = store.page(catalog, empty, empty, false, 0, 100);
    const QVariantList hiddenItems = hidden.value(QStringLiteral("items")).toList();
    require(hiddenItems.size() == 2, "explicit hidden, plain + adult-audience visible");
    QSet<QString> hiddenIds;
    for (const QVariant &v : hiddenItems)
        hiddenIds.insert(v.toMap().value(QStringLiteral("canonicalId")).toString());
    require(hiddenIds.contains(QStringLiteral("plain")), "plain visible when explicit hidden");
    require(hiddenIds.contains(QStringLiteral("adult-aud")),
            "Adult-audience work is NOT explicit and stays visible");
    require(!hiddenIds.contains(QStringLiteral("explicit-one")),
            "source-confirmed explicit work is hidden when includeExplicit=false");

    // includeExplicit=true: all three appear.
    const QVariantMap shown = store.page(catalog, empty, empty, true, 0, 100);
    require(shown.value(QStringLiteral("items")).toList().size() == 3,
            "all three visible when includeExplicit=true");
}

void testCanonicalUniquenessRejectsDuplicateId()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    // First, establish a known-good prior snapshot so rollback is observable.
    BiblioCatalogSnapshot prior = eightWorkSnapshot(utc(2026, 7, 30));
    require(store.publish(prior), "prior snapshot publishes");
    require(store.hasSnapshot(), "prior snapshot is active");
    const QDateTime priorSuccess = store.lastSuccessUtc();
    require(priorSuccess == utc(2026, 7, 30), "prior lastSuccessUtc recorded");

    // Now a snapshot with a DUPLICATE canonicalId in one payload -> must reject,
    // rollback, and leave the prior active snapshot fully intact.
    BiblioCatalogSnapshot bad;
    bad.capturedAt = utc(2026, 8, 1);
    BiblioWork a = makeWork(QStringLiteral("dup"), QStringLiteral("Dup A"),
                            QStringLiteral("X"));
    BiblioWork b = makeWork(QStringLiteral("dup"), QStringLiteral("Dup B"),
                            QStringLiteral("Y"));
    bad.works.append(a);
    bad.works.append(b);
    bad.rankings.append(makeRanking(QStringLiteral("popular"),
                                    QStringLiteral("dup"), 5.0, 1));
    bad.rankings.append(makeRanking(QStringLiteral("popular"),
                                    QStringLiteral("dup"), 4.0, 2));

    require(!store.publish(bad), "duplicate canonicalId in one snapshot is rejected");
    require(store.hasSnapshot(), "hasSnapshot still true after rejected publish");
    require(store.lastSuccessUtc() == priorSuccess,
            "lastSuccessUtc unchanged after rejected publish (prior snapshot intact)");

    // The prior snapshot's data must still page correctly.
    const QVariantMap p = store.page(QStringLiteral("popular"), QString(), QString(),
                                     true, 0, 100);
    require(p.value(QStringLiteral("items")).toList().size() == 8,
            "prior snapshot data still readable after rejected publish");
}

void testUnknownFacetKeyRejected()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    BiblioCatalogSnapshot prior = eightWorkSnapshot(utc(2026, 7, 30));
    require(store.publish(prior), "prior snapshot publishes");

    BiblioCatalogSnapshot bad = eightWorkSnapshot(utc(2026, 8, 1));
    // Inject an UNKNOWN facet axis+key (not in the controlled taxonomy).
    bad.facets.append({QStringLiteral("work-0"),
                       makeFacet(QStringLiteral("genre"), QStringLiteral("not-a-real-genre"))});
    require(!store.publish(bad), "unknown facet key is rejected on publish");
    require(store.lastSuccessUtc() == utc(2026, 7, 30),
            "prior snapshot intact after unknown-facet rejection");

    // Also reject an unknown AXIS entirely.
    BiblioCatalogSnapshot badAxis = eightWorkSnapshot(utc(2026, 8, 1));
    badAxis.facets.append({QStringLiteral("work-0"),
                           makeFacet(QStringLiteral("made-up-axis"),
                                     QStringLiteral("anything"))});
    require(!store.publish(badAxis), "unknown facet axis is rejected on publish");
}

void testBrokenEditionFkRejected()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    require(store.publish(eightWorkSnapshot(utc(2026, 7, 30))), "prior publishes");

    BiblioCatalogSnapshot bad = eightWorkSnapshot(utc(2026, 8, 1));
    // Edition whose canonicalId does not correspond to any work -> FK violation.
    BiblioCatalogEdition orphanEd;
    orphanEd.editionId = QStringLiteral("orphan-ed");
    orphanEd.language = QStringLiteral("en");
    orphanEd.englishReadable = true;
    orphanEd.pageCount = 300;
    orphanEd.format = QStringLiteral("print");
    orphanEd.published = QDate(2020, 6, 1);
    bad.editions.append({QStringLiteral("nonexistent-work-id"), orphanEd});
    require(!store.publish(bad), "edition with no parent work (broken FK) is rejected");
    require(store.lastSuccessUtc() == utc(2026, 7, 30),
            "prior snapshot intact after broken-FK rejection");
}

void testSevenDayRetentionKeepsLastEight()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    // Publish NINE daily snapshots, each carrying one fresh history reading for
    // the same work. After the ninth publish, only the most recent EIGHT daily
    // readings must survive (plan: eight snapshots suffice for a seven-day delta,
    // 8 >= 7+1).
    const QString work = QStringLiteral("work-0");
    for (int day = 1; day <= 9; ++day) {
        BiblioCatalogSnapshot snap = eightWorkSnapshot(utc(2026, 8, day));
        // Each daily snapshot carries ONE fresh reading dated to its capture day.
        snap.history.append(makeHistory(work, utc(2026, 8, day), double(day)));
        require(store.publish(snap), "daily publish succeeds");
    }

    // Inspect retained history via the store's diagnostic accessor: only the last
    // eight days (days 2..9) must remain; day 1 must be pruned.
    const QVariantList hist = store.rankingHistoryFor(work);
    require(hist.size() == 8, "exactly eight daily ranking snapshots retained");
    QSet<int> days;
    for (const QVariant &v : hist) {
        const QDateTime when = v.toMap().value(QStringLiteral("capturedAt")).toDateTime();
        days.insert(when.date().day());
    }
    require(!days.contains(1), "the oldest (9th-ago) reading is pruned");
    require(days.contains(2), "day 2 retained");
    require(days.contains(9), "day 9 (most recent) retained");
}

void testCachedTop10ReflectsLatestPublish()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    // First publish: top10 returns the ranked rows.
    BiblioCatalogSnapshot first = eightWorkSnapshot(utc(2026, 8, 1));
    require(store.publish(first), "first publish");
    {
        const QVariantList top = store.top10(10, true);
        require(top.size() == 8, "top10 returns all ranked works (fewer than 10)");
        require(top.first().toMap().value(QStringLiteral("canonicalId")).toString()
                == QStringLiteral("work-0"),
                "top10 first item is rank 1 in the first snapshot");
    }

    // Second publish with REORDERED rankings: top10 reflects the new order.
    BiblioCatalogSnapshot second = eightWorkSnapshot(utc(2026, 8, 2));
    second.rankings.clear();
    // Reverse the popular ranking: work-7 is now rank 1.
    for (int i = 0; i < 8; ++i) {
        const QString id = QStringLiteral("work-%1").arg(i);
        second.rankings.append(makeRanking(QStringLiteral("popular"), id,
                                           double(i), 8 - i));
    }
    require(store.publish(second), "second publish with reordered rankings");
    {
        const QVariantList top = store.top10(10, true);
        require(top.first().toMap().value(QStringLiteral("canonicalId")).toString()
                == QStringLiteral("work-7"),
                "top10 reflects the reordered ranking after re-publish");
    }

    // top10 is capped at 10 even if a snapshot ranks more works.
    BiblioCatalogSnapshot big;
    big.capturedAt = utc(2026, 8, 3);
    for (int i = 0; i < 15; ++i) {
        const QString id = QStringLiteral("big-%1").arg(i);
        BiblioWork w = makeWork(id, QStringLiteral("Big %1").arg(i),
                                QStringLiteral("Z"));
        w.editions.append(makeEdition(id + QStringLiteral("-ed"),
                                      QStringLiteral("print"), 200, true));
        big.works.append(w);
        big.rankings.append(makeRanking(QStringLiteral("popular"), id,
                                        15.0 - i, i + 1));
    }
    require(store.publish(big), "publish fifteen works");
    require(store.top10(10, true).size() == 10, "top10 capped at ten");
}

void testFilterGroupsAdvertiseControlledAxes()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish");

    // filterGroups must advertise the controlled axes the taxonomy owns.
    const QVariantList groups = store.filterGroups(true);
    require(!groups.empty(), "filterGroups returns the controlled axes");
    QSet<QString> axes;
    for (const QVariant &g : groups)
        axes.insert(g.toMap().value(QStringLiteral("axis")).toString());
    // 2026-08-06 BIBLIO_DISCOVER_FILTERS_HONEST_ADVERTISE: axes are now
    // advertised only when the snapshot populates at least one of their keys.
    // eightWorkSnapshot binds ONLY genre/science-fiction, so only `genre`
    // appears. Axes with no populated keys (audience, length, era, language,
    // theme, setting, period) are intentionally omitted — that is the fix, not
    // a regression. A snapshot with a broader facet set advertises more axes.
    require(axes.contains(QStringLiteral("genre")), "genre axis advertised (populated)");
    require(!axes.contains(QStringLiteral("audience")),
            "audience axis omitted when no audience facet is populated");
    require(!axes.contains(QStringLiteral("length")),
            "length axis omitted when no length facet is populated");
    require(!axes.contains(QStringLiteral("theme")),
            "theme axis omitted when no theme facet is populated");

    // Each advertised non-publisher group must carry its controlled facet values.
    for (const QVariant &g : groups) {
        const QVariantMap gm = g.toMap();
        if (gm.value(QStringLiteral("axis")).toString() == QStringLiteral("genre")) {
            const QVariantList facets = gm.value(QStringLiteral("facets")).toList();
            require(!facets.empty(), "genre group carries controlled facet values");
        }
    }
}

// 2026-08-06 BIBLIO_DISCOVER_FILTERS_HONEST_ADVERTISE, Slice 1: the parity
// test that the bug-shipping arc lacked. The filtered page() path is already
// covered by testExactFacetFiltering above; what was NEVER asserted is that the
// advertisement (filterGroups) mirrors what the active snapshot actually
// populates. Against the current code this case FAILS RED — it is the
// regression sentinel proving the advertisement is data-derived, not a static
// vocabulary dump. Slice 2 turns it green by making filterGroups() query
// work_facets for populated keys the way it already queries the works table
// for publisher values.
void testFilterGroupsMirrorSnapshotFacets()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    // eightWorkSnapshot binds genre/science-fiction to the four even-indexed
    // works (line ~145). No other genre/audience/theme/setting/period keys are
    // bound. This is the controlled fixture: exactly one populated non-publisher
    // facet key.
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish");

    // Collect the snapshot's own facet bindings as the oracle — the set of
    // (axis,key) pairs the snapshot actually populated.
    const BiblioCatalogSnapshot probe = eightWorkSnapshot(utc(2026, 8, 1));
    QSet<QString> populatedPairs;
    for (const auto &snapFacet : probe.facets)
        populatedPairs.insert(snapFacet.facet.axis + QChar('/') + snapFacet.facet.key);

    const QVariantList groups = store.filterGroups(true);
    require(!groups.empty(), "filterGroups returns groups over a published snapshot");

    // Every advertised (axis,key) pair must exist in the snapshot's populated
    // set. Against the unmodified code this fails because filterGroups()
    // advertises the full static vocabulary (Fantasy, Thriller, Horror, etc.)
    // that the snapshot never bound.
    for (const QVariant &g : groups) {
        const QVariantMap gm = g.toMap();
        const QString axis = gm.value(QStringLiteral("axis")).toString();
        if (axis == QStringLiteral("publisher"))
            continue; // publisher keys are data-derived from works.publisher, not work_facets
        const QVariantList facets = gm.value(QStringLiteral("facets")).toList();
        for (const QVariant &f : facets) {
            const QString key = f.toMap().value(QStringLiteral("key")).toString();
            const QString pair = axis + QChar('/') + key;
            require(populatedPairs.contains(pair),
                    QByteArray("advertised facet is populated in snapshot: ") + pair.toUtf8());
        }
    }

    // Negative control: a key the taxonomy static table knows but this snapshot
    // does NOT populate must not be advertised. "fantasy" is in BiblioTaxonomy's
    // genre table (so the normalizer still recognizes it — A-arc readiness) but
    // eightWorkSnapshot binds no work to genre/fantasy, so it must not appear.
    for (const QVariant &g : groups) {
        const QVariantMap gm = g.toMap();
        const QString axis = gm.value(QStringLiteral("axis")).toString();
        const QVariantList facets = gm.value(QStringLiteral("facets")).toList();
        for (const QVariant &f : facets) {
            const QString key = f.toMap().value(QStringLiteral("key")).toString();
            require(!(axis == QStringLiteral("genre") && key == QStringLiteral("fantasy")),
                    "genre/fantasy is not advertised when the snapshot has no fantasy evidence");
        }
    }

    // Positive: the one populated key (genre/science-fiction) IS advertised.
    bool sawScienceFiction = false;
    for (const QVariant &g : groups) {
        const QVariantMap gm = g.toMap();
        if (gm.value(QStringLiteral("axis")).toString() != QStringLiteral("genre"))
            continue;
        for (const QVariant &f : gm.value(QStringLiteral("facets")).toList()) {
            if (f.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("science-fiction"))
                sawScienceFiction = true;
        }
    }
    require(sawScienceFiction,
            "genre/science-fiction is advertised when the snapshot populates it");
}

void testPreviewRowsAndFreshness()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish");

    const QVariantList preview = store.previewRows(3, true);
    require(preview.size() == 3, "previewRows honors the limit");

    // A clean page must carry an empty warning and a freshness token.
    const QVariantMap p = store.page(QStringLiteral("popular"), QString(), QString(),
                                     true, 0, 5);
    require(p.contains(QStringLiteral("freshness")), "page result carries a freshness token");
    require(p.value(QStringLiteral("warning")).toString().isEmpty(),
            "clean page carries no warning");
}

// 2026-08-06 shelf-quality fix: page()/previewRows() must heal a cover_url already
// persisted in the shape Apple's RSS feed actually ships (verified live against
// production: "0x170bb.png" — Apple's own CDN rejects it, "Cannot produce 0x170 image
// with Resize Style: 'bb'"). This is what makes the fix apply to an EXISTING cache
// immediately, without waiting up to 7 days for the next snapshot to naturally refresh.
void testBrokenCoverUrlHealedOnRead()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    BiblioCatalogSnapshot snap;
    snap.capturedAt = utc(2026, 8, 1);
    BiblioWork w = makeWork(QStringLiteral("broken-cover"), QStringLiteral("Broken Cover Book"),
                            QStringLiteral("Author"));
    w.coverUrl = QStringLiteral(
        "https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/0x170bb.png");
    snap.works.append(w);
    snap.rankings.append(makeRanking(QStringLiteral("popular"), QStringLiteral("broken-cover"), 5.0, 1));
    require(store.publish(snap), "publish a snapshot carrying the broken RSS cover shape");

    const QVariantMap p = store.page(QStringLiteral("popular"), QString(), QString(), true, 0, 10);
    const QVariantList items = p.value(QStringLiteral("items")).toList();
    require(items.size() == 1, "one work returned");
    const QString healed = items.at(0).toMap().value(QStringLiteral("coverUrl")).toString();
    require(healed == QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/600x600bb.jpg"),
            "page() heals a cache row's broken cover_url on read");

    const QVariantList preview = store.previewRows(10, true);
    require(preview.size() == 1, "one preview row returned");
    const QString healedPreview = preview.at(0).toMap().value(QStringLiteral("coverUrl")).toString();
    require(healedPreview == QStringLiteral("https://is1-ssl.mzstatic.com/image/thumb/x/y/z.jpg/600x600bb.jpg"),
            "previewRows() also heals the broken cover_url on read");
}

void testCatalogAllowlistRejectsUnknown()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");
    require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))), "publish");

    // An unknown catalogue id never reaches SQL: empty result, no crash.
    const QVariantMap p = store.page(QStringLiteral("not-a-catalog"), QString(), QString(),
                                     true, 0, 10);
    require(p.value(QStringLiteral("items")).toList().empty(),
            "unknown catalogue id returns no items");
    // The four allowlisted catalogues each return rows.
    for (const char *cat : {"popular", "top-rated", "new-releases", "trending"}) {
        const QVariantMap q = store.page(QString::fromLatin1(cat), QString(), QString(),
                                         true, 0, 100);
        // popular is ranked in the fixture; the others may legitimately be empty
        // (no ranking rows for them) — the contract is that they do not crash and
        // return the standard shape.
        require(q.contains(QStringLiteral("items")), "allowlisted catalogue returns the page shape");
        require(q.contains(QStringLiteral("exhausted")), "allowlisted catalogue returns exhausted flag");
    }
}

// 2026-08-15 Open Library catalogues (slice 2): the store's allowlist grew to
// six house catalogues. "most-read" and "classics" must page with the exact
// DiscoverBrowser shape, paginate across two calls, and filter by an existing
// facet axis exactly the way "popular" does.
void testMostReadAndClassicsPageLikeHouseCatalogues()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    // Eight works ranked under ALL SIX house catalogues, in id order so paging
    // is deterministic; even-indexed works carry genre/science-fiction.
    BiblioCatalogSnapshot snap;
    snap.capturedAt = utc(2026, 8, 1);
    for (int i = 0; i < 8; ++i) {
        const QString id = QStringLiteral("work-%1").arg(i);
        BiblioWork w = makeWork(id, QStringLiteral("Title %1").arg(i),
                                QStringLiteral("Author %1").arg(i));
        w.editions.append(makeEdition(id + QStringLiteral("-ed"), QStringLiteral("print"),
                                      250, true));
        snap.works.append(w);
        if (i % 2 == 0)
            snap.facets.append({id, makeFacet(QStringLiteral("genre"),
                                              QStringLiteral("science-fiction"))});
        for (const char *cat : {"popular", "top-rated", "new-releases", "trending",
                                "most-read", "classics"}) {
            snap.rankings.append(makeRanking(QString::fromLatin1(cat), id,
                                             10.0 - double(i), i + 1));
        }
    }
    require(store.publish(snap), "publish ranks all six house catalogues");

    for (const char *cat : {"most-read", "classics"}) {
        const QString catalog = QString::fromLatin1(cat);
        const QString empty;
        // Page size 5 over 8 ranked works: two calls tile everything.
        const QVariantMap p1 = store.page(catalog, empty, empty, true, 0, 5);
        require(p1.contains(QStringLiteral("items"))
                    && p1.contains(QStringLiteral("nextOffset"))
                    && p1.contains(QStringLiteral("exhausted"))
                    && p1.contains(QStringLiteral("freshness"))
                    && p1.contains(QStringLiteral("warning")),
                "most-read/classics page carries the {items,nextOffset,exhausted,freshness,warning} shape");
        require(p1.value(QStringLiteral("items")).toList().size() == 5,
                "most-read/classics page 1 returns five items");
        require(p1.value(QStringLiteral("nextOffset")).toInt() == 5,
                "most-read/classics page 1 nextOffset == 5");
        require(!p1.value(QStringLiteral("exhausted")).toBool(),
                "most-read/classics page 1 is not exhausted");
        const QVariantMap p2 = store.page(catalog, empty, empty, true, 5, 5);
        require(p2.value(QStringLiteral("items")).toList().size() == 3,
                "most-read/classics page 2 returns the remaining three items");
        require(p2.value(QStringLiteral("exhausted")).toBool(),
                "most-read/classics page 2 is exhausted");
        // Ordering is the catalogue's ranking (work-0 first), same as "popular".
        require(p1.value(QStringLiteral("items")).toList().first().toMap()
                    .value(QStringLiteral("canonicalId")).toString()
                    == QStringLiteral("work-0"),
                "most-read/classics page 1 starts at rank 1 (work-0)");

        // Facet filtering on an existing axis behaves exactly as for "popular":
        // only the four even-indexed works carry genre/science-fiction.
        const QVariantMap filtered = store.page(catalog, QStringLiteral("genre"),
                                                QStringLiteral("science-fiction"),
                                                true, 0, 100);
        require(filtered.value(QStringLiteral("items")).toList().size() == 4,
                "most-read/classics filter by genre/science-fiction like popular");
    }
}

// The allowlist grew, it did not soften: a ranking row for a catalogue outside
// the six still fails publish validation and leaves the prior snapshot intact.
void testUnknownCatalogIdRejectedOnPublish()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    BiblioCatalogSnapshot prior = eightWorkSnapshot(utc(2026, 7, 30));
    require(store.publish(prior), "prior snapshot publishes");

    BiblioCatalogSnapshot bad = eightWorkSnapshot(utc(2026, 8, 1));
    bad.rankings.append(makeRanking(QStringLiteral("award-winners"),
                                    QStringLiteral("work-0"), 99.0, 1));
    require(!store.publish(bad),
            "a ranking row for a catalogue outside the six-entry allowlist is rejected");
    require(store.lastSuccessUtc() == utc(2026, 7, 30),
            "prior snapshot intact after unknown-catalogue rejection");
}

void testReopenPreservesSnapshot()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/b.sqlite");
    QDateTime captured;
    {
        BiblioCatalogStore store;
        require(store.open(path), "open");
        captured = utc(2026, 8, 1);
        require(store.publish(eightWorkSnapshot(captured)), "publish");
        require(store.hasSnapshot(), "snapshot active before close");
    }
    // Reopen a fresh store handle on the same file: the active snapshot must
    // survive (spec 8: last successful snapshot stays browsable across runs).
    BiblioCatalogStore store;
    require(store.open(path), "reopen succeeds");
    require(store.hasSnapshot(), "snapshot survives reopen");
    require(store.lastSuccessUtc() == captured, "lastSuccessUtc survives reopen");
    const QVariantMap p = store.page(QStringLiteral("popular"), QString(), QString(),
                                     true, 0, 100);
    require(p.value(QStringLiteral("items")).toList().size() == 8,
            "paged reads work after reopen");
}

// 2026-08-15 lazy grid enrichment (slice 4): pending rows round-trip through
// the parking table. Three states go in; the map reads every field back
// byte-exact; upsert replaces a row in place; clearFoldedPending deletes
// EXACTLY the folded ('enriched') keys and keeps the others untouched.
void testPendingEnrichmentRoundTrip()
{
    QTemporaryDir dir;
    BiblioCatalogStore store;
    require(store.open(dir.path() + QStringLiteral("/b.sqlite")), "open");

    const QByteArray enrichedBody = QByteArrayLiteral(R"({"results":[{"trackName":"Round Trip"}]})");
    require(store.upsertPendingEnrichment(QStringLiteral("enriched|key"),
                                          enrichedBody,
                                          QStringLiteral("enriched"),
                                          QStringLiteral("2026-08-15")),
            "upsert an enriched row (body parked raw)");
    require(store.upsertPendingEnrichment(QStringLiteral("pending|key"),
                                          QByteArray(),
                                          QStringLiteral("pending"),
                                          QStringLiteral("2026-08-15")),
            "upsert a pending marker row");
    require(store.upsertPendingEnrichment(QStringLiteral("unavailable|key"),
                                          QByteArray(),
                                          QStringLiteral("unavailable"),
                                          QStringLiteral("2026-08-14")),
            "upsert an unavailable row (yesterday's attempt)");

    const QHash<QString, BiblioPendingEnrichmentRow> map = store.pendingEnrichmentMap();
    require(map.size() == 3, "map reads back all three rows");
    const BiblioPendingEnrichmentRow enriched =
        map.value(QStringLiteral("enriched|key"));
    require(enriched.workKey == QStringLiteral("enriched|key"), "enriched row keeps its work_key");
    require(enriched.state == QStringLiteral("enriched"), "enriched row state round-trips");
    require(enriched.body == enrichedBody, "enriched row body round-trips byte-exact");
    require(enriched.lastAttempt == QStringLiteral("2026-08-15"),
            "enriched row last_attempt round-trips");
    const BiblioPendingEnrichmentRow pendingRow =
        map.value(QStringLiteral("pending|key"));
    require(pendingRow.state == QStringLiteral("pending"), "pending row state round-trips");
    require(pendingRow.body.isEmpty(), "pending row carries no body");
    const BiblioPendingEnrichmentRow unavailableRow =
        map.value(QStringLiteral("unavailable|key"));
    require(unavailableRow.state == QStringLiteral("unavailable"),
            "unavailable row state round-trips");
    require(unavailableRow.lastAttempt == QStringLiteral("2026-08-14"),
            "unavailable row last_attempt round-trips (the gate's data)");

    // Upsert REPLACES: flip the pending marker to enriched with a body, in place.
    require(store.upsertPendingEnrichment(QStringLiteral("pending|key"),
                                          enrichedBody,
                                          QStringLiteral("enriched"),
                                          QStringLiteral("2026-08-15")),
            "upsert over an existing work_key replaces the row");
    const QHash<QString, BiblioPendingEnrichmentRow> map2 = store.pendingEnrichmentMap();
    require(map2.size() == 3, "replace does not grow the table");
    require(map2.value(QStringLiteral("pending|key")).state == QStringLiteral("enriched")
                && map2.value(QStringLiteral("pending|key")).body == enrichedBody,
            "replaced row carries the new state and body");

    // clearFoldedPending removes EXACTLY the folded enriched keys.
    require(store.clearFoldedPending({QStringLiteral("enriched|key"),
                                      QStringLiteral("pending|key")}),
            "clearFoldedPending succeeds");
    const QHash<QString, BiblioPendingEnrichmentRow> map3 = store.pendingEnrichmentMap();
    require(map3.size() == 1, "only the folded rows were removed");
    require(map3.contains(QStringLiteral("unavailable|key")),
            "the unavailable gate row survives the fold-in clear");
    require(map3.value(QStringLiteral("unavailable|key")).lastAttempt
                == QStringLiteral("2026-08-14"),
            "the surviving row's fields are untouched");

    // Clearing unknown keys is a no-op, not an error.
    require(store.clearFoldedPending({QStringLiteral("no-such-key")}),
            "clearing an absent work_key succeeds");
    require(store.pendingEnrichmentMap().size() == 1,
            "clearing an absent work_key removes nothing");
    require(store.clearFoldedPending({}), "clearing an empty list succeeds");
    require(store.pendingEnrichmentMap().size() == 1,
            "clearing an empty list removes nothing");
}

// 2026-08-15 lazy grid enrichment (slice 4): old-DB migration. A database
// written BEFORE the pending table existed must gain it transparently on its
// next open (the harness simulates the pre-slice schema by dropping the table
// through a raw connection) — and the previously published snapshot must keep
// paging unchanged beside it.
void testPendingEnrichmentOldDbGainsTable()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/b.sqlite");
    {
        BiblioCatalogStore store;
        require(store.open(path), "open");
        require(store.publish(eightWorkSnapshot(utc(2026, 8, 1))),
                "publish a snapshot the way a pre-slice db would carry one");
    }

    // Rewind the schema: a pre-Slice-4 database has no biblio_pending_enrichment.
    {
        QSqlDatabase shim = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                      QStringLiteral("pre-slice-shim"));
        shim.setDatabaseName(path);
        require(shim.open(), "raw shim opens the db");
        QSqlQuery drop(shim);
        require(drop.exec(QStringLiteral("drop table if exists biblio_pending_enrichment")),
                "drop the pending table (simulate a pre-slice-4 schema)");
        shim.close();
        QSqlDatabase::removeDatabase(QStringLiteral("pre-slice-shim"));
    }

    // Reopen through the store: the CREATE IF NOT EXISTS idiom in open() must
    // add the missing table, and everything must work through it.
    BiblioCatalogStore store;
    require(store.open(path), "reopen the pre-slice db");
    require(store.pendingEnrichmentMap().isEmpty(),
            "the gained pending table starts empty");
    require(store.upsertPendingEnrichment(QStringLiteral("migrated|key"),
                                          QByteArrayLiteral("{\"results\":[]}"),
                                          QStringLiteral("pending"),
                                          QStringLiteral("2026-08-15")),
            "upsert works on the gained table");
    const QHash<QString, BiblioPendingEnrichmentRow> map = store.pendingEnrichmentMap();
    require(map.size() == 1
                && map.value(QStringLiteral("migrated|key")).state
                    == QStringLiteral("pending"),
            "the gained table round-trips rows");

    // The pre-existing snapshot is untouched by the migration.
    require(store.hasSnapshot(), "the old snapshot survived the reopen");
    require(store.lastSuccessUtc() == utc(2026, 8, 1), "lastSuccessUtc unchanged");
    const QVariantMap p = store.page(QStringLiteral("popular"), QString(), QString(),
                                     true, 0, 100);
    require(p.value(QStringLiteral("items")).toList().size() == 8,
            "the old snapshot still pages exactly as before");
    require(store.clearFoldedPending({QStringLiteral("migrated|key")}),
            "clearing the migrated row works");
    require(store.pendingEnrichmentMap().isEmpty(), "migrated row cleared");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testOpenCreatesDbAndSchema();
    testPublishEstablishesSnapshot();
    testStablePaging();
    testOffsetClampedNegativeLimitClampedHigh();
    testExactFacetFiltering();
    testExplicitGating();
    testCanonicalUniquenessRejectsDuplicateId();
    testUnknownFacetKeyRejected();
    testBrokenEditionFkRejected();
    testSevenDayRetentionKeepsLastEight();
    testCachedTop10ReflectsLatestPublish();
    testFilterGroupsAdvertiseControlledAxes();
    testFilterGroupsMirrorSnapshotFacets();
    testPreviewRowsAndFreshness();
    testBrokenCoverUrlHealedOnRead();
    testCatalogAllowlistRejectsUnknown();
    testMostReadAndClassicsPageLikeHouseCatalogues();
    testUnknownCatalogIdRejectedOnPublish();
    testReopenPreservesSnapshot();
    testPendingEnrichmentRoundTrip();
    testPendingEnrichmentOldDbGainsTable();

    if (g_failures == 0) {
        std::cout << "BIBLIO_CATALOG_STORE_OK\n";
        return 0;
    }
    std::cerr << g_failures << " store harness assertion(s) failed\n";
    return 1;
}
