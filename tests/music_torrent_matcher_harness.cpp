#include "torrent/MusicTorrentMatcher.h"
#include "torrent/MusicTorrentQueryPlanner.h"
#include <cstdlib>
#include <iostream>

void require(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
int main()
{
    MusicTorrentTrack track{QStringLiteral("Artist"), QStringLiteral("Album"), QStringLiteral("Song"), 1, 1, 180000};
    MusicTorrentFile file{7, QStringLiteral("Artist/Album/01 Song.flac")};
    require(MusicTorrentMatcher::pick(track, {file}).index == 7, "exact manifest path");
    auto other = file; other.path = QStringLiteral("Other/Album/01 Song.flac");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject wrong artist");
    other.path = QStringLiteral("Artist/Other/01 Song.flac");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject wrong album");
    other.path = QStringLiteral("Song.flac");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject title only");
    other.path = QStringLiteral("Artist/Album/02 Song.flac");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject wrong track");
    other = file; other.durationMs = 190000;
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject duration mismatch");
    other = file; other.discNumber = 2;
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject disc mismatch");
    other = file; other.index = 8;
    require(MusicTorrentMatcher::pick(track, {file,other}).error == QStringLiteral("ambiguous-match"), "reject ambiguity");
    other.path = QStringLiteral("album.cue");
    require(MusicTorrentMatcher::pick(track, {file,other}).error == QStringLiteral("unsupported-cue"), "reject single audio image with cue");
    auto secondTrack = file; secondTrack.index = 10; secondTrack.path = QStringLiteral("Artist/Album/02 Another Song.flac");
    require(MusicTorrentMatcher::pick(track, {file,secondTrack,other}).index == 7, "individual tracks tolerate cue sidecar");
    other = file; other.path = QStringLiteral("Artist/Album/01 Song (Live).flac");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject different recording");
    other = file; other.artist = QStringLiteral("Wrong");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "tags cannot contradict identity");
    other = {9, QStringLiteral("audio.opus"), track.artist, track.album, track.title, 1, 1, 180500};
    require(MusicTorrentMatcher::pick(track, {other}).index == 9, "exact metadata tags");
    auto release = file; release.path = QStringLiteral("Artist - Album (1994) [MP3 320] 88/01. Song.mp3");
    require(MusicTorrentMatcher::pick(track, {release}).index == 7, "combined release root with bounded decorations");
    release.path = QStringLiteral("Artist - Album [FLAC]/CD1/01 - Song.flac");
    require(MusicTorrentMatcher::pick(track, {release}).index == 7, "release root and disc folder");
    release.path = QStringLiteral("Artist/Album/Disc 1/01. Song.flac");
    require(MusicTorrentMatcher::pick(track, {release}).index == 7, "structured artist album disc folder");
    release.path = QStringLiteral("Artist - Album/CD2/01 Song.flac");
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "reject path disc mismatch");
    release.discNumber = 1;
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "disc tag cannot override path conflict");
    release = file; release.path = QStringLiteral("Other - Album (1994) [MP3 320] 88/01 Song.mp3");
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "reject wrong combined-root artist");
    for (const auto& edition : {"Deluxe", "Live", "Instrumental", "Remix", "Remastered"}) {
        release.path = QStringLiteral("Artist - Album (%1) [FLAC]/01 Song.flac").arg(QLatin1String(edition));
        require(MusicTorrentMatcher::pick(track, {release}).index < 0, "reject unknown release edition");
    }
    release.path = QStringLiteral("Artist - Album [Unknown]/01 Song.flac");
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "reject arbitrary stripped suffix");
    release.path = QStringLiteral("Artist - Album [FLAC]/01 Song (Live).flac"); release.title = track.title;
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "title tag cannot hide numbered alternate recording");
    release.path = QStringLiteral("Artist - Album [FLAC]/Song (Live).flac");
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "title tag cannot hide unnumbered alternate recording");
    release = file; release.artist = track.artist; release.album = track.album;
    release.path = QStringLiteral("Artist - Album (Live)/01 Song.flac");
    require(MusicTorrentMatcher::pick(track, {release}).index < 0, "tags cannot hide an alternate release root");
    release = file; release.path = QStringLiteral("Artist - Album [FLAC]/01 Song.flac");
    auto duplicate = release; duplicate.index = 20; duplicate.path = QStringLiteral("Artist - Album (1994) [MP3 320]/01 Song.mp3");
    require(MusicTorrentMatcher::pick(track, {release,duplicate}).error == QStringLiteral("ambiguous-match"), "reject duplicate release choices");
    track.releaseYear = 1994;
    require(MusicTorrentMatcher::pick(track, {duplicate}).index == 20, "accept supplied matching release year");
    track.releaseYear = 2004;
    require(MusicTorrentMatcher::pick(track, {duplicate}).index < 0, "reject conflicting release year");
    track.releaseYear = 0;
    release = file; release.path = QStringLiteral("Nas - Illmatic (1994) [MP3 320] 88/01. The Genesis.mp3");
    const MusicTorrentTrack nas{QStringLiteral("Nas"), QStringLiteral("Illmatic"), QStringLiteral("The Genesis"), 1, 1, 0, 1994};
    require(MusicTorrentMatcher::pick(nas, {release}).index == 7, "observed Nas manifest selects exact track");
    release.path = QStringLiteral("Nas - Illmatic (1994) FLAC 88/01. The Genesis.flac");
    require(MusicTorrentMatcher::pick(nas, {release}).index == 7, "observed unbracketed format and uploader suffix");
    release.path = QStringLiteral("Nas - Illmatic (2004) FLAC 88/01. The Genesis.flac");
    require(MusicTorrentMatcher::pick(nas, {release}).index < 0, "plain format cannot hide conflicting year");
    release.path = QStringLiteral("Nas - Illmatic (Live) FLAC 88/01. The Genesis.flac");
    require(MusicTorrentMatcher::pick(nas, {release}).index < 0, "plain format cannot hide alternate edition");
    auto deluxe = track; deluxe.album = QStringLiteral("Album (Deluxe)");
    release.path = QStringLiteral("Artist - Album (Deluxe) [FLAC]/01 Song.flac");
    require(MusicTorrentMatcher::pick(deluxe, {release}).index == 7, "explicit exact edition remains selectable");
    require(MusicTorrentQueryPlanner::automaticQueries(track) == QStringList({QStringLiteral("Artist Album"), QStringLiteral("Artist Song")}), "artist qualified queries");
    track.artist.clear();
    require(MusicTorrentQueryPlanner::automaticQueries(track).isEmpty(), "no title only query");
    std::cout << "music matcher/query contracts passed\n";
}
