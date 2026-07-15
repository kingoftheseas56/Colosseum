// AnimeOrderIndex deterministic parser + identity-resolution contract.
//
// Loads the two hand-authored fixtures from argv, builds an immutable index,
// and proves: cross-source identity merge (Fribb IDs joined to the Anime-Lists
// TVDB id), typed/list-valued IMDb and TMDB preservation, ambiguity on
// conflicting works, title-matching refusal, and deterministic rejection of
// malformed/empty/duplicate sources. Task 2 extends this same harness with
// provider-episode reconciliation.
//
// House convention: require() prints "FAIL: <msg>" and exits 1 (Release-safe,
// unlike Q_ASSERT which compiles out under NDEBUG). Success prints one PASS line.
#include "anime/AnimeOrderIndex.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

QByteArray readFixture(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: cannot open fixture " << path.toStdString() << '\n';
        std::exit(1);
    }
    return f.readAll();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    require(args.size() >= 3, "usage: anime_order_index_harness <fribb.json> <anime-list.xml>");

    const QByteArray fribb = readFixture(args.at(1));
    const QByteArray xml = readFixture(args.at(2));

    QString error = QStringLiteral("sentinel");
    const auto index = AnimeOrderIndex::fromSources(fribb, xml, &error);
    require(index != nullptr, "index built from valid fixtures");
    require(error.isEmpty(), "no error string on valid fixtures");
    require(index->entryCount() == 7, "seven identity entries parsed from fribb fixture");

    // ── One Piece: identity merged across Fribb + Anime-Lists, typed IDs kept ──
    const QVariantMap onePiece =
        index->resolve({{"sourceId", "mal:21"}, {"resolvedId", "tt0388629"}}, {});
    require(onePiece.value("status").toString() == "mapped", "one piece resolves mapped");
    const QVariantMap opIds = onePiece.value("ids").toMap();
    require(opIds.value("anidb").toInt() == 69, "one piece anidb id");
    require(opIds.value("tvdb").toInt() == 81797, "one piece tvdb id joined from anime-list");
    require(opIds.value("imdb").toStringList() == QStringList{"tt0388629"},
            "one piece imdb list preserved");
    require(opIds.value("tmdb").toMap().value("tv").toInt() == 37854, "one piece tmdb tv id");

    // A raw AniDB-only identity resolves the same work.
    require(index->resolve({{"sourceId", "anidb:69"}}, {}).value("status").toString() == "mapped",
            "anidb-prefixed identity resolves");

    // ── Movie: typed TMDB movie list cardinality preserved ────────────────────
    const QVariantMap movie = index->resolve({{"sourceId", "mal:7000"}}, {});
    require(movie.value("status").toString() == "mapped", "movie resolves mapped");
    require(movie.value("ids").toMap().value("tmdb").toMap().value("movies").toList()
                == QVariantList({101, 102}),
            "movie tmdb movies list preserved");

    // ── Naruto: multiple IMDb ids, either one resolves the same work ──────────
    require(index->resolve({{"resolvedId", "tt0409592"}}, {}).value("ids").toMap()
                .value("anidb").toInt() == 239,
            "second naruto imdb id still resolves the work");

    // ── Conflicting identities across two works → ambiguous, no ordering ──────
    const QVariantMap conflict =
        index->resolve({{"sourceId", "mal:21"}, {"resolvedId", "tt9999999"}}, {});
    require(conflict.value("status").toString() == "ambiguous", "conflicting ids are ambiguous");

    // ── Title-only input must never resolve (title matching forbidden) ────────
    const QVariantMap titleOnly = index->resolve({{"title", "Long Runner"}}, {});
    require(titleOnly.value("status").toString() == "unmapped", "title-only input is unmapped");

    // An unknown id resolves to nothing.
    require(index->resolve({{"sourceId", "mal:424242"}}, {}).value("status").toString() == "unmapped",
            "unknown source id is unmapped");

    // ── Deterministic rejection of corrupt / empty / duplicate sources ────────
    const QByteArray goodFribb = "[{\"type\":\"TV\",\"anidb_id\":1,\"mal_id\":1}]";
    const QByteArray goodXml =
        "<?xml version=\"1.0\"?><anime-list>"
        "<anime anidbid=\"1\" tvdbid=\"10\" defaulttvdbseason=\"a\" episodeoffset=\"\"/>"
        "</anime-list>";
    QString e2;
    require(AnimeOrderIndex::fromSources("{ not json ", goodXml, &e2) == nullptr && !e2.isEmpty(),
            "malformed json rejected with a non-empty error");
    require(AnimeOrderIndex::fromSources(goodFribb, "<nope/>", &e2) == nullptr && !e2.isEmpty(),
            "non anime-list xml root rejected");
    require(AnimeOrderIndex::fromSources("[{\"anidb_id\":1},{\"anidb_id\":1}]", goodXml, &e2) == nullptr
                && !e2.isEmpty(),
            "duplicate anidb record rejected");
    require(AnimeOrderIndex::fromSources("[]", goodXml, &e2) == nullptr && !e2.isEmpty(),
            "empty fribb source rejected");
    require(AnimeOrderIndex::fromSources(goodFribb, "<anime-list></anime-list>", &e2) == nullptr
                && !e2.isEmpty(),
            "empty xml source rejected");

    // ── A reused NON-primary id makes that lookup ambiguous but keeps the source
    //    valid (only a duplicate primary AniDB key invalidates a source) ───────
    const QByteArray sharedMal =
        "[{\"type\":\"TV\",\"anidb_id\":1,\"mal_id\":5},"
        " {\"type\":\"TV\",\"anidb_id\":2,\"mal_id\":5}]";
    const QByteArray twoXml =
        "<?xml version=\"1.0\"?><anime-list>"
        "<anime anidbid=\"1\" tvdbid=\"10\"/><anime anidbid=\"2\" tvdbid=\"20\"/></anime-list>";
    QString e3 = QStringLiteral("x");
    const auto shared = AnimeOrderIndex::fromSources(sharedMal, twoXml, &e3);
    require(shared != nullptr && e3.isEmpty(), "shared non-primary id keeps source valid");
    require(shared->resolve({{"sourceId", "mal:5"}}, {}).value("status").toString() == "ambiguous",
            "shared mal id resolves ambiguous across two works");

    // ═══════════════ Provider-episode reconciliation (Task 2) ═══════════════
    // Every provider row and its original stream id survive; canonical data only
    // annotates and reorders. Absolute completeness requires unique, positive,
    // contiguous absolute numbers across the regular rows supplied (a truthful
    // window is allowed; holes are not).
    const auto row = [](const QString& id, int season, int episode, const QString& title) {
        return QVariantMap{{"id", id}, {"season", season}, {"episode", episode}, {"title", title}};
    };
    const auto genSeasonRows = [&](const QString& imdb, int season, int count) {
        QVariantList out;
        for (int e = 1; e <= count; ++e)
            out.append(row(QStringLiteral("%1:%2:%3").arg(imdb).arg(season).arg(e), season, e,
                           QStringLiteral("Ep %1").arg(e)));
        return out;
    };

    // ── Primary: a contiguous window (abs 7,8,9) crossing the S1→S2 provider
    //    boundary, plus one special. Complete, and default for a "a" mapping. ──
    {
        const QVariantList rows{
            row("tt0388629:1:7", 1, 7, "Seventh"),
            row("tt0388629:1:8", 1, 8, "Eighth"),
            row("tt0388629:2:1", 2, 1, "Ninth"),
            row("tt0388629:0:3", 0, 3, "Special")
        };
        const QVariantMap result = index->resolve({{"sourceId", "mal:21"}}, rows);
        require(result.value("status").toString() == "mapped", "one piece window resolves mapped");
        require(result.value("absoluteComplete").toBool(), "contiguous window is absolute-complete");
        require(result.value("defaultOrder").toString() == "absolute", "a-mapping defaults to absolute");
        const QVariantList eps = result.value("episodes").toList();
        require(eps.size() == rows.size(), "every provider row preserved (regular + special)");
        require(eps.at(2).toMap().value("streamId").toString() == "tt0388629:2:1",
                "regular rows sorted by absolute number, S2E1 third");
        require(eps.at(2).toMap().value("absoluteNumber").toInt() == 9,
                "cross-season absolute number is 9");
        require(eps.at(2).toMap().value("sourceSeason").toInt() == 2
                    && eps.at(2).toMap().value("sourceEpisode").toInt() == 1,
                "source season/episode preserved on the absolute row");
        require(eps.at(3).toMap().value("kind").toString() == "special", "season 0 row is a special");
        require(eps.at(3).toMap().value("absoluteNumber").isNull(),
                "special carries no absolute number");
        require(result.value("seasons").toList().size() == 3, "three seasons summarised");
    }

    // ── Long-runner terminals: full contiguous season arrays, exact end ───────
    {
        const QVariantMap r = index->resolve({{"sourceId", "mal:918"}}, genSeasonRows("tt0988818", 1, 367));
        require(r.value("absoluteComplete").toBool(), "gintama 1-367 is complete");
        const QVariantList eps = r.value("episodes").toList();
        require(eps.size() == 367, "gintama has 367 episodes with no missing/duplicate slot");
        require(eps.last().toMap().value("absoluteNumber").toInt() == 367, "gintama terminal absolute 367");
    }
    {
        const QVariantMap r = index->resolve({{"sourceId", "mal:20"}}, genSeasonRows("tt0409591", 1, 220));
        require(r.value("absoluteComplete").toBool(), "naruto 1-220 is complete");
        require(r.value("episodes").toList().last().toMap().value("absoluteNumber").toInt() == 220,
                "naruto terminal absolute 220");
    }
    {
        const QVariantMap r = index->resolve({{"sourceId", "mal:1735"}}, genSeasonRows("tt0988824", 1, 500));
        require(r.value("absoluteComplete").toBool(), "shippuden 1-500 is complete");
        require(r.value("episodes").toList().last().toMap().value("absoluteNumber").toInt() == 500,
                "shippuden terminal absolute 500");
    }

    // ── Default season + episode offset (no mapping-list): S1E11 → absolute 1 ─
    {
        const QVariantList rows{
            row("tt9999999:1:11", 1, 11, "a"), row("tt9999999:1:12", 1, 12, "b"),
            row("tt9999999:1:13", 1, 13, "c"), row("tt9999999:1:14", 1, 14, "d"),
            row("tt9999999:1:15", 1, 15, "e")
        };
        const QVariantMap r = index->resolve({{"sourceId", "mal:8000"}}, rows);
        require(r.value("absoluteComplete").toBool(), "default-offset mapping is complete");
        require(r.value("defaultOrder").toString() == "seasons",
                "numeric default season keeps seasons as the default view");
        const QVariantList eps = r.value("episodes").toList();
        require(eps.first().toMap().value("absoluteNumber").toInt() == 1,
                "default episodeoffset 10 maps S1E11 to absolute 1");
        require(eps.last().toMap().value("absoluteNumber").toInt() == 5, "S1E15 maps to absolute 5");
    }

    // ── One-to-many body (;1-1+2;) → duplicate absolute → incomplete ──────────
    {
        const QVariantList rows{ row("tt0900000:1:1", 1, 1, "a"), row("tt0900000:1:2", 1, 2, "b") };
        const QVariantMap r = index->resolve({{"sourceId", "mal:9000"}}, rows);
        require(r.value("status").toString() == "mapped", "one-to-many show still resolves identity");
        require(!r.value("absoluteComplete").toBool(),
                "one-to-many / duplicate absolute makes it incomplete");
        require(r.value("defaultOrder").toString() == "seasons", "incomplete falls back to seasons");
        require(r.value("episodes").toList().size() == 2, "one-to-many rows preserved");
    }

    // ── Unmatched provider row: preserved byte-for-byte, and forces incomplete ─
    {
        const QVariantList rows{
            row("tt0388629:1:7", 1, 7, "Seventh"),
            row("tt0388629:1:8", 1, 8, "Eighth"),
            row("tt0388629:2:1", 2, 1, "Ninth"),
            row("tt0388629:5:99", 5, 99, "Orphan") // tvdb season 5 has no mapping
        };
        const QVariantMap r = index->resolve({{"sourceId", "mal:21"}}, rows);
        require(!r.value("absoluteComplete").toBool(),
                "an unmatched regular row makes the mapping incomplete");
        const QVariantList eps = r.value("episodes").toList();
        require(eps.size() == 4, "unmatched row is not dropped");
        bool found = false;
        for (const QVariant& v : eps) {
            const QVariantMap m = v.toMap();
            if (m.value("streamId").toString() != "tt0388629:5:99")
                continue;
            found = true;
            require(!m.value("mapped").toBool(), "orphan row is unmapped");
            require(m.value("sourceSeason").toInt() == 5 && m.value("sourceEpisode").toInt() == 99,
                    "orphan row keeps its source season/episode");
            require(m.value("title").toString() == "Orphan", "orphan row keeps its title");
        }
        require(found, "orphan row remains identifiable by its original stream id");
    }

    // ── Target zero / specials never affect regular completeness ──────────────
    {
        const QVariantList rows{
            row("tt0388629:1:7", 1, 7, "a"), row("tt0388629:1:8", 1, 8, "b"),
            row("tt0388629:2:1", 2, 1, "c"), row("tt0388629:0:0", 0, 0, "zero")
        };
        const QVariantMap r = index->resolve({{"sourceId", "mal:21"}}, rows);
        require(r.value("absoluteComplete").toBool(), "a season-0 row does not affect regular completeness");
        bool foundZero = false;
        for (const QVariant& v : r.value("episodes").toList()) {
            const QVariantMap m = v.toMap();
            if (m.value("streamId").toString() != "tt0388629:0:0")
                continue;
            foundZero = true;
            require(m.value("kind").toString() == "special", "target-zero season-0 row is a special");
            require(!m.value("mapped").toBool(), "target-zero row is not mapped");
        }
        require(foundZero, "target-zero row remains present");
    }

    // ── Provider input order does not affect canonical regular ordering ───────
    {
        const QVariantList forward{
            row("tt0388629:1:7", 1, 7, "a"), row("tt0388629:1:8", 1, 8, "b"), row("tt0388629:2:1", 2, 1, "c") };
        const QVariantList reversed{
            row("tt0388629:2:1", 2, 1, "c"), row("tt0388629:1:8", 1, 8, "b"), row("tt0388629:1:7", 1, 7, "a") };
        QStringList aIds, bIds;
        for (const QVariant& v : index->resolve({{"sourceId", "mal:21"}}, forward).value("episodes").toList())
            aIds << v.toMap().value("streamId").toString();
        for (const QVariant& v : index->resolve({{"sourceId", "mal:21"}}, reversed).value("episodes").toList())
            bIds << v.toMap().value("streamId").toString();
        require(aIds == bIds, "canonical ordering is independent of provider input order");
        require(aIds == (QStringList{"tt0388629:1:7", "tt0388629:1:8", "tt0388629:2:1"}),
                "regular rows ordered by ascending absolute number");
    }

    // ── Explicit body pair beats a range mapping (inline fixture) ─────────────
    {
        const QByteArray fj = "[{\"type\":\"TV\",\"anidb_id\":500,\"mal_id\":500}]";
        const QByteArray fx =
            "<anime-list><anime anidbid=\"500\" tvdbid=\"5000\" defaulttvdbseason=\"a\">"
            "<mapping-list>"
            "<mapping anidbseason=\"1\" tvdbseason=\"1\" start=\"1\" end=\"100\" offset=\"0\">;42-5;</mapping>"
            "</mapping-list></anime></anime-list>";
        QString er;
        const auto mini = AnimeOrderIndex::fromSources(fj, fx, &er);
        require(mini != nullptr, "inline body-precedence fixture builds");
        const QVariantList rows{ row("x:1:5", 1, 5, "five"), row("x:1:6", 1, 6, "six") };
        const QVariantList eps = mini->resolve({{"sourceId", "mal:500"}}, rows).value("episodes").toList();
        require(eps.at(0).toMap().value("streamId").toString() == "x:1:6", "range row sorts first");
        require(eps.at(1).toMap().value("absoluteNumber").toInt() == 42,
                "explicit body pair overrides the range mapping");
    }

    // ── Many-to-one body (two anidb episodes share one tvdb episode) → incomplete ─
    {
        const QByteArray fj = "[{\"type\":\"TV\",\"anidb_id\":600,\"mal_id\":600}]";
        const QByteArray fx =
            "<anime-list><anime anidbid=\"600\" tvdbid=\"6000\" defaulttvdbseason=\"a\">"
            "<mapping-list>"
            "<mapping anidbseason=\"1\" tvdbseason=\"1\">;1-5;2-5;</mapping>"
            "</mapping-list></anime></anime-list>";
        QString er;
        const auto mini = AnimeOrderIndex::fromSources(fj, fx, &er);
        require(mini != nullptr, "inline many-to-one fixture builds");
        const QVariantMap r = mini->resolve({{"sourceId", "mal:600"}}, QVariantList{ row("y:1:5", 1, 5, "five") });
        require(!r.value("absoluteComplete").toBool(),
                "many-to-one mapping is incomplete (V1 will not guess the owning row)");
        require(r.value("episodes").toList().size() == 1, "many-to-one row still preserved");
    }

    // ── A mapped-but-incomplete show still lets the identity resolve ──────────
    require(index->resolve({{"sourceId", "mal:9000"}}, {}).value("status").toString() == "mapped",
            "mapped identity holds even with no provider rows");

    std::cout << "PASS AnimeOrderIndex source parsing, identity resolution, and episode reconciliation\n";
    return 0;
}
