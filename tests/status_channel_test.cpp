#include "status_channel.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace ac8throttle;
    assert(encode_status_percent(-62.5f) == -62500);
    assert(encode_status_percent(-101.0f) == -100000);
    assert(encode_status_percent(101.0f) == 100000);
    StatusRecord value{};
    value.magic = kStatusMagic;
    value.sequence = 7;
    value.active = 1;
    value.mode = 1;
    value.percent_milli = encode_status_percent(-62.5f);
    value.flags = 3;
    value.checksum = status_checksum(value);
    assert(valid_status(value));
    value.percent_milli++;
    assert(!valid_status(value));
    std::cout << "PASS signed throttle HUD status channel\n";
}
