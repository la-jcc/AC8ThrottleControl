// AC8ThrottleControl 0.1.0
//
// Standalone percentage/toggle throttle controller. A guarded post-prologue
// hook owns only raw axes 3/4 and remains downstream of KoreanAIR-AC8-ModPack's entry
// detour without loading or querying another mod.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "control_state.h"
#include "processor_contract.h"
#include "raw_axis_control.h"
#include "status_channel.h"
#include "third_party/koreanair/absolute_hook.h"
#include "throttle_keys.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

struct lua_State;

extern "C" void ac8_throttle_mid_hook();
extern "C" void* ac8_throttle_mid_continue = nullptr;

namespace {

constexpr DWORD kVerifiedTimeDateStamp = 0x6ad2d0a7;
constexpr DWORD kVerifiedSizeOfImage = 0x215a2000;
constexpr std::size_t kMaximumSaveBytes = 16 * 1024 * 1024;

struct Config {
    bool enabled{true};
    bool require_offline_launch{true};
    ac8throttle::ControlSettings control;
    UINT neutral_key{};
    UINT airbrake_key{};
    bool keys_follow_game{true};
    std::array<int, ac8throttle::keys::binding_count> fallback_keys{
        ac8throttle::keys::defaults()};
};

struct SaveIdentity {
    bool available{};
    std::uint64_t size{};
    FILETIME write{};
};

HMODULE self_module{};
Config config;
std::wstring mod_root;
std::wstring config_path;
std::wstring log_path;
std::wstring player_path;
std::wstring status_path;
std::wstring game_save_path;
SRWLOCK log_lock = SRWLOCK_INIT;
std::atomic<bool> started{false};
std::atomic<std::uintptr_t> player_pawn{0};
std::atomic<std::uint64_t> desired_output{0};
std::atomic<std::uint64_t> processor_calls{0};
std::atomic<std::uint64_t> player_calls{0};
std::atomic<std::uint64_t> injected_calls{0};
std::atomic<std::uint64_t> manual_yield_tick{0};
// Current throttle bindings (one virtual-key byte per keys::Binding), published
// by the worker for the game-thread hook's live-key check.
std::atomic<std::uint64_t> live_bindings{0};
std::atomic<bool> contract_fault{false};
void* processor_trampoline{};
void* published_continuation{};
bool hook_ready{};
bool hook_unavailable{};

HANDLE status_file{INVALID_HANDLE_VALUE};
HANDLE status_mapping{};
ac8throttle::StatusRecord* status_records{};
std::uint32_t status_sequence{};

bool parent_directory(std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    path.resize(slash);
    return true;
}

const wchar_t* base_name(const wchar_t* path) {
    const wchar_t* result = path;
    for (const wchar_t* cursor = path; cursor && *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') result = cursor + 1;
    }
    return result;
}

bool initialize_paths() {
    wchar_t path[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(self_module, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) return false;
    mod_root.assign(path, length);
    if (!parent_directory(mod_root) || !parent_directory(mod_root)) return false;
    config_path = mod_root + L"\\config.ini";
    log_path = mod_root + L"\\AC8ThrottleControl.log";
    player_path = mod_root + L"\\player.ptr";
    status_path = mod_root + L"\\throttle.status";

    wchar_t custom[MAX_PATH * 4]{};
    GetPrivateProfileStringW(L"keys", L"save_path", L"", custom,
                             static_cast<DWORD>(std::size(custom)), config_path.c_str());
    if (*custom) {
        game_save_path = custom;
    } else {
        wchar_t local[MAX_PATH * 4]{};
        const DWORD got = GetEnvironmentVariableW(L"LOCALAPPDATA", local,
                                                   static_cast<DWORD>(std::size(local)));
        if (got && got < std::size(local)) {
            game_save_path = std::wstring(local, got) +
                L"\\BANDAI NAMCO Entertainment\\ACE COMBAT 8\\Saved\\SaveGames\\System.sav";
        }
    }
    return true;
}

void reset_file(const std::wstring& path, const char* initial = nullptr) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    if (initial) {
        DWORD written{};
        WriteFile(file, initial, static_cast<DWORD>(std::strlen(initial)), &written, nullptr);
    }
    CloseHandle(file);
}

void log_line(const char* format, ...) {
    if (log_path.empty()) return;
    char message[1024]{};
    va_list args;
    va_start(args, format);
    _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char line[1280]{};
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds, message);
    AcquireSRWLockExclusive(&log_lock);
    HANDLE file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written{};
        WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&log_lock);
}

