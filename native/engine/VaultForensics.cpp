#include "VaultForensics.h"
#include "VaultLibrary.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>
#include <QVariantList>

#include <memory>

namespace {

// Mirrors the anonymous-namespace `kVaultSchemaVersion` in native/engine/VaultIndex.cpp:16 (F0
// pin). VaultIndex exposes NO public accessor for it, and this slice may not touch VaultIndex.h/
// .cpp to add one (F0 §10 names VaultLibrary as the ONLY seam) nor query SQLite directly to read
// it live (the plan's own baseline instruction: "do not query SQLite directly in production
// code"). This is therefore a mirrored literal, not a live read — bump it in lockstep whenever
// VaultIndex.cpp's own constant changes. A drifted value here is a documentation bug, never a
// safety one: nothing downstream depends on it being live.
constexpr int kIndexSchemaVersionMirror = 5;

// Bounds an agent-supplied request field before it is used for a lookup or embedded in a
// diagnostic. QVariant::toString() is always safe (empty string for a non-convertible value).
QString clampedRequestString(const QVariant& v, int maxChars)
{
    QString s = v.toString();
    if (s.size() > maxChars)
        s = s.left(maxChars) + QStringLiteral("…");
    return s;
}

} // namespace

VaultForensics::VaultForensics(VaultLibrary* library, QObject* parent)
    : QObject(parent), m_library(library)
{
}

int VaultForensics::clampLimit(const QVariant& requested)
{
    bool ok = false;
    int v = requested.toInt(&ok);
    if (!ok || v <= 0)
        v = kDefaultLimit;
    return qBound(kMinLimit, v, kMaxLimit);
}

QString VaultForensics::clampDiagnostic(const QString& text)
{
    if (text.size() <= kMaxDiagnosticChars)
        return text;
    return text.left(kMaxDiagnosticChars) + QStringLiteral("…");
}

QVariantMap VaultForensics::ownerThreadInfo()
{
    QVariantMap m;
    QThread* t = QThread::currentThread();
    m.insert(QStringLiteral("name"), t ? t->objectName() : QString());
    m.insert(QStringLiteral("id"),
             QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())));
    return m;
}

QString VaultForensics::coverRefProvenance(const QString& coverRef)
{
    if (coverRef.isEmpty())
        return QStringLiteral("none");
    // browseAt()/browseDetail() only ever populate a Film node's coverRef with the local-artwork
    // adoption's namespaced "file://" ref for VIDEO groups (VaultLibrary.cpp's own comment above
    // browseAt(); VaultEnricher::findLocalArtwork). Comic/book coverRef is left "" on purpose by
    // VaultLibrary itself. The "unknown" branch is a defensive bound on what this slice actually
    // observed composing VaultLibrary's public surface today, not a claim about every future shape.
    if (coverRef.startsWith(QStringLiteral("file://")))
        return QStringLiteral("local-artwork");
    return QStringLiteral("unknown");
}

QVariantMap VaultForensics::summarizeBrowseRow(const QVariantMap& row)
{
    // Row CONTENT (key/path/displayTitle/coverRef) is intentionally NOT field-clamped here — the
    // plan's own bounding contract is "clamp individual diagnostic strings" (errors[], via
    // clampDiagnostic) plus a whole-reply byte budget (enforceByteBudget) that drops rows from
    // the tail when content is oversized. A second, separate per-field clamp here would make the
    // byte-budget path nearly unreachable in practice and duplicate one bound with another.
    QVariantMap s;
    s.insert(QStringLiteral("key"), row.value(QStringLiteral("key")));
    s.insert(QStringLiteral("nodeType"), row.value(QStringLiteral("nodeType")));
    s.insert(QStringLiteral("displayTitle"), row.value(QStringLiteral("displayTitle")));
    s.insert(QStringLiteral("path"), row.value(QStringLiteral("path")));
    s.insert(QStringLiteral("state"), row.value(QStringLiteral("state")));
    s.insert(QStringLiteral("away"), row.value(QStringLiteral("away")));
    const QString coverRef = row.value(QStringLiteral("coverRef")).toString();
    s.insert(QStringLiteral("coverRef"), coverRef);
    s.insert(QStringLiteral("coverRefProvenance"), coverRefProvenance(coverRef));
    // browseAt() rows carry counts.items; recentArrivals() rows carry no `counts` at all — the
    // missing-key default (0) is the honest value for those rows (VaultLibrary.cpp:516-569 never
    // returns a `counts` field from recentArrivals()).
    const QVariantMap counts = row.value(QStringLiteral("counts")).toMap();
    s.insert(QStringLiteral("itemCount"), counts.value(QStringLiteral("items"), 0));
    return s;
}

