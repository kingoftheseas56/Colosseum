// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "SharedPcProfileCoordinator.h"

#include "ActivityStore.h"
#include "LegacyPersonalStateStorage.h"
#include "ProfileStoreRuntime.h"

#include <QFileInfo>
#include <QtGlobal>

#include <utility>

// Arc 36 Wave 4B lane N-17. This coordinator stays the single profile
// lifecycle seam the AccountController drives; what it adds on top of
// FirstAccountProfileCoordinator is the runtime-facing surface of the
// existing-account attachment lifecycle:
//
//   - attachLocalProfileToAccount still performs the LOCAL merge (and only
//     that); after it commits, the exact source identity the network
//     AccountAttachmentCoordinator flow needs is captured and parked as
//     pending. Consuming it (takePendingLocalAttachment) is the embedder's
//     decision, so a purely local merge remains the behavior whenever no
//     authenticated account session ever follows.
//   - sealAccountSession fails closed while a cloud attachment is in flight
//     (injectable probe), so signing out mid-flow can never retire or seal
//     away a local source whose content was not yet verified into the
//     cloud.

SharedPcProfileCoordinator::SharedPcProfileCoordinator(
    ProfileStoreRuntime *profileRuntime,
    const QString &appDataRoot)
    : m_profileRuntime(profileRuntime),
      m_firstAccount(
          profileRuntime,
          appDataRoot) {
    Q_ASSERT(m_profileRuntime);
}

bool SharedPcProfileCoordinator::prepareCreatedAccount(
    const QString &accountId,
    QString *error) {
    // Only the attach path reports an attachment identity; every other
    // lifecycle transition voids a stale one so a later unrelated sign-in
    // cannot start a phantom attachment.
    m_pendingLocalAttachment.reset();
    return m_firstAccount.prepareCreatedAccount(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::prepareAccountSession(
    const QString &accountId,
    QString *error) {
    m_pendingLocalAttachment.reset();
    return m_firstAccount.prepareAccountSession(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::attachLocalProfileToAccount(
    const QString &accountId,
    QString *error) {
    // The source must be identified before the merge: the merge activates
    // the account profile, after which the local source is no longer the
    // active profile to derive an identity from.
    const ProfilePaths source =
        m_profileRuntime->activeProfile();
    const bool localSource =
        source.kind()
            == ProfilePaths::Kind::LegacyLocal
        || source.kind()
            == ProfilePaths::Kind::LocalOnly;

    if (!m_firstAccount.attachLocalProfileToAccount(
            accountId,
            error)) {
        m_pendingLocalAttachment.reset();
        return false;
    }

    if (localSource) {
        QString identityError;
        auto identity = captureLocalAttachmentIdentity(
            accountId,
            source,
            &identityError);
        if (!identity.has_value()) {
            // The local merge is already committed and active, so the
            // attach itself succeeded; the network half simply cannot start
            // without a faithful identity. Fail closed to a purely local
            // merge — the local source is never retired that way.
            m_pendingLocalAttachment.reset();
            qWarning(
                "SharedPcProfileCoordinator: local "
                "attachment merged but its source "
                "identity could not be derived: %s",
                qPrintable(identityError));
            return true;
        }
        m_pendingLocalAttachment = *identity;
    }

    return true;
}

bool SharedPcProfileCoordinator::prepareRememberedAccount(
    const QString &accountId,
    QString *error) {
    m_pendingLocalAttachment.reset();
    return m_firstAccount.prepareRememberedAccount(
        accountId,
        error);
}

bool SharedPcProfileCoordinator::prepareLocalOnly(
    QString *error) {
    m_pendingLocalAttachment.reset();
    return m_firstAccount.prepareLocalOnly(
        error);
}

bool SharedPcProfileCoordinator::sealAccountSession(
    const QString &accountId,
    QString *error) {
    if (m_attachmentInFlightProbe
        && m_attachmentInFlightProbe()) {
        return setError(
            error,
            QStringLiteral(
                "The account profile cannot be sealed "
                "while a profile attachment is still "
                "in flight."));
    }
    return m_profileRuntime->sealAccountProfile(
        accountId,
        error);
}

void SharedPcProfileCoordinator::
    setAttachmentInFlightProbe(
        std::function<bool()> probe) {
    m_attachmentInFlightProbe = std::move(probe);
}

std::optional<SharedPcProfileCoordinator::
                 LocalAttachmentIdentity>
SharedPcProfileCoordinator::
    takePendingLocalAttachment() {
    return std::exchange(
        m_pendingLocalAttachment,
        std::nullopt);
}

std::optional<SharedPcProfileCoordinator::
                 LocalAttachmentIdentity>
SharedPcProfileCoordinator::
    captureLocalAttachmentIdentity(
        const QString &accountId,
        const ProfilePaths &source,
        QString *error) const {
    // Same source-resolution the local merge uses: the live legacy storage
    // for a legacy session, the profile's own storage otherwise.
    std::optional<LegacyPersonalStateStorage> storage;
    if (source.kind()
        == ProfilePaths::Kind::LegacyLocal) {
        storage = m_profileRuntime
                      ->legacyStorage();
    } else {
        storage = LegacyPersonalStateStorage::
            forProfile(source,
                       error);
    }
    if (!storage.has_value())
        return std::nullopt;

    const auto snapshot =
        storage->capture(error);
    if (!snapshot.has_value())
        return std::nullopt;

    LocalAttachmentIdentity identity;
    identity.accountId = accountId;
    identity.sourceKind =
        source.kind()
                == ProfilePaths::Kind::LegacyLocal
            ? QStringLiteral(
                  "legacy_local")
            : QStringLiteral(
                  "local_only");
    identity.sourceProfileId =
        source.profileId();
    identity.sourceSemanticDigest =
        snapshot->semanticDigest();

    // The local merge treats an existing ledger that yields no digest as a
    // failure; an absent file is the valid empty sentinel.
    QString digestError;
    identity.sourceActivityDigest =
        ActivityStore::semanticEventDigest(
            storage->activityDbPath(),
            &digestError);
    if (QFileInfo::exists(
            storage->activityDbPath())
        && identity.sourceActivityDigest
               .isEmpty()) {
        setError(
            error,
            QStringLiteral(
                "Could not digest the "
                "local activity ledger for "
                "attachment."));
        return std::nullopt;
    }

    return identity;
}

bool SharedPcProfileCoordinator::setError(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
