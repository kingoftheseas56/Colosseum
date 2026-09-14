#include "server1/policy/PieceStore.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using server1::policy::ByteBuffer;
using server1::policy::CommitState;
using server1::policy::PersistentPieceStore;
using server1::policy::StoreFile;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

ByteBuffer bytes(std::string_view value)
{
    return ByteBuffer(value.begin(), value.end());
}

PersistentPieceStore makeEightByteStore(const std::filesystem::path &root)
{
    return PersistentPieceStore(root, 4, 8, 4,
        {StoreFile{0, 5}, StoreFile{5, 3}},
        {"81fe8bfe87576c3ecb22426f8e57847382917acf",
         "af22ae53b04cc158b44032b537842902627055dc"});
}

void caseK06_01(const std::filesystem::path &root)
{
    const auto caseRoot = root / "K06-01";
    std::filesystem::remove_all(caseRoot);
    auto store = makeEightByteStore(caseRoot);
    store.stage(0, bytes("abcd"));
    auto first = store.verify(0);
    require(first.complete && first.success, "first real piece verifies");
    require(store.commit(first.start, first.endExclusive).state == CommitState::Committed,
            "first piece commits");

    const auto overridePath = caseRoot / "override-second.bin";
    store.setDestination(1, overridePath);
    store.stage(1, bytes("eFGH"));
    auto second = store.verify(1);
    require(second.complete && second.success, "cross-file piece verifies");
    const auto committed = store.commit(second.start, second.endExclusive);
    require(committed.state == CommitState::Committed && committed.callbackCount == 2,
            "single-piece source commit reports per-piece plus terminal callbacks");
    require(std::filesystem::exists(caseRoot / "0")
                && std::filesystem::exists(overridePath),
            "numeric default and changed destination both receive bytes");
    require(store.destination(1) == overridePath, "destination override replaces open path");
    auto reopenedCommitted = makeEightByteStore(caseRoot);
    require(reopenedCommitted.isVerified(0),
            "persisted verification survives only when its destination bytes exist");

    PersistentPieceStore absent(caseRoot / "absent", 4, 2, 4,
        {StoreFile{0, 2}}, {""});
    std::string error;
    require(!absent.read(0, &error).has_value()
                && error.find("does not exist") != std::string::npos,
            "absent destination remains a visible error");

    PersistentPieceStore shortStore(caseRoot / "short", 4, 6, 4,
        {StoreFile{0, 6}},
        {"81fe8bfe87576c3ecb22426f8e57847382917acf",
         "f822051471957b7bbebb8ab088fe9bd6d14f4261"});
    shortStore.stage(0, bytes("abcd"));
    shortStore.verify(0);
    shortStore.commit(0, 1);
    shortStore.stage(1, bytes("ef"));
    auto shortVerify = shortStore.verify(1);
    require(shortVerify.complete && shortVerify.success, "final short piece verifies");
    shortStore.commit(1, 2);
    require(shortStore.read(1).value() == bytes("ef"), "final short read keeps its exact size");
    bool rejected = false;
    try {
        PersistentPieceStore invalid(caseRoot / "invalid", 0, 4, 4,
                                     {StoreFile{0, 4}}, {""});
        static_cast<void>(invalid);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "geometry is rejected before any piece-count division");
    std::cout << "K06-01 PASS\n";
}

void caseK06_02(const std::filesystem::path &root)
{
    const auto caseRoot = root / "K06-02";
    std::filesystem::remove_all(caseRoot);
    PersistentPieceStore store(caseRoot, 4, 8, 8, {StoreFile{0, 8}},
        {"425af12a0743502b322e93a015bcf868e324d56a"});
    store.stage(0, bytes("abcd"));
    require(store.isAssembled(0) && !store.isVerified(0) && !store.isCommitted(0),
            "assembled availability is distinct from verification and commit");
    require(store.read(0).value() == bytes("abcd"),
            "assembled virtual data is observable before real-piece verification");
    require(!store.verify(0).complete, "incomplete real group cannot verify");

    store.stage(1, bytes("xxxx"));
    const auto corrupt = store.verify(1);
    require(corrupt.complete && !corrupt.success, "corrupt complete group is rejected");
    require(!store.isAssembled(0) && !store.isAssembled(1),
            "corrupt verification resets the exact real-piece range");

    store.stage(0, bytes("abcd"));
    store.stage(1, bytes("efgh"));
    const auto valid = store.verify(1);
    require(valid.complete && valid.success && valid.start == 0
                && valid.endExclusive == 2,
            "valid virtual group maps to one real verification piece");
    require(store.isVerified(0) && store.isVerified(1) && !store.isCommitted(0),
            "verified remains distinct from disk-committed");
    server1::policy::VerificationBitmap persisted(2, caseRoot / ".verification-bitmap");
    require(!persisted.get(0) && !persisted.get(1),
            "staged verification is not persisted before destination commit");
    PersistentPieceStore noDestination(caseRoot, 4, 8, 8, {StoreFile{0, 8}},
        {"425af12a0743502b322e93a015bcf868e324d56a"});
    require(!noDestination.isVerified(0) && !noDestination.isVerified(1),
            "uncommitted verified bytes reopen unverified");
    store.stage(0, bytes("abcd"));
    require(!store.isVerified(0), "restaging clears prior verification state");
    server1::policy::VerificationBitmap restaged(2, caseRoot / ".verification-bitmap");
    require(!restaged.get(0), "restaging clears the persisted verification bit");
    require(store.commit(0, 1).state == CommitState::Error,
            "restaged bytes cannot commit before reverification");
    std::cout << "K06-02 PASS\n";
}

