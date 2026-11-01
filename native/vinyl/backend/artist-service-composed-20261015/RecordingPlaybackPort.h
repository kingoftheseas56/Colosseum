#pragma once

#include "../artist-live-prototypes-20261015/bridge/ArtistServiceBridge.h"
#include "IArtistSource.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>
#include <optional>

namespace vinyl::artist {

struct PlayRequest {
    ProfileScopeId profile;
    ArtistMbid artist;
    ReleaseGroupMbid releaseGroup;
    ReleaseMbid release;
    RecordingMbid recording;

    QString profileScopeId() const { return profile.value; }
    QString artistMbid() const { return artist.value; }
    QString releaseGroupMbid() const { return releaseGroup.value; }
    QString releaseMbid() const { return release.value; }
    QString recordingMbid() const { return recording.value; }

    bool operator==(const PlayRequest &other) const {
        return profile.value == other.profile.value &&
               artist.value == other.artist.value &&
               releaseGroup.value == other.releaseGroup.value &&
               release.value == other.release.value &&
               recording.value == other.recording.value;
    }
};

// Concrete IPlaybackCommandPort implementation that validates edition->group
// and recording->edition ownership against IArtistSource and emits one typed
// playRequested(PlayRequest) signal without starting real playback.
class RecordingPlaybackPort final : public QObject, public IPlaybackCommandPort {
    Q_OBJECT
public:
    explicit RecordingPlaybackPort(const IArtistSource &source, QObject *parent = nullptr);
    ~RecordingPlaybackPort() override = default;

    CommandReply selectRecording(const ExactSelection &selection) override;
    CommandReply selectRelease(const ExactSelection &selection) override;

    const QVector<PlayRequest> &playRequests() const noexcept { return m_playRequests; }
    std::optional<PlayRequest> lastPlayRequest() const;
    const QVector<ExactSelection> &releaseSelections() const noexcept { return m_releaseSelections; }
    void clearHistory();

signals:
    void playRequested(const ::vinyl::artist::PlayRequest &request);
    void releaseBrowseRequested(const QString &artistMbid,
                                const QString &releaseGroupMbid,
                                const QString &releaseMbid);

private:
    const IArtistSource &m_source;
    QVector<PlayRequest> m_playRequests;
    QVector<ExactSelection> m_releaseSelections;
};

} // namespace vinyl::artist

Q_DECLARE_METATYPE(vinyl::artist::PlayRequest)
