// book_torrent_filepicker_harness.cpp — pick() contract: choose the single best ebook
// file; title separated from author; exact-title wins in a same-author pack; djvu excluded.
#include "torrent/BookTorrentFilePicker.h"
#include <cstdlib>
#include <iostream>

namespace {
void require(bool c, const char* m){ if(!c){ std::cerr<<"FAIL: "<<m<<'\n'; std::exit(1);} }
ManifestFile mf(int idx, const QString& name, qint64 len=2*1024*1024){ return ManifestFile{idx,name,len}; }
}

int main(){
    // 1) Picks the title-matching epub over an unrelated pdf
    QList<ManifestFile> a{ mf(0,"readme.txt"), mf(1,"Dune - Frank Herbert.epub"), mf(2,"Some Other Book.pdf") };
    auto p1 = BookTorrentFilePicker::pick("Dune","Frank Herbert",a);
    require(p1.idx==1, "picks the matching epub");
    require(p1.ext=="epub", "records ext");

    // 2) No ebook files -> honest -1
    QList<ManifestFile> b{ mf(0,"cover.jpg"), mf(1,"metadata.opf") };
    auto p2 = BookTorrentFilePicker::pick("Dune","",b);
    require(p2.idx==-1, "no ebook file -> -1");

    // 3) On equal name match, epub beats pdf
    QList<ManifestFile> c{ mf(0,"Dune.pdf"), mf(1,"Dune.epub") };
    auto p3 = BookTorrentFilePicker::pick("Dune","",c);
    require(p3.idx==1, "epub preferred over pdf on equal match");

    // 4) Inside a pack, picks the one titled file
    QList<ManifestFile> d{ mf(0,"Asimov - Foundation.epub"),
                           mf(1,"Herbert - Dune.epub"),
                           mf(2,"Tolkien - LOTR.epub") };
    auto p4 = BookTorrentFilePicker::pick("Dune","Frank Herbert",d);
    require(p4.idx==1, "matches the requested title inside a pack");

    // 5) Same-SERIES pack + same author: EXACT title beats sequels sharing the base token
    QList<ManifestFile> e{ mf(0,"Frank Herbert - Children of Dune.epub"),
                           mf(1,"Dune Messiah.epub"),
                           mf(2,"Dune.epub") };
    auto p5 = BookTorrentFilePicker::pick("Dune","Frank Herbert",e);
    require(p5.idx==2, "exact-title file beats same-series siblings + author-token overlap");

    // 6) djvu is NOT an ebook we pick (reader can't render it)
    QList<ManifestFile> f{ mf(0,"Dune.djvu") };
    auto p6 = BookTorrentFilePicker::pick("Dune","",f);
    require(p6.idx==-1, "djvu excluded -> no pickable ebook");

    // 7) azw3 is NOT wired into the reader → excluded (a lone azw3 = nothing pickable)
    QList<ManifestFile> g{ mf(0,"Dune.azw3") };
    auto p7 = BookTorrentFilePicker::pick("Dune","",g);
    require(p7.idx==-1, "azw3 excluded -> no pickable ebook");

    // 8) with both, the reader-renderable epub is picked over the azw3
    QList<ManifestFile> h{ mf(0,"Dune.azw3"), mf(1,"Dune.epub") };
    auto p8 = BookTorrentFilePicker::pick("Dune","",h);
    require(p8.idx==1 && p8.ext=="epub", "epub picked over azw3");

    std::cout<<"book_torrent_filepicker_harness PASS\n"; return 0;
}