void VaultForensics::buildSummary(int limit, QVariantMap& out, bool& truncated) const
{
    const QVariantList allRoots = m_library->rootsDetail(); // {path,name,available,itemCount,fileCount}
    QVariantMap roots;
    roots.insert(QStringLiteral("count"), allRoots.size());
    QVariantList boundedRoots;
    boundedRoots.reserve(qMin(allRoots.size(), qsizetype(limit)));
    // Each row's itemCount is already browseAt(path).size() (VaultLibrary.cpp:510) — summing it
    // here is an honest aggregate over an already-computed fact, not a fresh unbounded walk.
    int browseTotal = 0;
    for (const QVariant& r : allRoots) {
        const QVariantMap m = r.toMap();
        browseTotal += m.value(QStringLiteral("itemCount")).toInt();
        if (boundedRoots.size() < limit)
            boundedRoots.append(m);
    }
    if (boundedRoots.size() < allRoots.size())
        truncated = true;
    roots.insert(QStringLiteral("rows"), boundedRoots);
    out.insert(QStringLiteral("roots"), roots);
    out.insert(QStringLiteral("browseCount"), browseTotal);
    out.insert(QStringLiteral("itemCount"), m_library->itemCount());

    const QVariantList recent = m_library->recentArrivals(limit); // already limit-bounded by construction
    QVariantList recentRows;
    recentRows.reserve(recent.size());
    for (const QVariant& v : recent)
        recentRows.append(summarizeBrowseRow(v.toMap()));
    QVariantMap recentMap;
    recentMap.insert(QStringLiteral("count"), recentRows.size());
    recentMap.insert(QStringLiteral("rows"), recentRows);
    out.insert(QStringLiteral("recent"), recentMap);
}

void VaultForensics::buildBrowseScope(const QString& key, int limit, bool isRootScope,
                                      QVariantMap& out, bool& truncated,
                                      QStringList& errors) const
{
    if (key.isEmpty()) {
        errors.append(clampDiagnostic(QStringLiteral(
            "scope=%1 requires a non-empty key/path").arg(isRootScope
                ? QStringLiteral("root") : QStringLiteral("node"))));
        QVariantMap browse;
        browse.insert(QStringLiteral("count"), 0);
        browse.insert(QStringLiteral("rows"), QVariantList());
        out.insert(QStringLiteral("browse"), browse);
        if (isRootScope)
            out.insert(QStringLiteral("root"), QVariantMap{{QStringLiteral("found"), false}});
        else
            out.insert(QStringLiteral("node"), QVariantMap{{QStringLiteral("key"), key}});
        return;
    }

    if (isRootScope) {
        const QVariantList allRoots = m_library->rootsDetail();
        QVariantMap rootRow;
        bool found = false;
        for (const QVariant& r : allRoots) {
            const QVariantMap m = r.toMap();
            if (m.value(QStringLiteral("path")).toString() == key) {
                rootRow = m;
                found = true;
                break;
            }
        }
        rootRow.insert(QStringLiteral("found"), found);
        out.insert(QStringLiteral("root"), rootRow);
        if (!found)
            errors.append(clampDiagnostic(QStringLiteral(
                "scope=root: key '%1' does not match a known confirmed/synthetic root")
                    .arg(key)));
    } else {
        out.insert(QStringLiteral("node"), QVariantMap{{QStringLiteral("key"), key}});
    }

    const QVariantList children = m_library->browseAt(key);
    QVariantMap browse;
    browse.insert(QStringLiteral("count"), children.size());
    QVariantList rows;
    rows.reserve(qMin(children.size(), qsizetype(limit)));
    for (const QVariant& c : children) {
        if (rows.size() >= limit)
            break;
        rows.append(summarizeBrowseRow(c.toMap()));
    }
    if (rows.size() < children.size())
        truncated = true;
    browse.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("browse"), browse);
}

