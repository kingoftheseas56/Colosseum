#include "server1/policy/PieceStore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

void writeFile(const std::filesystem::path &path, std::string_view value)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    require(static_cast<bool>(output), "fixture file write succeeds");
}

void seedBitmap(const std::filesystem::path &root,
                std::size_t count,
                const std::vector<std::size_t> &trueBits)
{
    server1::policy::VerificationBitmap bitmap(count, root / ".verification-bitmap");
    for (const auto bit : trueBits)
        bitmap.set(bit, true);
    bitmap.persist();
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

    const auto multiRoot = caseRoot / "multi-piece-error";
    auto multiFailure = makeEightByteStore(multiRoot);
    multiFailure.stage(0, bytes("abcd"));
    multiFailure.stage(1, bytes("eFGH"));
    require(multiFailure.verify(0).success && multiFailure.verify(1).success,
            "multi-piece failure fixture verifies both pieces in memory");
    multiFailure.failNextWrite("later piece write", 1);
    const auto laterFailure = multiFailure.commit(0, 2);
    require(laterFailure.state == CommitState::Error
                && !multiFailure.isCommitted(0) && !multiFailure.isCommitted(1),
            "later write failure cannot commit any piece in the atomic range");
    server1::policy::VerificationBitmap multiBitmap(
        2, multiRoot / ".verification-bitmap");
    require(!multiBitmap.get(0) && !multiBitmap.get(1),
            "later write failure persists no bitmap bit for the atomic range");
    auto multiReopen = makeEightByteStore(multiRoot);
    require(!multiReopen.isVerified(0) && !multiReopen.isVerified(1),
            "partially written atomic range reopens wholly unverified");

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

void caseK06_F2(const std::filesystem::path &root)
{
    const auto caseRoot = root / "K06-F2";
    std::filesystem::remove_all(caseRoot);

    const auto validRoot = caseRoot / "valid-cross-file-tail";
    {
        PersistentPieceStore store(validRoot, 4, 6, 4,
            {StoreFile{0, 5}, StoreFile{5, 1}},
            {"81fe8bfe87576c3ecb22426f8e57847382917acf",
             "f822051471957b7bbebb8ab088fe9bd6d14f4261"});
        store.stage(0, bytes("abcd"));
        require(store.verify(0).success, "valid first piece verifies before commit");
        require(store.commit(0, 1).state == CommitState::Committed,
                "valid first piece commits");
        store.stage(1, bytes("ef"));
        require(store.verify(1).success, "valid cross-file tail verifies before commit");
        require(store.commit(1, 2).state == CommitState::Committed,
                "valid cross-file tail commits");
    }
    {
        PersistentPieceStore reopened(validRoot, 4, 6, 4,
            {StoreFile{0, 5}, StoreFile{5, 1}},
            {"81fe8bfe87576c3ecb22426f8e57847382917acf",
             "f822051471957b7bbebb8ab088fe9bd6d14f4261"});
        require(reopened.isVerified(0) && reopened.isCommitted(0)
                    && !reopened.isAssembled(0),
                "valid persisted first piece restores verified and committed only");
        require(reopened.isVerified(1) && reopened.isCommitted(1)
                    && !reopened.isAssembled(1),
                "valid persisted cross-file tail restores verified and committed only");
        require(reopened.read(1).value() == bytes("ef"),
                "restored cross-file tail reads exact physical bytes");
    }

    const auto missingRoot = caseRoot / "missing";
    writeFile(missingRoot / "0", "abcd");
    seedBitmap(missingRoot, 1, {0});
    std::filesystem::remove(missingRoot / "0");
    {
        PersistentPieceStore missing(missingRoot, 4, 4, 4,
            {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
        require(!missing.isVerified(0) && !missing.isCommitted(0)
                    && !missing.isAssembled(0),
                "missing destination invalidates restored state");
    }
    {
        server1::policy::VerificationBitmap persisted(
            1, missingRoot / ".verification-bitmap");
        require(!persisted.get(0), "missing-destination invalidation is persisted");
    }

    const auto shortRoot = caseRoot / "short";
    writeFile(shortRoot / "0", "abcd");
    seedBitmap(shortRoot, 1, {0});
    std::filesystem::resize_file(shortRoot / "0", 3);
    {
        PersistentPieceStore shortFile(shortRoot, 4, 4, 4,
            {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
        require(!shortFile.isVerified(0) && !shortFile.isCommitted(0),
                "short physical destination invalidates persisted true bit");
    }
    {
        server1::policy::VerificationBitmap persisted(
            1, shortRoot / ".verification-bitmap");
        require(!persisted.get(0), "short-destination invalidation is persisted");
    }

    const auto gapRoot = caseRoot / "logical-gap";
    writeFile(gapRoot / "0", "ab");
    writeFile(gapRoot / "1", "d");
    seedBitmap(gapRoot, 1, {0});
    {
        PersistentPieceStore gap(gapRoot, 4, 4, 4,
            {StoreFile{0, 2}, StoreFile{3, 1}},
            {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
        require(!gap.isVerified(0) && !gap.isCommitted(0),
                "logical destination gap invalidates persisted true bit");
    }
    {
        server1::policy::VerificationBitmap persisted(
            1, gapRoot / ".verification-bitmap");
        require(!persisted.get(0), "logical-gap invalidation is persisted");
    }

    const auto falseRoot = caseRoot / "false-bit";
    writeFile(falseRoot / "0", "abcd");
    seedBitmap(falseRoot, 1, {});
    PersistentPieceStore falseBit(falseRoot, 4, 4, 4,
        {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
    require(!falseBit.isVerified(0) && !falseBit.isCommitted(0)
                && !falseBit.isAssembled(0),
            "physical bytes never promote a persisted false bit");

    const auto verifyOnlyRoot = caseRoot / "verify-before-commit";
    {
        PersistentPieceStore verifyOnly(verifyOnlyRoot, 4, 4, 4,
            {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
        verifyOnly.stage(0, bytes("abcd"));
        require(verifyOnly.verify(0).success && verifyOnly.isVerified(0)
                    && !verifyOnly.isCommitted(0),
                "hash verification alone is not a disk commit");
    }
    PersistentPieceStore verifyOnlyReopen(verifyOnlyRoot, 4, 4, 4,
        {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
    require(!verifyOnlyReopen.isVerified(0) && !verifyOnlyReopen.isCommitted(0),
            "verify-before-commit does not restore durable state");

    const auto failedRoot = caseRoot / "failed-write";
    {
        PersistentPieceStore failed(failedRoot, 4, 4, 4,
            {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
        failed.stage(0, bytes("abcd"));
        require(failed.verify(0).success, "failed-write fixture verifies");
        failed.failNextWrite("injected failure");
        require(failed.commit(0, 1).state == CommitState::Error,
                "injected write failure rejects commit");
    }
    PersistentPieceStore failedReopen(failedRoot, 4, 4, 4,
        {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
    require(!failedReopen.isVerified(0) && !failedReopen.isCommitted(0),
            "failed write does not restore durable state");

    const auto stagedRoot = caseRoot / "staged-read";
    writeFile(stagedRoot / "0", "WXYZ");
    PersistentPieceStore staged(stagedRoot, 4, 4, 4,
        {StoreFile{0, 4}}, {"81fe8bfe87576c3ecb22426f8e57847382917acf"});
    staged.stage(0, bytes("abcd"));
    require(staged.read(0).value() == bytes("abcd"),
            "staged bytes remain the memory-first read source");

    const auto blockedPath = caseRoot / "bitmap-open-failure";
    std::filesystem::create_directories(blockedPath);
    bool directPersistFailed = false;
    try {
        server1::policy::VerificationBitmap blocked(1, blockedPath);
        blocked.set(0, true);
        blocked.persist();
    } catch (const std::exception &) {
        directPersistFailed = true;
    }
    require(directPersistFailed,
            "bitmap persistence reports destination-open failure");

    const auto deniedRoot = caseRoot / "denied-bitmap-repair";
    seedBitmap(deniedRoot, 1, {0});
    const auto deniedBitmap = deniedRoot / ".verification-bitmap";
    const auto originalPermissions = std::filesystem::status(deniedBitmap).permissions();
    std::filesystem::permissions(deniedBitmap,
        std::filesystem::perms::owner_write
            | std::filesystem::perms::group_write
            | std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove);
    std::optional<PersistentPieceStore> escaped;
    bool repairFailed = false;
    try {
        escaped.emplace(deniedRoot, 4, 4, 4,
            std::vector<StoreFile>{{0, 4}},
            std::vector<std::string>{"81fe8bfe87576c3ecb22426f8e57847382917acf"});
    } catch (const std::exception &) {
        repairFailed = true;
    }
    std::filesystem::permissions(deniedBitmap, originalPermissions,
                                 std::filesystem::perm_options::replace);
    require(repairFailed && !escaped.has_value(),
            "failed stale-bit repair prevents committed store exposure");
    server1::policy::VerificationBitmap deniedPersisted(1, deniedBitmap);
    require(deniedPersisted.get(0),
            "failed stale-bit repair does not masquerade as a durable clear");

    std::cout << "K06-F2 PASS\n";
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
        if (requested == "all" || requested == "K06-F2")
            caseK06_F2(root);
        if (requested != "all" && requested != "K06-01" && requested != "K06-02"
            && requested != "K06-03" && requested != "K06-F2")
            throw std::runtime_error("unknown K06 case");
    } catch (const std::exception &error) {
        std::cerr << "K06 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
