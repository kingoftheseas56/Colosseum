// tst_vault_kit — Slice 1 of the Vault execution plan. Proves the Vault's
// pure-logic kit (VaultKit): the census classifier's kind inference + mixed-leaf
// flag + loose capture + scanIgnore needle exclusion, the ported title cleaner,
// the season/episode grammar and season-climb guard, and the walker's depth cap
// + cooperative cancellation. Pure QtCore, GUILESS — VaultKit has no app deps.
//
// The committed fixture tree lives at VAULT_FIXTURES_DIR (baked at configure
// time, house pattern: TANKOBAN_FIXTURES_DIR). Depth-cap, cancellation, and the
// season-climb cases build their own trees in a QTemporaryDir.

#include "engine/VaultKit.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace VaultKit;

class tst_vault_kit : public QObject
{
    Q_OBJECT

private:
    static QString fixtures() { return QStringLiteral(VAULT_FIXTURES_DIR); }

    // Find the one census slice whose subtree path ends with `suffix`, or a
    // default-constructed slice (kind Unknown) if none — the caller asserts.
    static CensusSlice endingWith(const QList<CensusSlice>& slices, const QString& suffix)
    {
        for (const CensusSlice& s : slices)
            if (s.subtreePath.endsWith(suffix))
                return s;
        return CensusSlice{};
    }
    static CensusSlice looseSlice(const QList<CensusSlice>& slices)
    {
        for (const CensusSlice& s : slices)
            if (s.loose)
                return s;
        return CensusSlice{};
    }

private slots:
    void kind_for_file_data();
    void kind_for_file();
    void classify_mixed_leaf_flags_and_leftovers();
    void census_mixed_root_yields_kind_pure_slices();
    void census_scan_ignore_needle_excludes_subtree();
    void census_mixed_leaf_flagged_with_leftover();
    void census_survives_accent_filename();
    void title_cleaner_data();
    void title_cleaner();
    void season_episode_data();
    void season_episode();
    void season_like_climb_collapses_seasons_but_not_embedded();
    void walk_depth_cap_stops_deep_recursion();
    void walk_cancellation_returns_early();
};

void tst_vault_kit::kind_for_file_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("kind");
    QTest::newRow("cbz_is_comic")   << "X/Berserk v01.cbz"        << "comic";
    QTest::newRow("cbr_is_comic")   << "X/Akira.cbr"              << "comic";
    QTest::newRow("epub_is_book")   << "X/Dune.epub"              << "book";
    QTest::newRow("txt_is_book")    << "X/notes.txt"              << "book";
    QTest::newRow("mkv_is_video")   << "X/S01E01.mkv"             << "video";
    QTest::newRow("mp4_is_video")   << "X/clip.mp4"               << "video";
    QTest::newRow("jpg_is_unknown") << "X/cover.jpg"              << "unknown";
    QTest::newRow("noext_unknown")  << "X/README"                 << "unknown";
}

void tst_vault_kit::kind_for_file()
{
    QFETCH(QString, path);
    QFETCH(QString, kind);
    QCOMPARE(kindName(kindForFile(path)), kind);
}

void tst_vault_kit::classify_mixed_leaf_flags_and_leftovers()
{
    const QStringList files = {
        "leaf/Akira 01.cbz", "leaf/Akira 02.cbz", "leaf/notes.txt"
    };
    const LeafClassification c = classifyLeaf(files);
    QCOMPARE(kindName(c.dominant), QStringLiteral("comic"));
    QVERIFY(c.mixed);
    QCOMPARE(c.counts.value(MediaKind::Comic), 2);
    QCOMPARE(c.counts.value(MediaKind::Book), 1);
    QCOMPARE(c.leftovers.size(), 1);
    QVERIFY(c.leftovers.first().endsWith(QStringLiteral("notes.txt")));
}

void tst_vault_kit::census_mixed_root_yields_kind_pure_slices()
{
    const QList<CensusSlice> slices = census(fixtures() + "/mixed-root");
    QCOMPARE(slices.size(), 5);

    const CensusSlice berserk = endingWith(slices, "Berserk");
    QCOMPARE(kindName(berserk.kind), QStringLiteral("comic"));
    QCOMPARE(berserk.count, 3);
    QVERIFY(!berserk.mixed);
    QVERIFY(!berserk.loose);
    QVERIFY(berserk.sampleTitles.contains(QStringLiteral("Berserk")));

    const CensusSlice dune = endingWith(slices, "Dune");
    QCOMPARE(kindName(dune.kind), QStringLiteral("book"));
    QCOMPARE(dune.count, 2);

    const CensusSlice sopranos = endingWith(slices, "The Sopranos");
    QCOMPARE(kindName(sopranos.kind), QStringLiteral("video"));
    QCOMPARE(sopranos.count, 3); // S01E01, S01E02, S02E01 — flattened across seasons

    const CensusSlice sample = endingWith(slices, "SAMPLE");
    QCOMPARE(kindName(sample.kind), QStringLiteral("video"));

    const CensusSlice loose = looseSlice(slices);
    QCOMPARE(kindName(loose.kind), QStringLiteral("comic"));
    QCOMPARE(loose.count, 1);
    QVERIFY(loose.loose);
}

void tst_vault_kit::census_scan_ignore_needle_excludes_subtree()
{
    const QList<CensusSlice> plain = census(fixtures() + "/mixed-root");
    QVERIFY(kindName(endingWith(plain, "SAMPLE").kind) == QStringLiteral("video"));

    const QList<CensusSlice> filtered =
        census(fixtures() + "/mixed-root", QStringList{QStringLiteral("sample")});
    QCOMPARE(filtered.size(), 4);
    QCOMPARE(kindName(endingWith(filtered, "SAMPLE").kind), QStringLiteral("unknown"));
}

