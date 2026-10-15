#pragma once
// VaultForensics — Slice F1-Core (Colosseum Agent Visibility Phase 2). A bounded, read-only
// projection over the SAME live Vault object Hemanth is looking at, so an agent can answer "what
// does the Vault actually hold right now" without hand-archaeology over index-v1.sqlite.
//
// Owner and thread law (F0, docs/visibility/vault-forensic-owner-thread.md, HEAD b9dded2 at trace
// time): the named safe seam is VaultLibrary — the existing QML façade — composing ONLY its
// already-shipped read-only Q_INVOKABLE/public methods (revision(), itemCount(), rootsDetail(),
// recentArrivals(), browseAt(), browseDetail()), never VaultIndex directly (F0 §10 is explicit:
// "not VaultIndex directly — QML never touches VaultIndex itself"). VaultForensics holds a
// VaultLibrary* only. It opens no SQLite connection, retains no QSqlQuery, writes nothing, mutates
// nothing, scans/enriches/identifies nothing, and never calls publish() — it only reads what
// VaultLibrary already computed for QML.
//
// query(request) is the direct, synchronous entry point — same-thread, no marshalling, exactly the
// pattern qml/VaultPage.qml and (once F1-Bridge lands) LanistaServer already use (F0: both share
// the GUI/main thread with every Vault object today, so no thread hop is needed at all). It MUST
// be called on VaultLibrary's owner thread; it does not check this itself (VaultLibrary's own
// Q_INVOKABLE methods carry the same contract, unenforced the same way).
//
// queryMarshalled(request, deadlineMs) is the general-purpose entry point safe from ANY thread: on
// the owner thread it degrades to a direct query() call (no marshalling, matching F0's finding
// that none is needed today); from a foreign thread it posts the call onto the owner via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection) and waits on a QSemaphore bounded by
// deadlineMs — never Qt::BlockingQueuedConnection, which has no deadline and would hang forever if
// the owner thread's event loop stalls. A missed deadline returns an error map, never a hang.
//
// Response envelope: schema "colosseum.vault.forensics.v1", indexSchemaVersion (see the note by
// kIndexSchemaVersionMirror below — NOT a live read), revision, ownerThread{name,id}, truncated,
// and errors[] (each entry clamped to kMaxDiagnosticChars). Per scope: summary adds roots{count,
// rows}/browseCount/itemCount/recent{count,rows}; root and node add root{...}/browse{count,rows};
// identity adds identity{found,state,candidateCount,coverRef,coverRefProvenance,displayTitle,
// copiesHeld} — deliberately WITHOUT VaultBrowseDetail's unbounded copies/companions/extras arrays
// (identity_scope_is_bounded proves this). Every row list is clamped to `limit` (1..100) AND the
// whole serialized reply is clamped to a 256 KiB budget (kByteBudgetBytes), safely under Lanista's
// 1 MiB line ceiling (kMaxLineBytes, native/devtools/LanistaServer.cpp:37) — rows are dropped from
// the tail and `truncated` is set whenever either bound bites.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class VaultLibrary;
class QThread;

class VaultForensics : public QObject
{
    Q_OBJECT

public:
    // `library` is non-owning — the same VaultLibrary* main.cpp already constructed and wired to
    // QML. Lifetime contract: VaultForensics must not outlive `library`'s owner thread's ability
    // to purge its queued calls — in practice, parent both to the same object (&app in
    // production, matching every other Vault object per F0 §1) so they are destroyed together.
    // A foreign-thread queryMarshalled() call already in flight when `library` is destroyed is
    // safely dropped (Qt purges posted events targeting a destroyed QObject on destruction); the
    // narrower case this contract guards is VaultForensics itself outliving that guarantee.
    explicit VaultForensics(VaultLibrary* library, QObject* parent = nullptr);

    // Request keys: "scope" (summary|root|node|identity, required), "key" (QString, scope-
    // dependent), "limit" (int, clamped to [kMinLimit,kMaxLimit], default kDefaultLimit).
    // MUST be called on the owner (VaultLibrary's) thread — see the class comment.
    QVariantMap query(const QVariantMap& request) const;

    // Thread-safe general entry point. On the owner thread this is exactly query() (no
    // marshalling). From a foreign thread it marshals onto the owner and waits up to `deadlineMs`;
    // past the deadline it returns an error response rather than blocking indefinitely.
    Q_INVOKABLE QVariantMap queryMarshalled(const QVariantMap& request, int deadlineMs = 2000) const;

    static constexpr int kMinLimit = 1;
    static constexpr int kMaxLimit = 100;
    static constexpr int kDefaultLimit = 20;
    // 256 KiB — safely below Lanista's 1 MiB command-line ceiling
    // (kMaxLineBytes = 1 << 20, native/devtools/LanistaServer.cpp:37, F1-Core plan text).
    static constexpr qsizetype kByteBudgetBytes = 256 * 1024;
    static constexpr int kMaxDiagnosticChars = 500;
    // Bounds only the INCOMING request's `key` before it is used/embedded in a diagnostic — a
    // defensive input clamp, distinct from response row content, which is bounded solely by
    // enforceByteBudget() (see summarizeBrowseRow()'s comment for why there is no second,
    // separate per-field output clamp).
    static constexpr int kMaxFieldChars = 400;

private:
    void buildSummary(int limit, QVariantMap& out, bool& truncated) const;
    // isRootScope distinguishes only whether a matching `root{...}` metadata block is added
    // (looked up from rootsDetail()); the browse/child listing composition is identical for
    // scope=root and scope=node — both are "list what browseAt(key) returns", bounded the same
    // way — so both scopes share this one implementation rather than duplicating the loop.
    void buildBrowseScope(const QString& key, int limit, bool isRootScope, QVariantMap& out,
                          bool& truncated, QStringList& errors) const;
    void buildIdentity(const QString& key, QVariantMap& out, QStringList& errors) const;

    static QVariantMap summarizeBrowseRow(const QVariantMap& row);
    static QString coverRefProvenance(const QString& coverRef);
    static QString clampDiagnostic(const QString& text);
    static int clampLimit(const QVariant& requested);
    static QVariantMap ownerThreadInfo();
    // Trims row lists (tail-first, roots/browse/recent) until the serialized reply fits the byte
    // budget; sets truncated=true and appends a diagnostic whenever it has to trim. Last resort:
    // trims the errors[] list itself so the reply NEVER exceeds the budget silently.
    static void enforceByteBudget(QVariantMap& out);

    VaultLibrary* m_library = nullptr;
};
