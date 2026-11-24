#include "raw_axis_control.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace ac8throttle;
    float axes[6]{};
    RawAxisOwnership ownership;
    const auto half = pack_raw_command({true, true, false, 0.5f, 0.0f});
    axes[kThrottleAxis] = 1.0f;
    auto result = apply_raw_command(0x10000, axes, half, ownership);
    assert(result.wrote && !result.manual_yield);
    assert(std::abs(axes[kThrottleAxis] - 0.5f) < 0.0001f);
    result = apply_raw_command(0x10000, axes, 0, ownership);
    assert(!result.wrote && axes[kThrottleAxis] == 1.0f);
    axes[kThrottleAxis] = 0.4f;
    const auto no_key = pack_raw_command({true, false, false, 0.8f, 0.0f});
    result = apply_raw_command(0x10000, axes, no_key, ownership);
    assert(!result.wrote && result.manual_yield && axes[kThrottleAxis] == 0.4f);
    axes[kThrottleAxis] = 0.0f;
    axes[kBrakeAxis] = 1.0f;
    const auto expected_brake = pack_raw_command({true, false, true, 0.0f, 0.25f});
    result = apply_raw_command(0x10000, axes, expected_brake, ownership);
    assert(result.wrote && std::abs(axes[kBrakeAxis] - 0.25f) < 0.0001f);

    // First callback after a fresh key press: the worker's last sample did not
    // see the key yet (expected flags false) but the bound key is held now.
    int probes = 0;
    const auto throttle_key = [&probes] { ++probes; return kLiveThrottleKey; };
    const auto brake_key = [&probes] { ++probes; return kLiveBrakeKey; };
    const auto no_live_key = [&probes] { ++probes; return std::uint8_t{0}; };
    const auto stale = pack_raw_command({true, false, false, 0.6f, 0.0f});
    RawAxisOwnership race;
    float raced[6]{};
    raced[kThrottleAxis] = 1.0f;
    result = apply_raw_command(0x20000, raced, stale, race, throttle_key);
    assert(result.wrote && !result.manual_yield && probes == 1);
    assert(std::abs(raced[kThrottleAxis] - 0.6f) < 0.0001f);

    // Reversal toward neutral: native brake 1.0 from a just-pressed S.
    const auto stale_reverse = pack_raw_command({true, false, false, 0.4f, 0.0f});
    raced[kThrottleAxis] = 0.0f;
    raced[kBrakeAxis] = 1.0f;
    result = apply_raw_command(0x20000, raced, stale_reverse, race, brake_key);
    assert(result.wrote && !result.manual_yield && probes == 2);
    assert(std::abs(raced[kThrottleAxis] - 0.4f) < 0.0001f && raced[kBrakeAxis] == 0.0f);

    // A held throttle key does not explain brake input, and vice versa.
    raced[kThrottleAxis] = 0.0f;
    raced[kBrakeAxis] = 1.0f;
    result = apply_raw_command(0x20000, raced, stale_reverse, race, throttle_key);
    assert(!result.wrote && result.manual_yield && raced[kBrakeAxis] == 1.0f && probes == 3);

    // No bound key held: unbound analog input still yields, as before.
    raced[kBrakeAxis] = 0.0f;
    raced[kThrottleAxis] = 0.4f;
    result = apply_raw_command(0x20000, raced, stale, race, no_live_key);
    assert(!result.wrote && result.manual_yield && raced[kThrottleAxis] == 0.4f && probes == 4);

    // Invalid native values yield without asking for keys.
    raced[kThrottleAxis] = std::nanf("");
    result = apply_raw_command(0x20000, raced, stale, race, throttle_key);
    assert(!result.wrote && result.manual_yield && probes == 4);

    // Explained or zero native axes never call the probe.
    raced[kThrottleAxis] = 0.0f;
    result = apply_raw_command(0x20000, raced, stale, race, throttle_key);
    assert(result.wrote && probes == 4);
    raced[kThrottleAxis] = 1.0f;
    result = apply_raw_command(0x20000, raced, half, race, throttle_key);
    assert(result.wrote && probes == 4);

    // The default probe keeps the previous behaviour: stale flags yield.
    raced[kThrottleAxis] = 1.0f;
    result = apply_raw_command(0x20000, raced, stale, race);
    assert(!result.wrote && result.manual_yield);

    // Key release: the native brake axis lingers at 1.0 for a frame or two after
    // the worker sees S released. Within the grace window it is still expected,
    // so the stored command (0% here, after a held brake at the floor) is written.
    ReleaseGrace grace;
    auto expected = grace.update(1000, false, true);
    assert(!expected.throttle && expected.brake);
    expected = grace.update(1010, false, false);
    assert(expected.brake);
    const auto after_release = pack_raw_command({true, expected.throttle, expected.brake, 0.0f, 0.0f});
    RawAxisOwnership released;
    float lag[6]{};
    lag[kBrakeAxis] = 1.0f;
    result = apply_raw_command(0x30000, lag, after_release, released, no_live_key);
    assert(result.wrote && !result.manual_yield && lag[kBrakeAxis] == 0.0f);
    expected = grace.update(1000 + kReleaseGraceMs - 1, false, false);
    assert(expected.brake);
    expected = grace.update(1000 + kReleaseGraceMs, false, false);
    assert(!expected.throttle && !expected.brake);
    // After the window, unexplained input yields again.
    lag[kBrakeAxis] = 1.0f;
    result = apply_raw_command(0x30000, lag, pack_raw_command({true, false, false, 0.0f, 0.0f}),
                               released, no_live_key);
    assert(!result.wrote && result.manual_yield);
    // Each axis has its own window; a held key keeps renewing it.
    expected = grace.update(2000, true, false);
    assert(expected.throttle && !expected.brake);
    expected = grace.update(2000 + kReleaseGraceMs + 5, true, false);
    assert(expected.throttle);
    expected = grace.update(2000 + 2 * kReleaseGraceMs + 4, false, false);
    assert(expected.throttle);
    expected = grace.update(2000 + 2 * kReleaseGraceMs + 5, false, false);
    assert(!expected.throttle);
    std::cout << "PASS raw throttle axis control\n";
}
