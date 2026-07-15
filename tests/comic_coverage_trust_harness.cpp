// Comic coverage + uploader trust contract: format-scoped range grammar
// ("Compendiums v01-v03" covers Compendium 1, never TPB/issue 1) and a
// bounded release-tag trust reader ("(- Nem -)" trusted, "Nemesis" is not).
#include "torrent/ComicCoverage.h"
#include "torrent/ComicEditionIdentity.h"
#include "torrent/ComicUploaderTrust.h"

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
    using namespace ComicCoverage;

    // ── Coverage: the exact three-clause grammar from the design spec ──
    const auto spans = detectComicCoverage("TPBs v01-v25, Compendiums v01-v03, Omnibus 2");
    require(coverageCovers(spans, ComicCollectionFormat::Compendium, 1),
            "compendium range covers target");
    require(!coverageCovers(spans, ComicCollectionFormat::TradePaperback, 26),
            "range upper bound");
    require(!coverageCovers(detectComicCoverage("Invincible 025"), ComicCollectionFormat::Volume, 25),
            "bare issue number is not volume coverage");

    // ── Coverage: additional Rules from the design not exercised above ──
    require(coverageCovers(spans, ComicCollectionFormat::TradePaperback, 1)
                && coverageCovers(spans, ComicCollectionFormat::TradePaperback, 25),
            "tpb range covers both bounds inclusively");
    require(!coverageCovers(spans, ComicCollectionFormat::Omnibus, 1)
                && coverageCovers(spans, ComicCollectionFormat::Omnibus, 2),
            "single Omnibus 2 span covers only its own ordinal");

    const auto generic = detectComicCoverage("v01");
    require(coverageCovers(generic, ComicCollectionFormat::Volume, 1),
            "generic v01 with no other format context reads as Volume");
    require(!coverageCovers(generic, ComicCollectionFormat::Compendium, 1)
                && !coverageCovers(generic, ComicCollectionFormat::TradePaperback, 1),
            "generic v01 never masquerades as Compendium or TPB");

    require(coverageCovers(detectComicCoverage("Omnibus 02"), ComicCollectionFormat::Omnibus, 2),
            "zero-padded single ordinal zero-strips to canonical int");

    require(coverageCovers(detectComicCoverage("Book One-Three"), ComicCollectionFormat::Book, 2),
            "worded range covers an ordinal inside its span");

    // ── The headline production case (DoD 1): the real Nem mega-pack must cover
    //    Compendium 1 (via its "Compendiums v01-v03" clause) but not Compendium 4,
    //    and the year range / bare issue range must NOT register as coverage. ──
    const auto nem = detectComicCoverage(
        "Invincible Collection (000-144,Spin-offs,TPBs v01-v25,Compendiums v01-v03+Extras) (2003-2018) (digital) [ettv] (- Nem -)");
    require(coverageCovers(nem, ComicCollectionFormat::Compendium, 1),
            "Nem pack covers Compendium 1");
    require(!coverageCovers(nem, ComicCollectionFormat::Compendium, 4),
            "Nem pack does not cover Compendium 4");
    require(coverageCovers(nem, ComicCollectionFormat::TradePaperback, 25),
            "Nem pack covers TPB 25 from the same name");

    // ── Trust: exact bounded tag, false substring rejection ──
    const auto trust = ComicUploaderTrust::load();
    require(ComicUploaderTrust::taggedUploader("Invincible Collection (- Nem -)", trust).tier == 1,
            "bounded Nem tag trusted");
    require(ComicUploaderTrust::taggedUploader("The Nemesis Collection", trust).tier == 99,
            "substring is not uploader evidence");

    // ── Trust: blocked tag sentinel (synthetic table; no resource edit needed) ──
    ComicUploaderTrust::TrustTable blockedTable;
    blockedTable.blocked << QStringLiteral("Ripper");
    require(ComicUploaderTrust::taggedUploader("Some Comic [Ripper]", blockedTable).tier == -1,
            "blocked bounded tag reports the blocked sentinel");

    std::cout << "COMIC_COVERAGE_TRUST_OK\n";
    return 0;
}
