#include "bootstrap/InstanceLock.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

static int fails = 0;
#define CHECK(c, l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (false)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir root;
    CHECK(root.isValid(), "temporary app-data root created");
    if (!root.isValid())
        return 1;

    {
        ColosseumInstanceLock first(root.path());
        CHECK(first.tryAcquire(), "first instance acquires the shared lock");
        CHECK(first.isLocked(), "first instance reports ownership");

        ColosseumInstanceLock second(root.path());
        CHECK(!second.tryAcquire(), "second instance is rejected while the first is alive");
        CHECK(!second.isLocked(), "rejected instance does not report ownership");
    }

    ColosseumInstanceLock afterRelease(root.path());
    CHECK(afterRelease.tryAcquire(), "a new instance can acquire the lock after release");

    QTemporaryDir isolatedRoot;
    CHECK(isolatedRoot.isValid(), "isolated app-data root created");
    if (isolatedRoot.isValid()) {
        ColosseumInstanceLock isolated(isolatedRoot.path());
        CHECK(isolated.tryAcquire(), "an isolated test app-data root gets its own lock");
    }

    std::printf(fails ? "FAILS: %d\n" : "instance_lock_harness: ALL PASS\n", fails);
    return fails;
}
