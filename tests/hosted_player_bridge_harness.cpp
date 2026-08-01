// hosted_player_bridge_harness.cpp — the least-privilege WebChannel bridge accepts only
// a well-formed, in-bounds VidKing PLAYER_EVENT and drops everything else. This is a
// security boundary: the bridge is the ONLY thing the hosted VidKing iframe can reach,
// so it must be paranoid about what it forwards into the app.
//
// Custom main() in the house harness style (no GoogleTest): print a sentinel and exit
// with the failure count. playerEvent is emitted synchronously on this thread, so a
// direct-connected lambda counts emissions without an event loop.
#include "hostedplayer/HostedPlayerBridge.h"

#include <QCoreApplication>
#include <QMetaMethod>
#include <QVariantMap>
#include <cstdio>

static int failures = 0;
static void ok(bool cond, const char *label)
{
    std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", label);
    if (!cond)
        ++failures;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    HostedPlayerBridge bridge;

    int emissions = 0;
    QVariantMap last;
    QObject::connect(&bridge, &HostedPlayerBridge::playerEvent,
                     [&](const QVariantMap &e) { ++emissions; last = e; });
    auto reset = [&]() { emissions = 0; last.clear(); };

    // ---- accepted: exactly one event, finite values, allowlisted fields ----
    reset();
    bridge.postPlayerEvent(R"({"event":"timeupdate","currentTime":12.5,"duration":100,"session":"abc"})");
    ok(emissions == 1, "valid timeupdate emits exactly one event");
    ok(last.value("event").toString() == "timeupdate", "event name preserved");
    ok(last.value("currentTime").toDouble() == 12.5, "currentTime preserved");
    ok(last.value("duration").toDouble() == 100.0, "duration preserved");
    ok(last.value("session").toString() == "abc", "session token preserved");

    // ---- rejected: no signal at all ----
    reset(); bridge.postPlayerEvent(R"({"event":"malware","currentTime":1,"duration":2})");
    ok(emissions == 0, "unknown event name rejected");

    reset(); bridge.postPlayerEvent(R"({"event":"timeupdate","currentTime":-1,"duration":100})");
    ok(emissions == 0, "negative currentTime rejected");

    reset(); bridge.postPlayerEvent(R"({"event":"timeupdate","currentTime":1,"duration":-5})");
    ok(emissions == 0, "negative duration rejected");

    reset(); bridge.postPlayerEvent(R"({"event":"timeupdate","currentTime":1,"duration":90000})");
    ok(emissions == 0, "duration over 24 hours rejected");

    reset(); bridge.postPlayerEvent(R"({"event":"timeupdate","currentTime":200,"duration":100})");
    ok(emissions == 0, "currentTime over duration + tolerance rejected");

    reset(); bridge.postPlayerEvent(R"({"event":"timeupdate","currentTime":104,"duration":100})");
    ok(emissions == 1, "currentTime within the 5s tolerance accepted");

    reset(); bridge.postPlayerEvent(R"({"event":"play"})");
    ok(emissions == 1, "a bare play event (no numbers) is accepted");

    // oversize payload (> 4096 bytes) → dropped before parse
    reset();
    {
        QString big = QStringLiteral("{\"event\":\"timeupdate\",\"currentTime\":1,\"duration\":100,\"pad\":\"")
                      + QString(5000, QLatin1Char('x')) + QStringLiteral("\"}");
        bridge.postPlayerEvent(big);
    }
    ok(emissions == 0, "payload over 4096 bytes rejected");

    reset(); bridge.postPlayerEvent(QStringLiteral("{not valid json"));
    ok(emissions == 0, "malformed JSON rejected");

    reset(); bridge.postPlayerEvent(QStringLiteral("[1,2,3]"));
    ok(emissions == 0, "a non-object JSON value rejected");

    // ---- least privilege: exactly ONE public invokable (postPlayerEvent) ----
    const QMetaObject *mo = bridge.metaObject();
    int publicInvokables = 0;
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.methodType() == QMetaMethod::Method && m.access() == QMetaMethod::Public)
            ++publicInvokables;
    }
    ok(publicInvokables == 1, "bridge exposes exactly one public invokable (postPlayerEvent)");

    if (failures == 0)
        std::printf("\nHOSTED_BRIDGE_OK\n");
    else
        std::printf("\n%d FAILED\n", failures);
    return failures;
}