void VaultForensics::buildIdentity(const QString& key, QVariantMap& out,
                                   QStringList& errors) const
{
    QVariantMap identity;
    if (key.isEmpty()) {
        errors.append(clampDiagnostic(QStringLiteral("scope=identity requires a non-empty key")));
        identity.insert(QStringLiteral("found"), false);
        out.insert(QStringLiteral("identity"), identity);
        return;
    }

    // browseDetail(key) — VaultLibrary's own "key is a Film browse-row's own key" projection
    // (VaultLibrary.h:123-128). Deliberately the ONLY per-identity read used here; its unbounded
    // copies/companions/extras arrays are read but never copied into the response below —
    // identity_scope_is_bounded proves the reply stays flat regardless of how many copies exist.
    const QVariantMap detail = m_library->browseDetail(key);
    const bool found = detail.value(QStringLiteral("found")).toBool();
    identity.insert(QStringLiteral("found"), found);
    if (!found) {
        errors.append(clampDiagnostic(QStringLiteral(
            "scope=identity: key '%1' resolved to no rows (stale key)").arg(key)));
        out.insert(QStringLiteral("identity"), identity);
        return;
    }

    const QString state = detail.value(QStringLiteral("identityState")).toString(); // identified|uncertain|resolving
    identity.insert(QStringLiteral("state"), state);
    // candidateCount is NOT reachable through any VaultLibrary public method: VaultBrowseDetail::
    // detailFor only folds VaultIndex::FileRow::identityCandidateCount into a human evidence
    // sentence (native/engine/VaultBrowseDetail.cpp:152-154), never as its own field, and reading
    // VaultIndex::rowsForGroup() directly to recover the number would mean touching VaultIndex
    // directly — the exact thing F0 §10 names this slice must NOT do. Reported as unavailable
    // (-1 sentinel), never guessed or parsed out of prose.
    identity.insert(QStringLiteral("candidateCount"), -1);
    if (state == QLatin1String("uncertain"))
        errors.append(clampDiagnostic(QStringLiteral(
            "candidateCount unavailable: VaultLibrary's public surface exposes it only inside "
            "browseDetail()'s prose evidence line, never as a queryable field — see "
            "docs/visibility/vault-forensic-owner-thread.md §10")));
    const QString coverRef = detail.value(QStringLiteral("coverRef")).toString();
    identity.insert(QStringLiteral("coverRef"), coverRef);
    identity.insert(QStringLiteral("coverRefProvenance"), coverRefProvenance(coverRef));
    identity.insert(QStringLiteral("displayTitle"), detail.value(QStringLiteral("displayTitle")));
    identity.insert(QStringLiteral("copiesHeld"), detail.value(QStringLiteral("copiesHeld"), 0));
    // Deliberately stops here: browseDetail()'s `copies`/`companions`/`extras` arrays are read
    // (above, via `detail`) but never copied into `identity` — identity_scope_is_bounded proves
    // this stays flat regardless of how many copies/companions/extras the fixture holds.
    out.insert(QStringLiteral("identity"), identity);
}

void VaultForensics::enforceByteBudget(QVariantMap& out)
{
    auto serializedSize = [&out]() {
        return QJsonDocument(QJsonObject::fromVariantMap(out)).toJson(QJsonDocument::Compact).size();
    };
    if (serializedSize() <= kByteBudgetBytes)
        return;

    bool truncated = out.value(QStringLiteral("truncated")).toBool();
    QVariantList errors = out.value(QStringLiteral("errors")).toList();

    // Trim tail rows, round-robin over every row-list-carrying block this response might hold,
    // until the reply fits or every row list is exhausted.
    static const QStringList kCarriers = {QStringLiteral("roots"), QStringLiteral("browse"),
                                          QStringLiteral("recent")};
    bool trimmedAny = false;
    bool progress = true;
    while (serializedSize() > kByteBudgetBytes && progress) {
        progress = false;
        for (const QString& carrier : kCarriers) {
            if (!out.contains(carrier))
                continue;
            QVariantMap block = out.value(carrier).toMap();
            QVariantList rows = block.value(QStringLiteral("rows")).toList();
            if (rows.isEmpty())
                continue;
            rows.removeLast();
            block.insert(QStringLiteral("rows"), rows);
            out.insert(carrier, block);
            trimmedAny = true;
            progress = true;
            if (serializedSize() <= kByteBudgetBytes)
                break;
        }
    }
    if (trimmedAny) {
        truncated = true;
        errors.append(clampDiagnostic(QStringLiteral(
            "response exceeded the %1-byte budget; row(s) were dropped to fit").arg(kByteBudgetBytes)));
    }

    out.insert(QStringLiteral("truncated"), truncated);
    out.insert(QStringLiteral("errors"), errors);

    // Last resort — a pathologically long single field (not a row list) is still over budget:
    // trim the errors[] list itself so the reply NEVER silently exceeds the budget.
    while (serializedSize() > kByteBudgetBytes) {
        QVariantList errs = out.value(QStringLiteral("errors")).toList();
        if (errs.isEmpty())
            break;
        errs.removeLast();
        out.insert(QStringLiteral("errors"), errs);
    }
}

