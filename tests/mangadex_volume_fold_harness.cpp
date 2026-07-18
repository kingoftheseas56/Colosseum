// Phantom-volume fold contract (the Berserk finding, 2026-07-18).
//
// MangaDex files alternate-edition covers under decimal volume keys ("1.1" =
// a variant cover OF volume 1). foldPhantomCoverVolumes must: drop those keys
// from the shelf, donate a variant's cover to its base volume when the base
// has none, and KEEP a decimal volume the chapter aggregate itself anchors
// (a genuine half-volume). Shapes below mirror the live probes: Berserk
// (ja deluxe reprints as x.1/x.2), Slam Dunk (x.1 + x.2), and a synthetic
// aggregate-anchored 8.5.
#include "engine/MangaDexCatalogClient.h"

#include <QMap>
#include <QSet>
#include <QString>

#include <cstdlib>
#include <iostream>

using tankoban::manga::mangadex::foldPhantomCoverVolumes;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    // 1. Berserk shape: base has its own cover; x.1/x.2 variants fold away silently.
    {
        QMap<double, QString> covers{{1.0, "v1"}, {1.1, "v1-deluxe"}, {1.2, "v1-anniv"},
                                     {2.0, "v2"}, {2.1, "v2-deluxe"}};
        foldPhantomCoverVolumes(covers, QSet<double>{1.0, 2.0});
        require(covers.size() == 2, "variant covers fold away when the base is covered");
        require(covers.value(1.0) == "v1" && covers.value(2.0) == "v2",
                "the base volume keeps its own cover, never a variant's");
    }

    // 2. Donation: the base volume exists only through its variant cover — the
    //    volume is real (a variant cover proves it), so base inherits the cover.
    {
        QMap<double, QString> covers{{41.3, "v41-zh"}};
        foldPhantomCoverVolumes(covers, QSet<double>{});
        require(covers.size() == 1 && covers.contains(41.0),
                "an orphan variant mints its integer base, not a decimal tile");
        require(covers.value(41.0) == "v41-zh", "the orphan variant's cover is donated");
    }

    // 3. Lowest variant donates when several exist (map order is ascending).
    {
        QMap<double, QString> covers{{10.1, "first"}, {10.2, "second"}, {10.3, "third"}};
        foldPhantomCoverVolumes(covers, QSet<double>{});
        require(covers.size() == 1 && covers.value(10.0) == "first",
                "with multiple orphan variants, the lowest-numbered cover wins");
    }

    // 4. A chapter-anchored decimal is a REAL volume and must survive untouched.
    {
        QMap<double, QString> covers{{8.0, "v8"}, {8.5, "v8.5"}, {8.1, "v8-deluxe"}};
        foldPhantomCoverVolumes(covers, QSet<double>{8.0, 8.5});
        require(covers.size() == 2 && covers.contains(8.5),
                "an aggregate-anchored half-volume survives the fold");
        require(covers.value(8.0) == "v8", "the anchored fold still drops unanchored variants");
    }

    // 5. Integers pass through untouched (the common, phantom-free series).
    {
        QMap<double, QString> covers{{1.0, "a"}, {2.0, "b"}, {3.0, "c"}};
        foldPhantomCoverVolumes(covers, QSet<double>{});
        require(covers.size() == 3, "a clean integer shelf is left exactly as it was");
    }

    std::cout << "mangadex_volume_fold_harness: PASS (5 contracts)\n";
    return 0;
}
