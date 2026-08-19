#pragma once

#include "watchparty/WatchPartyIdentity.h"
#include "watchparty/WatchPartyPlayerSync.h"
#include "watchparty/WatchPartyRoomServiceClient.h"
#include "watchparty/WatchPartyTransport.h"
#include "watchparty/WatchPartyTypes.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace Colosseum::WatchParty {

class WebSocketTransport;

// QML-facing Watch Party application controller.
//
// This is deliberately a presentation/lifecycle adapter over the Slice 1-5
// owners. It does not become a second room state machine, player state machine,
// account directory, or source classifier. Authoritative room state still comes
// from RoomServiceClient; timeline application still belongs to
// PlayerSyncController; source eligibility still comes from WatchPartySource.
class UiController final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool serviceConfigured READ serviceConfigured NOTIFY serviceChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY identityChanged)
    Q_PROPERTY(QString signedInUsername READ signedInUsername NOTIFY identityChanged)

    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY feedbackChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY feedbackChanged)
    Q_PROPERTY(QString noticeText READ noticeText NOTIFY feedbackChanged)

    // Slice 7 observability is scalar/credential-free by construction. These
    // properties intentionally expose categories/counts only: never Room ID,
    // participant identifiers, source descriptors, chat text, or credentials.
    Q_PROPERTY(QString roomState READ roomState NOTIFY observabilityChanged)
    Q_PROPERTY(QString transportState READ transportState NOTIFY observabilityChanged)
    Q_PROPERTY(int participantCount READ participantCount NOTIFY observabilityChanged)
    Q_PROPERTY(int bufferingParticipantCount READ bufferingParticipantCount NOTIFY observabilityChanged)
    Q_PROPERTY(QString hostIdentityKind READ hostIdentityKind NOTIFY observabilityChanged)
    Q_PROPERTY(bool hostGraceActive READ hostGraceActive NOTIFY observabilityChanged)
    Q_PROPERTY(QString localSyncStatus READ localSyncStatus NOTIFY observabilityChanged)

    Q_PROPERTY(bool inRoom READ inRoom NOTIFY roomChanged)
    Q_PROPERTY(QString roomId READ roomId NOTIFY roomChanged)
    Q_PROPERTY(QString localParticipantId READ localParticipantId NOTIFY roomChanged)
    Q_PROPERTY(bool localIsHost READ localIsHost NOTIFY roomChanged)
    Q_PROPERTY(QString controlMode READ controlMode NOTIFY roomChanged)
    Q_PROPERTY(QVariantMap roomSource READ roomSource NOTIFY roomChanged)
    Q_PROPERTY(bool localSourceReady READ localSourceReady NOTIFY roomChanged)

    Q_PROPERTY(bool canInvite READ canInvite NOTIFY roomChanged)
    Q_PROPERTY(bool canToggleControlMode READ canToggleControlMode NOTIFY roomChanged)
    Q_PROPERTY(bool canEnd READ canEnd NOTIFY roomChanged)
    Q_PROPERTY(bool canLeave READ canLeave NOTIFY roomChanged)
    Q_PROPERTY(bool canChat READ canChat NOTIFY roomChanged)
    Q_PROPERTY(bool inviteBusy READ inviteBusy NOTIFY stateChanged)

    Q_PROPERTY(QVariantList participants READ participants NOTIFY participantsChanged)
    Q_PROPERTY(QVariantList chatMessages READ chatMessages NOTIFY chatChanged)
    Q_PROPERTY(QVariantList reactions READ reactions NOTIFY reactionsChanged)