void caseK06_03(const std::filesystem::path &root)
{
    const auto caseRoot = root / "K06-03";
    std::filesystem::remove_all(caseRoot);
    auto store = makeEightByteStore(caseRoot);
    store.stage(0, bytes("abcd"));
    store.verify(0);
    store.pauseWrites();
    require(store.commit(0, 1).state == CommitState::Queued,
            "paused single-writer queue retains commit");
    store.close();
    require(store.closeQueued() && !store.closed(), "close queues behind writes");
    store.resumeWrites();
    require(store.isCommitted(0) && store.closed(), "write drains before queued close");
    require(store.ledger() == std::vector<std::string>({"commit:0", "close"}),
            "commit and close ordering is causal");

    auto failing = makeEightByteStore(caseRoot / "errors");
    failing.stage(0, bytes("abcd"));
    failing.verify(0);
    failing.failNextWrite("partial write");
    const auto failed = failing.commit(0, 1);
    require(failed.state == CommitState::Error && !failing.isCommitted(0),
            "partial or disk error cannot become committed");
    require(failing.isVerified(0), "disk failure does not fabricate or erase hash result");
    auto failedReopen = makeEightByteStore(caseRoot / "errors");
    require(!failedReopen.isVerified(0),
            "failed destination write reopens unverified because no committed bit was persisted");

    const auto bitmapPath = caseRoot / "bitmap";
    {
        server1::policy::VerificationBitmap bitmap(2, bitmapPath);
        bitmap.set(0, true);
        bitmap.persist();
    }
    std::filesystem::remove(caseRoot / "0");
    server1::policy::VerificationBitmap reopened(2, bitmapPath);
    reopened.invalidateMissing({false, true});
    require(!reopened.get(0), "missing destination invalidates stale persisted verification");

    auto queuedFailure = makeEightByteStore(caseRoot / "queued-error");
    queuedFailure.stage(0, bytes("abcd"));
    queuedFailure.verify(0);
    queuedFailure.pauseWrites();
    queuedFailure.failNextWrite("queued partial write");
    queuedFailure.commit(0, 1);
    queuedFailure.close();
    queuedFailure.resumeWrites();
    require(queuedFailure.ledger() == std::vector<std::string>({
                "error:queued partial write", "close"}),
            "queued write error remains visible before the queued close");

    PersistentPieceStore uncovered(caseRoot / "uncovered", 4, 4, 4,
        {StoreFile{0, 2}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
    uncovered.stage(0, bytes("abcd"));
    require(uncovered.verify(0).success, "coverage regression reaches commit");
    const auto incomplete = uncovered.commit(0, 1);
    require(incomplete.state == CommitState::Error && !uncovered.isCommitted(0),
            "destination coverage gaps cannot become committed");
    std::cout << "K06-03 PASS\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const std::filesystem::path root = argc > 1 ? argv[1] : ".";
        const std::string requested = argc > 2 ? argv[2] : "all";
        if (requested == "all" || requested == "K06-01")
            caseK06_01(root);
        if (requested == "all" || requested == "K06-02")
            caseK06_02(root);
        if (requested == "all" || requested == "K06-03")
            caseK06_03(root);
        if (requested != "all" && requested != "K06-01" && requested != "K06-02"
            && requested != "K06-03")
            throw std::runtime_error("unknown K06 case");
    } catch (const std::exception &error) {
        std::cerr << "K06 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
