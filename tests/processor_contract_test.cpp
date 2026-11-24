#include "processor_contract.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace ac8throttle;
    constexpr std::uintptr_t player = 0x10000000;
    assert(player_contract(player, player + kInputStateOffset,
                           player + kRawPacketOffset, player + kSettingsOffset));
    assert(!player_contract(0, kInputStateOffset, kRawPacketOffset, kSettingsOffset));
    assert(!player_contract(player, player + kInputStateOffset + 8,
                            player + kRawPacketOffset, player + kSettingsOffset));
    assert(!player_contract(player, player + kInputStateOffset,
                            player + kRawPacketOffset + 8, player + kSettingsOffset));
    assert(kInputProcessorPrefix.size() == 16);
    static_assert(kPostHookOffset == kInputProcessorPrefix.size());
    static_assert(kPostHookPrefix.size() == 15);
    assert(kPostHookPrefix.front() == 0x48 && kPostHookPrefix.back() == 0xb8);
    std::cout << "PASS AC8ThrottleControl player input contract\n";
    return 0;
}
