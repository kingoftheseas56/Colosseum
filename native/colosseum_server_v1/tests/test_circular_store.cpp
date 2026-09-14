#include "server1/policy/CircularPieceStore.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace {

using server1::policy::ByteBuffer;
using server1::policy::CircularPieceStore;
using server1::policy::CircularStoreMode;

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        fail(message);
    }
}

ByteBuffer bytes(std::size_t size, std::uint8_t value)
{
    return ByteBuffer(size, value);
}

std::string hashForByte(std::uint8_t value)
{
    if (value == 0) return "9069ca78e7450a285173431b3e52c5c25299e473";
    if (value == 1) return "a93755f8273b0e8dc4b0ecc158e5853119a24bf0";
    if (value == 2) return "322862604601a5a17f8adbf96e318bf2adecf872";
    if (value == 8) return "2b8ea1e3273d046280cbc5f24c9b615fbded944f";
    return {};
}

void caseK0701(const std::filesystem::path &root)
{
    CircularPieceStore store(root, CircularStoreMode::Memory, 12, 4);
    expect(store.capacity() == 3, "K07-01 capacity is floor(size/pieceLength)");
    expect(store.write(0, bytes(4, 0), {}, {}, 10).success, "K07-01 write slot 0");
    expect(store.write(1, bytes(4, 1), {}, {}, 20).success, "K07-01 write slot 1");
    expect(store.write(2, bytes(4, 2), {}, {}, 30).success, "K07-01 write slot 2");
    expect(store.commit(0, 0, hashForByte(0)).success, "K07-01 real SHA-1 commit 0");
    expect(store.commit(1, 1, hashForByte(1)).success, "K07-01 real SHA-1 commit 1");
    const auto full = store.write(3, bytes(4, 3), {0}, {1}, 40);
    expect(!full.success, "K07-01 selected, locked and uncommitted slots are unfreeable");
    expect(full.error == "circular buf is full, unable to free; unfreeable pieces: 0, 2uc",
           "K07-01 source-compatible full-buffer error");
}

void caseK0702(const std::filesystem::path &root)
{
    CircularPieceStore store(root, CircularStoreMode::Memory, 12, 4);
    for (std::size_t piece = 0; piece < 3; ++piece) {
        expect(store.write(piece, bytes(4, static_cast<std::uint8_t>(piece)), {}, {}, 10 + piece).success,
               "K07-02 fill");
        expect(store.commit(piece, piece, hashForByte(static_cast<std::uint8_t>(piece))).success,
               "K07-02 real SHA-1 commit");
    }
    expect(store.read(0, 100).has_value(), "K07-02 controlled access refresh");
    const auto replacement = store.write(3, bytes(4, 3), {1}, {}, 110);
    expect(replacement.success && replacement.resetPiece && *replacement.resetPiece == 2,
           "K07-02 oldest committed nonselected unlocked victim");
    expect(store.resetEvents() == std::vector<std::size_t>{2}, "K07-02 exact reset event");
    expect(!store.read(2, 120).has_value() && store.read(3, 120).has_value(),
           "K07-02 victim replaced with exact bytes");
}

void caseK0703(const std::filesystem::path &root)
{
    CircularPieceStore store(root, CircularStoreMode::Filesystem, 4, 4);
    expect(store.write(7, bytes(3, 7), {}, {}, 1).success, "K07-03 tail write");
    const auto committed = store.commit(7, 7, "381a8d92c69637ebb04909dcd1dbf60f217b7c45");
    expect(committed.success && committed.verification.complete
               && committed.verification.success && committed.noNotifyHave
               && committed.spillTokens.size() == 1,
           "K07-03 verified commit suppresses have notification and queues one spill");
    const auto corrupt = store.commit(7, 7, "9069ca78e7450a285173431b3e52c5c25299e473");
    expect(!corrupt.success && !corrupt.noNotifyHave,
           "K07-03 corrupt SHA-1 cannot commit circular bytes");
    expect(!store.commit(7, 8, "381a8d92c69637ebb04909dcd1dbf60f217b7c45").success,
           "K07-03 missing required piece cannot commit a range");
    expect(!store.commit(7, 7, {}).success,
           "K07-03 absent expected SHA-1 cannot fabricate verification");
    const auto before = store.read(7, 2);
    expect(before && before->size() == 3 && (*before)[2] == 7,
           "K07-03 read during spill returns in-memory tail bytes");

    const auto overwrite = store.write(8, bytes(4, 8), {}, {}, 3);
    expect(overwrite.success && overwrite.resetPiece == 7, "K07-03 committed spill may be evicted");
    expect(!store.completeSpill(committed.spillTokens[0], true), "K07-03 stale spill completion ignored");
    const auto current = store.read(8, 4);
    expect(current && (*current)[0] == 8, "K07-03 stale completion cannot overwrite replacement");

    const auto nextCommit = store.commit(8, 8, hashForByte(8));
    expect(nextCommit.spillTokens.size() == 1 && store.cancelSpill(nextCommit.spillTokens[0]), "K07-03 spill cancellation");
    expect(!store.completeSpill(nextCommit.spillTokens[0], true), "K07-03 canceled completion ignored");
    store.close();
    expect(!store.read(8, 5).has_value(), "K07-03 close clears slots");

    CircularPieceStore zero(root / "zero", CircularStoreMode::Memory, 3, 4);
    expect(zero.capacity() == 0, "K07-03 zero-capacity floor");
    expect(!zero.write(0, bytes(4, 0), {}, {}, 1).success,
           "K07-03 zero-capacity configuration reports full");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        fail("expected root and case id");
    }
    const std::filesystem::path root = argv[1];
    const std::string id = argv[2];
    if (id == "K07-01") {
        caseK0701(root);
    } else if (id == "K07-02") {
        caseK0702(root);
    } else if (id == "K07-03") {
        caseK0703(root);
    } else {
        fail("unknown case id");
    }
    std::cout << id << " PASS\n";
    return 0;
}
