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

    std::cout<<"book_torrent_ranker_harness PASS\n"; return 0;
}
