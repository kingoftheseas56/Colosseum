// Canonical collected-edition identity contract: format aliasing, ordinal
// parsing scoped to a recognized collection token, and multi-series
// collected-issue expansion with diagnostics for unparseable fragments.
#include "torrent/ComicEditionIdentity.h"

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
    using namespace ComicEditionIdentity;

    require(parseFormat("Compendiums") == ComicCollectionFormat::Compendium, "compendium alias");
    require(parseOrdinal("Invincible Compendium #01", ComicCollectionFormat::Compendium) == 1,
            "numbered compendium");
    require(parseOrdinal("Saga Book Two", ComicCollectionFormat::Book) == 2, "worded book");
    require(parseOrdinal("Hellboy Omnibus IV", ComicCollectionFormat::Omnibus) == 4, "roman omnibus");
    require(parseOrdinal("Batman 2016 Deluxe", ComicCollectionFormat::Deluxe) == -1,
            "unscoped year is not ordinal");

    const auto parsed = parseCollectedIssues("Invincible", "Invincible #0, #14-16; The Pact #4");
    require(parsed.complete && parsed.issues.size() == 5, "multi-series issue expansion");
    require(parsed.issues[4].series == "The Pact" && parsed.issues[4].number == 4,
            "cross-series issue identity");

    require(!parseCollectedIssues("Invincible", "#1-3 plus bonus material").complete,
            "unparsed required fragment disables automatic issue set");

    // buildTarget assembles the shared target and MUST carry the completeness
    // signal through — the issue-range path depends on it (DoD 4).
    const auto complete = buildTarget("chId1", "sid", "Invincible", "Invincible Compendium #1",
                                      "Compendium", "978-1-60706-411-4", "Invincible #1-3");
    require(complete.format == ComicCollectionFormat::Compendium && complete.ordinal == 1,
            "buildTarget resolves format + ordinal");
    require(complete.isbnDigits == "9781607064114", "buildTarget extracts ISBN digits");
    require(complete.collectedIssuesComplete && complete.collectedIssues.size() == 3,
            "buildTarget preserves a complete collected-issue set");
    const auto partial = buildTarget("chId2", "sid", "Invincible", "Invincible Compendium #1",
                                     "Compendium", "", "#1-3 plus bonus material");
    require(!partial.collectedIssuesComplete,
            "buildTarget preserves an incomplete collected-issue set");

    std::cout << "COMIC_EDITION_IDENTITY_OK\n";
    return 0;
}