bool read_bool(const wchar_t* section, const wchar_t* key, bool fallback) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(section, key, fallback ? L"true" : L"false", value,
                             static_cast<DWORD>(std::size(value)), config_path.c_str());
    if (_wcsicmp(value, L"true") == 0 || _wcsicmp(value, L"yes") == 0 ||
        _wcsicmp(value, L"on") == 0 || wcscmp(value, L"1") == 0) return true;
    if (_wcsicmp(value, L"false") == 0 || _wcsicmp(value, L"no") == 0 ||
        _wcsicmp(value, L"off") == 0 || wcscmp(value, L"0") == 0) return false;
    return fallback;
}

std::wstring read_text(const wchar_t* section, const wchar_t* key,
                       const wchar_t* fallback) {
    wchar_t value[256]{};
    GetPrivateProfileStringW(section, key, fallback, value,
                             static_cast<DWORD>(std::size(value)), config_path.c_str());
    return value;
}

float read_float(const wchar_t* section, const wchar_t* key, float fallback,
                 float minimum, float maximum) {
    const auto text = read_text(section, key, L"");
    if (text.empty()) return fallback;
    wchar_t* end{};
    const double value = wcstod(text.c_str(), &end);
    if (end == text.c_str() || (end && *end) || !std::isfinite(value)) return fallback;
    return std::clamp(static_cast<float>(value), minimum, maximum);
}

int parse_key_name(const std::wstring& text, int fallback) {
    if (text.empty()) return fallback;
    std::string ascii;
    ascii.reserve(text.size());
    for (const wchar_t value : text) {
        if (value < 0 || value > 127) return fallback;
        ascii.push_back(static_cast<char>(value));
    }
    if (ascii.size() == 1) ascii[0] = static_cast<char>(toupper(ascii[0]));
    const int code = ac8throttle::keys::virtual_key(ascii);
    return code >= 0 && ac8throttle::keys::pollable(code) ? code : fallback;
}

