#pragma once

#include "watchparty/WatchPartyRoomServiceClient.h"
#include "watchparty/WatchPartyTypes.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace Colosseum::WatchParty {

// Account-owned current identity. The bearer is a secret transport credential:
// Watch Party may hand it only to RoomServiceClient::openService(), which places
// it in the WSS upgrade header. It must never enter room protocol payloads,
// snapshots, QML-visible state, logs, diagnostics, or durable storage.
struct SignedInAccountIdentity {
    QString username;
    QByteArray bearerToken;

    bool isValid() const;
};

enum class InviteDeliveryStatus {
    AcceptedForDelivery,
    Rejected
};

struct InviteDeliveryResult {
    InviteDeliveryStatus status = InviteDeliveryStatus::Rejected;
    QString detail;

    bool ok() const
    {
        return status == InviteDeliveryStatus::AcceptedForDelivery;
    }
};

// Narrow account boundary required by the locked Watch Party MVP.
//
// Deliberately absent:
// - username search;
// - autocomplete;
// - user directory;
// - friends/followers;
// - presence.
//
// The account owner resolves the exact username and delivers the invite. Watch
// Party never owns an account database and never infers identity from room data.
class IWatchPartyAccountBridge
{
public:
    using InviteCompletion =
        std::function<void(const InviteDeliveryResult&)>;

    virtual ~IWatchPartyAccountBridge() = default;

    virtual std::optional<SignedInAccountIdentity>
    currentSignedInIdentity() const = 0;

    virtual bool exactUsernameInviteAvailable() const { return false; }

    virtual void inviteExactUsername(
        const QString& roomId,
        const QString& exactUsername,
        InviteCompletion completion) = 0;
};

enum class IdentityActionError {
    None,
    ServiceUnavailable,
    AccountBindingUnavailable,
    SignedInRequired,
    SignedOutRequired,
    InvalidSignedInIdentity,
    WrongServiceIdentity,
    InvalidInviteTarget,
    RoomSnapshotRequired,
    HostRequired,
    ServiceRejected
};

struct IdentityActionResult {
    IdentityActionError error = IdentityActionError::None;
    QString detail;

    bool ok() const { return error == IdentityActionError::None; }

    static IdentityActionResult success()
    {
        return {};
    }

    static IdentityActionResult failure(
        IdentityActionError error,
        const QString& detail = QString())
    {
        return IdentityActionResult{error, detail};
    }
};

// Repo-local identity/lifecycle seam for Slice 5.
//
// This coordinator is intentionally not a QObject and exposes no QML surface.
// Slice 6 may wrap/use it after a real account implementation is adopted.
// RoomServiceClient remains the room protocol/session owner; this class only
// supplies the correct identity source for create/join/invite lifecycle actions.
class IdentityCoordinator
{
public:
    IdentityCoordinator(
        RoomServiceClient* service,
        IWatchPartyAccountBridge* accountBridge = nullptr);

    bool hasSignedInIdentity() const;
    QString signedInUsername() const;

    // Signed-in mode obtains the bearer only from the account owner.
    IdentityActionResult openSignedInService(const QUrl& serviceUrl);

    // Guest mode is valid only while no signed-in identity exists. A missing
    // account bridge is treated as the accountless/signed-out product path.
    IdentityActionResult openGuestService(const QUrl& serviceUrl);

    void closeService();

    IdentityActionResult createRoom(const SourceDescriptor& source);
    IdentityActionResult joinSignedIn(const QString& roomId);
    IdentityActionResult joinGuest(
        const QString& roomId,
        const QString& temporaryDisplayName);

    // A successful return means the request was handed to the account owner.
    // Delivery success/failure arrives through completion.
    IdentityActionResult inviteExactUsername(
        const QString& exactUsername,
        IWatchPartyAccountBridge::InviteCompletion completion = {});

private:
    enum class ServiceIdentityMode {
        None,
        SignedIn,
        Guest
    };

    IdentityActionResult requireService() const;
    IdentityActionResult requireSignedInIdentity(
        SignedInAccountIdentity* identity = nullptr) const;
    IdentityActionResult requireSignedOutIdentity() const;
    IdentityActionResult requireSignedInServiceIdentity() const;
    IdentityActionResult requireGuestServiceIdentity() const;
    IdentityActionResult requireLocalSignedInHost() const;

    RoomServiceClient* m_service = nullptr;
    IWatchPartyAccountBridge* m_accountBridge = nullptr;
    ServiceIdentityMode m_serviceIdentityMode = ServiceIdentityMode::None;
};

QString identityActionErrorName(IdentityActionError error);

} // namespace Colosseum::WatchParty
