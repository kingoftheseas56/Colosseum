#pragma once

// GuiStallProbe — what is holding the GUI thread while a film is playing?
//
// WHY (2026-07-29). tests/frame_pacing.sh proved the stutter is NOT the video engine, the
// hardware, copy-back, or OpenGL. Frames are ready on time; the GUI thread is too busy to let
// them through. Measured over 59s of playback: the gap between frames reaching the screen ran
// median 26ms / p99 223ms / worst 3131ms, and the GUI thread's own polishAndSync gaps tracked
// them almost exactly (median 26ms / p99 217ms / worst 3582ms). Qt Quick's threaded render loop
// cannot present a frame until the GUI thread hands it over, so every GUI-thread stall is a
// visible hitch. mpv's dropped-frame counter is blind to all of it: mpv delivered the frame, the
// app sat on it.
//
// This says WHICH work. Every event delivered on the GUI thread is timed; anything over the
// threshold is attributed to (event type, receiver class) and ranked at exit. No guessing.
//
// OFF unless COLOSSEUM_GUI_STALL_PROBE is set (value = threshold in ms, default 40). It is a
// diagnostic, never a shipped code path: when the variable is absent notify() is a straight
// passthrough to the base class.
//
//   set COLOSSEUM_GUI_STALL_PROBE=40   -> report every GUI event that blocks 40ms or longer
//
// Report goes to stderr on quit, as "GUI_STALL_PROBE ..." lines.

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QHash>
#include <QString>
#include <QMetaObject>
#include <QNetworkReply>
#include <QByteArray>
#include <QThread>
#include <QDebug>
#include <QUrl>
#include <algorithm>
#include <vector>

namespace Colosseum::Diagnostics {

inline QString startupMilestoneLine(const QString &milestone, qint64 atMs)
{
    return QStringLiteral("COLOSSEUM_STARTUP_MILESTONE milestone=%1 atMs=%2").arg(milestone).arg(atMs);
}

inline QString stallSeverity(qint64 blockedMs, int thresholdMs)
{
    // The probe threshold controls which events are reported; these fixed bands keep severity
    // comparable across runs even when thresholdMs is changed for collection.
    Q_UNUSED(thresholdMs);
    if (blockedMs >= 250)
        return QStringLiteral("critical");
    if (blockedMs >= 150)
        return QStringLiteral("severe");
    return QStringLiteral("warning");
}

inline QString stallContextFields(const QString &operation, const QString &surface)
{
    if (operation.isEmpty() && surface.isEmpty())
        return {};
    return QStringLiteral(" operation=%1 surface=%2")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(operation)),
             QString::fromLatin1(QUrl::toPercentEncoding(surface)));
}

} // namespace Colosseum::Diagnostics

class ProbedGuiApplication : public QGuiApplication {
public:
    ProbedGuiApplication(int &argc, char **argv)
        : QGuiApplication(argc, argv)
    {
        const QByteArray raw = qgetenv("COLOSSEUM_GUI_STALL_PROBE");
        m_enabled = !raw.isEmpty();
        m_startupEnabled = qEnvironmentVariableIsSet("COLOSSEUM_STARTUP_PROBE");
        if (m_startupEnabled) {
            m_startupWall.start();
            qWarning().noquote() << Colosseum::Diagnostics::startupMilestoneLine(
                                        QStringLiteral("launch"), m_startupWall.elapsed());
        }
        if (m_enabled) {
            bool ok = false;
            const int v = raw.toInt(&ok);
            m_thresholdMs = (ok && v > 0) ? v : 40;
            m_guiThread = QThread::currentThread();
            m_wall.start();
            qWarning().noquote() << QStringLiteral("GUI_STALL_PROBE armed: reporting GUI-thread events >= %1 ms")
                                        .arg(m_thresholdMs);
        }
    }

    // These are explicit boundary markers for startup qualification. They deliberately do not
    // run from notify(), so enabling the probe cannot add work to every GUI event delivery.
    void markFirstFrame()
    {
        if (m_startupEnabled && !m_firstFrameMarked) {
            m_firstFrameMarked = true;
            qWarning().noquote() << Colosseum::Diagnostics::startupMilestoneLine(
                                        QStringLiteral("first-frame"), m_startupWall.elapsed());
        }
    }

    void markShellInteractive()
    {
        if (m_startupEnabled && !m_shellInteractiveMarked) {
            m_shellInteractiveMarked = true;
            qWarning().noquote() << Colosseum::Diagnostics::startupMilestoneLine(
                                        QStringLiteral("shell-interactive"), m_startupWall.elapsed());
        }
    }