void load_config() {
    config.enabled = read_bool(L"general", L"enabled", true);
    config.require_offline_launch = read_bool(L"general", L"require_offline_launch", true);
    // Only percent and toggle remain; any other value selects percent.
    config.control.mode = _wcsicmp(read_text(L"control", L"mode", L"percent").c_str(), L"toggle") == 0
        ? ac8throttle::ControlMode::toggle : ac8throttle::ControlMode::percent;
    const auto both = read_text(L"control", L"both_keys", L"high_g_only");
    config.control.both_keys = _wcsicmp(both.c_str(), L"neutral") == 0
        ? ac8throttle::BothKeysMode::neutral
        : ac8throttle::BothKeysMode::high_g_only;
    config.control.sensitivity_percent_per_second =
        read_float(L"control", L"sensitivity", 50.0f, 1.0f, 200.0f);
    config.control.zero_snap_percent =
        read_float(L"control", L"zero_snap", 2.0f, 0.0f, 25.0f);
    config.control.brake_floor_percent =
        read_float(L"control", L"brake_floor", -100.0f, -100.0f, 0.0f);
    config.control.toggle_accelerate = read_bool(L"control", L"toggle_accelerate", true);
    config.control.toggle_decelerate = read_bool(L"control", L"toggle_decelerate", true);
    config.control.toggle_hold_to_neutral =
        _wcsicmp(read_text(L"control", L"toggle_after_hold", L"resume").c_str(), L"neutral") == 0;
    config.control.toggle_accelerate_keep =
        _wcsicmp(read_text(L"control", L"toggle_accelerate_repress", L"neutral").c_str(), L"keep") == 0;
    config.neutral_key = static_cast<UINT>(parse_key_name(
        read_text(L"control", L"neutral_key", L"None"), 0));
    config.airbrake_key = static_cast<UINT>(parse_key_name(
        read_text(L"control", L"airbrake_key", L"None"), 0));
    config.control.airbrake_toggle =
        _wcsicmp(read_text(L"control", L"airbrake_mode", L"hold").c_str(), L"toggle") == 0;
    config.keys_follow_game =
        _wcsicmp(read_text(L"keys", L"source", L"game").c_str(), L"custom") != 0;
    config.fallback_keys = ac8throttle::keys::defaults();
    for (int binding = 0; binding < ac8throttle::keys::binding_count; ++binding) {
        config.fallback_keys[binding] = parse_key_name(
            read_text(L"keys", ac8throttle::keys::config_name(binding),
                      binding == ac8throttle::keys::accelerate ? L"W" :
                      binding == ac8throttle::keys::decelerate ? L"S" : L"None"),
            config.fallback_keys[binding]);
    }
    ac8throttle::keys::remove_combined_conflicts(config.fallback_keys);
}

bool command_line_has_token(const wchar_t* token) {
    const wchar_t* command = GetCommandLineW();
    if (!command || !token || !*token) return false;
    const std::size_t token_length = wcslen(token);
    for (const wchar_t* cursor = command; *cursor; ++cursor) {
        if (_wcsnicmp(cursor, token, token_length) != 0) continue;
        const wchar_t before = cursor == command ? 0 : cursor[-1];
        const wchar_t after = cursor[token_length];
        if ((!before || iswspace(before) || before == L'"') &&
            (!after || iswspace(after) || after == L'"')) return true;
    }
    return false;
}

bool offline_launch() {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"EOS_USE_ANTICHEATCLIENTNULL", value,
                                                  static_cast<DWORD>(std::size(value)));
    return (length > 0 && length < std::size(value) && wcscmp(value, L"1") == 0) ||
           command_line_has_token(L"-eac-nop-loaded");
}

bool verified_game_build() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
               nt->FileHeader.TimeDateStamp == kVerifiedTimeDateStamp &&
               nt->OptionalHeader.SizeOfImage == kVerifiedSizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool foreground_is_game() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD process{};
    GetWindowThreadProcessId(foreground, &process);
    return process == GetCurrentProcessId();
}

bool key_down(unsigned key) {
    return key != 0 && (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
}

std::uint64_t pack_bindings(const std::array<int, ac8throttle::keys::binding_count>& bindings) {
    std::uint64_t packed{};
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const int code = bindings[i];
        if (code > 0 && code < 256) packed |= static_cast<std::uint64_t>(code) << (8 * i);
    }
    return packed;
}

// Game thread, only when an axis is not explained by the worker's last sample.
std::uint8_t live_axis_keys() {
    const auto packed = live_bindings.load(std::memory_order_relaxed);
    const auto held = [packed](int binding) {
        return key_down(static_cast<unsigned>((packed >> (8 * binding)) & 0xffu));
    };
    using namespace ac8throttle::keys;
    const bool combined_held = held(combined) || held(combined_secondary);
    std::uint8_t live{};
    if (combined_held || held(accelerate) || held(accelerate_secondary)) live |= ac8throttle::kLiveThrottleKey;
    if (combined_held || held(decelerate) || held(decelerate_secondary)) live |= ac8throttle::kLiveBrakeKey;
    return live;
}

