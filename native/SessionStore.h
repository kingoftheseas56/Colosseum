#pragma once

// SessionStore - the OS-shell's open-sessions model, exposed to QML as `Sessions`.
// One small thing: the list of "things you currently have open" (a comic, a movie, a
// book or browser activity), which one is ACTIVE, and the saved-state blob used to
// rebuild local-media surfaces. FeriaQtWebEngine browser activities instead retain
// live pages in FeriaSessionDeck while parked. The Taskbar draws app-grouped tiles; Main.qml's
// switch glue listens to activeChanged to capture/teardown/restore.
//
// QML contract:
//   Sessions.openOrSwitch({appType, contentKind, target, title}) -> id  (dedups by target key)
//   Sessions.openBrowser(descriptor, fresh, navigate, activate) -> id
//     (provider/private-mode MRU or explicit New; activate=false opens in background)
//   Sessions.switchTo(id)
//   Sessions.close(id)
//   Sessions.saveState(id, obj)   // switch glue writes captured state before teardown
//   QVariantMap Sessions.get(id)  // one record (empty map if not found)
//   QVariantList Sessions.list()  // records in open order
//   QVariantList Sessions.groups()// [{appType,title,icon,sessions:[record,...]}] for the taskbar
//   Sessions.activeId             // "" = none (home shell)
//   Sessions.revision             // bump on every change; name it in a binding to stay reactive
// signals: activeChanged(prevId, nextId)

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>
#include <QUrl>
#include <QUuid>

class SessionStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChangedProp)

