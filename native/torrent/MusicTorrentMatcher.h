#pragma once
#include <QList>
#include <QString>

struct MusicTorrentTrack {
    QString artist, album, title;
    int trackNumber = 0, discNumber = 0;
    qint64 durationMs = 0;
    int releaseYear = 0;
};

// Exact tags or an exact artist/album/title path identify the recording;
// combined release roots may carry a bounded year/audio-format suffix.
struct MusicTorrentFile {
    int index = -1;
    QString path, artist, album, title;
    int trackNumber = 0, discNumber = 0;
    qint64 durationMs = 0;
};

struct MusicTorrentMatch {
    int index = -1;
    QString path, error;
};

class MusicTorrentMatcher {
public:
    static MusicTorrentMatch pick(const MusicTorrentTrack& track,
                                  const QList<MusicTorrentFile>& files);
};
