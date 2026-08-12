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

    // Find the one browse node of a given type, or a default-constructed node (nodeType Folder,
    // empty key) if none — the caller asserts on the type it expects.
    static BrowseNode onlyNode(const QList<BrowseNode>& nodes)
    {
        return nodes.size() == 1 ? nodes.first() : BrowseNode{};
    }
    static BrowseNode nodeOfType(const QList<BrowseNode>& nodes, BrowseNodeType type)
    {
        for (const BrowseNode& n : nodes)
            if (n.nodeType == type)
                return n;
        return BrowseNode{};
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
    // ── Absolute-numbering episode grammar (browse-face execution plan, Slice 1) ──
    void absolute_episode_grammar_data();
    void absolute_episode_grammar();
    void episode_number_prefers_sxxexx_over_absolute();
    // ── Browse-collapse planner: the five real library shapes ──
    void browse_collapse_one_film_folder_folds_companions_and_extras();
    void browse_collapse_sibling_season_folders_fold_to_one_show();
    void browse_collapse_nested_season_folder_reports_honest_presence();
    void browse_collapse_flat_absolute_numbered_folder_is_a_show();
    void browse_collapse_loose_clips_folder_stays_a_folder();
    void browse_collapse_embedded_season_token_is_not_swallowed();
};

void tst_vault_kit::kind_for_file_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("kind");
    QTest::newRow("cbz_is_comic")   << "X/Berserk v01.cbz"        << "comic";
    QTest::newRow("cbr_is_comic")   << "X/Akira.cbr"              << "comic";
    QTest::newRow("epub_is_book")   << "X/Dune.epub"              << "book";
    // Regression guard: a loose .txt is release junk, NOT a book — counting it as one shelved
    // movie folders (video + junk .txt) under Books via the tie-break. (2026-08-09)
    QTest::newRow("txt_is_not_book") << "X/YIFYStatus.com.txt"    << "unknown";
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
    // A real book (.epub) as the non-dominant leftover — .txt is no longer a book format.
    const QStringList files = {
        "leaf/Akira 01.cbz", "leaf/Akira 02.cbz", "leaf/notes.epub"
    };
    const LeafClassification c = classifyLeaf(files);
    QCOMPARE(kindName(c.dominant), QStringLiteral("comic"));
    QVERIFY(c.mixed);
    QCOMPARE(c.counts.value(MediaKind::Comic), 2);
    QCOMPARE(c.counts.value(MediaKind::Book), 1);
    QCOMPARE(c.leftovers.size(), 1);
    QVERIFY(c.leftovers.first().endsWith(QStringLiteral("notes.epub")));
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
    QVERIFY(akira.leftovers.first().endsWith(QStringLiteral("notes.epub")));
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
    // A multi-season folder is a show root spanning seasons — collapse to the bare show
    // name, never the doubled "Season 1 Season 5" artifact (Slice 11 Thread C).
    QTest::newRow("multi_season_short")  << "The Wire S01 S05"                 << "The Wire";
    QTest::newRow("multi_season_worded") << "The Wire Season 1 Season 5"       << "The Wire";
    QTest::newRow("too_short_keeps_raw") << "A"                                << "A";
    // Real folder from Hemanth's library (bracket-noise ordering bug, browse-face plan Slice 2
    // addendum): stripNoiseBracketChunks used to run AFTER the blanket '.'->' ' replace, so
    // "[5.1]" had already become "[5 1]" by the time the noise test ran — a two-token fragment
    // that fails "pure numeric" and slips through as stray "5 1" / "YTS MX" title noise.
    // Pre-fix baseline (confirmed live before this fix): "Spider-Man No Way Home 5 1 YTS MX".
    QTest::newRow("audio_channel_and_site_tag_stripped")
        << "Spider-Man No Way Home (2021) [1080p] [WEBRip] [5.1] [YTS.MX]"
        << "Spider-Man No Way Home";
    // A DDP-prefixed channel tag (no space before the digits) and a different release-site
    // domain tag — proves the fix is a general pattern, not a one-off string match.
    QTest::newRow("ddp_audio_tag_and_domain_tag_stripped")
        << "Movie Name (2019) [2160p] [DDP5.1] [RARBG.to]"
        << "Movie Name";
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

void tst_vault_kit::absolute_episode_grammar_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("matched");
    QTest::addColumn<bool>("absolute");
    QTest::addColumn<int>("episode");
    // The real Gintama filename from Hemanth's library — SxxExx-less, fansub "- NNN" convention.
    QTest::newRow("gintama_003")
        << QStringLiteral("[Judas] Gintama - 003 [BD 1080p][HEVC x265 10bit][Eng-Subs].mkv")
        << true << true << 3;
    QTest::newRow("gintama_001") << QStringLiteral("[Judas] Gintama - 001 [BD 1080p].mkv")
                                  << true << true << 1;
    // A 4-digit year after " - " must NOT be read as an absolute episode number.
    QTest::newRow("year_not_episode") << QStringLiteral("Movie - 2021 [1080p].mkv")
                                       << false << false << 0;
    // No spaced hyphen at all: a plain resolution/codec tag never matches.
    QTest::newRow("no_hyphen_token") << QStringLiteral("Random.Clip.1080p.mp4")
                                      << false << false << 0;
    // A bare release-range token ("1-5") carries no surrounding whitespace around '-' — must
    // not be mistaken for an absolute episode number either.
    QTest::newRow("range_token_not_episode")
        << QStringLiteral("The Wire Season 1-5 S01-S05.mkv") << false << false << 0;
}

