#include "account/WindowsAccountCredentialStore.h"

#include <QCoreApplication>
#include <QList>

#include <cstdlib>
#include <iostream>

namespace {

const QByteArray kProbeToken("colosseum-l04-secret-service-cross-process-probe-v1");

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ACCOUNT_CREDENTIAL_PERSISTENCE_FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool containsProbe(const WindowsAccountCredentialStore& store)
{
    return store.pendingRevocations().contains(kProbeToken);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    require(argc == 2, "expected exactly one mode: seed, verify, delete-empty, or cleanup");

    WindowsAccountCredentialStore store;
    require(store.isAvailable(), "Secret Service credential store is unavailable");

    const QByteArray mode(argv[1]);
    if (mode == "seed") {
        require(store.removePendingRevocation(kProbeToken), "stale probe cleanup failed before seed");
        require(store.addPendingRevocation(kProbeToken), "seed write failed");
        require(containsProbe(store), "seed process could not read its persisted probe");
        std::cout << "ACCOUNT_CREDENTIAL_SEED_OK\n";
        return 0;
    }
    if (mode == "verify") {
        require(containsProbe(store), "second process did not observe the seeded probe");
        std::cout << "ACCOUNT_CREDENTIAL_VERIFY_OK\n";
        return 0;
    }
    if (mode == "delete-empty") {
        require(store.removePendingRevocation(kProbeToken), "third-process delete failed");
        require(!containsProbe(store), "third process still sees the deleted probe");
        std::cout << "ACCOUNT_CREDENTIAL_DELETE_EMPTY_OK\n";
        return 0;
    }
    if (mode == "cleanup") {
        require(store.removePendingRevocation(kProbeToken), "cleanup delete failed");
        require(!containsProbe(store), "cleanup left the probe behind");
        std::cout << "ACCOUNT_CREDENTIAL_CLEANUP_OK\n";
        return 0;
    }

    require(false, "unknown mode");
    return 1;
}
