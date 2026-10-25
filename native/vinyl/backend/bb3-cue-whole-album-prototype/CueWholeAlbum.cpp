#include "CueWholeAlbum.h"

#include <QChar>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringList>

#include <limits>

namespace vinyl {

namespace {

struct ParsedCueTrack final {
    int trackNumber = 0;
    QString title;
    QString performer;
    std::optional<qint64> startMs;
    int trackLine = 0;
};

bool containsControl(const QString &text)
{
    for (const QChar ch : text) {
        const ushort value = ch.unicode();
        if (value < 0x20 || value == 0x7f)
            return true;
    }
    return false;
}

QString directiveValue(const QRegularExpressionMatch &match)
{
    const QString quoted = match.captured(1);
    if (!quoted.isNull())
        return quoted.trimmed();
    return match.captured(2).trimmed();
}

CueWholeAlbumResult<qint64> cueTimeToMs(qint64 minutes,
                                        int seconds,
                                        int frames,
                                        int line)
{
    if (seconds < 0 || seconds >= 60) {
        return CueWholeAlbumResult<qint64>::failure(
            CueWholeAlbumErrorCode::InvalidCueSeconds,
            line,
            QStringLiteral("CUE seconds must be in 0..59"));
    }
    if (frames < 0 || frames >= 75) {
        return CueWholeAlbumResult<qint64>::failure(
            CueWholeAlbumErrorCode::InvalidCueFrame,
            line,
            QStringLiteral("CUE frames must be in 0..74"));
    }

    const qint64 max = std::numeric_limits<qint64>::max();
    if (minutes < 0 || minutes > (max / 75 - seconds) / 60) {
        return CueWholeAlbumResult<qint64>::failure(
            CueWholeAlbumErrorCode::CueTimeOverflow,
            line,
            QStringLiteral("CUE timestamp is too large"));
    }

    const qint64 totalSeconds = minutes * 60 + seconds;
    if (totalSeconds > (max - frames) / 75) {
        return CueWholeAlbumResult<qint64>::failure(
            CueWholeAlbumErrorCode::CueTimeOverflow,
            line,
            QStringLiteral("CUE timestamp is too large"));
    }
    const qint64 totalFrames = totalSeconds * 75 + frames;
    if (totalFrames > (max - 37) / 1000) {
        return CueWholeAlbumResult<qint64>::failure(
            CueWholeAlbumErrorCode::CueTimeOverflow,
            line,
            QStringLiteral("CUE timestamp is too large"));
    }

    // Standard CUE INDEX timing is 75 frames per second.
    return CueWholeAlbumResult<qint64>::success(
        (totalFrames * 1000 + 37) / 75);
}

} // namespace

QString cueWholeAlbumErrorCodeName(CueWholeAlbumErrorCode code)
{
    switch (code) {
    case CueWholeAlbumErrorCode::None: return QStringLiteral("none");
    case CueWholeAlbumErrorCode::InvalidSourceId: return QStringLiteral("invalid_source_id");
    case CueWholeAlbumErrorCode::EmptyCue: return QStringLiteral("empty_cue");
    case CueWholeAlbumErrorCode::MissingFile: return QStringLiteral("missing_file");
    case CueWholeAlbumErrorCode::MultipleFileSections: return QStringLiteral("multiple_file_sections");
    case CueWholeAlbumErrorCode::InvalidFileDirective: return QStringLiteral("invalid_file_directive");
    case CueWholeAlbumErrorCode::TrackBeforeFile: return QStringLiteral("track_before_file");
    case CueWholeAlbumErrorCode::InvalidTrackDirective: return QStringLiteral("invalid_track_directive");
    case CueWholeAlbumErrorCode::InvalidMetadataDirective: return QStringLiteral("invalid_metadata_directive");
    case CueWholeAlbumErrorCode::NonAudioTrack: return QStringLiteral("non_audio_track");
    case CueWholeAlbumErrorCode::InvalidTrackNumber: return QStringLiteral("invalid_track_number");
    case CueWholeAlbumErrorCode::InvalidTrackOrder: return QStringLiteral("invalid_track_order");
    case CueWholeAlbumErrorCode::NoAudioTracks: return QStringLiteral("no_audio_tracks");
    case CueWholeAlbumErrorCode::IndexBeforeTrack: return QStringLiteral("index_before_track");
    case CueWholeAlbumErrorCode::InvalidIndexDirective: return QStringLiteral("invalid_index_directive");
    case CueWholeAlbumErrorCode::DuplicateIndex01: return QStringLiteral("duplicate_index_01");
    case CueWholeAlbumErrorCode::MissingIndex01: return QStringLiteral("missing_index_01");
    case CueWholeAlbumErrorCode::InvalidCueSeconds: return QStringLiteral("invalid_cue_seconds");
    case CueWholeAlbumErrorCode::InvalidCueFrame: return QStringLiteral("invalid_cue_frame");
    case CueWholeAlbumErrorCode::CueTimeOverflow: return QStringLiteral("cue_time_overflow");
    case CueWholeAlbumErrorCode::NonIncreasingStart: return QStringLiteral("non_increasing_start");
    case CueWholeAlbumErrorCode::InvalidBackingDuration: return QStringLiteral("invalid_backing_duration");
    case CueWholeAlbumErrorCode::StartBeyondBackingDuration: return QStringLiteral("start_beyond_backing_duration");
    case CueWholeAlbumErrorCode::InvalidMediumPosition: return QStringLiteral("invalid_medium_position");
    case CueWholeAlbumErrorCode::Bb2BridgeFailure: return QStringLiteral("bb2_bridge_failure");
    case CueWholeAlbumErrorCode::ExactResolutionRequired: return QStringLiteral("exact_resolution_required");
    case CueWholeAlbumErrorCode::ExactResolutionEntryMissing: return QStringLiteral("exact_resolution_entry_missing");
    }
    return QStringLiteral("unknown");
}

CueWholeAlbumResult<WholeAlbumSourceId> WholeAlbumSourceId::parse(
    const QString &text)
{
    if (text.isEmpty()
        || text.size() > 1024
        || text.trimmed() != text
        || containsControl(text)) {
        return CueWholeAlbumResult<WholeAlbumSourceId>::failure(
            CueWholeAlbumErrorCode::InvalidSourceId,
            0,
            QStringLiteral("whole-album source ID must be non-empty, trimmed, control-free, and <= 1024 characters"));
    }
    return CueWholeAlbumResult<WholeAlbumSourceId>::success(
        WholeAlbumSourceId(text));
}

CueWholeAlbumResult<VirtualTrackId> VirtualTrackId::fromSourceAndCueTrack(
    const WholeAlbumSourceId &sourceId,
    int trackNumber)
{
    if (trackNumber < 1 || trackNumber > 99) {
        return CueWholeAlbumResult<VirtualTrackId>::failure(
            CueWholeAlbumErrorCode::InvalidTrackNumber);
    }
    const QByteArray sourceHash = QCryptographicHash::hash(
        sourceId.value().toUtf8(),
        QCryptographicHash::Sha256).toHex();
    return CueWholeAlbumResult<VirtualTrackId>::success(
        VirtualTrackId(
            QStringLiteral("cue1-%1-track-%2")
                .arg(QString::fromLatin1(sourceHash),
                     QStringLiteral("%1").arg(trackNumber, 2, 10, QChar('0')))));
}

QString cueInputOriginName(CueInputOrigin origin)
{
    switch (origin) {
    case CueInputOrigin::ExternalSidecar: return QStringLiteral("external_sidecar");
    case CueInputOrigin::EmbeddedMetadata: return QStringLiteral("embedded_metadata");
    }
    return QStringLiteral("unknown");
}

QString virtualTrackEndKindName(VirtualTrackEndKind kind)
{
    switch (kind) {
    case VirtualTrackEndKind::NextTrackStart: return QStringLiteral("next_track_start");
    case VirtualTrackEndKind::BackingDuration: return QStringLiteral("backing_duration");
    case VirtualTrackEndKind::OpenEnded: return QStringLiteral("open_ended");
    }
    return QStringLiteral("unknown");
}

CueWholeAlbumResult<CueVirtualAlbum> CueWholeAlbum::normalize(
    WholeAlbumSourceId sourceId,
    CueInputOrigin origin,
    const QString &cueText,
    std::optional<qint64> backingDurationMs)
{
    if (cueText.trimmed().isEmpty()) {
        return CueWholeAlbumResult<CueVirtualAlbum>::failure(
            CueWholeAlbumErrorCode::EmptyCue);
    }
    if (backingDurationMs && *backingDurationMs <= 0) {
        return CueWholeAlbumResult<CueVirtualAlbum>::failure(
            CueWholeAlbumErrorCode::InvalidBackingDuration);
    }

    static const QRegularExpression fileRe(
        QStringLiteral(R"cue(^FILE\s+(?:"([^"]+)"|(\S+))\s+\S+\s*$)cue"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trackRe(
        QStringLiteral(R"(^TRACK\s+(\d+)\s+(\S+)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression indexRe(
        QStringLiteral(R"(^INDEX\s+(\d{2})\s+(\d+):(\d{2}):(\d{2})\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression titleRe(
        QStringLiteral(R"cue(^TITLE\s+(?:"([^"]*)"|(.+))$)cue"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression performerRe(
        QStringLiteral(R"cue(^PERFORMER\s+(?:"([^"]*)"|(.+))$)cue"),
        QRegularExpression::CaseInsensitiveOption);

    QString declaredFileName;
    QString albumTitle;
    QString albumPerformer;
    QVector<ParsedCueTrack> parsedTracks;
    bool sawFile = false;
    int currentTrack = -1;
    int lastTrackNumber = 0;

    const QStringList lines = cueText.split(QChar('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNumber = i + 1;
        const QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QStringLiteral("FILE"), Qt::CaseInsensitive)) {
            const auto match = fileRe.match(line);
            if (!match.hasMatch()) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidFileDirective,
                    lineNumber);
            }
            if (sawFile) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::MultipleFileSections,
                    lineNumber,
                    QStringLiteral("BB3 single-physical-source normalization accepts exactly one FILE section"));
            }
            declaredFileName = directiveValue(match);
            if (declaredFileName.isEmpty()) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidFileDirective,
                    lineNumber);
            }
            sawFile = true;
            currentTrack = -1;
            continue;
        }

