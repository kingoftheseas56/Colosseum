#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>
#include <functional>

namespace vinyl::artist {
Q_NAMESPACE

// State is explicit; an empty page or offline service is never "Ready with []".
enum class State { Idle, Loading, Ready, Empty, NotFound, Unavailable, Error };
Q_ENUM_NS(State)
enum class Channel { Artist, Discography, Media, Artwork };
Q_ENUM_NS(Channel)

struct SourceStamp {
    QString authority; // e.g. MusicBrainz corpus, provider-scoped media source
    QString itemId;    // source-owned item ID, NEVER the canonical artist ID
    QString revision;  // immutable snapshot/corpus generation where available
    bool valid() const { return !authority.trimmed().isEmpty() && !itemId.trimmed().isEmpty(); }
    bool operator==(const SourceStamp &b) const {
        return authority == b.authority && itemId == b.itemId && revision == b.revision;
    }
    QVariantMap toMap() const {
        return {{"authority", authority}, {"itemId", itemId}, {"revision", revision}};
    }
};
struct ReadRequest {
    Channel channel = Channel::Artist;
    quint64 token = 0;
    quint64 generation = 0;
    QString profileScopeId;
    QString artistMbid;
    QString cursor; // BG1-opaque; never synthesized or parsed by this bridge
    QString filter;
    SourceStamp pageSource; // previously acknowledged page source for continuation
};
struct ReadReply {
    Channel channel = Channel::Artist;
    quint64 token = 0;
    QString profileScopeId;
    QString artistMbid;
    QString cursor;
    QString filter;
    State state = State::Error;
    QVariantMap entity;      // artist/media/artwork domain projection
    QVariantList items;      // ReleaseGroup rows for Discography, not Release rows
    QString nextCursor;
    SourceStamp source;
    QString errorCode;
    QString message;
};
// All callbacks must be delivered on the bridge QObject thread.
// cancel() is best-effort: the bridge independently fences late replies.
class IArtistReadPort {
public:
    virtual ~IArtistReadPort() = default;
    virtual void request(const ReadRequest &, std::function<void(ReadReply)> complete) = 0;
    virtual void cancel(quint64 token) = 0;
};
struct SavedReply {
    State state = State::Error;
    bool saved = false;
    QString profileScopeId;
    QString artistMbid;
    QString errorCode;
    QString message;
};
class ISavedArtistPort {
public:
    virtual ~ISavedArtistPort() = default;
    virtual SavedReply read(const QString &profileScopeId, const QString &artistMbid) = 0;
    virtual SavedReply write(const QString &profileScopeId, const QString &artistMbid, bool saved) = 0;
};
// Strongly named canonical IDs prevent a provider video ID becoming a RecordingMbid.
struct ArtistMbid { QString value; };
struct ReleaseGroupMbid { QString value; };
struct ReleaseMbid { QString value; };
struct RecordingMbid { QString value; };
struct ProfileScopeId { QString value; };
struct ExactSelection {
    ProfileScopeId profile;
    ArtistMbid artist;
    ReleaseGroupMbid group;
    ReleaseMbid release;
    RecordingMbid recording; // absent for selectRelease
};
struct CommandReply {
    bool accepted = false;
    QString errorCode;
    QString message;
};
class IPlaybackCommandPort {
public:
    virtual ~IPlaybackCommandPort() = default;
    virtual CommandReply selectRecording(const ExactSelection &) = 0;
    virtual CommandReply selectRelease(const ExactSelection &) = 0;
};

class ArtistServiceBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString profileScopeId READ profileScopeId NOTIFY contextChanged)
    Q_PROPERTY(QString artistMbid READ artistMbid NOTIFY contextChanged)
    Q_PROPERTY(bool active READ active NOTIFY contextChanged)
    Q_PROPERTY(State artistState READ artistState NOTIFY resultsChanged)
    Q_PROPERTY(State discographyState READ discographyState NOTIFY resultsChanged)
    Q_PROPERTY(State mediaState READ mediaState NOTIFY resultsChanged)
    Q_PROPERTY(State artworkState READ artworkState NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap artist READ artist NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList discography READ discography NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap media READ media NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap artwork READ artwork NOTIFY resultsChanged)
    Q_PROPERTY(QString nextCursor READ nextCursor NOTIFY resultsChanged)
    Q_PROPERTY(QString discographyFilter READ discographyFilter NOTIFY resultsChanged)
    Q_PROPERTY(State savedState READ savedState NOTIFY savedChanged)
    Q_PROPERTY(bool saved READ saved NOTIFY savedChanged)
    Q_PROPERTY(QString savedError READ savedError NOTIFY savedChanged)
    Q_PROPERTY(QString playbackError READ playbackError NOTIFY playbackErrorChanged)
