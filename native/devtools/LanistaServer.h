#pragma once
// Lanista — Colosseum's dev-control bridge (spec 2026-08-01, locked).
//
// QLocalServer on pipe "ColosseumLanista" (env COLOSSEUM_LANISTA_PIPE overrides,
// so suite-booted instances never collide with the daily app). One command per
// connection: one JSON line in, one JSON line out, close. Single-threaded on
// the Qt UI thread. ALWAYS ON for reads/grabs — local machine only, never a
// network port. Driving gates on COLOSSEUM_LANISTA_DRIVE=1 per command;
// mutations on COLOSSEUM_LANISTA_WRITE=1 (two separate switches, Tankoban 2's
// proven split).
//
// Wire format (Tankoban 2's, deliberately unchanged so brothers who know
// tankoctl already know lanista):
//   request = {"cmd": <name>, "seq": <int>, "payload": {...}}
//   reply   = {"type":"reply","seq":<int>, ...}
//   error   = {"type":"error","seq":<int>,"code":"UPPER_SNAKE","message":"..."}
//
// THE COMBINED REPLY (the heart): any command's payload may carry
//   "grab": {"target": "<objectName>"|"window"}
// and the reply gains grabPath + grabbedAt — state and pixels captured in the
// SAME event-loop turn, so they can never disagree about which instant they
// describe. Grabs are async (grabToImage callback); the socket stays open
// until the callback replies. Schema: colosseum.dev.v1.
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QLocalServer;
class QLocalSocket;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;

class LanistaServer : public QObject
{
    Q_OBJECT
public:
    explicit LanistaServer(QQmlApplicationEngine* engine, QObject* parent = nullptr);

    static QString pipeName();
    QString runDir() const { return m_runDir; }

    static constexpr const char* kSchema = "colosseum.dev.v1.0";

private:
    enum Gate { Read, Drive, Write };
    struct Command {
        Gate gate;
        // Returns true when the reply was (or will be) sent by the handler
        // itself (async grabs); false → the returned object is the reply body.
        std::function<QJsonObject(const QJsonObject& payload, QLocalSocket* sock,
                                  int seq, bool* async)> fn;
    };

    void onNewConnection();
    void onReadyRead(QLocalSocket* sock);
    void dispatch(QLocalSocket* sock, const QJsonObject& req);
    void sendReply(QLocalSocket* sock, int seq, QJsonObject body);
    void sendError(QLocalSocket* sock, int seq, const char* code, const QString& msg);

    // Task 1
    QJsonObject cmdPing() const;
    QJsonObject cmdGetState() const;
    // Task 2
    bool attachGrab(const QJsonObject& payload, QJsonObject body,
                    QLocalSocket* sock, int seq);   // true → async reply armed
    // Task 3
    QJsonObject cmdQmlGet(const QJsonObject& p) const;
    QJsonObject cmdDumpUi(const QJsonObject& p) const;
    QJsonObject cmdUiQuery(const QJsonObject& p) const;
    // Task 4
    QJsonObject cmdUiSnapshot(const QJsonObject& p);
    // Task 5
    QJsonObject cmdUiClick(const QJsonObject& p) const;
    QJsonObject cmdUiKeypress(const QJsonObject& p) const;
    QJsonObject cmdUiTextInput(const QJsonObject& p) const;
    QJsonObject cmdUiScroll(const QJsonObject& p) const;
    QJsonObject cmdUiWaitFor(const QJsonObject& p, QLocalSocket* sock, int seq,
                             bool* async);
    // Task 9
    QJsonObject cmdInvokeRead(const QJsonObject& p) const;

    QQuickWindow* mainWindow() const;
    QQuickItem* findItem(const QString& objectName) const;
    QQuickItem* resolveTarget(const QString& ref) const;   // objectName or "h<N>" handle

    QQmlApplicationEngine* m_engine;
    QLocalServer* m_server = nullptr;
    QHash<QString, Command> m_commands;
    QHash<QLocalSocket*, QByteArray> m_buf;
    QHash<QString, QPointer<QQuickItem>> m_handles;   // last ui-snapshot
    QString m_runDir;
    int m_grabCounter = 0;
};