        if (line.startsWith(QStringLiteral("TRACK"), Qt::CaseInsensitive)) {
            if (!sawFile) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::TrackBeforeFile,
                    lineNumber);
            }
            const auto match = trackRe.match(line);
            if (!match.hasMatch()) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidTrackDirective,
                    lineNumber);
            }
            if (match.captured(2).compare(QStringLiteral("AUDIO"),
                                          Qt::CaseInsensitive) != 0) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::NonAudioTrack,
                    lineNumber);
            }
            bool ok = false;
            const int trackNumber = match.captured(1).toInt(&ok);
            if (!ok || trackNumber < 1 || trackNumber > 99) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidTrackNumber,
                    lineNumber);
            }
            if (trackNumber <= lastTrackNumber) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidTrackOrder,
                    lineNumber);
            }
            parsedTracks.push_back(ParsedCueTrack{
                trackNumber,
                QString(),
                QString(),
                std::nullopt,
                lineNumber,
            });
            currentTrack = parsedTracks.size() - 1;
            lastTrackNumber = trackNumber;
            continue;
        }

        if (line.startsWith(QStringLiteral("TITLE"), Qt::CaseInsensitive)) {
            const auto match = titleRe.match(line);
            if (!match.hasMatch()) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidMetadataDirective,
                    lineNumber,
                    QStringLiteral("malformed TITLE directive"));
            }
            const QString value = directiveValue(match);
            if (currentTrack >= 0)
                parsedTracks[currentTrack].title = value;
            else
                albumTitle = value;
            continue;
        }

        if (line.startsWith(QStringLiteral("PERFORMER"), Qt::CaseInsensitive)) {
            const auto match = performerRe.match(line);
            if (!match.hasMatch()) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidMetadataDirective,
                    lineNumber,
                    QStringLiteral("malformed PERFORMER directive"));
            }
            const QString value = directiveValue(match);
            if (currentTrack >= 0)
                parsedTracks[currentTrack].performer = value;
            else
                albumPerformer = value;
            continue;
        }

        if (line.startsWith(QStringLiteral("INDEX"), Qt::CaseInsensitive)) {
            if (currentTrack < 0) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::IndexBeforeTrack,
                    lineNumber);
            }
            const auto match = indexRe.match(line);
            if (!match.hasMatch()) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidIndexDirective,
                    lineNumber);
            }

            bool okIndex = false;
            bool okMinutes = false;
            const int indexNumber = match.captured(1).toInt(&okIndex);
            const qint64 minutes = match.captured(2).toLongLong(&okMinutes);
            const int seconds = match.captured(3).toInt();
            const int frames = match.captured(4).toInt();
            if (!okIndex || !okMinutes) {
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    CueWholeAlbumErrorCode::InvalidIndexDirective,
                    lineNumber);
            }

            const auto time = cueTimeToMs(minutes, seconds, frames, lineNumber);
            if (!time.ok())
                return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                    time.error.code, time.error.line, time.error.detail);

            if (indexNumber == 1) {
                if (parsedTracks[currentTrack].startMs) {
                    return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                        CueWholeAlbumErrorCode::DuplicateIndex01,
                        lineNumber);
                }
                parsedTracks[currentTrack].startMs = *time.value;
            }
            continue;
        }

        // Other legal CUE metadata such as REM, ISRC, FLAGS, PREGAP, and
        // POSTGAP does not own canonical identity or INDEX 01 range semantics.
    }

    if (!sawFile) {
        return CueWholeAlbumResult<CueVirtualAlbum>::failure(
            CueWholeAlbumErrorCode::MissingFile);
    }
    if (parsedTracks.isEmpty()) {
        return CueWholeAlbumResult<CueVirtualAlbum>::failure(
            CueWholeAlbumErrorCode::NoAudioTracks);
    }

    for (int i = 0; i < parsedTracks.size(); ++i) {
        const ParsedCueTrack &track = parsedTracks[i];
        if (!track.startMs) {
            return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                CueWholeAlbumErrorCode::MissingIndex01,
                track.trackLine,
                QStringLiteral("every AUDIO track needs exactly one INDEX 01"));
        }
        if (i > 0 && *track.startMs <= *parsedTracks[i - 1].startMs) {
            return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                CueWholeAlbumErrorCode::NonIncreasingStart,
                track.trackLine,
                QStringLiteral("CUE virtual track starts must be strictly increasing"));
        }
        if (backingDurationMs && *track.startMs >= *backingDurationMs) {
            return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                CueWholeAlbumErrorCode::StartBeyondBackingDuration,
                track.trackLine);
        }
    }

    QVector<CueVirtualTrack> tracks;
    tracks.reserve(parsedTracks.size());
    for (int i = 0; i < parsedTracks.size(); ++i) {
        const ParsedCueTrack &parsed = parsedTracks[i];
        const auto id = VirtualTrackId::fromSourceAndCueTrack(
            sourceId,
            parsed.trackNumber);
        if (!id.ok()) {
            return CueWholeAlbumResult<CueVirtualAlbum>::failure(
                id.error.code, parsed.trackLine, id.error.detail);
        }

        std::optional<qint64> endMs;
        VirtualTrackEndKind endKind = VirtualTrackEndKind::OpenEnded;
        if (i + 1 < parsedTracks.size()) {
            endMs = parsedTracks[i + 1].startMs;
            endKind = VirtualTrackEndKind::NextTrackStart;
        } else if (backingDurationMs) {
            endMs = backingDurationMs;
            endKind = VirtualTrackEndKind::BackingDuration;
        }

        tracks.push_back(CueVirtualTrack{
            *id.value,
            parsed.trackNumber,
            parsed.title,
            parsed.performer,
            *parsed.startMs,
            endMs,
            endKind,
        });
    }

    return CueWholeAlbumResult<CueVirtualAlbum>::success(CueVirtualAlbum{
        std::move(sourceId),
        origin,
        declaredFileName,
        albumTitle,
        albumPerformer,
        backingDurationMs,
        std::move(tracks),
    });
}

