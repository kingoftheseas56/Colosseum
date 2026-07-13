// Comic torrent picker contract: only comic archives are eligible, exact/title
// coverage wins inside packs, and the easiest-to-extract format breaks ties.
#include "torrent/ComicTorrentFilePicker.h"

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

ManifestFile file(int index, const QString& name, qint64 bytes = 32 * 1024 * 1024)
{
    return ManifestFile{index, name, bytes};
}
} // namespace

int main()
{
    const QList<ManifestFile> pack{
        file(0, "Batman Vol 2 I Am Suicide.cbr"),
        file(1, "Batman Vol 1 I Am Gotham.cbz"),
        file(2, "Batman Vol 3 I Am Bane.cb7")
    };
    const PickedFile picked = ComicTorrentFilePicker::pick("Batman I Am Gotham", pack);
    require(picked.idx == 1, "picks the requested edition from a multi-volume pack");

    const QList<ManifestFile> noComics{file(0, "cover.jpg"), file(1, "notes.txt")};
    require(ComicTorrentFilePicker::pick("Batman", noComics).idx == -1,
            "manifest without a comic archive fails honestly");

    const QList<ManifestFile> tied{file(0, "Batman I Am Gotham.cbr"),
                                   file(1, "Batman I Am Gotham.cbz")};
    require(ComicTorrentFilePicker::pick("Batman I Am Gotham", tied).idx == 1,
            "cbz wins an otherwise equal match");

    require(ComicTorrentFilePicker::isComicArchive("a.CBR"), "cbr accepted");
    require(ComicTorrentFilePicker::isComicArchive("a.cbz"), "cbz accepted");
    require(ComicTorrentFilePicker::isComicArchive("a.cb7"), "cb7 accepted");
    require(ComicTorrentFilePicker::isComicArchive("a.cbt"), "cbt accepted");
    require(!ComicTorrentFilePicker::isComicArchive("a.pdf"), "pdf rejected");

    std::cout << "comic_torrent_filepicker_harness PASS\n";
    return 0;
}