public:
    ArtistServiceBridge(IArtistReadPort &reader, ISavedArtistPort &saved,
                        IPlaybackCommandPort &playback, QObject *parent = nullptr);
    ~ArtistServiceBridge() override;
    QString profileScopeId() const { return m_profile; }
    QString artistMbid() const { return m_artistId; }
    bool active() const { return m_active; }
    State artistState() const;
    State discographyState() const;
    State mediaState() const;
    State artworkState() const;
    QVariantMap artist() const;
    QVariantList discography() const;
    QVariantMap media() const;
    QVariantMap artwork() const;
    QString nextCursor() const;
    QString discographyFilter() const { return m_filter; }
    State savedState() const { return m_savedState; }
    bool saved() const { return m_saved; }
    QString savedError() const { return m_savedError; }
    QString playbackError() const { return m_playbackError; }

    Q_INVOKABLE bool enterArtist(const QString &profileScopeId, const QString &artistMbid);
    Q_INVOKABLE void leaveArtist(); // return/back; never touches active audio
    Q_INVOKABLE bool setProfile(const QString &profileScopeId); // invalidates artist navigation
    Q_INVOKABLE bool requestArtist(const QString &artistMbid);
    Q_INVOKABLE bool requestDiscography(const QString &cursor = {},
                                       const QString &filter = {});
    Q_INVOKABLE bool requestMedia();
    Q_INVOKABLE bool requestArtwork();
    Q_INVOKABLE bool refreshSavedArtist();
    Q_INVOKABLE bool toggleSavedArtist();
    Q_INVOKABLE bool retry(Channel channel);
    Q_INVOKABLE bool selectRecording(const QString &artistMbid, const QString &releaseGroupMbid,
                                    const QString &releaseMbid, const QString &recordingMbid);
    Q_INVOKABLE bool selectRelease(const QString &artistMbid, const QString &releaseGroupMbid,
                                  const QString &releaseMbid);
    Q_INVOKABLE QVariantMap result(Channel channel) const;
signals:
    void contextChanged();
    void resultsChanged();
    void savedChanged();
    void playbackErrorChanged();
    void rejected(Channel channel, const QString &errorCode);
private:
    struct Slot {
        State state = State::Idle;
        QVariantMap entity;
        QVariantList items;
        SourceStamp source;
        QString nextCursor, errorCode, message;
    };
    struct Ticket {
        ReadRequest request;
        bool continuation = false;
    };
    IArtistReadPort &m_reader;
    ISavedArtistPort &m_savedPort;
    IPlaybackCommandPort &m_playbackPort;
    quint64 m_generation = 0, m_serial = 0;
    QString m_profile, m_artistId, m_filter = QStringLiteral("all");
    QString m_savedError, m_playbackError;
    bool m_active = false, m_saved = false;
    State m_savedState = State::Idle;
    QHash<int, Slot> m_slots;
    QHash<int, Ticket> m_pending;
    QHash<int, Ticket> m_lastIntent;
    static bool validMbid(const QString &id);
    static bool validProfile(const QString &profile);
    bool validContext() const;
    void resetContext();
    void cancelPending();
    void clearSlots();
    void setError(Channel channel, const QString &code, const QString &message);
    bool issue(Channel channel, const QString &cursor = {}, const QString &filter = {},
               bool continuation = false);
    void accept(const ReadReply &reply);
    bool refreshSaved(bool write, bool target);
    bool validateSelection(const QString &artistMbid, const QString &group,
                           const QString &release, const QString &recording);
    bool dispatchSelection(ExactSelection selected, bool recording);
    static QString stateName(State state);
    const Slot &slot(Channel channel) const;
    Slot &slot(Channel channel);
};
} // namespace vinyl::artist
