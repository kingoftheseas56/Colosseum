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
    require(MusicTorrentMatcher::pick(track, {file,other}).error == QStringLiteral("unsupported-cue"), "reject cue");
    other = file; other.path = QStringLiteral("Artist/Album/01 Song (Live).flac");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "reject different recording");
    other = file; other.artist = QStringLiteral("Wrong");
    require(MusicTorrentMatcher::pick(track, {other}).index < 0, "tags cannot contradict identity");
    other = {9, QStringLiteral("audio.opus"), track.artist, track.album, track.title, 1, 1, 180500};
    require(MusicTorrentMatcher::pick(track, {other}).index == 9, "exact metadata tags");
    require(MusicTorrentQueryPlanner::automaticQueries(track) == QStringList({QStringLiteral("Artist Album"), QStringLiteral("Artist Song")}), "artist qualified queries");
    track.artist.clear();
    require(MusicTorrentQueryPlanner::automaticQueries(track).isEmpty(), "no title only query");
    std::cout << "music matcher/query contracts passed\n";
}
