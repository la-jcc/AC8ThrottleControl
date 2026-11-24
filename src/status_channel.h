#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ac8throttle {

inline constexpr std::uint32_t kStatusMagic = 0x31544341; // "ACT1"
inline constexpr std::uint32_t kStatusSalt = 0xa58c3f19;

struct StatusRecord {
    std::uint32_t magic{};
    std::uint32_t sequence{};
    std::uint32_t active{};
    std::uint32_t mode{};
    std::int32_t percent_milli{};
    std::uint32_t flags{};
    std::uint32_t checksum{};
    std::uint32_t reserved{};
};

static_assert(sizeof(StatusRecord) == 32);

inline std::int32_t encode_status_percent(float percent) {
    if (!std::isfinite(percent)) return 0;
    return static_cast<std::int32_t>(
        std::lround(std::clamp(percent, -100.0f, 100.0f) * 1000.0f));
}

inline std::uint32_t status_checksum(const StatusRecord& record) {
    return kStatusSalt ^ record.magic ^ record.sequence ^ record.active ^ record.mode ^
           static_cast<std::uint32_t>(record.percent_milli) ^ record.flags;
}

inline bool valid_status(const StatusRecord& record) {
    return record.magic == kStatusMagic && record.checksum == status_checksum(record);
}

} // namespace ac8throttle
