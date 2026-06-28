#pragma once

// SessionStore - the OS-shell's open-sessions model, exposed to QML as `Sessions`.
// One small thing: the list of "things you currently have open" (a comic, a movie, a
// book), which one is ACTIVE, and the saved-state blob each carries so it can be torn
// down and rebuilt exactly where you left it (Approach 2 - only the active session is
// ever instantiated). The Taskbar reads this to draw app-grouped tiles; Main.qml's
// switch glue listens to activeChanged to capture/teardown/restore.
//
// QML contract:
//   Sessions.openOrSwitch({appType, contentKind, target, title}) -> id  (dedups by target key)
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
        for (const QVariant &v : m_sessions) {
            const QVariantMap rec = v.toMap();
            if (rec.value(QStringLiteral("key")).toString() == key) {
                const QString id = rec.value(QStringLiteral("id")).toString();
                setActive(id);
                return id;
            }
        }

        QVariantMap rec;
        const QString id = QStringLiteral("s%1").arg(++m_idSeq);
        rec.insert(QStringLiteral("id"), id);
        rec.insert(QStringLiteral("key"), key);
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

    Q_INVOKABLE void switchTo(const QString &id) { setActive(id); }

    Q_INVOKABLE void close(const QString &id) {
        const int idx = indexOf(id);
        if (idx < 0)
            return;

        const bool wasActive = (m_activeId == id);
        m_sessions.removeAt(idx);
        bump();
        if (wasActive) {
            const QString next = m_sessions.isEmpty()
                ? QString()
                : m_sessions.at(qMin(idx, m_sessions.size() - 1))
                      .toMap()
                      .value(QStringLiteral("id"))
                      .toString();
            setActive(next);
        }
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
                group.insert(QStringLiteral("title"), appTitle(app));
                group.insert(QStringLiteral("icon"), appIcon(app));
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
        ok &= (m_activeId == c || m_activeId == a);
        ok &= (m_sessions.size() == 2);
        qInfo("[session-selftest] %s", ok ? "PASS" : "FAIL");
    }

signals:
    void changed();
    void activeChangedProp();
    void activeChanged(const QString &prevId, const QString &nextId);

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
        QString tk = target.value(QStringLiteral("id")).toString();
        if (tk.isEmpty())
            tk = target.value(QStringLiteral("path")).toString();
        if (tk.isEmpty())
            tk = target.value(QStringLiteral("infoHash")).toString();
        if (tk.isEmpty())
            tk = desc.value(QStringLiteral("title")).toString();
        return desc.value(QStringLiteral("appType")).toString() + QStringLiteral("\x1f")
             + desc.value(QStringLiteral("contentKind")).toString() + QStringLiteral("\x1f") + tk;
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
};