QVariantMap VaultForensics::query(const QVariantMap& request) const
{
    QVariantMap out;
    out.insert(QStringLiteral("schema"), QStringLiteral("colosseum.vault.forensics.v1"));
    out.insert(QStringLiteral("indexSchemaVersion"), kIndexSchemaVersionMirror);
    out.insert(QStringLiteral("ownerThread"), ownerThreadInfo());

    const QString scope = request.value(QStringLiteral("scope")).toString();
    const QString key = clampedRequestString(request.value(QStringLiteral("key")), kMaxFieldChars);
    const int limit = clampLimit(request.value(QStringLiteral("limit")));
    out.insert(QStringLiteral("scope"), scope);

    QStringList errors;
    bool truncated = false;

    if (!m_library) {
        errors.append(clampDiagnostic(QStringLiteral("no VaultLibrary owner bound")));
    } else {
        out.insert(QStringLiteral("revision"), m_library->revision());
        if (scope == QLatin1String("summary")) {
            buildSummary(limit, out, truncated);
        } else if (scope == QLatin1String("root")) {
            buildBrowseScope(key, limit, /*isRootScope=*/true, out, truncated, errors);
        } else if (scope == QLatin1String("node")) {
            buildBrowseScope(key, limit, /*isRootScope=*/false, out, truncated, errors);
        } else if (scope == QLatin1String("identity")) {
            buildIdentity(key, out, errors);
        } else {
            errors.append(clampDiagnostic(QStringLiteral(
                "unknown scope '%1' — expected summary|root|node|identity").arg(scope)));
        }
    }

    out.insert(QStringLiteral("truncated"), truncated);
    QVariantList errorList;
    errorList.reserve(errors.size());
    for (const QString& e : errors)
        errorList.append(e);
    out.insert(QStringLiteral("errors"), errorList);

    enforceByteBudget(out);
    return out;
}

QVariantMap VaultForensics::queryMarshalled(const QVariantMap& request, int deadlineMs) const
{
    if (!m_library) {
        QVariantMap out;
        out.insert(QStringLiteral("schema"), QStringLiteral("colosseum.vault.forensics.v1"));
        out.insert(QStringLiteral("scope"), request.value(QStringLiteral("scope")));
        out.insert(QStringLiteral("truncated"), false);
        out.insert(QStringLiteral("errors"),
                   QVariantList{QStringLiteral("no VaultLibrary owner bound")});
        return out;
    }

    // Already on the owner thread: exactly query(), no marshalling — F0's finding that the
    // bridge and every Vault object already share one thread today, so no hop is needed.
    if (QThread::currentThread() == m_library->thread())
        return query(request);

    // Foreign thread: marshal onto the owner via a queued call and wait on a bounded semaphore.
    // Deliberately NOT Qt::BlockingQueuedConnection — that has no deadline and would hang this
    // call forever if the owner thread's event loop ever stalls; a QSemaphore::tryAcquire with an
    // explicit timeout is how the bound is actually enforced.
    struct MarshalState {
        QSemaphore done;
        QVariantMap result;
    };
    auto state = std::make_shared<MarshalState>();
    const VaultForensics* self = this;
    const bool posted = QMetaObject::invokeMethod(
        m_library,
        [self, request, state]() {
            state->result = self->query(request);
            state->done.release();
        },
        Qt::QueuedConnection);

    if (!posted || !state->done.tryAcquire(1, deadlineMs)) {
        QVariantMap out;
        out.insert(QStringLiteral("schema"), QStringLiteral("colosseum.vault.forensics.v1"));
        out.insert(QStringLiteral("scope"), request.value(QStringLiteral("scope")));
        out.insert(QStringLiteral("ownerThread"), QVariantMap{
                       {QStringLiteral("name"), m_library->thread()
                                                     ? m_library->thread()->objectName()
                                                     : QString()},
                       {QStringLiteral("id"), QString()}});
        out.insert(QStringLiteral("truncated"), false);
        out.insert(QStringLiteral("errors"),
                   QVariantList{clampDiagnostic(!posted
                       ? QStringLiteral("dispatch to owner thread failed")
                       : QStringLiteral("timeout: owner thread did not answer within %1 ms")
                             .arg(deadlineMs))});
        return out;
    }
    return state->result;
}