public:
    // Production constructor. It owns the WSS transport/service/identity stack
    // and intentionally starts without an account bridge.
    explicit UiController(PlayerSyncController* playerSync,
                          QObject* parent = nullptr);

    // Deterministic test/adoption seam. The transport remains caller-owned; the
    // service/identity wrappers are still owned by this controller.
    UiController(ITransport* transport,
                 PlayerSyncController* playerSync,
                 IWatchPartyAccountBridge* accountBridge,
                 QObject* parent = nullptr);

    ~UiController() override;

    UiController(const UiController&) = delete;
    UiController& operator=(const UiController&) = delete;

    // Non-QML adoption seams. Service/account ownership remains outside QML so
    // credentials and endpoint policy are never exposed as UI properties.
    bool configureServiceUrl(const QUrl& serviceUrl);
    bool setAccountBridge(IWatchPartyAccountBridge* accountBridge);

    bool serviceConfigured() const { return m_serviceConfigured; }
    bool signedIn() const;
    QString signedInUsername() const;

    QString phase() const { return m_phase; }
    bool busy() const;
    QString errorCategory() const { return m_errorCategory; }
    QString errorText() const { return m_errorText; }
    QString noticeText() const { return m_noticeText; }

    QString roomState() const { return m_phase; }
    QString transportState() const;
    int participantCount() const { return m_participants.size(); }
    int bufferingParticipantCount() const;
    QString hostIdentityKind() const;
    bool hostGraceActive() const { return m_hostGraceActive; }
    QString localSyncStatus() const;

    bool inRoom() const { return m_inRoom; }
    QString roomId() const { return m_roomId; }
    QString localParticipantId() const { return m_localParticipantId; }
    bool localIsHost() const { return m_localIsHost; }
    QString controlMode() const { return m_controlMode; }
    QVariantMap roomSource() const { return m_roomSource; }
    bool localSourceReady() const { return m_localSourceReady; }

    bool canInvite() const;
    bool canToggleControlMode() const;
    bool canEnd() const;
    bool canLeave() const { return m_inRoom; }
    bool canChat() const { return m_inRoom; }
    bool inviteBusy() const { return m_inviteBusy; }

    QVariantList participants() const { return m_participants; }
    QVariantList chatMessages() const { return m_chatMessages; }
    QVariantList reactions() const { return m_reactions; }

    // sourceInfo is the credential-free map produced by
    // WatchPartySource.describeCandidate(). Unsupported rows are rejected here
    // again so UI visibility cannot weaken the source authority boundary.
    Q_INVOKABLE bool startParty(const QVariantMap& sourceInfo);
    Q_INVOKABLE bool joinRoom(const QString& requestedRoomId,
                              const QString& guestDisplayName = QString());
    Q_INVOKABLE bool setSharedControl(bool enabled);
    Q_INVOKABLE bool inviteExactUsername(const QString& exactUsername);
    Q_INVOKABLE bool removeParticipant(const QString& participantId);
    Q_INVOKABLE bool sendChat(const QString& message);
    Q_INVOKABLE bool sendReaction(const QString& reaction);
    Q_INVOKABLE bool catchUp();
    Q_INVOKABLE bool leaveParty();
    Q_INVOKABLE bool endParty();

    // PlayerPage reports only scalar participant-local readiness/sync state.
    // The controller deduplicates before publishing to the room service.
    Q_INVOKABLE void setLocalSourceReady(bool exactRoomSourceReady);
    Q_INVOKABLE void updateLocalParticipantState(bool ready,
                                                 const QString& syncStatus);

    Q_INVOKABLE void clearFeedback();
    Q_INVOKABLE void refreshIdentity();

    // Account sign-out or identity replacement must close any AUTHENTICATED
    // party session so a new identity can never reuse the previous identity's
    // socket; guest sessions are untouched.
    void handleAccountIdentityChanged();

    // Deterministic diagnostics contract for Slice 7/Lanista. The returned map
    // is allow-list based and must remain free of secrets, Room IDs, source
    // identity, participant identifiers, and chat/reaction content.
    Q_INVOKABLE QVariantMap diagnosticSnapshot() const;

