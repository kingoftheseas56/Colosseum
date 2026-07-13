// book_torrent_ranker_harness.cpp — rank() contract: dedup by infoHash keeping max
// seeders; sort by match tier desc then seeders desc; WORD-BOUNDARY match; pack + format.
#include "torrent/BookTorrentRanker.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool c, const char* m){ if(!c){ std::cerr<<"FAIL: "<<m<<'\n'; std::exit(1);} }
TorrentResult mk(const QString& title, int seeders, const QString& hash, qint64 size=2*1024*1024){
    TorrentResult r; r.title=title; r.seeders=seeders; r.infoHash=hash; r.sizeBytes=size; return r;
}
}

int main(){
    // 1) Dedup by infoHash keeps the max-seeder copy
    QList<TorrentResult> a{ mk("Dune",10,"a"), mk("Dune",99,"a"), mk("Dune",5,"b") };
    auto r1 = BookTorrentRanker::rank("Dune","Frank Herbert",a);
    require(r1.size()==2, "dedup by infoHash");
    require(r1.first().src.seeders==99, "kept max-seeder copy of the dup");

    // 2) Exact title beats partial regardless of seeders
    QList<TorrentResult> b{ mk("Random Unrelated Book",900,"x"), mk("Dune",3,"y") };
    auto r2 = BookTorrentRanker::rank("Dune","Frank Herbert",b);
    require(r2.first().src.infoHash=="y", "exact-title match outranks higher-seed partial");

    // 3) Same tier -> seeders desc
    QList<TorrentResult> c{ mk("Dune epub",7,"p"), mk("Dune epub",50,"q") };
    auto r3 = BookTorrentRanker::rank("Dune","",c);
    require(r3.first().src.seeders==50, "within a tier, most seeders first");

    // 4) Article-insensitive match ("The Hobbit" ~ "Hobbit")
    QList<TorrentResult> d{ mk("Hobbit",4,"h") };
    auto r4 = BookTorrentRanker::rank("The Hobbit","",d);
    require(r4.first().matchTier >= 3, "leading-article stripped for matching");

    // 5) WORD-BOUNDARY: short title must NOT match a longer unrelated word
    QList<TorrentResult> wb{ mk("Emmanuels Gift",500,"e"), mk("Emma",2,"g") };
    auto r5 = BookTorrentRanker::rank("Emma","",wb);
    require(r5.first().src.infoHash=="g", "\"Emma\" matches \"Emma\", not \"Emmanuels Gift\"");

    // 6) Pack flagged for an explicit multi-count title
    QList<TorrentResult> e{ mk("Sci-Fi EPUB Collection 5000 books",800,"c",40LL*1024*1024*1024) };
    auto r6 = BookTorrentRanker::rank("Dune","",e);
    require(r6.first().pack, "explicit '5000 books' flagged as pack");

    // 7) A legit single novel with a pack-ish word + normal size is NOT a pack
    QList<TorrentResult> f{ mk("The Midnight Library",30,"m",3LL*1024*1024) };
    auto r7 = BookTorrentRanker::rank("The Midnight Library","",f);
    require(!r7.first().pack, "single novel 'The Midnight Library' not badged pack");

    // 8) A large single PDF (scanned textbook) is NOT a pack
    QList<TorrentResult> g{ mk("Gray's Anatomy.pdf",12,"pdf1",120LL*1024*1024) };
    auto r8 = BookTorrentRanker::rank("Grays Anatomy","",g);
    require(!r8.first().pack, "120MB single PDF not badged pack");

    // 9) Format guessed from the title suffix
    QList<TorrentResult> h{ mk("Dune.epub",4,"z") };
    auto r9 = BookTorrentRanker::rank("Dune","",h);
    require(r9.first().formatGuess=="EPUB", "format guessed from .epub in title");

    // ── isReadableBook: the reading shelf keeps ebooks, drops audiobooks/video ──
    // Tankorent already classifies by category; a clean helper so an audiobook
    // never masquerades as an epub on the download-to-read shelf.
    auto mkc = [](const QString& title, const QString& sourceKey,
                  const QString& catId, const QString& cat){
        TorrentResult r; r.title=title; r.seeders=10; r.infoHash="h";
        r.sourceKey=sourceKey; r.categoryId=catId; r.category=cat; return r;
    };

    // 10) Audiobook betrayed by its title (no category) -> NOT readable
    require(!BookTorrentRanker::isReadableBook(
                mk("A Game of Thrones George R R Martin 2003 Audiobook Fantasy",5,"ab")),
            "audiobook title dropped from reading shelf");

    // 11) PirateBay Audio category (102) -> NOT readable even with a clean title
    require(!BookTorrentRanker::isReadableBook(
                mkc("A Song of Ice and Fire","piratebay","102","Audio")),
            "piratebay audio category (102) dropped");

    // 12) PirateBay E-books category (601) -> readable
    require(BookTorrentRanker::isReadableBook(
                mkc("A Game of Thrones","piratebay","601","Other")),
            "piratebay e-books category (601) kept");

    // 13) Video release betrayed by its title -> NOT readable
    require(!BookTorrentRanker::isReadableBook(
                mk("Game of Thrones S01E01 1080p BluRay x264",900,"tv")),
            "video title (S01E01/1080p) dropped");

    // 14) PirateBay Video category (207) -> NOT readable
    require(!BookTorrentRanker::isReadableBook(
                mkc("Game of Thrones","piratebay","207","Video")),
            "piratebay video category (2xx) dropped");

    // 15) ExtTorrents Music (where audiobooks often land there) -> NOT readable
    require(!BookTorrentRanker::isReadableBook(
                mkc("A Game of Thrones","exttorrents","music","Music")),
            "exttorrents music category dropped");

    // 16) ExtTorrents Books -> readable
    require(BookTorrentRanker::isReadableBook(
                mkc("Dune","exttorrents","books","Books")),
            "exttorrents books category kept");

    // 17) Plain book title, no category (torrents-csv) -> readable (unknown kept)
    require(BookTorrentRanker::isReadableBook(mk("A Game of Thrones",8,"csv")),
            "uncategorized clean book title kept");

    // ── ranking: among genuine matches, seeders decide (title-first vs author-first
    //    is the SAME book — it must not gate above the seeder count) ──

    // 18) A 14-seed all-tokens match ("GRRM ... A Game of Thrones") must outrank a
    //     1-seed title-leading match. Before the fix the title-leading string won on
    //     match tier alone — the exact weirdness Hemanth saw (high seeders sunk low).
    QList<TorrentResult> sr{ mk("A Game of Thrones - GRRM",1,"low"),
                             mk("GRRM A Game of Thrones Epub",14,"high") };
    auto r18 = BookTorrentRanker::rank("A Game of Thrones","GRRM",sr);
    require(r18.first().src.infoHash=="high", "among matches, higher seeders ranks first");

    // 19) GUARD: an exact title still beats a higher-seed prefix superset (a sequel /
    //     boxset sharing the leading words) — collapsing prefix↔all-tokens must not
    //     let "Dune Messiah" (500 seed) leapfrog the exact "Dune".
    QList<TorrentResult> ex{ mk("Dune",3,"exact"), mk("Dune Messiah",500,"seq") };
    auto r19 = BookTorrentRanker::rank("Dune","",ex);
    require(r19.first().src.infoHash=="exact", "exact title beats higher-seed prefix superset");

    std::cout<<"book_torrent_ranker_harness PASS\n"; return 0;
}
