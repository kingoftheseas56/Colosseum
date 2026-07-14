// Comic torrent archive-decision contract: only comic archives are eligible; a
// lone archive or a single exact-title archive auto-selects; every other pack
// (multi-volume, or multiple exact matches) pauses for a manual choice. Format
// preference may order candidates but never silently decides an ambiguous pack.
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
    // A lone comic archive auto-selects.
    const ComicArchiveDecision lone = ComicTorrentFilePicker::decide(
        QStringLiteral("Saga Book One"), {file(0, "Saga Book One.cbz")});
    require(!lone.requiresChoice && lone.selected.idx == 0,
            "one comic archive auto-selects");
    require(lone.candidates.size() == 1, "the lone archive is the only candidate");

    // A single exact-title archive inside a pack auto-selects; the near-miss doesn't.
    const ComicArchiveDecision exact = ComicTorrentFilePicker::decide(
        QStringLiteral("Batman I Am Gotham"),
        {file(0, "Batman I Am Suicide.cbr"), file(1, "Batman I Am Gotham.cbz")});
    require(!exact.requiresChoice && exact.selected.idx == 1,
            "one unique exact title auto-selects inside a pack");

    // A multi-volume pack with no exact match pauses for a manual choice.
    const ComicArchiveDecision ambiguous = ComicTorrentFilePicker::decide(
        QStringLiteral("Saga Book One"),
        {file(0, "Saga v01.cbz"), file(1, "Saga v02.cbz")});
    require(ambiguous.requiresChoice && ambiguous.candidates.size() == 2,
            "multi-volume pack pauses for manual choice");
    require(ambiguous.selected.idx == -1, "an ambiguous pack selects nothing on its own");

    // Two exact-title archives (cbr vs cbz) are ambiguous — format never breaks it.
    const ComicArchiveDecision twoExact = ComicTorrentFilePicker::decide(
        QStringLiteral("Batman I Am Gotham"),
        {file(0, "Batman I Am Gotham.cbr"), file(1, "Batman I Am Gotham.cbz")});
    require(twoExact.requiresChoice && twoExact.candidates.size() == 2,
            "format preference cannot silently decide two exact-title archives");

    // A manifest with no comic archive yields no candidates and no auto-pick.
    const ComicArchiveDecision none = ComicTorrentFilePicker::decide(
        QStringLiteral("Batman"), {file(0, "cover.jpg"), file(1, "notes.txt")});
    require(none.candidates.isEmpty() && !none.requiresChoice && none.selected.idx == -1,
            "manifest without a comic archive offers nothing");

    // Non-comic files never become candidates even amid a comic archive.
    const ComicArchiveDecision mixed = ComicTorrentFilePicker::decide(
        QStringLiteral("Saga Book One"),
        {file(0, "Saga Book One.cbz"), file(1, "readme.txt"), file(2, "Saga Extras.pdf")});
    require(!mixed.requiresChoice && mixed.selected.idx == 0 && mixed.candidates.size() == 1,
            "only comic archives are eligible candidates");

    // pick() stays a compatibility wrapper: the lone/exact selection, else nothing.
    require(ComicTorrentFilePicker::pick(QStringLiteral("Saga Book One"),
                {file(0, "Saga Book One.cbz")}).idx == 0,
            "pick() compatibility returns the auto-selected archive");
    require(ComicTorrentFilePicker::pick(QStringLiteral("Saga Book One"),
                {file(0, "Saga v01.cbz"), file(1, "Saga v02.cbz")}).idx == -1,
            "pick() compatibility yields nothing when a choice is required");

    require(ComicTorrentFilePicker::isComicArchive("a.CBR"), "cbr accepted");
    require(ComicTorrentFilePicker::isComicArchive("a.cbz"), "cbz accepted");
    require(ComicTorrentFilePicker::isComicArchive("a.cb7"), "cb7 accepted");
    require(ComicTorrentFilePicker::isComicArchive("a.cbt"), "cbt accepted");
    require(!ComicTorrentFilePicker::isComicArchive("a.pdf"), "pdf rejected");

    std::cout << "comic_torrent_filepicker_harness PASS\n";
    return 0;
}
