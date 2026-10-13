#include "MusicTorrentQueryPlanner.h"

QStringList MusicTorrentQueryPlanner::automaticQueries(const MusicTorrentTrack& track)
{
    const QString artist = track.artist.simplified();
    const QString title = track.title.simplified();
    const QString album = track.album.simplified();
    if (artist.isEmpty() || title.isEmpty()) return {};
    QStringList queries;
    if (!album.isEmpty()) queries.append(artist + QLatin1Char(' ') + album);
    const QString single = artist + QLatin1Char(' ') + title;
    if (!queries.contains(single, Qt::CaseInsensitive)) queries.append(single);
    return queries;
}