    void setStallContext(const QString &operation, const QString &surface)
    {
        if (!m_enabled)
            return;
        m_contextOperation = operation;
        m_contextSurface = surface;
    }

    bool notify(QObject *receiver, QEvent *event) override
    {
        if (!m_enabled || QThread::currentThread() != m_guiThread)
            return QGuiApplication::notify(receiver, event);

        // Nested delivery would double-count the outer event in the TOTALS, so only the OUTERMOST
        // delivery contributes to them. But the outermost receiver is usually
        // QWindowsGuiEventDispatcher handling a WM_TIMER — which names Qt's plumbing, not the code
        // actually burning the thread. So nested deliveries are still TIMED, and the slowest ones
        // inside a stalling outer event are reported alongside it. That is what turns "a timer did
        // it" into "THIS object did it". (2026-07-29: the first timestamped run showed 188 stalls
        // all attributed to the dispatcher, which is true and useless.)
        if (m_depth > 0) {
            ++m_depth;
            QElapsedTimer inner;
            inner.start();
            const bool r = QGuiApplication::notify(receiver, event);
            const qint64 innerMs = inner.elapsed();
            --m_depth;
            if (innerMs >= m_thresholdMs && receiver && receiver->metaObject()) {
                const QString iname = receiver->objectName();
                m_innerThisPass.push_back(
                    QStringLiteral("%1ms %2|%3%4")
                        .arg(innerMs)
                        .arg(static_cast<int>(event->type()))
                        .arg(QString::fromLatin1(receiver->metaObject()->className()),
                             iname.isEmpty() ? QString() : QStringLiteral(" [") + iname + QLatin1Char(']')));
            }
            return r;
        }
        ++m_depth;
        m_innerThisPass.clear();

        const int type = static_cast<int>(event->type());
        // Capture identity BEFORE delivery: a receiver may be deleteLater'd by its own handler.
        const QString cls = receiver && receiver->metaObject()
                                ? QString::fromLatin1(receiver->metaObject()->className())
                                : QStringLiteral("(null)");
        const QString name = receiver ? receiver->objectName() : QString();
        const QObject *owner = receiver ? receiver->parent() : nullptr;
        const QString ownerLabel = (cls == QLatin1String("QFutureWatcherBase") && owner && owner->metaObject())
                                       ? QStringLiteral(" parent=%1%2")
                                             .arg(QString::fromLatin1(owner->metaObject()->className()),
                                                  owner->objectName().isEmpty() ? QString()
                                                      : QStringLiteral("[") + owner->objectName() + QLatin1Char(']'))
                                       : QString();
        const auto *networkReply = qobject_cast<const QNetworkReply *>(receiver);
        const QString networkLabel = networkReply
            ? QStringLiteral(" url=%1").arg(networkReply->url().toString(QUrl::FullyEncoded))
            : QString();

        QElapsedTimer t;
        t.start();
        const bool result = QGuiApplication::notify(receiver, event);
        const qint64 ms = t.elapsed();
        --m_depth;

        if (ms >= m_thresholdMs) {
            const QString key = QStringLiteral("%1|%2%3%4%5")
                                    .arg(type)
                                    .arg(cls, name.isEmpty() ? QString() : QStringLiteral(" [") + name + QLatin1Char(']'), ownerLabel, networkLabel);
            // Emit immediately WITH a timestamp. Totals alone cannot separate start-up work from
            // work that blocks the thread mid-film, and that distinction is the whole point: a
            // 700ms stall during launch is invisible to the viewer, the same stall at 00:40 is a
            // hitch. (2026-07-29: the first version of this probe lacked timestamps and could not
            // tell the two apart — the same attribute-without-isolating mistake this investigation
            // has already made four times.)
            if (!m_sinceStart.isValid())
                m_sinceStart.start();
            QString inners;
            for (const QString &s : m_innerThisPass)
                inners += QStringLiteral("  <<< ") + s;
            qWarning().noquote() << QStringLiteral(
                                        "GUI_STALL_PROBE HIT atMs=%1 blockedMs=%2 severity=%3 "
                                        "eventTypeAndReceiver=%4%5%6")
                                        .arg(m_wall.elapsed())
                                        .arg(ms)
                                        .arg(Colosseum::Diagnostics::stallSeverity(ms, m_thresholdMs))
                                        .arg(key)
                                        .arg(Colosseum::Diagnostics::stallContextFields(
                                            m_contextOperation, m_contextSurface))
                                        .arg(inners);
            // Tally the INNER culprits too — these are the rows worth acting on.
            for (const QString &s : m_innerThisPass) {
                const int sp = s.indexOf(QLatin1Char(' '));
                if (sp <= 0) continue;
                const qint64 ims = s.left(sp - 2).toLongLong();   // strip the trailing "ms"
                Entry &ie = m_inner[s.mid(sp + 1)];
                ie.count++; ie.totalMs += ims; ie.worstMs = std::max(ie.worstMs, ims);
            }
            Entry &e = m_tally[key];
            e.count++;
            e.totalMs += ms;
            e.worstMs = std::max(e.worstMs, ms);
            m_totalBlockedMs += ms;
            m_stallCount++;
        }
        return result;
    }

