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
    require(axes.contains(QStringLiteral("genre")), "genre axis advertised");
    require(axes.contains(QStringLiteral("audience")), "audience axis advertised");
    require(axes.contains(QStringLiteral("length")), "length axis advertised");

    // Each advertised non-publisher group must carry its controlled facet values.
    for (const QVariant &g : groups) {
        const QVariantMap gm = g.toMap();
        if (gm.value(QStringLiteral("axis")).toString() == QStringLiteral("genre")) {
            const QVariantList facets = gm.value(QStringLiteral("facets")).toList();
            require(!facets.empty(), "genre group carries controlled facet values");
        }
    }
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
    testPreviewRowsAndFreshness();
    testCatalogAllowlistRejectsUnknown();
    testReopenPreservesSnapshot();

    if (g_failures == 0) {
        std::cout << "BIBLIO_CATALOG_STORE_OK\n";
        return 0;
    }
    std::cerr << g_failures << " store harness assertion(s) failed\n";
    return 1;
}
