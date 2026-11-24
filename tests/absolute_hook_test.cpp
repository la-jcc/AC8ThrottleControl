#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "processor_contract.h"
#include "third_party/koreanair/absolute_hook.h"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

using Function = int(*)();
Function original{};

int detour() {
    return original() + 1;
}

} // namespace

int main() {
    using namespace ac8throttle;
    constexpr unsigned char tail[]{
        0x48, 0x81, 0xc4, 0xf8, 0x00, 0x00, 0x00, // add rsp, 0xf8
        0x5f, 0x5b,                               // pop rdi; pop rbx
        0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3,       // return 42
    };
    constexpr std::size_t copied = kInputProcessorPrefix.size();
    constexpr std::size_t trampoline_size = copied + 14;

    auto* code = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(code);
    std::memcpy(code, kInputProcessorPrefix.data(), copied);
    std::memcpy(code + copied, tail, sizeof(tail));
    DWORD old{};
    assert(VirtualProtect(code, 64, PAGE_EXECUTE_READ, &old));
    assert(reinterpret_cast<Function>(code)() == 42);

    auto* trampoline = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, trampoline_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(trampoline);
    std::memcpy(trampoline, kInputProcessorPrefix.data(), copied);
    ac8hook::absolute_jump(trampoline + copied, code + copied);
    assert(VirtualProtect(trampoline, trampoline_size, PAGE_EXECUTE_READ, &old));

    constexpr ac8hook::Boundary boundaries[]{{0, 0}, {3, 3}, {4, 4}, {5, 5}, {12, 12}};
    bool reachable = false;
    assert(ac8hook::install(code, reinterpret_cast<void*>(&detour), trampoline,
                            kInputProcessorPrefix.data(), static_cast<unsigned>(copied),
                            boundaries, static_cast<unsigned>(std::size(boundaries)),
                            reinterpret_cast<void**>(&original), reachable));
    assert(reachable && original);
    assert(original() == 42);
    assert(reinterpret_cast<Function>(code)() == 43);

    std::cout << "PASS AC8ThrottleControl absolute processor hook\n";
    return 0;
}
