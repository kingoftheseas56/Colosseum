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

    std::cout << "PASS AnimeOrderIndex source parsing and identity resolution\n";
    return 0;
}
