#pragma once

#include "TorrentHttpSurface.h"

#include <QHash>
#include <QSet>

#include <optional>

namespace colosseum::server::torrent_http {

struct StreamFileContext {
    QString infoHash;
    int fileIndex = -1;
    QString name;
    qint64 offset = 0;
    qint64 length = 0;
    qint64 pieceLength = 0;
    qint64 realPieceLength = 0;
    qint64 verificationLength = 0;
    bool hasBuffer = false;
    QSet<qint64> verifiedPieces;
    QString cachedDestination;
};

struct StreamSelection {
    qint64 from = 0;
    qint64 to = 0;
    bool priority = false;
};

enum class StreamEventKind {
    Created,
    Progress,
    Cached,
};

struct StreamProgressEvent {
    StreamEventKind kind = StreamEventKind::Created;
    QString infoHash;
    int fileIndex = -1;
    double progress = 0.0;
    QString destination;
    bool global = false;
    QString fileName;
    qint64 fileOffset = 0;
    qint64 fileLength = 0;
};

struct StreamOpenResult {
    QVector<StreamProgressEvent> events;
    std::optional<StreamSelection> automaticSelection;
};

class StreamProgressTracker final {
public:
    StreamOpenResult open(const StreamFileContext &context);
    QVector<StreamProgressEvent> verified(const QString &infoHash,
                                          int fileIndex,
                                          qint64 piece,
                                          const QString &cachedDestination = {});

private:
    struct State {
        StreamFileContext context;
        QSet<qint64> missingPieces;
        qint64 filePieces = 0;
        bool completed = false;
    };

    static QString key(const QString &infoHash, int fileIndex);
    static StreamProgressEvent event(StreamEventKind kind,
                                     const State &state,
                                     double progress,
                                     const QString &destination,
                                     bool global);
    static double progress(const State &state);

    QHash<QString, State> states_;
};

} // namespace colosseum::server::torrent_http
