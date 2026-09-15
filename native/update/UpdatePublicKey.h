#pragma once

#include <array>
#include <cstdint>

namespace Colosseum::Update {

// Production Ed25519 public key, rotated for the manual 1.1.6 trust reset.
// DER SPKI SHA-256: 4cffb5eea31c17ee342dff6b6e2b1412d4e95306986596d4ba5aa5e6549a892f
// Previous 1.1.5 trust root: 7bbb3bc13cfdcd20f1c02e94da103c0be70b2ae09346897a1dc203dc660ca3fa
inline constexpr std::array<std::uint8_t, 32> kUpdatePublicKey = {
    0x19, 0xf7, 0x43, 0xcc, 0xcd, 0x2c, 0xe6, 0xca,
    0xc4, 0x83, 0x37, 0x12, 0x75, 0x7a, 0xe4, 0x1e,
    0xcf, 0xf0, 0x0a, 0x01, 0x47, 0xa5, 0x56, 0x6e,
    0x33, 0x80, 0xf7, 0x30, 0x9a, 0x20, 0x93, 0x49,
};

} // namespace Colosseum::Update
