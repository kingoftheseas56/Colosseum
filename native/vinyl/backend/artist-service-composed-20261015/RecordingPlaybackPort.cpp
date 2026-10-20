#include "RecordingPlaybackPort.h"

namespace vinyl::artist {

RecordingPlaybackPort::RecordingPlaybackPort(const IArtistSource &source, QObject *parent)
    : QObject(parent), m_source(source) {
    qRegisterMetaType<::vinyl::artist::PlayRequest>("vinyl::artist::PlayRequest");
    qRegisterMetaType<::vinyl::artist::PlayRequest>("::vinyl::artist::PlayRequest");
    qRegisterMetaType<::vinyl::artist::PlayRequest>("PlayRequest");
}

std::optional<PlayRequest> RecordingPlaybackPort::lastPlayRequest() const {
    if (m_playRequests.isEmpty()) return std::nullopt;
    return m_playRequests.last();
}

void RecordingPlaybackPort::clearHistory() {
    m_playRequests.clear();
    m_releaseSelections.clear();
}

CommandReply RecordingPlaybackPort::selectRecording(const ExactSelection &selection) {
    const auto parsedProfile = ::vinyl::ProfileScopeId::parse(selection.profile.value);
    if (!parsedProfile.ok()) {
        return {false, QStringLiteral("invalid_profile"),
                QStringLiteral("Profile scope ID is invalid.")};
    }
    const auto parsedArtist = ::vinyl::ArtistMbid::parse(selection.artist.value);
    const auto parsedGroup = ::vinyl::ReleaseGroupMbid::parse(selection.group.value);
    const auto parsedRelease = ::vinyl::ReleaseMbid::parse(selection.release.value);
    const auto parsedRecording = ::vinyl::RecordingMbid::parse(selection.recording.value);

    if (!parsedArtist.ok() || !parsedGroup.ok() || !parsedRelease.ok() || !parsedRecording.ok()) {
        return {false, QStringLiteral("invalid_mbid"),
                QStringLiteral("All four canonical MusicBrainz IDs are required.")};
    }

    if (!m_source.hasReleaseInGroup(*parsedArtist.value, *parsedGroup.value, *parsedRelease.value)) {
        return {false, QStringLiteral("edition_group_mismatch"),
                QStringLiteral("Selected release edition does not belong to the release group.")};
    }

    if (!m_source.hasRecordingInRelease(
            *parsedArtist.value, *parsedGroup.value, *parsedRelease.value, *parsedRecording.value)) {
        return {false, QStringLiteral("recording_edition_mismatch"),
                QStringLiteral("Selected recording does not belong to the specified release edition.")};
    }

    PlayRequest req{
        ProfileScopeId{parsedProfile.value->value()},
        ArtistMbid{parsedArtist.value->toString().toLower()},
        ReleaseGroupMbid{parsedGroup.value->toString().toLower()},
        ReleaseMbid{parsedRelease.value->toString().toLower()},
        RecordingMbid{parsedRecording.value->toString().toLower()}
    };
    m_playRequests.push_back(req);
    emit playRequested(req);
    return {true, {}, {}};
}

CommandReply RecordingPlaybackPort::selectRelease(const ExactSelection &selection) {
    const auto parsedProfile = ::vinyl::ProfileScopeId::parse(selection.profile.value);
    if (!parsedProfile.ok()) {
        return {false, QStringLiteral("invalid_profile"),
                QStringLiteral("Profile scope ID is invalid.")};
    }
    const auto parsedArtist = ::vinyl::ArtistMbid::parse(selection.artist.value);
    const auto parsedGroup = ::vinyl::ReleaseGroupMbid::parse(selection.group.value);
    const auto parsedRelease = ::vinyl::ReleaseMbid::parse(selection.release.value);

    if (!parsedArtist.ok() || !parsedGroup.ok() || !parsedRelease.ok()) {
        return {false, QStringLiteral("invalid_mbid"),
                QStringLiteral("Artist, Release Group, and Release MBIDs are required.")};
    }

    if (!m_source.hasReleaseInGroup(*parsedArtist.value, *parsedGroup.value, *parsedRelease.value)) {
        return {false, QStringLiteral("edition_group_mismatch"),
                QStringLiteral("Selected release edition does not belong to the release group.")};
    }

    m_releaseSelections.push_back(selection);
    emit releaseBrowseRequested(
        parsedArtist.value->toString().toLower(),
        parsedGroup.value->toString().toLower(),
        parsedRelease.value->toString().toLower());
    return {true, {}, {}};
}

} // namespace vinyl::artist
