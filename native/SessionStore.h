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
                rec.insert(QStringLiteral("title"), desc.value(QStringLiteral("title")));
                rec.insert(QStringLiteral("target"), desc.value(QStringLiteral("target")));
                m_sessions[i] = rec;
                bump();
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
        closeBrowserSessions();
        m_browserProfile = profile;
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
        target.insert(QStringLiteral("id"), id);
        QVariantMap rec{{QStringLiteral("id"), id}, {QStringLiteral("key"), id},
            {QStringLiteral("contentKey"), id}, {QStringLiteral("appType"), QStringLiteral("feria/") + provider},
            {QStringLiteral("contentKind"), QStringLiteral("browser")},
            {QStringLiteral("title"), desc.value(QStringLiteral("title"))},
            {QStringLiteral("groupTitle"), desc.value(QStringLiteral("providerName"), provider)},
            {QStringLiteral("groupIcon"), desc.value(QStringLiteral("logo"))},
            {QStringLiteral("target"), target}, {QStringLiteral("savedState"), QVariantMap{}}};
        m_sessions.append(rec);
        bump();
        if (activate) setActive(id);
        return id;
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

private:
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
};

