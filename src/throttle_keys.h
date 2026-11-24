#pragma once

// Adapted from KoreanAIR-AC8-ModPack flight_keys.h under the MIT License.
// This standalone copy only recognizes the six throttle bindings.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ac8throttle::keys {

enum Binding {
    accelerate,
    accelerate_secondary,
    decelerate,
    decelerate_secondary,
    combined,
    combined_secondary,
    binding_count,
};

inline const char* save_name(int binding) {
    static const char* names[binding_count]{
        "ThrottleUpKey", "ThrottleUpKey2", "ThrottleDownKey", "ThrottleDownKey2",
        "ThrottleUpDownKey", "ThrottleUpDownKey2",
    };
    return names[binding];
}

inline const wchar_t* config_name(int binding) {
    static const wchar_t* names[binding_count]{
        L"accelerate", L"accelerate2", L"decelerate", L"decelerate2",
        L"combined", L"combined2",
    };
    return names[binding];
}

inline std::array<int, binding_count> defaults() {
    return {{'W', 0, 'S', 0, 0, 0}};
}

inline bool pollable(int value) {
    return value == 0 || value == 1 || value == 2 || (value >= 4 && value <= 6) ||
           value == 8 || value == 9 || value == 13 || (value >= 16 && value <= 20) ||
           value == 27 || (value >= 32 && value <= 40) || value == 45 || value == 46 ||
           (value >= 48 && value <= 57) || (value >= 65 && value <= 90) ||
           (value >= 96 && value <= 111) || (value >= 112 && value <= 135) ||
           value == 144 || value == 145 || (value >= 160 && value <= 165) ||
           (value >= 186 && value <= 192) || (value >= 219 && value <= 222);
}

inline int virtual_key(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return name[0];
    static const char* digits[]{"Zero", "One", "Two", "Three", "Four",
                                "Five", "Six", "Seven", "Eight", "Nine"};
    for (int i = 0; i < 10; ++i) {
        if (name == digits[i]) return '0' + i;
        if (name == std::string("NumPad") + digits[i]) return 96 + i;
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F') {
        int number = 0;
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') return -1;
            number = number * 10 + name[i] - '0';
        }
        if (number >= 1 && number <= 24) return 111 + number;
    }
    struct Named { const char* name; int code; };
    static const Named named[]{
        {"None", 0}, {"MouseScrollUp", 0}, {"MouseScrollDown", 0}, {"MouseWheelAxis", 0},
        {"LeftMouseButton", 1}, {"RightMouseButton", 2}, {"MiddleMouseButton", 4},
        {"ThumbMouseButton", 5}, {"ThumbMouseButton2", 6}, {"BackSpace", 8}, {"Tab", 9},
        {"Enter", 13}, {"Pause", 19}, {"CapsLock", 20}, {"Escape", 27}, {"SpaceBar", 32},
        {"PageUp", 33}, {"PageDown", 34}, {"End", 35}, {"Home", 36}, {"Left", 37},
        {"Up", 38}, {"Right", 39}, {"Down", 40}, {"Insert", 45}, {"Delete", 46},
        {"Multiply", 106}, {"Add", 107}, {"Subtract", 109}, {"Decimal", 110},
        {"Divide", 111}, {"NumLock", 144}, {"ScrollLock", 145}, {"LeftShift", 160},
        {"RightShift", 161}, {"LeftControl", 162}, {"RightControl", 163}, {"LeftAlt", 164},
        {"RightAlt", 165}, {"Semicolon", 186}, {"Equals", 187}, {"Comma", 188},
        {"Hyphen", 189}, {"Underscore", 189}, {"Period", 190}, {"Slash", 191},
        {"Tilde", 192}, {"LeftBracket", 219}, {"Backslash", 220}, {"RightBracket", 221},
        {"Apostrophe", 222}, {"Quote", 222},
    };
    for (const auto& item : named) if (name == item.name) return item.code;
    return -1;
}

struct SaveBindings {
    std::array<int, binding_count> code{};
    std::array<bool, binding_count> found{};
    std::array<std::string, binding_count> name;
};

inline std::uint32_t read_u32(const unsigned char* data) {
    return std::uint32_t(data[0]) | (std::uint32_t(data[1]) << 8) |
           (std::uint32_t(data[2]) << 16) | (std::uint32_t(data[3]) << 24);
}

inline SaveBindings parse_save(const unsigned char* data, std::size_t size) {
    SaveBindings result;
    if (!data || size < 8 || std::memcmp(data, "GVAS", 4) != 0) return result;
    static constexpr char prefix[] = "EOptionsKeys::";
    constexpr std::size_t prefix_size = sizeof(prefix) - 1;
    for (int binding = 0; binding < binding_count; ++binding) {
        const char* property = save_name(binding);
        const std::size_t property_size = std::strlen(property) + 1;
        for (std::size_t at = 4; at + 4 + property_size <= size; ++at) {
            if (read_u32(data + at - 4) != property_size || data[at + property_size - 1] != 0 ||
                std::memcmp(data + at, property, property_size - 1) != 0) continue;
            const std::size_t end = std::min(size, at + property_size + 192);
            for (std::size_t value = at + property_size + 4;
                 value + prefix_size < end; ++value) {
                if (std::memcmp(data + value, prefix, prefix_size) != 0) continue;
                const std::uint32_t value_size = read_u32(data + value - 4);
                if (value_size <= prefix_size + 1 || value_size > 64 ||
                    value + value_size > size || data[value + value_size - 1] != 0) break;
                const std::string key(reinterpret_cast<const char*>(data + value + prefix_size),
                                      value_size - 1 - prefix_size);
                const int code = virtual_key(key);
                result.name[binding] = key;
                if (code >= 0 && pollable(code)) {
                    result.code[binding] = code;
                    result.found[binding] = true;
                }
                break;
            }
            break;
        }
    }
    return result;
}

inline bool direction_down(const std::array<int, binding_count>& bindings,
                           Binding primary, Binding secondary,
                           bool (*down)(unsigned)) {
    const int first = bindings[primary];
    const int second = bindings[secondary];
    return (first > 0 && down(static_cast<unsigned>(first))) ||
           (second > 0 && down(static_cast<unsigned>(second)));
}

inline void remove_combined_conflicts(std::array<int, binding_count>& bindings) {
    for (const int combo : {combined, combined_secondary}) {
        if (!bindings[combo]) continue;
        for (const int direction : {accelerate, accelerate_secondary,
                                    decelerate, decelerate_secondary}) {
            if (bindings[combo] == bindings[direction]) {
                bindings[combo] = 0;
                break;
            }
        }
    }
}

} // namespace ac8throttle::keys
