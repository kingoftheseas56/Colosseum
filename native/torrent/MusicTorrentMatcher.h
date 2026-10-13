#pragma once
#include <QList>
#include <QString>

struct MusicTorrentTrack {
    QString artist, album, title;
    int trackNumber = 0, discNumber = 0;
    qint64 durationMs = 0;
};

// Tags are optional. Without tags the exact Artist/Album/title path is required.
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
