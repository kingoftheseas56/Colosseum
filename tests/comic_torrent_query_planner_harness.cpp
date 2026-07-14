// Comic torrent query-planner contract: an edition fans out into canonical
// title, ISBN, and collected-range identity variants without duplicates; a
// manual query is a single trimmed search that never appends the auto variants.
#include "torrent/ComicTorrentQueryPlanner.h"

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
} // namespace

int main()
{
    const QStringList saga = ComicTorrentQueryPlanner::automaticQueries(
        "Saga", "Saga: Book One", "9781632150783", "Saga #1-18");
    require(saga == QStringList({"Saga: Book One", "9781632150783", "Saga #1-18"}),
            "Saga identity cascade is canonical title, ISBN, then range");

    const QStringList noDuplicate = ComicTorrentQueryPlanner::automaticQueries(
        "Batman", "Batman: I Am Gotham", "", "Batman #1-6");
    require(noDuplicate == QStringList({"Batman: I Am Gotham", "Batman #1-6"}),
            "missing ISBN is omitted and collection already owns the series name");

    // A bare collected range gets the series prefixed exactly once.
    const QStringList prefixed = ComicTorrentQueryPlanner::automaticQueries(
        "Saga", "Saga: Book One", "", "#1-18");
    require(prefixed == QStringList({"Saga: Book One", "Saga #1-18"}),
            "a series-less collection string is prefixed with the series once");

    require(ComicTorrentQueryPlanner::manualQuery("  Saga Compendium One  ")
                == QStringList({"Saga Compendium One"}),
            "manual query is trimmed and remains a single search");
    require(ComicTorrentQueryPlanner::manualQuery("   ").isEmpty(),
            "blank manual query starts no search");

    std::cout << "comic_torrent_query_planner_harness PASS\n";
    return 0;
}
