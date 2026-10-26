#include "server1/policy/PieceBuffer.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using server1::policy::ByteBuffer;
using server1::policy::PieceBuffer;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

ByteBuffer bytes(std::size_t count, std::uint8_t value)
{
    return ByteBuffer(count, value);
}

void caseK02_01()
{
    PieceBuffer piece(16385, 7);
    require(piece.parts() == 2, "16385 bytes produce two blocks");
    require(piece.size(0) == 16384 && piece.size(1) == 1,
            "the final block retains the one-byte remainder");
    require(piece.offset(0) == 0 && piece.offset(1) == 16384,
            "block offsets advance by exactly 16 KiB");
    require(piece.reserve() == 0 && piece.reserve() == 1,
            "reservations are issued monotonically");
    require(piece.reserve() == PieceBuffer::kNoReservation,
            "an exhausted piece returns the source sentinel");
    std::cout << "K02-01 PASS\n";
}

void caseK02_02()
{
    PieceBuffer piece(3 * PieceBuffer::kBlockSize, 11);
    require(!piece.cancel(0), "cancel-before-reserve is rejected");
    require(piece.reserve() == 0 && piece.reserve() == 1 && piece.reserve() == 2,
            "initial reservations are monotonic");
    require(piece.cancel(0) && piece.cancel(1), "live reservations can be cancelled");
    require(!piece.cancel(0) && !piece.cancel(1),
            "an already-cancelled reservation cannot be duplicated");
    require(piece.reserve() == 1 && piece.reserve() == 0,
            "cancelled reservations are reused in LIFO order");

    require(!piece.set(11, 0, bytes(PieceBuffer::kBlockSize, 0x41)),
            "one delivered block is not complete");
    const auto buffered = piece.buffered();
    const auto missing = piece.missing();
    require(!piece.set(11, 0, bytes(PieceBuffer::kBlockSize, 0x42)),
            "duplicate delivery is not complete");
    require(piece.buffered() == buffered && piece.missing() == missing,
            "duplicate delivery does not advance counters or replace bytes");
    require(piece.get(0).value() == bytes(PieceBuffer::kBlockSize, 0x41),
            "the first delivered block wins");
    require(!piece.cancel(0), "a delivered block cannot return to the reservation pool");
    std::cout << "K02-02 PASS\n";
}

void caseK02_03()
{
    PieceBuffer piece(16385, 19);
    require(!piece.flush().has_value(), "flush before completion returns no buffer");
    require(piece.reserve() == 0 && piece.reserve() == 1,
            "both blocks can be reserved");
    require(!piece.set(19, 0, bytes(16384, 0x61)), "first block remains partial");
    require(piece.set(19, 1, bytes(1, 0x62)), "last block completes the piece");
    const auto flushed = piece.flush();
    require(flushed.has_value() && flushed->size() == 16385,
            "complete flush returns the exact piece length");
    require((*flushed)[0] == 0x61 && (*flushed)[16384] == 0x62,
            "flush preserves block order and tail byte");
    require(piece.flushed(), "flush is terminal");
    require(piece.reserve() == PieceBuffer::kNoReservation,
            "reserve after flush returns the source sentinel");
    require(!piece.set(19, 0, bytes(16384, 0x63)),
            "set after flush cannot mutate terminal state");

    PieceBuffer replacement(16384, 20);
    replacement.reserve();
    require(!replacement.set(19, 0, bytes(16384, 0x7f)),
            "a completion from the replaced generation is rejected");
    require(replacement.buffered() == 0 && replacement.missing() == 16384,
            "stale completion does not mutate replacement counters");
    require(replacement.set(20, 0, bytes(16384, 0x7e)),
            "the current generation completion is accepted");
    std::cout << "K02-03 PASS\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const std::string requested = argc > 1 ? argv[1] : "all";
        if (requested == "all" || requested == "K02-01")
            caseK02_01();
        if (requested == "all" || requested == "K02-02")
            caseK02_02();
        if (requested == "all" || requested == "K02-03")
            caseK02_03();
        if (requested != "all" && requested != "K02-01" && requested != "K02-02"
            && requested != "K02-03")
            throw std::runtime_error("unknown K02 case");
    } catch (const std::exception &error) {
        std::cerr << "K02 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