bool read_player_bridge(std::uintptr_t& value) {
    HANDLE file = CreateFileW(player_path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char text[64]{};
    DWORD read{};
    const bool ok = ReadFile(file, text, sizeof(text) - 1, &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok || !read) return false;
    char* end{};
    const auto parsed = _strtoui64(text, &end, 16);
    if (end == text) return false;
    if (parsed != 0 && (parsed < 0x10000 || parsed > 0x00007fffffff0000ull || (parsed & 7))) return false;
    value = static_cast<std::uintptr_t>(parsed);
    return true;
}

bool initialize_status_channel() {
    status_file = CreateFileW(status_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (status_file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    length.QuadPart = sizeof(ac8throttle::StatusRecord) * 2;
    if (!SetFilePointerEx(status_file, length, nullptr, FILE_BEGIN) || !SetEndOfFile(status_file)) return false;
    status_mapping = CreateFileMappingW(status_file, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(length.QuadPart), nullptr);
    if (!status_mapping) return false;
    status_records = static_cast<ac8throttle::StatusRecord*>(
        MapViewOfFile(status_mapping, FILE_MAP_ALL_ACCESS, 0, 0, length.QuadPart));
    if (!status_records) return false;
    std::memset(status_records, 0, static_cast<std::size_t>(length.QuadPart));
    return true;
}

void publish_status(bool active, ac8throttle::ControlMode mode, float percent,
                    bool brake_hold, bool high_g_yield, bool airbrake = false) {
    if (!status_records) return;
    ac8throttle::StatusRecord record{};
    record.magic = ac8throttle::kStatusMagic;
    record.sequence = ++status_sequence;
    record.active = active ? 1u : 0u;
    record.mode = static_cast<std::uint32_t>(mode);
    record.percent_milli = ac8throttle::encode_status_percent(percent);
    const bool manual = GetTickCount64() - manual_yield_tick.load(std::memory_order_relaxed) < 250;
    record.flags = (brake_hold ? 1u : 0u) | (high_g_yield ? 2u : 0u) |
                   (manual ? 4u : 0u) | (hook_ready ? 8u : 0u) | (airbrake ? 16u : 0u);
    record.checksum = ac8throttle::status_checksum(record);
    auto* slot = &status_records[record.sequence & 1u];
    slot->checksum = 0;
    MemoryBarrier();
    slot->magic = record.magic;
    slot->sequence = record.sequence;
    slot->active = record.active;
    slot->mode = record.mode;
    slot->percent_milli = record.percent_milli;
    slot->flags = record.flags;
    slot->reserved = 0;
    MemoryBarrier();
    slot->checksum = record.checksum;
}

SaveIdentity save_identity() {
    SaveIdentity result;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (game_save_path.empty() ||
        !GetFileAttributesExW(game_save_path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return result;
    result.available = true;
    result.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    result.write = data.ftLastWriteTime;
    return result;
}

bool same_identity(const SaveIdentity& left, const SaveIdentity& right) {
    return left.available == right.available && left.size == right.size &&
           left.write.dwLowDateTime == right.write.dwLowDateTime &&
           left.write.dwHighDateTime == right.write.dwHighDateTime;
}

bool read_save(std::vector<unsigned char>& bytes) {
    HANDLE file = CreateFileW(game_save_path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
              size.QuadPart <= static_cast<LONGLONG>(kMaximumSaveBytes);
    if (ok) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD got{};
            const DWORD wanted = static_cast<DWORD>(
                std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
            if (!ReadFile(file, bytes.data() + offset, wanted, &got, nullptr) || !got) {
                ok = false; break;
            }
            offset += got;
        }
    }
    CloseHandle(file);
    if (!ok) bytes.clear();
    return ok;
}

bool load_game_bindings(std::array<int, ac8throttle::keys::binding_count>& bindings) {
    bindings = config.fallback_keys;
    if (!config.keys_follow_game) return true;
    std::vector<unsigned char> bytes;
    if (!read_save(bytes)) return false;
    const auto parsed = ac8throttle::keys::parse_save(bytes.data(), bytes.size());
    bool direction_found = false;
    for (int binding = 0; binding < ac8throttle::keys::binding_count; ++binding) {
        if (!parsed.found[binding]) continue;
        bindings[binding] = parsed.code[binding];
        if (binding == ac8throttle::keys::accelerate || binding == ac8throttle::keys::decelerate) {
            direction_found = true;
        }
    }
    ac8throttle::keys::remove_combined_conflicts(bindings);
    return direction_found;
}

bool install_processor_hook() {
    if (hook_ready) return true;
    if (hook_unavailable) return false;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* target = base ? base + ac8throttle::kInputProcessorRva + ac8throttle::kPostHookOffset : nullptr;
    bool prefix_matches = false;
    __try {
        prefix_matches = target &&
            std::memcmp(target, ac8throttle::kPostHookPrefix.data(),
                        ac8throttle::kPostHookPrefix.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { prefix_matches = false; }
    if (!prefix_matches) {
        hook_unavailable = true;
        log_line("hook refused: post-prologue target is patched or build bytes mismatched");
        return false;
    }
    constexpr std::size_t copied = ac8throttle::kPostHookPrefix.size();
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, copied + 14, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!trampoline) {
        hook_unavailable = true;
        log_line("hook refused: post-prologue trampoline allocation failed (win32=%lu)", GetLastError());
        return false;
    }
    std::memcpy(trampoline, ac8throttle::kPostHookPrefix.data(), copied);
    ac8hook::absolute_jump(trampoline + copied, target + copied);
    DWORD old{};
    if (!VirtualProtect(trampoline, copied + 14, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), trampoline, copied + 14)) {
        const DWORD error = GetLastError();
        VirtualFree(trampoline, 0, MEM_RELEASE);
        hook_unavailable = true;
        log_line("hook refused: post-prologue trampoline protection failed (win32=%lu)", error);
        return false;
    }
    ac8_throttle_mid_continue = target + copied;
    constexpr ac8hook::Boundary boundaries[]{{0, 0}, {3, 3}, {7, 7}, {10, 10}};
    bool reachable = false;
    const bool installed = ac8hook::install(
        target, reinterpret_cast<void*>(&ac8_throttle_mid_hook), trampoline,
        ac8throttle::kPostHookPrefix.data(), static_cast<unsigned>(copied),
        boundaries, static_cast<unsigned>(std::size(boundaries)),
        &published_continuation, reachable);
    if (!installed) {
        const DWORD error = GetLastError();
        if (!reachable) VirtualFree(trampoline, 0, MEM_RELEASE);
        else processor_trampoline = trampoline;
        hook_unavailable = true;
        log_line("hook refused: safe post-prologue install failed (win32=%lu reachable=%d)",
                 error, reachable ? 1 : 0);
        return false;
    }
    processor_trampoline = trampoline;
    hook_ready = true;
    log_line("post-prologue throttle hook ready at RVA %llX; axes 3/4 only",
             static_cast<unsigned long long>(ac8throttle::kInputProcessorRva +
                                             ac8throttle::kPostHookOffset));
    return true;
}

thread_local ac8throttle::RawAxisOwnership raw_ownership;

const char* mode_name(ac8throttle::ControlMode mode) {
    return mode == ac8throttle::ControlMode::toggle ? "toggle" : "percent";
}

DWORD WINAPI worker_main(void*) {
    ac8throttle::ControlState controller;
    controller.reset(config.control);
    std::array<int, ac8throttle::keys::binding_count> bindings = config.fallback_keys;
    SaveIdentity last_save{};
    bool save_checked{};
    bool previous_accelerate{};
    bool previous_decelerate{};
    bool previous_neutral{};
    bool previous_airbrake{};
    std::uintptr_t active_player{};
    ULONGLONG previous_tick = GetTickCount64();
    ULONGLONG next_player_poll{};
    ULONGLONG next_save_poll{};
    ULONGLONG last_contract_log{};
    ac8throttle::ReleaseGrace release_grace;
    log_line("ready: automatic control; mode=%s sensitivity=%.1f%%/s zeroSnap=%.1f%% brakeFloor=%.1f%% both=%s neutralVK=%02X airbrakeVK=%02X/%s",
             mode_name(config.control.mode), config.control.sensitivity_percent_per_second,
             config.control.zero_snap_percent, ac8throttle::brake_floor(config.control),
             config.control.both_keys == ac8throttle::BothKeysMode::neutral ? "neutral" : "high_g_only",
             config.neutral_key, config.airbrake_key,
             config.control.airbrake_toggle ? "toggle" : "hold");

    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= next_player_poll) {
            std::uintptr_t incoming{};
            if (read_player_bridge(incoming)) {
                const auto previous = player_pawn.exchange(incoming, std::memory_order_acq_rel);
                if (incoming != previous) log_line("player bridge: %s", incoming ? "single-player aircraft acquired" : "released");
            }
            next_player_poll = now + 100;
        }
        if (now >= next_save_poll) {
            const auto identity = save_identity();
            if (!save_checked || !same_identity(identity, last_save)) {
                last_save = identity;
                save_checked = true;
                const bool loaded = load_game_bindings(bindings);
                log_line("throttle keys: %s up=%02X/%02X down=%02X/%02X combined=%02X/%02X",
                         config.keys_follow_game && loaded ? "game System.sav" : "config fallback",
                         bindings[ac8throttle::keys::accelerate], bindings[ac8throttle::keys::accelerate_secondary],
                         bindings[ac8throttle::keys::decelerate], bindings[ac8throttle::keys::decelerate_secondary],
                         bindings[ac8throttle::keys::combined], bindings[ac8throttle::keys::combined_secondary]);
                live_bindings.store(pack_bindings(bindings), std::memory_order_relaxed);
            }
            next_save_poll = now + 1000;
        }

        const bool accelerate = ac8throttle::keys::direction_down(
            bindings, ac8throttle::keys::accelerate,
            ac8throttle::keys::accelerate_secondary, &key_down);
        const bool decelerate = ac8throttle::keys::direction_down(
            bindings, ac8throttle::keys::decelerate,
            ac8throttle::keys::decelerate_secondary, &key_down);
        const bool combined = ac8throttle::keys::direction_down(
            bindings, ac8throttle::keys::combined,
            ac8throttle::keys::combined_secondary, &key_down);
        const bool neutral = key_down(config.neutral_key);
        const bool airbrake = key_down(config.airbrake_key);
        const auto expected = release_grace.update(now, accelerate || combined, decelerate || combined);
        const auto player = player_pawn.load(std::memory_order_acquire);
        const bool foreground = foreground_is_game();

        if (!player || !foreground || !hook_ready) {
            desired_output.store(0, std::memory_order_release);
            if (active_player) controller.reset(config.control);
            active_player = 0;
            publish_status(false, config.control.mode, controller.percent(), false, false);
        } else {
            if (active_player != player) {
                controller.reset(config.control);
                active_player = player;
                previous_tick = now;
            }
            const float elapsed = static_cast<float>(std::min<ULONGLONG>(now - previous_tick, 100)) / 1000.0f;
            const auto command = controller.update(config.control, {
                accelerate, decelerate, combined,
                neutral && !previous_neutral,
                accelerate && !previous_accelerate,
                decelerate && !previous_decelerate,
                elapsed,
                airbrake,
                airbrake && !previous_airbrake,
            });
            desired_output.store(ac8throttle::pack_raw_command({
                command.write, expected.throttle, expected.brake,
                command.throttle, command.brake,
            }), std::memory_order_release);
            publish_status(true, config.control.mode, command.percent,
                           command.brake_hold, command.high_g_yield, command.airbrake);
        }
        if (contract_fault.exchange(false, std::memory_order_relaxed) && now - last_contract_log > 1000) {
            last_contract_log = now;
            log_line("player input contract mismatch: output yielded for the affected callback");
        }
        previous_accelerate = accelerate;
        previous_decelerate = decelerate;
        previous_neutral = neutral;
        previous_airbrake = airbrake;
        previous_tick = now;
        Sleep(10);
    }
}

bool is_ace_combat_process() {
    wchar_t path[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return length > 0 && length < std::size(path) &&
           _wcsicmp(base_name(path), L"AceCombat8.exe") == 0;
}

} // namespace

extern "C" void __fastcall ac8_throttle_apply(unsigned char* state, unsigned char* context) {
    ++processor_calls;
    const auto player = player_pawn.load(std::memory_order_acquire);
    if (!player || reinterpret_cast<std::uintptr_t>(state) != player + ac8throttle::kInputStateOffset) return;
    ++player_calls;
    __try {
        const auto owner = *reinterpret_cast<const std::uintptr_t*>(context);
        const auto settings = *reinterpret_cast<const std::uintptr_t*>(context + 8);
        if (!ac8throttle::player_contract(player, reinterpret_cast<std::uintptr_t>(state), owner, settings)) {
            contract_fault.store(true, std::memory_order_relaxed);
            return;
        }
        const auto result = ac8throttle::apply_raw_command(
            owner, reinterpret_cast<float*>(owner),
            desired_output.load(std::memory_order_acquire), raw_ownership, &live_axis_keys);
        if (result.wrote) ++injected_calls;
        if (result.manual_yield) manual_yield_tick.store(GetTickCount64(), std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        contract_fault.store(true, std::memory_order_relaxed);
    }
}

extern "C" __declspec(dllexport) int ac8_throttle_start(lua_State*) {
    if (started.exchange(true)) return 0;
    if (!initialize_paths()) return 0;
    reset_file(log_path);
    reset_file(player_path, "0\n");
    log_line("AC8ThrottleControl 0.1.0 starting (standalone post-prologue controller)");
    if (!is_ace_combat_process()) { log_line("disabled: host executable is not AceCombat8.exe"); return 0; }
    load_config();
    if (!config.enabled) { log_line("disabled by config.ini"); return 0; }
    if (config.require_offline_launch && !offline_launch()) {
        log_line("disabled: no explicit offline launch marker (-eac-nop-loaded or EOS_USE_ANTICHEATCLIENTNULL=1)");
        return 0;
    }
    if (!verified_game_build()) {
        log_line("disabled: AceCombat8.exe build guard mismatch (expected timestamp=%08lX image=%08lX)",
                 kVerifiedTimeDateStamp, kVerifiedSizeOfImage);
        return 0;
    }
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&ac8_throttle_start), &pinned)) {
        log_line("disabled: native DLL could not be pinned (win32=%lu)", GetLastError());
        return 0;
    }
    if (!initialize_status_channel()) {
        log_line("disabled: HUD status channel unavailable (win32=%lu)", GetLastError());
        return 0;
    }
    if (!install_processor_hook()) {
        publish_status(false, config.control.mode,
                       0.0f, false, false);
        return 0;
    }
    HANDLE worker = CreateThread(nullptr, 0, &worker_main, nullptr, 0, nullptr);
    if (!worker) { log_line("disabled: worker thread creation failed (win32=%lu)", GetLastError()); return 0; }
    CloseHandle(worker);
    log_line("configuration: mode=%u toggle_up=%d toggle_down=%d after_hold=%s accelerate_repress=%s key_source=%s",
             static_cast<unsigned>(config.control.mode), config.control.toggle_accelerate ? 1 : 0,
             config.control.toggle_decelerate ? 1 : 0,
             config.control.toggle_hold_to_neutral ? "neutral" : "resume",
             config.control.toggle_accelerate_keep ? "keep" : "neutral",
             config.keys_follow_game ? "game" : "custom");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
