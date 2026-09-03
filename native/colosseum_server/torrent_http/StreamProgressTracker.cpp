#include "StreamProgressTracker.h"

#include <QtMath>

#include <cmath>

namespace colosseum::server::torrent_http {

QString StreamProgressTracker::key(const QString &infoHash, int fileIndex)
{
    return infoHash.toLower() + QLatin1Char(':') + QString::number(fileIndex);
}

StreamProgressEvent StreamProgressTracker::event(StreamEventKind kind,
                                                 const State &state,
                                                 double progressValue,
                                                 const QString &destination,
                                                 bool global)
{
    StreamProgressEvent result;
    result.kind = kind;
    result.infoHash = state.context.infoHash.toLower();
    result.fileIndex = state.context.fileIndex;
    result.progress = progressValue;
    result.destination = destination;
    result.global = global;
    result.fileName = state.context.name;
    result.fileOffset = state.context.offset;
    result.fileLength = state.context.length;
    return result;
}

double StreamProgressTracker::progress(const State &state)
{
    if (state.filePieces <= 0)
        return 0.0;
    return static_cast<double>(state.filePieces - state.missingPieces.size())
        / static_cast<double>(state.filePieces);
}

StreamOpenResult StreamProgressTracker::open(const StreamFileContext &context)
{
    StreamOpenResult result;
    const QString stateKey = key(context.infoHash, context.fileIndex);
    if (states_.contains(stateKey))
        return result;

    State state;
    state.context = context;
    state.context.infoHash = context.infoHash.toLower();

    if (context.length > 0 && context.pieceLength > 0) {
        const qint64 startPiece = context.offset / context.pieceLength;
        const qint64 endPiece = (context.offset + context.length - 1) / context.pieceLength;
        for (qint64 piece = startPiece; piece <= endPiece; ++piece) {
            if (!context.verifiedPieces.contains(piece))
                state.missingPieces.insert(piece);
        }
        // Provenance: module 172 uses ceil(file.length / torrent.pieceLength), not
        // the number of physical pieces touched by an unaligned file.
        state.filePieces = static_cast<qint64>(
            std::ceil(static_cast<double>(context.length) / static_cast<double>(context.pieceLength)));
    }

    states_.insert(stateKey, state);
    State &stored = states_[stateKey];

    result.events.push_back(event(StreamEventKind::Created, stored, 0.0, {}, true));
    result.events.push_back(event(StreamEventKind::Progress, stored, progress(stored), {}, false));

    if (stored.missingPieces.isEmpty()) {
        stored.completed = true;
        result.events.push_back(event(StreamEventKind::Cached,
                                      stored,
                                      1.0,
                                      context.cachedDestination,
                                      false));
        result.events.push_back(event(StreamEventKind::Cached,
                                      stored,
                                      1.0,
                                      context.cachedDestination,
                                      true));
    }

    if (!context.hasBuffer && context.length > 0 && context.pieceLength > 0) {
        const qint64 verificationPieceLength = context.realPieceLength > 0
            ? context.realPieceLength
            : (context.verificationLength > 0 ? context.verificationLength : context.pieceLength);
        const qint64 startPiece = context.offset / verificationPieceLength;
        const qint64 endPiece = (context.offset + context.length - 1) / verificationPieceLength;
        const double ratio = static_cast<double>(verificationPieceLength)
            / static_cast<double>(context.pieceLength);
        StreamSelection selection;
        selection.from = qRound64(static_cast<double>(startPiece) * ratio);
        selection.to = qRound64(static_cast<double>(endPiece + 1) * ratio);
        selection.priority = false;
        result.automaticSelection = selection;
    }

    return result;
}

QVector<StreamProgressEvent> StreamProgressTracker::verified(const QString &infoHash,
                                                              int fileIndex,
                                                              qint64 piece,
                                                              const QString &cachedDestination)
{
    QVector<StreamProgressEvent> result;
    auto it = states_.find(key(infoHash, fileIndex));
    if (it == states_.end() || it->completed || !it->missingPieces.contains(piece))
        return result;

    it->missingPieces.remove(piece);
    const double currentProgress = progress(*it);
    result.push_back(event(StreamEventKind::Progress, *it, currentProgress, {}, false));
    if (it->missingPieces.isEmpty()) {
        it->completed = true;
        result.push_back(event(StreamEventKind::Cached,
                               *it,
                               currentProgress,
                               cachedDestination,
                               false));
        result.push_back(event(StreamEventKind::Cached,
                               *it,
                               currentProgress,
                               cachedDestination,
                               true));
    }
    return result;
}

} // namespace colosseum::server::torrent_http