void tst_vault_kit::absolute_episode_grammar()
{
    QFETCH(QString, name);
    QFETCH(bool, matched);
    QFETCH(bool, absolute);
    QFETCH(int, episode);
    const SeasonEpisode se = parseAbsoluteEpisode(name);
    QCOMPARE(se.matched, matched);
    QCOMPARE(se.absolute, absolute);
    if (matched)
        QCOMPARE(se.episode, episode);
}

void tst_vault_kit::episode_number_prefers_sxxexx_over_absolute()
{
    // An explicit SxxExx marking always wins over the absolute grammar, even when a filename
    // could plausibly read either way.
    const SeasonEpisode se = parseEpisodeNumber(QStringLiteral("The.Sopranos.S01E02.mkv"));
    QVERIFY(se.matched);
    QVERIFY(!se.absolute);
    QCOMPARE(se.season, 1);
    QCOMPARE(se.episode, 2);

    // No SxxExx present: falls through to absolute numbering.
    const SeasonEpisode abs = parseEpisodeNumber(
        QStringLiteral("[Judas] Gintama - 003 [BD 1080p][HEVC x265 10bit][Eng-Subs].mkv"));
    QVERIFY(abs.matched);
    QVERIFY(abs.absolute);
    QCOMPARE(abs.episode, 3);

    // Neither grammar fires: honest no-match, not a wrong guess.
    const SeasonEpisode none = parseEpisodeNumber(QStringLiteral("random-movie.mkv"));
    QVERIFY(!none.matched);
}

void tst_vault_kit::browse_collapse_one_film_folder_folds_companions_and_extras()
{
    // Real shape: Spider-Man No Way Home — one film, a subtitle + a Subs/ folder (companions),
    // an Extras/ and a Featurettes/ folder (folded, never counted, never their own tile), and
    // two junk files (a status .txt and a stray .jpg) that were never media to begin with.
    const QString root = fixtures() + "/browse-film";
    const QList<BrowseNode> nodes = planBrowseLevel(root);
    QCOMPARE(nodes.size(), 1); // companions/extras/junk contribute NO extra tiles

    const BrowseNode film = onlyNode(nodes);
    QCOMPARE(film.nodeType, BrowseNodeType::Film);
    QCOMPARE(film.mediaCount, 1); // the two Extras/Featurettes trailers never counted
    const QString rawName = QStringLiteral(
        "Spider-Man No Way Home (2021) [1080p] [WEBRip] [5.1] [YTS.MX]");
    QCOMPARE(film.displayTitle, cleanMediaFolderTitle(rawName));
    QVERIFY(film.path.endsWith(rawName));
}

void tst_vault_kit::browse_collapse_sibling_season_folders_fold_to_one_show()
{
    // Real shape: Loki Season 1 and Season 2 as SEPARATE sibling folders — must collapse to
    // ONE show tile (locked design #8), not two.
    const QString root = fixtures() + "/browse-show-siblings";
    const QList<BrowseNode> nodes = planBrowseLevel(root);
    QCOMPARE(nodes.size(), 1);

    const BrowseNode show = onlyNode(nodes);
    QCOMPARE(show.nodeType, BrowseNodeType::Show);
    QCOMPARE(show.displayTitle, QStringLiteral("Loki"));
    QCOMPARE(show.heldSeasons, (QList<int>{1, 2}));
    QCOMPARE(show.physicalFact, QStringLiteral("2 seasons"));
    QVERIFY(show.key.contains(QStringLiteral("::show::loki")));

    // Drilling into the collapsed show hands back its two seasons, in order, each pointing at
    // its REAL sibling folder — the collapse never destroys the underlying structure.
    const QList<BrowseNode> seasons = planBrowseLevel(show.key);
    QCOMPARE(seasons.size(), 2);
    QCOMPARE(seasons.at(0).nodeType, BrowseNodeType::Season);
    QCOMPARE(seasons.at(0).seasonNumber, 1);
    QCOMPARE(seasons.at(0).mediaCount, 2);
    QVERIFY(seasons.at(0).path.contains(QStringLiteral("Season 1")));
    QCOMPARE(seasons.at(1).seasonNumber, 2);
    QCOMPARE(seasons.at(1).mediaCount, 2);
    QVERIFY(seasons.at(1).path.contains(QStringLiteral("Season 2")));
}

