#include "engine/MangaImageHostResolver.h"

#include <QCoreApplication>
#include <QHash>
#include <QQueue>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <utility>

static int fails = 0;
#define CHECK(c, l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    int lookupCount = 0;
    QHash<QString, QQueue<MangaImageHostResolver::LookupDone>> completions;
    MangaImageHostResolver resolver(
        [&lookupCount, &completions](const QString& host,
                                     MangaImageHostResolver::LookupDone done) {
            ++lookupCount;
            completions[host].enqueue(std::move(done));
        });

    QStringList dispatchedIps;
    QList<int> dispatchedAttempts;
    resolver.resolve(QStringLiteral("cdn.example"), [&](const QString& ipv4) {
        dispatchedIps.append(ipv4);
        dispatchedAttempts.append(0);
    });
    resolver.resolve(QStringLiteral("cdn.example"), [&](const QString& ipv4) {
        dispatchedIps.append(ipv4);
        dispatchedAttempts.append(2);
    });
    CHECK(lookupCount == 1, "two pages on one unresolved host start one lookup");
    CHECK(resolver.pendingCount(QStringLiteral("cdn.example")) == 2,
          "both pages remain queued until delayed DNS completion");
    CHECK(dispatchedIps.isEmpty(), "delayed lookup does not dispatch before completion");

    auto delayedDone = std::move(completions[QStringLiteral("cdn.example")].dequeue());
    delayedDone(QStringLiteral("203.0.113.10"));
    delayedDone(QStringLiteral("203.0.113.11")); // hostile duplicate callback is ignored
    CHECK((dispatchedIps == QStringList{QStringLiteral("203.0.113.10"),
                                        QStringLiteral("203.0.113.10")} ),
          "one lookup dispatches each queued page with its resolved IPv4");
    CHECK((dispatchedAttempts == QList<int>{0, 2}),
          "lookup completion preserves each page's retry attempt");
    CHECK(resolver.pendingCount(QStringLiteral("cdn.example")) == 0,
          "completed host drains all queued pages");

    int fallbackCompletions = 0;
    QString fallbackIp;
    resolver.resolve(QStringLiteral("missing.example"), [&](const QString& ipv4) {
        ++fallbackCompletions;
        fallbackIp = ipv4;
    });
    CHECK(lookupCount == 2, "a different host starts its own lookup");
    auto missingDone = std::move(completions[QStringLiteral("missing.example")].dequeue());
    missingDone(QString());
    missingDone(QStringLiteral("203.0.113.12"));
    CHECK(fallbackCompletions == 1 && fallbackIp.isEmpty(),
          "no-IPv4 completion answers once with empty pin for hostname fallback");

    int cancelledCompletions = 0;
    const auto cancelledId = resolver.resolve(QStringLiteral("cancel.example"),
        [&](const QString&) { ++cancelledCompletions; });
    CHECK(resolver.pendingCount(QStringLiteral("cancel.example")) == 1,
          "cancellable request occupies one pending slot");
    CHECK(resolver.cancel(cancelledId), "cancelling a pending request succeeds once");
    CHECK(!resolver.cancel(cancelledId), "cancelling the same request twice is a no-op");
    CHECK(resolver.pendingCount(QStringLiteral("cancel.example")) == 0,
          "cancellation releases the pending slot");
    auto cancelledDone = std::move(completions[QStringLiteral("cancel.example")].dequeue());
    cancelledDone(QStringLiteral("203.0.113.13"));
    CHECK(cancelledCompletions == 0,
          "late DNS completion cannot dispatch a cancelled page");

    int restartedCompletions = 0;
    resolver.resolve(QStringLiteral("cancel.example"), [&](const QString& ipv4) {
        ++restartedCompletions;
        CHECK(ipv4 == QStringLiteral("203.0.113.14"),
              "a fresh generation receives the fresh resolver result");
    });
    CHECK(lookupCount == 4,
          "cancelling the last request clears the host gate for a fresh lookup");
    CHECK(resolver.pendingCount(QStringLiteral("cancel.example")) == 1,
          "fresh lookup owns a new pending request");
    auto restartedDone = std::move(completions[QStringLiteral("cancel.example")].dequeue());
    restartedDone(QStringLiteral("203.0.113.14"));
    CHECK(restartedCompletions == 1,
          "fresh lookup dispatches its request exactly once");

    MangaImageHostResolver::LookupDone lateAfterDestroy;
    int afterDestroyCompletions = 0;
    {
        MangaImageHostResolver doomedResolver(
            [&lookupCount, &completions](const QString& host,
                                         MangaImageHostResolver::LookupDone done) {
                ++lookupCount;
                completions[host].enqueue(std::move(done));
            });
        doomedResolver.resolve(QStringLiteral("destroy.example"), [&](const QString&) {
            ++afterDestroyCompletions;
        });
        lateAfterDestroy = std::move(completions[QStringLiteral("destroy.example")].dequeue());
    }
    lateAfterDestroy(QStringLiteral("203.0.113.15"));
    CHECK(afterDestroyCompletions == 0,
          "late injected completion after resolver destruction cannot dispatch");

    std::printf(fails ? "FAILS: %d\n" : "manga_image_host_resolver_harness: ALL PASS\n", fails);
    return fails;
}
