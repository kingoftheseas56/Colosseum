#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.
//
// Arc 36 Wave 4A lane N-14: the crash-safe cloud attachment receipt. This is
// the durable local record that lets an existing-account cloud attachment
// survive crashes, restarts, offline remembered sessions, and response loss
// without losing the identity of the local source that still must be retired.
//
// This unit owns persistence only. It does not start network attachment,
// retire the source, or change account runtime behavior.

#include "ProfilePaths.h"
#include "SyncProtocol.h"

#include <QList>
#include <QString>
#include <QStringList>

struct AccountAttachmentReceiptData {
    int version = 1;
    QString attachmentId;
    QString sourceKind;       // legacy_local | local_only
    QString sourceProfileId;
    QString sourceSemanticDigest;
    QString sourceActivityDigest;
    // Optional account/device bindings make a receipt safe to resume only
    // under the session that created it. Legacy receipts may omit them; the
    // server still enforces the authenticated account/device pair.
    QString accountId;
    QString deviceId;
    // Mutation identities that already belonged to the account before the
    // local source was merged. They continue through ordinary sync and must
    // never be claimed by this attachment's exact contribution manifest.
    QStringList preexistingMutationIds;
    QString manifestDigest;
    QList<SyncWireAttachmentManifestItem> manifest;
    // Durable source-retirement phase.  Empty is accepted when reading a
    // v1 receipt written before this field existed and is normalized to
    // pending on the next write.
    QString retirementPhase;
    bool sourceRetired = false;
};

class AccountAttachmentReceipt {
public:
    // Reads distinguish absence from corruption so runtime orchestration can
    // fail closed later: Missing means "no cloud attachment is pending";
    // Invalid means "a receipt exists but failed validation" and must never be
    // silently coerced into absence.
    enum class ReadStatus {
        Missing,
        Ok,
        Invalid
    };

    struct ReadResult {
        ReadStatus status = ReadStatus::Missing;
        AccountAttachmentReceiptData data;
        QString error;
    };

    static QString sourceKindLegacyLocal();
    static QString sourceKindLocalOnly();
    static QString retirementPhasePending();
    static QString retirementPhaseStarted();
    static QString retirementPhaseSourceRetired();

    // Fails closed on non-account profiles and on invalid receipt data; never
    // writes a receipt that read() would reject.
    static bool save(const ProfilePaths &paths,
                     const AccountAttachmentReceiptData &data,
                     QString *error = nullptr);

    static ReadResult read(const ProfilePaths &paths);

    // Rewrites the stored receipt with sourceRetired = true, preserving every
    // source identity/digest field. Fails closed when no valid receipt exists.
    static bool markSourceRetired(const ProfilePaths &paths,
                                  QString *error = nullptr);

    // Persists the intent to retire before touching the source.  The phase
    // is idempotent so a restart can safely retry an unchanged source.
    static bool markRetirementStarted(const ProfilePaths &paths,
                                      QString *error = nullptr);

    // Explicit, idempotent removal. Absence afterwards means no cloud
    // attachment is pending.
    static bool clear(const ProfilePaths &paths,
                      QString *error = nullptr);

    static bool isValidData(const AccountAttachmentReceiptData &data,
                            QString *error = nullptr);

private:
    static QString validate(const AccountAttachmentReceiptData &data);
    static QByteArray serialize(const AccountAttachmentReceiptData &data);
    static bool writeAtomic(const QString &path,
                            const AccountAttachmentReceiptData &data,
                            QString *error);
    static ReadResult parse(const QByteArray &payload);
    static bool setError(QString *error, const QString &message);
};