public:
    explicit SessionStore(QObject *parent = nullptr) : QObject(parent) {}

    int revision() const { return m_revision; }
    QString activeId() const { return m_activeId; }

    // Open a session for `target`, or switch to it if one already exists (no duplicate).
    // Returns the session id and makes it active. spec: appType drives taskbar grouping,
    // contentKind drives which surface the dispatcher loads, target is the reopen payload.
    Q_INVOKABLE QString openOrSwitch(const QVariantMap &desc) {
        const QString key = targetKey(desc);
        const QString ck = contentKeyFor(desc);
        for (int i = 0; i < m_sessions.size(); ++i) {
            QVariantMap rec = m_sessions.at(i).toMap();
            if (rec.value(QStringLiteral("key")).toString() != key)
                continue;
            const QString id = rec.value(QStringLiteral("id")).toString();
            if (rec.value(QStringLiteral("contentKey")).toString() != ck) {
                // one-tab-per-show replace: same show, new content — same tile, new
                // target; the saved position described the OLD content, so it goes.
                rec.insert(QStringLiteral("contentKey"), ck);
                rec.insert(QStringLiteral("title"), desc.value(QStringLiteral("title")));
                rec.insert(QStringLiteral("target"), desc.value(QStringLiteral("target")));
                rec.insert(QStringLiteral("savedState"), QVariantMap());
                m_sessions[i] = rec;
                bump();
                emit targetReplaced(id);
            } else if (rec.value(QStringLiteral("title")) != desc.value(QStringLiteral("title"))
                       || rec.value(QStringLiteral("target")) != desc.value(QStringLiteral("target"))) {
                // Exact-content reopen: refresh transport/path/metadata while preserving
                // captured state. A parked session must reactivate from the newest target,
                // but its resume position still belongs to this exact content identity.
                const bool newSource = sourceKeyFor(rec.value(QStringLiteral("target")).toMap())
                                    != sourceKeyFor(desc.value(QStringLiteral("target")).toMap());
                rec.insert(QStringLiteral("title"), desc.value(QStringLiteral("title")));
                rec.insert(QStringLiteral("target"), desc.value(QStringLiteral("target")));
                m_sessions[i] = rec;
                bump();
                // a different source of the same content: a warm-kept old stream must not
                // resume in its place (seen on Android, 2026-10-23).
                if (newSource)
                    emit sourceReplaced(id);
            }
            setActive(id);
            return id;
        }

        QVariantMap rec;
        const QString id = QStringLiteral("s%1").arg(++m_idSeq);
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("key"), key);
        rec.insert(QStringLiteral("contentKey"), ck);
        rec.insert(QStringLiteral("appType"), desc.value(QStringLiteral("appType")));
        rec.insert(QStringLiteral("contentKind"), desc.value(QStringLiteral("contentKind")));
        rec.insert(QStringLiteral("title"), desc.value(QStringLiteral("title")));
        rec.insert(QStringLiteral("target"), desc.value(QStringLiteral("target")));
        rec.insert(QStringLiteral("savedState"), QVariantMap());
        m_sessions.append(rec);
        bump();
        setActive(id);
        return id;
    }

    // Browser identity is an activity ID, never its changing destination/title.
    Q_INVOKABLE void setBrowserProfile(const QString &profile) {
        if (profile == m_browserProfile)
            return;
        // Retire the account before signals from teardown can dispatch old callbacks.
        m_browserProfile.clear();
        ++m_browserEpoch;
        m_closedBrowsers.clear();
        closeBrowserSessions();
        m_browserProfile = profile;
        bump();
    }
    Q_INVOKABLE QVariantList browsers() const {
        QVariantList result;
        for (const QVariant &value : m_sessions)
            if (value.toMap().value(QStringLiteral("contentKind")) == QStringLiteral("browser"))
                result.append(value);
        return result;
    }
    // Read-only app/space projections over the canonical activity registry.
    // No parallel tab IDs, URL identity, account store or persisted selection.
    Q_INVOKABLE QVariantList browserSpaces(const QString &provider) const {
        QVariantList result;
        for (const QVariant &value : browsers()) {
            const QVariantMap rec = value.toMap();
            if (rec.value(QStringLiteral("target")).toMap()
                    .value(QStringLiteral("providerId")).toString() == provider)
                result.append(rec);
        }
        return result;
    }
    Q_INVOKABLE QString selectedBrowser(const QString &provider) const {
        QString selected;
        quint64 latest = 0;
        for (const QVariant &value : browserSpaces(provider)) {
            const QVariantMap rec = value.toMap();
            const QString id = rec.value(QStringLiteral("id")).toString();
            if (id == m_activeId) return id;
            const quint64 order = rec.value(QStringLiteral("browserUseOrder")).toULongLong();
            if (selected.isEmpty() || order > latest) {
                selected = id;
                latest = order;
            }
        }
        return selected;
    }
    Q_INVOKABLE QVariantList browserApps() const {
        QVariantList result;
        QStringList seen;
        for (const QVariant &value : browsers()) {
            const QVariantMap rec = value.toMap();
            const QVariantMap target = rec.value(QStringLiteral("target")).toMap();
            const QString provider = target.value(QStringLiteral("providerId")).toString();
            if (seen.contains(provider)) continue;
            seen.append(provider);
            const QString selected = selectedBrowser(provider);
            result.append(QVariantMap{
                {QStringLiteral("providerId"), provider},
                {QStringLiteral("title"), rec.value(QStringLiteral("groupTitle"))},
                {QStringLiteral("icon"), rec.value(QStringLiteral("groupIcon"))},
                {QStringLiteral("mode"), target.value(QStringLiteral("mode"))},
                {QStringLiteral("selectedSpaceId"), selected},
                {QStringLiteral("foreground"), selected == m_activeId}});
        }
        return result;
    }
    Q_INVOKABLE bool activateBrowserApp(const QString &provider) {
        const QString selected = selectedBrowser(provider);
        if (selected.isEmpty()) return false;
        setActive(selected == m_activeId ? QString{} : selected);
        return true;
    }
    Q_INVOKABLE void closeBrowserSessions() {
        const QVariantList records = browsers();
        for (const QVariant &value : records)
            close(value.toMap().value(QStringLiteral("id")).toString());
    }
    Q_INVOKABLE QString openBrowser(const QVariantMap &desc, bool fresh = false, bool navigate = false, bool activate = true) {
        const QString provider = desc.value(QStringLiteral("providerId")).toString();
        const QUrl url(desc.value(QStringLiteral("url")).toString());
        const bool destinationAllowed = url.toString() == QStringLiteral("about:blank")
            || (!url.host().isEmpty() && (url.scheme() == QStringLiteral("http")
                                          || url.scheme() == QStringLiteral("https")));
        if (m_browserProfile.isEmpty() || provider.isEmpty() || !url.isValid() || !destinationAllowed)
            return {};
        // A prepared reopen is an explicit, one-record reconstruction transaction.
        // Bind the ORIGINAL requested URL before page loading can redirect/update it.
        const QString reopenRecordId = desc.value(QStringLiteral("reopenRecordId")).toString();
        if (desc.contains(QStringLiteral("reopenRecordId"))) {
            if (!fresh || reopenRecordId.isEmpty() || !url.userInfo().isEmpty()
                || desc.value(QStringLiteral("privateBrowsing")).toBool()) return {};
            bool valid = false;
            for (const QVariant &value : m_closedBrowsers) {
                const QVariantMap closed = value.toMap();
                const QVariantMap original = closed.value(QStringLiteral("descriptor")).toMap();
                if (closed.value(QStringLiteral("recordId")).toString() == reopenRecordId
                    && closed.value(QStringLiteral("accountEpoch")).toULongLong() == m_browserEpoch
                    && closed.value(QStringLiteral("profileId")).toString() == m_browserProfile + QStringLiteral("/ordinary")
                    && original.value(QStringLiteral("providerId")).toString() == provider
                    && QUrl(cleanBrowserUrl(desc.value(QStringLiteral("url"))))
                       == QUrl(original.value(QStringLiteral("url")).toString())) {
                    valid = true;
                    break;
                }
            }
            if (!valid) return {};
        }
        if (!fresh) {
            QString latest;
            quint64 order = 0;
            for (const QVariant &value : browsers()) {
                const QVariantMap rec = value.toMap();
                const QVariantMap target = rec.value(QStringLiteral("target")).toMap();
                const quint64 used = rec.value(QStringLiteral("browserUseOrder")).toULongLong();
                if (target.value(QStringLiteral("providerId")).toString() == provider
                    && target.value(QStringLiteral("privateBrowsing")).toBool() == desc.value(QStringLiteral("privateBrowsing")).toBool()
                    && (latest.isEmpty() || used > order)) {
                    latest = rec.value(QStringLiteral("id")).toString();
                    order = used;
                }
            }
            if (!latest.isEmpty()) {
                if (navigate) {
                    const int index = indexOf(latest);
                    QVariantMap rec = m_sessions.at(index).toMap();
                    QVariantMap target = desc;
                    target.insert(QStringLiteral("id"), latest);
                    rec.insert(QStringLiteral("target"), target);
                    rec.insert(QStringLiteral("title"), desc.value(QStringLiteral("title")));
                    m_sessions[index] = rec;
                    bump();
                }
                if (activate) setActive(latest);
                return latest;
            }
        }
        const QString id = QStringLiteral("browser-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVariantMap target = desc;
        target.remove(QStringLiteral("reopenRecordId"));
        target.insert(QStringLiteral("id"), id);
        QVariantMap rec{{QStringLiteral("id"), id}, {QStringLiteral("key"), id},
            {QStringLiteral("contentKey"), id}, {QStringLiteral("appType"), QStringLiteral("feria/") + provider},
            {QStringLiteral("contentKind"), QStringLiteral("browser")},
            {QStringLiteral("title"), desc.value(QStringLiteral("title"))},
            {QStringLiteral("groupTitle"), desc.value(QStringLiteral("providerName"), provider)},
            {QStringLiteral("groupIcon"), desc.value(QStringLiteral("logo"))},
            {QStringLiteral("target"), target}, {QStringLiteral("savedState"), QVariantMap{}}};
        rec.insert(QStringLiteral("browserIdentity"), QVariantMap{
            {QStringLiteral("accountEpoch"), m_browserEpoch},
            {QStringLiteral("profileId"), m_browserProfile + (desc.value(QStringLiteral("privateBrowsing")).toBool()
                ? QStringLiteral("/private") : QStringLiteral("/ordinary"))},
            {QStringLiteral("spaceId"), id}, {QStringLiteral("pageGeneration"), ++m_browserPageSeq},
            {QStringLiteral("documentGeneration"), quint64(1)},
            {QStringLiteral("hostId"), QStringLiteral("embedded")},
            {QStringLiteral("hostGeneration"), quint64(1)}});
        rec.insert(QStringLiteral("browserCloseAttempt"), QString());
        if (!reopenRecordId.isEmpty()) rec.insert(QStringLiteral("browserReopenRecordId"), reopenRecordId);
        m_sessions.append(rec);
        bump();
        if (activate) setActive(id);
        return id;
    }
    // Exact close state is part of the canonical record, never selected-page state.
    Q_INVOKABLE QVariantMap browserIdentity(const QString &id) const {
        return get(id).value(QStringLiteral("browserIdentity")).toMap();
    }
    Q_INVOKABLE QString beginBrowserClose(const QVariantMap &identity) {
        const int index = browserIdentityIndex(identity);
        if (index < 0) return {};
        QVariantMap rec = m_sessions.at(index).toMap();
        if (!rec.value(QStringLiteral("browserCloseAttempt")).toString().isEmpty()) return {};
        const QString attempt = QUuid::createUuid().toString(QUuid::WithoutBraces);
        rec.insert(QStringLiteral("browserCloseAttempt"), attempt);
        m_sessions[index] = rec;
        bump();
        return attempt;
    }
    Q_INVOKABLE bool settleBrowserClose(const QVariantMap &identity, const QString &attempt,
                                        bool accepted, const QVariantMap &descriptor) {
        const int index = browserIdentityIndex(identity);
        if (index < 0 || attempt.isEmpty()) return false;
        QVariantMap rec = m_sessions.at(index).toMap();
        if (rec.value(QStringLiteral("browserCloseAttempt")).toString() != attempt) return false;
        if (!accepted) {
            // Same-document retry requires an observed renderer rejection barrier,
            // or an observed terminal close callback while the embedder cancels
            // destruction. The latter does not undo renderer unload effects.
            // Elapsed time alone is never a cancellation acknowledgement.
            rec.insert(QStringLiteral("browserCloseAttempt"), QString());
            m_sessions[index] = rec;
            bump();
            return true;
        }
        const QVariantMap snapshot = sanitizedBrowserDescriptor(rec, descriptor);
        if (!snapshot.isEmpty()) {
            m_closedBrowsers.prepend(QVariantMap{
                {QStringLiteral("recordId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {QStringLiteral("accountEpoch"), m_browserEpoch},
                {QStringLiteral("profileId"), identity.value(QStringLiteral("profileId"))},
                {QStringLiteral("descriptor"), snapshot},
                // Fresh reopen must have been allocated after this close, not an existing tab.
                {QStringLiteral("closedPageSequence"), m_browserPageSeq}});
            while (m_closedBrowsers.size() > 10) m_closedBrowsers.removeLast();
        }
        close(identity.value(QStringLiteral("spaceId")).toString());
        return true;
    }
    Q_INVOKABLE QVariantMap advanceBrowserDocument(const QString &id) {
        const int index = indexOf(id);
        if (index < 0) return {};
        QVariantMap rec = m_sessions.at(index).toMap();
        QVariantMap identity = rec.value(QStringLiteral("browserIdentity")).toMap();
        if (identity.isEmpty()) return {};
        identity.insert(QStringLiteral("documentGeneration"),
                        identity.value(QStringLiteral("documentGeneration")).toULongLong() + 1);
        rec.insert(QStringLiteral("browserIdentity"), identity);
        rec.insert(QStringLiteral("browserCloseAttempt"), QString());
        m_sessions[index] = rec;
        bump();
        return identity;
    }
    Q_INVOKABLE QVariantMap setBrowserHost(const QVariantMap &identity, const QString &hostId) {
        const int index = browserIdentityIndex(identity);
        if (index < 0 || hostId.isEmpty()) return {};
        if (identity.value(QStringLiteral("hostId")).toString() == hostId) return identity;
        QVariantMap moved = identity;
        moved.insert(QStringLiteral("hostId"), hostId);
        moved.insert(QStringLiteral("hostGeneration"),
                     identity.value(QStringLiteral("hostGeneration")).toULongLong() + 1);
        QVariantMap rec = m_sessions.at(index).toMap();
        rec.insert(QStringLiteral("browserIdentity"), moved);
        rec.insert(QStringLiteral("browserCloseAttempt"), QString());
        m_sessions[index] = rec;
        bump();
        return moved;
    }
    Q_INVOKABLE QVariantList closedBrowserRecords(const QString &provider) const {
        QVariantList result;
        if (provider.isEmpty() || m_browserProfile.isEmpty()) return result;
        for (const QVariant &value : m_closedBrowsers) {
            const QVariantMap rec = value.toMap();
            if (rec.value(QStringLiteral("accountEpoch")).toULongLong() == m_browserEpoch
                && rec.value(QStringLiteral("descriptor")).toMap()
                   .value(QStringLiteral("providerId")).toString() == provider)
                result.append(rec);
        }
        return result;
    }
    Q_INVOKABLE QVariantMap prepareBrowserReopen(const QString &provider) const {
        const QVariantList records = closedBrowserRecords(provider);
        if (records.isEmpty()) return {};
        QVariantMap prepared = records.first().toMap();
        QVariantMap descriptor = prepared.value(QStringLiteral("descriptor")).toMap();
        descriptor.insert(QStringLiteral("reopenRecordId"), prepared.value(QStringLiteral("recordId")));
        prepared.insert(QStringLiteral("descriptor"), descriptor);
        return prepared;
    }
    // Called only after the adapter has successfully created the fresh runtime view.
    // Failure rolls that placeholder back with close(), retaining this record.
    Q_INVOKABLE bool commitBrowserReopen(const QString &recordId, const QString &newSpaceId) {
        const QVariantMap rec = get(newSpaceId);
        const QVariantMap identity = browserIdentity(newSpaceId);
        if (browserIdentityIndex(identity) < 0) return false;
        const QVariantMap target = sanitizedBrowserDescriptor(rec, {});
        if (target.isEmpty() || rec.value(QStringLiteral("browserReopenCommitted")).toBool()
            || rec.value(QStringLiteral("browserReopenRecordId")).toString() != recordId) return false;
        for (int i = 0; i < m_closedBrowsers.size(); ++i) {
            const QVariantMap closed = m_closedBrowsers.at(i).toMap();
            if (closed.value(QStringLiteral("recordId")).toString() != recordId) continue;
            const QVariantMap descriptor = closed.value(QStringLiteral("descriptor")).toMap();
            if (closed.value(QStringLiteral("accountEpoch")) != identity.value(QStringLiteral("accountEpoch"))
                || closed.value(QStringLiteral("profileId")) != identity.value(QStringLiteral("profileId"))
                || identity.value(QStringLiteral("pageGeneration")).toULongLong()
                   <= closed.value(QStringLiteral("closedPageSequence")).toULongLong()
                || target.value(QStringLiteral("providerId")) != descriptor.value(QStringLiteral("providerId"))) return false;
            QVariantMap reopened = rec;
            reopened.insert(QStringLiteral("browserReopenCommitted"), true);
            m_sessions[indexOf(newSpaceId)] = reopened;
            m_closedBrowsers.removeAt(i);
            bump();
            return true;
        }
        return false;
    }

    Q_INVOKABLE void updateBrowser(const QString &id, const QString &url, const QString &title) {
        const int index = indexOf(id);
        if (index < 0) return;
        QVariantMap rec = m_sessions.at(index).toMap();
        if (rec.value(QStringLiteral("contentKind")) != QStringLiteral("browser")) return;
        QVariantMap target = rec.value(QStringLiteral("target")).toMap();
        const QUrl destination(url);
        if (!destination.isValid() || destination.host().isEmpty()
            || (destination.scheme() != QStringLiteral("http") && destination.scheme() != QStringLiteral("https")))
            return;
        const QString label = title.isEmpty() ? rec.value(QStringLiteral("title")).toString() : title.left(300);
        if (target.value(QStringLiteral("url")).toString() == url
            && rec.value(QStringLiteral("title")).toString() == label) return;
        target.insert(QStringLiteral("url"), url);
        rec.insert(QStringLiteral("target"), target);
        rec.insert(QStringLiteral("title"), label);
        m_sessions[index] = rec;
        bump();
    }

    Q_INVOKABLE void switchTo(const QString &id) { setActive(id); }

    Q_INVOKABLE void close(const QString &id) {
        const int idx = indexOf(id);
        if (idx < 0)
            return;

        const bool wasActive = (m_activeId == id);
        m_sessions.removeAt(idx);
        bump();
        // Closing the ACTIVE session lands on the world behind — NEVER on a parked neighbor.
        // This used to promote the adjacent record (browser-tab semantics), which meant closing
        // a comic while a movie sat minimized in the taskbar force-restored the movie: close()
        // handed it activeId, and Main.qml's switch glue faithfully rebuilt its surface
        // (Hemanth-reported 2026-08-08: "exit the comic reader and the video player opens").
        // Sessions are taskbar windows, not tabs: closing one reveals the desktop behind it;
        // a minimized session comes back only when the user clicks its tile.
        if (wasActive)
            setActive(QString());
    }

    Q_INVOKABLE void saveState(const QString &id, const QVariantMap &state) {
        const int idx = indexOf(id);
        if (idx < 0)
            return;

        QVariantMap rec = m_sessions.at(idx).toMap();
        rec.insert(QStringLiteral("savedState"), state);
        m_sessions[idx] = rec;
        // no bump: saved-state is internal bookkeeping, not a visible change.
    }

    Q_INVOKABLE QVariantMap get(const QString &id) const {
        const int idx = indexOf(id);
        return idx < 0 ? QVariantMap() : m_sessions.at(idx).toMap();
    }

    Q_INVOKABLE QVariantList list() const { return m_sessions; }

    // Group sessions by appType, preserving first-seen order, for the taskbar's tiles.
    Q_INVOKABLE QVariantList groups() const {
        QVariantList out;
        QStringList order;
        for (const QVariant &v : m_sessions) {
            const QVariantMap rec = v.toMap();
            const QString app = rec.value(QStringLiteral("appType")).toString();
            const int gi = order.indexOf(app);
            if (gi < 0) {
                order.append(app);
                QVariantMap group;
                group.insert(QStringLiteral("appType"), app);
                group.insert(QStringLiteral("title"), rec.value(QStringLiteral("groupTitle"), appTitle(app)));
                group.insert(QStringLiteral("icon"), rec.value(QStringLiteral("groupIcon"), appIcon(app)));
                group.insert(QStringLiteral("sessions"), QVariantList{rec});
                out.append(group);
            } else {
                QVariantMap group = out.at(gi).toMap();
                QVariantList sessions = group.value(QStringLiteral("sessions")).toList();
                sessions.append(rec);
                group.insert(QStringLiteral("sessions"), sessions);
                out[gi] = group;
            }
        }
        return out;
    }

    // Env-gated self-test (codebase idiom - see MangaDownloader::selfTest). Logs PASS/FAIL.
    void selfTest() {
        auto mk = [](const QString &app, const QString &kind, const QString &tgt) {
            QVariantMap desc;
            desc.insert(QStringLiteral("appType"), app);
            desc.insert(QStringLiteral("contentKind"), kind);
            desc.insert(QStringLiteral("title"), tgt);
            QVariantMap target;
            target.insert(QStringLiteral("id"), tgt);
            desc.insert(QStringLiteral("target"), target);
            return desc;
        };

        bool ok = true;
        const QString a = openOrSwitch(mk(QStringLiteral("tankoban"),
                                          QStringLiteral("comic"),
                                          QStringLiteral("One Piece")));
        const QString b = openOrSwitch(mk(QStringLiteral("tankoban"),
                                          QStringLiteral("comic"),
                                          QStringLiteral("Berserk")));
        const QString c = openOrSwitch(mk(QStringLiteral("theatre"),
                                          QStringLiteral("movie"),
                                          QStringLiteral("Dune")));
        ok &= (m_sessions.size() == 3);
        const QString aAgain = openOrSwitch(mk(QStringLiteral("tankoban"),
                                               QStringLiteral("comic"),
                                               QStringLiteral("One Piece")));
        ok &= (aAgain == a) && (m_sessions.size() == 3);
        ok &= (m_activeId == a);
        ok &= (groups().size() == 2);
        ok &= (groups().at(0).toMap().value(QStringLiteral("sessions")).toList().size() == 2);
        switchTo(b);
        close(b);
        // closing the active session lands on the world behind ("" = none), never a neighbor —
        // the old promote-a-neighbor expectation is exactly the bug of 2026-08-08.
        ok &= m_activeId.isEmpty();
        ok &= (m_sessions.size() == 2);
        // one-tab-per-show (spec 2026-07-11): same show replaces in place, identical
        // content reuses, different show gets its own session.
        auto mkShow = [](const QString &show, const QString &hash, const QString &title) {
            QVariantMap desc;
            desc.insert(QStringLiteral("appType"), QStringLiteral("theatre"));
            desc.insert(QStringLiteral("contentKind"), QStringLiteral("movie"));
            desc.insert(QStringLiteral("title"), title);
            QVariantMap target;
            target.insert(QStringLiteral("showKey"), show);
            target.insert(QStringLiteral("infoHash"), hash);
            desc.insert(QStringLiteral("target"), target);
            return desc;
        };
        const int base = m_sessions.size();
        const QString s1 = openOrSwitch(mkShow(QStringLiteral("tt1"), QStringLiteral("hashA"), QStringLiteral("Show E1")));
        saveState(s1, QVariantMap{{QStringLiteral("pos"), 42}});
        const QString s2 = openOrSwitch(mkShow(QStringLiteral("tt1"), QStringLiteral("hashB"), QStringLiteral("Show E2")));
        ok &= (s2 == s1) && (m_sessions.size() == base + 1);                  // replaced, not piled
        ok &= (get(s1).value(QStringLiteral("target")).toMap()
                  .value(QStringLiteral("infoHash")).toString() == QStringLiteral("hashB"));
        ok &= get(s1).value(QStringLiteral("savedState")).toMap().isEmpty();  // stale state cleared
        saveState(s1, QVariantMap{{QStringLiteral("pos"), 7}});
        const QString s3 = openOrSwitch(mkShow(QStringLiteral("tt1"), QStringLiteral("hashB"), QStringLiteral("Show E2")));
        ok &= (s3 == s1);                                                     // identical content reuses
        ok &= !get(s1).value(QStringLiteral("savedState")).toMap().isEmpty(); // minimize position kept
        const QString s4 = openOrSwitch(mkShow(QStringLiteral("tt2"), QStringLiteral("hashC"), QStringLiteral("Other Show")));
        ok &= (s4 != s1) && (m_sessions.size() == base + 2);                  // different show = own tile
        qInfo("[session-selftest] %s", ok ? "PASS" : "FAIL");
    }

signals:
    void changed();
    void activeChangedProp();
    void activeChanged(const QString &prevId, const QString &nextId);
    // one-tab-per-show: an existing session's target was swapped in place (same id).
    void targetReplaced(const QString &id);
    // same content, another stream source (captured position kept).
    void sourceReplaced(const QString &id);

private:
    int browserIdentityIndex(const QVariantMap &identity) const {
        if (identity.isEmpty() || m_browserProfile.isEmpty()
            || identity.value(QStringLiteral("accountEpoch")).toULongLong() != m_browserEpoch) return -1;
        const int index = indexOf(identity.value(QStringLiteral("spaceId")).toString());
        if (index < 0) return -1;
        const QVariantMap canonical = m_sessions.at(index).toMap()
            .value(QStringLiteral("browserIdentity")).toMap();
        // Map equality rejects missing and extraneous fields as well as stale values.
        return !canonical.isEmpty() && canonical == identity ? index : -1;
    }
    static QString cleanBrowserUrl(const QVariant &value) {
        QUrl url(value.toString());
        if (!url.isValid()) return {};
        if (url.toString() == QStringLiteral("about:blank")) return url.toString();
        if (url.host().isEmpty() || (url.scheme() != QStringLiteral("http")
                                   && url.scheme() != QStringLiteral("https"))) return {};
        url.setUserInfo(QString());
        return url.toString(QUrl::FullyEncoded);
    }
    static QVariantMap sanitizedBrowserDescriptor(const QVariantMap &record, const QVariantMap &snapshot) {
        const QVariantMap target = record.value(QStringLiteral("target")).toMap();
        if (record.value(QStringLiteral("contentKind")) != QStringLiteral("browser")
            || target.value(QStringLiteral("privateBrowsing")).toBool()
            || target.value(QStringLiteral("providerId")).toString().isEmpty()) return {};
        QString url = cleanBrowserUrl(target.value(QStringLiteral("url")));
        const bool sameProvider = !snapshot.contains(QStringLiteral("providerId"))
            || snapshot.value(QStringLiteral("providerId")) == target.value(QStringLiteral("providerId"));
        const QString observed = sameProvider ? cleanBrowserUrl(snapshot.value(QStringLiteral("url"))) : QString();
        if (!observed.isEmpty()) url = observed;
        if (url.isEmpty()) return {};
        QVariantMap result;
        // An allowlist strips IDs, requests, POST/opener/visit state and credentials.
        for (const QString &key : {QStringLiteral("providerId"), QStringLiteral("providerName"),
                                  QStringLiteral("mode"), QStringLiteral("logo")})
            if (target.contains(key)) result.insert(key, target.value(key).toString().left(2048));
        const auto restoreId = target.value(QStringLiteral("restoreId"));
        if (restoreId.metaType().id() == QMetaType::QString && !restoreId.toString().isEmpty()
            && restoreId.toString().size() <= 100) result.insert(QStringLiteral("restoreId"), restoreId);
        result.insert(QStringLiteral("title"), (sameProvider && snapshot.contains(QStringLiteral("title"))
            ? snapshot.value(QStringLiteral("title")) : record.value(QStringLiteral("title"))).toString().left(300));
        result.insert(QStringLiteral("url"), url);
        result.insert(QStringLiteral("privateBrowsing"), false);
        const QString start = cleanBrowserUrl(target.value(QStringLiteral("startUrl")));
        if (!start.isEmpty()) result.insert(QStringLiteral("startUrl"), start);
        return result;
    }

    static QString appTitle(const QString &app) {
        if (app == QStringLiteral("tankoban"))
            return QStringLiteral("Tankoban");
        if (app == QStringLiteral("theatre"))
            return QStringLiteral("Theatre");
        if (app == QStringLiteral("biblio"))
            return QStringLiteral("Biblio");
        return app;
    }

    static QString appIcon(const QString &app) {
        if (app == QStringLiteral("tankoban"))
            return QStringLiteral("../assets/icons/manga.svg");
        if (app == QStringLiteral("theatre"))
            return QStringLiteral("../assets/icons/play.svg");
        if (app == QStringLiteral("biblio"))
            return QStringLiteral("../assets/icons/books.svg");
        return QString();
    }

    // Stable identity for dedup: appType + contentKind + the target's own id/path/key.
    static QString targetKey(const QVariantMap &desc) {
        const QVariantMap target = desc.value(QStringLiteral("target")).toMap();
        // one-tab-per-show (spec 2026-07-11): a caller-computed show identity wins —
        // every content of one show shares one session/tile. No showKey (raw
        // torrents, comics, books) -> the original per-content chain, unchanged.
        QString tk = target.value(QStringLiteral("showKey")).toString();
        if (tk.isEmpty())
            tk = contentKeyFor(desc);
        return desc.value(QStringLiteral("appType")).toString() + QStringLiteral("\x1f")
             + desc.value(QStringLiteral("contentKind")).toString() + QStringLiteral("\x1f") + tk;
    }

    // Which stream plays the content: picking another source changes this, not the content key.
    static QString sourceKeyFor(const QVariantMap &target) {
        return target.value(QStringLiteral("infoHash")).toString().toLower() + QStringLiteral("")
             + QString::number(target.value(QStringLiteral("fileIdx")).toInt()) + QStringLiteral("")
             + target.value(QStringLiteral("streamUrl")).toString();
    }

    // The per-content chain — on a show-key match this tells "same show, different
    // content" (replace) apart from "the exact same thing" (reuse). Every production
    // lane must resolve to content identity, never presentation text.
    static QString contentKeyFor(const QVariantMap &desc) {
        const QVariantMap target = desc.value(QStringLiteral("target")).toMap();
        QString tk = target.value(QStringLiteral("id")).toString();
        if (tk.isEmpty())
            tk = target.value(QStringLiteral("path")).toString();
        if (tk.isEmpty())
            tk = target.value(QStringLiteral("subId")).toString();
        if (tk.isEmpty()) {
            const QString chapterId = target.value(QStringLiteral("chapterId")).toString();
            if (!chapterId.isEmpty()) {
                QString seriesId = target.value(QStringLiteral("seriesId")).toString();
                if (seriesId.isEmpty())
                    seriesId = desc.value(QStringLiteral("title")).toString();
                QString entryKind = target.value(QStringLiteral("entryKind")).toString();
                if (entryKind.isEmpty())
                    entryKind = QStringLiteral("chapter");
                tk = QStringLiteral("comic\x1e") + seriesId + QStringLiteral("\x1e")
                   + entryKind + QStringLiteral("\x1e") + chapterId;
            }
        }
        if (tk.isEmpty()) {
            const QString infoHash = target.value(QStringLiteral("infoHash")).toString();
            if (!infoHash.isEmpty()) {
                tk = infoHash;
                if (target.contains(QStringLiteral("fileIdx")))
                    tk += QStringLiteral("\x1e") + QString::number(target.value(QStringLiteral("fileIdx")).toInt());
            }
        }
        if (tk.isEmpty())
            tk = desc.value(QStringLiteral("title")).toString();
        return tk;
    }

    int indexOf(const QString &id) const {
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions.at(i).toMap().value(QStringLiteral("id")).toString() == id)
                return i;
        }
        return -1;
    }

    void setActive(const QString &id) {
        if (m_activeId == id)
            return;
        const int index = indexOf(id);
        if (!id.isEmpty() && index < 0)
            return;
        if (index >= 0) {
            QVariantMap rec = m_sessions.at(index).toMap();
            if (rec.value(QStringLiteral("contentKind")) == QStringLiteral("browser")) {
                rec.insert(QStringLiteral("browserUseOrder"), ++m_browserUseOrder);
                m_sessions[index] = rec;
                bump();
            }
        }
        const QString prev = m_activeId;
        m_activeId = id;
        emit activeChangedProp();
        emit activeChanged(prev, id);
    }

    void bump() {
        ++m_revision;
        emit changed();
    }

    QVariantList m_sessions;
    QString m_activeId;
    int m_revision = 0;
    int m_idSeq = 0;
    QString m_browserProfile;
    quint64 m_browserUseOrder = 0;
    quint64 m_browserEpoch = 0;
    quint64 m_browserPageSeq = 0;
    QVariantList m_closedBrowsers;
};