void tst_vault_kit::browse_collapse_nested_season_folder_reports_honest_presence()
{
    // Real shape: "The Wire ... Season 1-5 S01-S05 ..." claims five seasons in its own name but
    // the disk holds only a "Season 4" subfolder — the tile must say so honestly, not claim 5.
    const QString root = fixtures() + "/browse-show-nested";
    const QList<BrowseNode> nodes = planBrowseLevel(root);
    QCOMPARE(nodes.size(), 1);

    const BrowseNode show = onlyNode(nodes);
    QCOMPARE(show.nodeType, BrowseNodeType::Show);
    const QString rawName = QStringLiteral(
        "The Wire (2002) Season 1-5 S01-S05 [1080p] [BluRay] [5.1]");
    QCOMPARE(show.displayTitle, cleanMediaFolderTitle(rawName));
    QCOMPARE(show.claimedSeasons, (QList<int>{1, 2, 3, 4, 5}));
    QCOMPARE(show.heldSeasons, (QList<int>{4}));
    QCOMPARE(show.physicalFact, QStringLiteral("Season 4 only"));
    QVERIFY(show.path.endsWith(rawName)); // a REAL folder, not a sentinel — one folder, nested

    // Drilling in hands back exactly the one held season.
    const QList<BrowseNode> seasons = planBrowseLevel(show.key);
    QCOMPARE(seasons.size(), 1);
    QCOMPARE(seasons.first().nodeType, BrowseNodeType::Season);
    QCOMPARE(seasons.first().seasonNumber, 4);
    QCOMPARE(seasons.first().mediaCount, 2);
}

void tst_vault_kit::browse_collapse_flat_absolute_numbered_folder_is_a_show()
{
    // Real shape: Gintama — absolute-numbered episodes directly in one folder, no season
    // subfolders at all. Today's SxxExx-only grammar could not resolve "- 003"; Slice 1's
    // combined grammar must.
    const QString root = fixtures() + "/browse-show-absolute";
    const QList<BrowseNode> nodes = planBrowseLevel(root);
    QCOMPARE(nodes.size(), 1);

    const BrowseNode show = onlyNode(nodes);
    QCOMPARE(show.nodeType, BrowseNodeType::Show);
    const QString rawName = QStringLiteral(
        "[Judas] Gintama 001-367 (Seasons 1-10) [BD 1080p][HEVC x265 10bit][Eng-Subs]");
    QCOMPARE(show.displayTitle, cleanMediaFolderTitle(rawName));
    QCOMPARE(show.mediaCount, 3); // the fixture's 3 stub episodes
    QCOMPARE(show.physicalFact, QStringLiteral("3 episodes"));
    QVERIFY(show.path.endsWith(rawName));

    // Drilling in resolves the absolute-numbered episodes directly (no season band — there is
    // no season subfolder to hold one).
    const QList<BrowseNode> episodes = planBrowseLevel(show.path);
    QCOMPARE(episodes.size(), 3);
    for (const BrowseNode& ep : episodes) {
        QCOMPARE(ep.nodeType, BrowseNodeType::Episode);
        QVERIFY(ep.episodeNumber >= 1 && ep.episodeNumber <= 3);
    }
    // "- 003" resolves to episode 3 — the exact case the plan names.
    const BrowseNode e3 = [&]() {
        for (const BrowseNode& ep : episodes)
            if (ep.episodeNumber == 3)
                return ep;
        return BrowseNode{};
    }();
    QCOMPARE(e3.physicalFact, QStringLiteral("Episode 3"));
}

void tst_vault_kit::browse_collapse_loose_clips_folder_stays_a_folder()
{
    // Real shape: a Cricket folder of loose, unrelated highlight clips — no episode grammar
    // applies to any of them, so this must NOT collapse to a show (or a film).
    const QString root = fixtures() + "/browse-clips";
    const QList<BrowseNode> nodes = planBrowseLevel(root);
    QCOMPARE(nodes.size(), 1);

    const BrowseNode folder = onlyNode(nodes);
    QCOMPARE(folder.nodeType, BrowseNodeType::Folder);
    QCOMPARE(folder.mediaCount, 4);
    QCOMPARE(folder.displayTitle, QStringLiteral("Cricket"));

    // Drilling in shows the clips directly, local-only (never episode-shaped, never a
    // catalogue's business — locked design: local-only is certain-and-yours).
    const QList<BrowseNode> clips = planBrowseLevel(folder.path);
    QCOMPARE(clips.size(), 4);
    for (const BrowseNode& c : clips)
        QCOMPARE(c.nodeType, BrowseNodeType::Clip);
}

void tst_vault_kit::browse_collapse_embedded_season_token_is_not_swallowed()
{
    // The existing anchored season-climb guard, proven again at the planner layer: a folder
    // whose name merely EMBEDS a season token ("Community Season 1 [1080p]") is not a bare
    // season directory — it must classify on its own content (here, one film), never get
    // silently absorbed as if it were some other show's season.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir base(tmp.path());
    QVERIFY(base.mkpath(QStringLiteral("Community Season 1 [1080p]")));
    { QFile f(base.filePath(QStringLiteral("Community Season 1 [1080p]/Community.S01E01.mkv")));
      QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }

    const QList<BrowseNode> nodes = planBrowseLevel(tmp.path());
    QCOMPARE(nodes.size(), 1);
    QCOMPARE(nodes.first().nodeType, BrowseNodeType::Film);
}

QTEST_GUILESS_MAIN(tst_vault_kit)
#include "tst_vault_kit.moc"
