#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ac8throttle {

inline constexpr std::size_t kThrottleAxis = 3;
inline constexpr std::size_t kBrakeAxis = 4;
inline constexpr std::size_t kHighGAxis = 5;

inline bool same_float_bits(float left, float right) {
    std::uint32_t a{};
    std::uint32_t b{};
    std::memcpy(&a, &left, sizeof(a));
    std::memcpy(&b, &right, sizeof(b));
    return a == b;
}

struct RawAxisOwnership {
    std::uintptr_t owner{};
    float native[2]{};
    float sent[2]{};
    std::uint8_t owned{};

    void reset() { *this = {}; }

    void begin(std::uintptr_t current_owner, float* axes) {
        if (owner != current_owner) {
            owner = current_owner;
            owned = 0;
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const auto axis = kThrottleAxis + i;
            if ((owned & (1u << i)) && same_float_bits(axes[axis], sent[i])) {
                axes[axis] = native[i];
            }
            native[i] = axes[axis];
        }
        owned = 0;
    }

    void wrote(std::size_t slot, float value) {
        sent[slot] = value;
        owned = static_cast<std::uint8_t>(owned | (1u << slot));
    }
};

struct RawCommand {
    bool active{};
    bool expected_native_throttle{};
    bool expected_native_brake{};
    float throttle{};
    float brake{};
};

// The game's throttle/brake axes follow a key release a frame or two after the
// OS key state does. A bound key released within this window still explains
// its axis, so the stored command keeps being written instead of yielding the
// lingering full-scale native value as "manual input".
inline constexpr std::uint64_t kReleaseGraceMs = 80;

struct ExpectedNative {
    bool throttle{};
    bool brake{};
};

struct ReleaseGrace {
    std::uint64_t throttle_until{};
    std::uint64_t brake_until{};

    ExpectedNative update(std::uint64_t now_ms, bool throttle_key, bool brake_key) {
        if (throttle_key) throttle_until = now_ms + kReleaseGraceMs;
        if (brake_key) brake_until = now_ms + kReleaseGraceMs;
        return {throttle_key || now_ms < throttle_until, brake_key || now_ms < brake_until};
    }
};

struct RawApplyResult {
    bool wrote{};
    bool manual_yield{};
};

inline std::uint64_t pack_raw_command(const RawCommand& command) {
    const auto throttle = static_cast<std::uint64_t>(
        std::lround(std::clamp(command.throttle, 0.0f, 1.0f) * 65535.0f));
    const auto brake = static_cast<std::uint64_t>(
        std::lround(std::clamp(command.brake, 0.0f, 1.0f) * 65535.0f));
    return throttle | (brake << 16) |
           (static_cast<std::uint64_t>(command.active) << 32) |
           (static_cast<std::uint64_t>(command.expected_native_throttle) << 33) |
           (static_cast<std::uint64_t>(command.expected_native_brake) << 34);
}

inline RawCommand unpack_raw_command(std::uint64_t packed) {
    return {
        ((packed >> 32) & 1u) != 0,
        ((packed >> 33) & 1u) != 0,
        ((packed >> 34) & 1u) != 0,
        static_cast<float>(packed & 0xffffu) / 65535.0f,
        static_cast<float>((packed >> 16) & 0xffffu) / 65535.0f,
    };
}

// Bits returned by a live-key probe: a bound throttle-direction key is held now.
inline constexpr std::uint8_t kLiveThrottleKey = 1;
inline constexpr std::uint8_t kLiveBrakeKey = 2;

struct NoLiveKeys {
    std::uint8_t operator()() const { return 0; }
};

// live_keys is asked only when a non-zero native axis is not explained by the
// worker's last key sample. The worker samples keys every 10 ms, so the game
// callback can see a freshly pressed bound key first; a held bound key explains
// its own axis, so the stored command is written instead of yielding one
// full-scale callback to native input. Unbound analog input still yields.
template <class LiveKeys = NoLiveKeys>
inline RawApplyResult apply_raw_command(std::uintptr_t owner, float* axes,
                                        std::uint64_t packed,
                                        RawAxisOwnership& ownership,
                                        LiveKeys live_keys = {},
                                        float manual_epsilon = 0.0001f) {
    ownership.begin(owner, axes);
    const auto command = unpack_raw_command(packed);
    if (!command.active) return {};

    const float native_throttle = axes[kThrottleAxis];
    const float native_brake = axes[kBrakeAxis];
    const bool invalid = !std::isfinite(native_throttle) || !std::isfinite(native_brake);
    bool unexpected_throttle = std::abs(native_throttle) > manual_epsilon &&
                               !command.expected_native_throttle;
    bool unexpected_brake = std::abs(native_brake) > manual_epsilon &&
                            !command.expected_native_brake;
    if (!invalid && (unexpected_throttle || unexpected_brake)) {
        const std::uint8_t live = live_keys();
        if (live & kLiveThrottleKey) unexpected_throttle = false;
        if (live & kLiveBrakeKey) unexpected_brake = false;
    }
    if (invalid || unexpected_throttle || unexpected_brake) return {false, true};

    axes[kThrottleAxis] = command.throttle;
    axes[kBrakeAxis] = command.brake;
    ownership.wrote(0, command.throttle);
    ownership.wrote(1, command.brake);
    return {true, false};
}

} // namespace ac8throttle