Q_SIGNALS:
    void serviceChanged();
    void identityChanged();
    void stateChanged();
    void feedbackChanged();
    void roomChanged();
    void participantsChanged();
    void chatChanged();
    void reactionsChanged();
    void observabilityChanged();

    // A successful guest/signed-in join may originate outside PlayerPage.
    // PlayerPage uses this only to refresh room/source readiness and chrome; it
    // carries no credentials or chat history.
    void roomActivated(QString roomId);

private:
    enum class PendingAction {
        None,
        Create,
        JoinSignedIn,
        JoinGuest
    };

    enum class ServiceIdentityMode {
        None,
        SignedIn,
        Guest
    };

    void initialize(ITransport* transport,
                    IWatchPartyAccountBridge* accountBridge);
    void installServiceHandlers();
    void clearServiceHandlers();

    bool beginPendingAction(PendingAction action,
                            const SourceDescriptor& source = {},
                            const QString& requestedRoomId = QString(),
                            const QString& guestDisplayName = QString());
    bool ensureServiceIdentityForPendingAction();
    bool dispatchPendingAction();

    void onSessionEstablished(const QString& roomId,
                              const QString& participantId);
    void onSnapshot(const RoomSnapshot& snapshot);
    void onTimeline(const TimelineState& timeline);
    void onParticipant(const ParticipantState& participant);
    void onHostChanged(const QString& hostParticipantId);
    void onChat(const ChatEvent& event);
    void onReaction(const ReactionEvent& event);
    void onRoomEnded();
    void onServiceError(const RoomServiceError& error);
    void onTransportState(TransportState state);
    void onTimelineCommandRequested(const QString& type,
                                    bool hasPosition,
                                    double positionSeconds);

    void applySnapshotPresentation(const RoomSnapshot& snapshot);
    void refreshSnapshotPresentation();
    void updatePlayerSyncActivation();
    void clearRoomPresentation();
    void setPhase(const QString& phase);
    void setError(const QString& category, const QString& text);
    void setNotice(const QString& text);
    void resetPendingAction();

    static bool sourceDescriptorFromInspection(const QVariantMap& sourceInfo,
                                               SourceDescriptor* descriptor);
    static QVariantMap sourceDescriptorToVariant(const SourceDescriptor& source);
    QVariantMap participantToVariant(const ParticipantState& participant) const;
    static QVariantMap chatToVariant(const ChatEvent& event);
    static QVariantMap reactionToVariant(const ReactionEvent& event);
    static QString trimmedRoomId(const QString& roomId);
    static QString trimmedSingleLine(const QString& value, int maxLength);

    WebSocketTransport* m_ownedWebSocketTransport = nullptr;
    ITransport* m_transport = nullptr;
    std::unique_ptr<RoomServiceClient> m_service;
    std::unique_ptr<IdentityCoordinator> m_identity;
    PlayerSyncController* m_playerSync = nullptr;
    IWatchPartyAccountBridge* m_accountBridge = nullptr;

    QUrl m_serviceUrl;
    bool m_serviceConfigured = false;
    ServiceIdentityMode m_serviceIdentityMode = ServiceIdentityMode::None;
    bool m_openingService = false;

    PendingAction m_pendingAction = PendingAction::None;
    SourceDescriptor m_pendingSource;
    QString m_pendingRoomId;
    QString m_pendingGuestDisplayName;

    QString m_phase = QStringLiteral("idle");
    QString m_errorCategory;
    QString m_errorText;
    QString m_noticeText;
    bool m_inviteBusy = false;

    bool m_inRoom = false;
    QString m_roomId;
    QString m_localParticipantId;
    bool m_localIsHost = false;
    QString m_controlMode = QStringLiteral("host");
    bool m_hostGraceActive = false;
    QVariantMap m_roomSource;
    bool m_localSourceReady = false;
    QVariantList m_participants;
    QVariantList m_chatMessages;
    QVariantList m_reactions;

    bool m_havePublishedParticipantState = false;
    bool m_lastPublishedReady = false;
    SyncStatus m_lastPublishedSyncStatus = SyncStatus::Unknown;
};

} // namespace Colosseum::WatchParty