CueWholeAlbumResult<AlbumSourceManifest> CueWholeAlbum::toBb2Manifest(
    const CueVirtualAlbum &album,
    const CueBb2ManifestContext &context)
{
    if (context.mediumPosition && *context.mediumPosition <= 0) {
        return CueWholeAlbumResult<AlbumSourceManifest>::failure(
            CueWholeAlbumErrorCode::InvalidMediumPosition);
    }

    QVector<CandidateTrackEntry> entries;
    entries.reserve(album.tracks.size());
    for (const CueVirtualTrack &track : album.tracks) {
        const auto entryId = ManifestEntryId::parse(track.id.value());
        if (!entryId.ok()) {
            return CueWholeAlbumResult<AlbumSourceManifest>::failure(
                CueWholeAlbumErrorCode::Bb2BridgeFailure,
                0,
                entryId.error.detail);
        }

        CandidateCanonicalEvidence evidence;
        evidence.mediumPosition = context.mediumPosition;
        evidence.trackPosition = track.cueTrackNumber;

        TrackTextEvidence text;
        if (!track.title.trimmed().isEmpty())
            text.title = track.title;
        const QString performer = track.performer.trimmed().isEmpty()
            ? album.albumPerformer
            : track.performer;
        if (!performer.trimmed().isEmpty())
            text.artist = performer;
        if (!album.albumTitle.trimmed().isEmpty())
            text.album = album.albumTitle;

        std::optional<qint64> duration;
        if (track.endMs)
            duration = *track.endMs - track.startMs;

        const auto entry = CandidateTrackEntry::create(
            *entryId.value,
            std::move(evidence),
            std::move(text),
            duration);
        if (!entry.ok()) {
            return CueWholeAlbumResult<AlbumSourceManifest>::failure(
                CueWholeAlbumErrorCode::Bb2BridgeFailure,
                0,
                entry.error.detail);
        }
        entries.push_back(*entry.value);
    }

    const auto manifest = AlbumSourceManifest::create(
        context.sourceCandidateKey,
        std::move(entries));
    if (!manifest.ok()) {
        return CueWholeAlbumResult<AlbumSourceManifest>::failure(
            CueWholeAlbumErrorCode::Bb2BridgeFailure,
            0,
            manifest.error.detail);
    }
    return CueWholeAlbumResult<AlbumSourceManifest>::success(*manifest.value);
}