    ~ProbedGuiApplication() override
    {
        if (!m_enabled)
            return;
        struct Row { QString key; qint64 total; qint64 worst; int count; };
        std::vector<Row> rows;
        rows.reserve(m_tally.size());
        for (auto it = m_tally.constBegin(); it != m_tally.constEnd(); ++it)
            rows.push_back({it.key(), it.value().totalMs, it.value().worstMs, it.value().count});
        std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.total > b.total; });

        qWarning().noquote() << QStringLiteral("GUI_STALL_PROBE SUMMARY stalls=%1 totalBlockedMs=%2 threshold=%3")
                                    .arg(m_stallCount).arg(m_totalBlockedMs).arg(m_thresholdMs);
        int shown = 0;
        for (const Row &r : rows) {
            qWarning().noquote() << QStringLiteral("GUI_STALL_PROBE ROW totalMs=%1 worstMs=%2 count=%3 eventTypeAndReceiver=%4")
                                        .arg(r.total).arg(r.worst).arg(r.count).arg(r.key);
            if (++shown >= 25)
                break;
        }
        std::vector<Row> irows;
        irows.reserve(m_inner.size());
        for (auto it = m_inner.constBegin(); it != m_inner.constEnd(); ++it)
            irows.push_back({it.key(), it.value().totalMs, it.value().worstMs, it.value().count});
        std::sort(irows.begin(), irows.end(), [](const Row &a, const Row &b) { return a.total > b.total; });
        qWarning().noquote() << QStringLiteral("GUI_STALL_PROBE ---- INNER (the actual work) ----");
        shown = 0;
        for (const Row &r : irows) {
            qWarning().noquote() << QStringLiteral("GUI_STALL_PROBE INNER totalMs=%1 worstMs=%2 count=%3 receiver=%4")
                                        .arg(r.total).arg(r.worst).arg(r.count).arg(r.key);
            if (++shown >= 25)
                break;
        }
    }

private:
    struct Entry { int count = 0; qint64 totalMs = 0; qint64 worstMs = 0; };
    bool m_enabled = false;
    bool m_startupEnabled = false;
    bool m_firstFrameMarked = false;
    bool m_shellInteractiveMarked = false;
    int m_thresholdMs = 40;
    int m_depth = 0;
    int m_stallCount = 0;
    qint64 m_totalBlockedMs = 0;
    QThread *m_guiThread = nullptr;
    QElapsedTimer m_wall;      // wall clock since the probe armed, so each stall is placed in time
    QElapsedTimer m_sinceStart;
    QElapsedTimer m_startupWall;
    QString m_contextOperation;
    QString m_contextSurface;
    QHash<QString, Entry> m_tally;
    QHash<QString, Entry> m_inner;        // the real culprits, nested inside dispatcher timer events
    std::vector<QString> m_innerThisPass;
};

// QML-facing context bridge. Keeping this separate from notify() means route/screen attribution
// is explicit at the call site and remains a no-op when the diagnostic probe is disabled.
class GuiStallProbeBridge : public QObject {
    Q_OBJECT
public:
    explicit GuiStallProbeBridge(ProbedGuiApplication *application, QObject *parent = nullptr)
        : QObject(parent), m_application(application) {}

    Q_INVOKABLE void setContext(const QString &operation, const QString &surface)
    {
        if (m_application)
            m_application->setStallContext(operation, surface);
    }

    void notifyFirstFrame()
    {
        emit firstFrameReady();
    }

signals:
    void firstFrameReady();

private:
    ProbedGuiApplication *m_application = nullptr;
};
