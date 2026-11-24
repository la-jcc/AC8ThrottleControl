#pragma once

#include <array>
#include <cstdint>

namespace ac8throttle {

inline constexpr std::uintptr_t kInputProcessorRva = 0x4d2c040;
inline constexpr std::uintptr_t kPostHookOffset = 0x10;
inline constexpr std::uintptr_t kRawPacketOffset = 0x2268;
inline constexpr std::uintptr_t kInputStateOffset = 0x22a0;
inline constexpr std::uintptr_t kSettingsOffset = 0x2c28;
inline constexpr std::array<unsigned char, 16> kInputProcessorPrefix{{
    0x48, 0x8b, 0xc4, 0x53, 0x57, 0x48, 0x81, 0xec,
    0xf8, 0x00, 0x00, 0x00, 0x0f, 0x29, 0x70, 0xd8,
}};
// All known native and KoreanAIR-AC8-ModPack trampolines converge here after the
// original 16-byte prologue. These four complete instructions do not use
// relative addressing and are replayed by processor_bridge.asm.
inline constexpr std::array<unsigned char, 15> kPostHookPrefix{{
    0x48, 0x8b, 0xfa,
    0x0f, 0x29, 0x78, 0xc8,
    0x48, 0x8b, 0xd9,
    0x44, 0x0f, 0x29, 0x40, 0xb8,
}};

inline constexpr bool player_contract(std::uintptr_t player,
                                      std::uintptr_t state,
                                      std::uintptr_t owner,
                                      std::uintptr_t settings) {
    return player >= 0x10000 &&
           state == player + kInputStateOffset &&
           owner == player + kRawPacketOffset &&
           settings == player + kSettingsOffset;
}

} // namespace ac8throttle
