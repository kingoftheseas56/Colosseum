#pragma once
#include "MusicTorrentMatcher.h"
#include <QStringList>

class MusicTorrentQueryPlanner {
public:
    static QStringList automaticQueries(const MusicTorrentTrack& track);
};