CueWholeAlbumResult<ExactVirtualTrackSlice> CueWholeAlbum::resolveExactSlice(
    const CueVirtualAlbum &album,
    const CueBb2ManifestContext &context,
    const RequestedTrackIdentity &request,
    const ExactTrackResolverPolicy &policy)
{
    const auto manifest = toBb2Manifest(album, context);
    if (!manifest.ok()) {
        return CueWholeAlbumResult<ExactVirtualTrackSlice>::failure(
            manifest.error.code,
            manifest.error.line,
            manifest.error.detail);
    }

    const ExactTrackResolution resolution = ExactTrackResolver::resolve(
        request,
        *manifest.value,
        policy);
    if (resolution.outcome != ExactTrackOutcome::Exact
        || !resolution.selectedEntryId) {
        return CueWholeAlbumResult<ExactVirtualTrackSlice>::failure(
            CueWholeAlbumErrorCode::ExactResolutionRequired,
            0,
            QStringLiteral("BB2 outcome was %1")
                .arg(exactTrackOutcomeName(resolution.outcome)));
    }

    const CueVirtualTrack *selected = nullptr;
    for (const CueVirtualTrack &track : album.tracks) {
        if (track.id.value() == resolution.selectedEntryId->value()) {
            selected = &track;
            break;
        }
    }
    if (!selected) {
        return CueWholeAlbumResult<ExactVirtualTrackSlice>::failure(
            CueWholeAlbumErrorCode::ExactResolutionEntryMissing,
            0,
            QStringLiteral("BB2 selected entry is not part of this normalized CUE album"));
    }

    std::optional<ReleaseTrackMbid> releaseTrackMbid;
    if (request.releaseTrackCoordinate())
        releaseTrackMbid = request.releaseTrackCoordinate()->releaseTrackMbid();

    return CueWholeAlbumResult<ExactVirtualTrackSlice>::success(
        ExactVirtualTrackSlice{
            request.recordingMbid(),
            std::move(releaseTrackMbid),
            album.sourceId,
            selected->id,
            selected->cueTrackNumber,
            selected->startMs,
            selected->endMs,
            selected->endKind,
            resolution.strength,
        });
}

} // namespace vinyl