void tst_vault_kit::census_mixed_leaf_flagged_with_leftover()
{
    const QList<CensusSlice> slices = census(fixtures() + "/mixed-leaf");
    QCOMPARE(slices.size(), 1);
    const CensusSlice akira = slices.first();
    QVERIFY(akira.subtreePath.endsWith(QStringLiteral("Akira")));
    QCOMPARE(kindName(akira.kind), QStringLiteral("comic"));
    QCOMPARE(akira.count, 2);
    QVERIFY(akira.mixed);
    QCOMPARE(akira.leftovers.size(), 1);
    QVERIFY(akira.leftovers.first().endsWith(QStringLiteral("notes.txt")));
}

void tst_vault_kit::census_survives_accent_filename()
{
    // The comic file carries U+00B4 (ACUTE ACCENT) in its name — the walker
    // must find it, not choke on the non-ASCII path.
    const QList<CensusSlice> slices = census(fixtures() + "/accent-root");
    QCOMPARE(slices.size(), 1);
    QCOMPARE(kindName(slices.first().kind), QStringLiteral("comic"));
    QCOMPARE(slices.first().count, 1);
}

void tst_vault_kit::title_cleaner_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");
    QTest::newRow("plain_unchanged")     << "Berserk"                          << "Berserk";
    QTest::newRow("quality_stripped")    << "One.Piece.1080p"                  << "One Piece";
    QTest::newRow("season_preserved")    << "Attack on Titan Season 3"         << "Attack on Titan Season 3";
    QTest::newRow("stray_num_scrubbed")  << "The Sopranos -6 Season 1 1080p"   << "The Sopranos Season 1";
    QTest::newRow("too_short_keeps_raw") << "A"                                << "A";
}

void tst_vault_kit::title_cleaner()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    QCOMPARE(cleanMediaFolderTitle(raw), expected);
}

void tst_vault_kit::season_episode_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("matched");
    QTest::addColumn<int>("season");
    QTest::addColumn<int>("episode");
    QTest::newRow("dotted_s01e02")  << "The.Sopranos.S01E02.mkv" << true  << 1  << 2;
    QTest::newRow("lower_s2e10")    << "show.s2e10.mkv"          << true  << 2  << 10;
    QTest::newRow("bare_s01e01")    << "S01E01"                  << true  << 1  << 1;
    QTest::newRow("no_episode")     << "random-movie.mkv"        << false << 0  << 0;
}

void tst_vault_kit::season_episode()
{
    QFETCH(QString, name);
    QFETCH(bool, matched);
    QFETCH(int, season);
    QFETCH(int, episode);
    const SeasonEpisode se = parseSeasonEpisode(name);
    QCOMPARE(se.matched, matched);
    if (matched) {
        QCOMPARE(se.season, season);
        QCOMPARE(se.episode, episode);
    }
}

void tst_vault_kit::season_like_climb_collapses_seasons_but_not_embedded()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir base(tmp.path());

    QVERIFY(base.mkpath(QStringLiteral("The Sopranos/Season 5")));
    QVERIFY(base.mkpath(QStringLiteral("Community Season 1 [1080p]")));
    const QString ep1 = base.filePath(QStringLiteral("The Sopranos/Season 5/ep.mkv"));
    const QString ep2 = base.filePath(QStringLiteral("Community Season 1 [1080p]/ep.mkv"));
    { QFile f(ep1); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }
    { QFile f(ep2); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }

    // Bare "Season 5" is climbed past → collapses to the show root.
    QVERIFY(showRootForEpisodePath(ep1).endsWith(QStringLiteral("The Sopranos")));
    // "Community Season 1 [1080p]" merely embeds the token → that folder IS the root.
    QVERIFY(showRootForEpisodePath(ep2).endsWith(QStringLiteral("[1080p]")));
}

void tst_vault_kit::walk_depth_cap_stops_deep_recursion()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir base(tmp.path());

    // A shallow file (depth 1) is found; a file buried 40 levels deep is past
    // kMaxWalkDepth (32) and must NOT be returned.
    QVERIFY(base.mkpath(QStringLiteral("shallow")));
    { QFile f(base.filePath(QStringLiteral("shallow/near.cbz")));
      QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }

    QString deep = QStringLiteral("deep");
    for (int i = 0; i < 40; ++i)
        deep += QStringLiteral("/d");
    QVERIFY(base.mkpath(deep));
    { QFile f(base.filePath(deep + QStringLiteral("/buried.cbz")));
      QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }

    const QStringList found = walkFiles(tmp.path(), comicFilters());
    bool sawNear = false, sawBuried = false;
    for (const QString& p : found) {
        if (p.endsWith(QStringLiteral("near.cbz")))   sawNear = true;
        if (p.endsWith(QStringLiteral("buried.cbz"))) sawBuried = true;
    }
    QVERIFY(sawNear);
    QVERIFY(!sawBuried);
}

void tst_vault_kit::walk_cancellation_returns_early()
{
    CancellationToken token;
    token.cancel(); // pre-cancelled
    const QStringList found =
        walkFiles(fixtures() + "/mixed-root", allMediaFilters(), &token);
    QVERIFY(found.isEmpty());
}

QTEST_GUILESS_MAIN(tst_vault_kit)
#include "tst_vault_kit.moc"
