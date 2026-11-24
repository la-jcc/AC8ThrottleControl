#include "control_state.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>

namespace { bool near(float a, float b) { return std::abs(a - b) < 0.001f; } }

int main() {
    using namespace ac8throttle;
    ControlSettings settings;
    ControlState state;
    ControlCommand command;
    const auto up = [&](float t = 0.1f) { return state.update(settings, {.accelerate=true, .elapsed_seconds=t}); };
    const auto down = [&](float t = 0.1f) { return state.update(settings, {.decelerate=true, .elapsed_seconds=t}); };
    const auto idle = [&]() { return state.update(settings, {}); };

    // Default: percent, floor -100, signed progressive scale.
    assert(settings.mode == ControlMode::percent && near(brake_floor(settings), -100.0f));
    settings.sensitivity_percent_per_second = 50.0f;
    settings.zero_snap_percent = 0.0f;
    state.reset(settings);
    assert(near(state.percent(), 0.0f));
    for (int i = 0; i < 10; ++i) command = up();
    assert(command.write && near(command.percent, 50.0f) && near(command.throttle, 0.5f));
    for (int i = 0; i < 10; ++i) command = up();
    assert(command.write && near(command.percent, 100.0f) && near(command.throttle, 1.0f));
    command = idle();
    assert(command.write && near(command.percent, 100.0f));
    for (int i = 0; i < 40; ++i) command = down();
    assert(command.write && !command.brake_hold && near(command.percent, -100.0f) && near(command.brake, 1.0f));
    command = down();
    assert(command.write && !command.brake_hold && near(command.brake, 1.0f));  // -100 floor writes full brake itself

    // W+S: neutral or High-G yield with the stored value kept.
    settings.both_keys = BothKeysMode::neutral;
    command = state.update(settings, {.accelerate=true, .decelerate=true});
    assert(command.write && near(command.percent, 0.0f));
    for (int i = 0; i < 10; ++i) command = up();
    settings.both_keys = BothKeysMode::high_g_only;
    command = state.update(settings, {.accelerate=true, .decelerate=true});
    assert(!command.write && command.high_g_yield && near(command.percent, 50.0f));
    command = state.update(settings, {.neutral_pressed=true});
    assert(command.write && near(command.percent, 0.0f));

    // Floor 0: throttle only; decelerate held at 0% is the game's full brake.
    settings.brake_floor_percent = 0.0f;
    state.reset(settings);
    command = down();
    assert(!command.write && command.brake_hold && near(command.percent, 0.0f));
    for (int i = 0; i < 10; ++i) command = up();
    for (int i = 0; i < 10; ++i) command = down();
    assert(command.write && near(command.percent, 0.0f));
    command = down();
    assert(!command.write && command.brake_hold);             // no re-press needed
    command = idle();
    assert(command.write && !command.brake_hold && near(command.percent, 0.0f));

    // Intermediate floor: progressive braking to -30%, then a held full brake.
    settings.brake_floor_percent = -30.0f;
    state.reset(settings);
    for (int i = 0; i < 4; ++i) command = down();
    assert(command.write && near(command.percent, -20.0f) && near(command.brake, 0.2f));
    for (int i = 0; i < 2; ++i) command = down();
    assert(command.write && near(command.percent, -30.0f) && near(command.brake, 0.3f));
    command = down();
    assert(!command.write && command.brake_hold && near(command.percent, -30.0f));
    command = idle();
    assert(command.write && !command.brake_hold && near(command.percent, -30.0f) && near(command.brake, 0.3f));
    for (int i = 0; i < 10; ++i) command = up();
    assert(command.write && near(command.percent, 20.0f) && near(command.throttle, 0.2f));
    settings.brake_floor_percent = -250.0f;
    assert(near(brake_floor(settings), -100.0f));
    settings.brake_floor_percent = 40.0f;
    assert(near(brake_floor(settings), 0.0f));

    // Neutral deadzone (zero_snap): never stops held input.
    settings.brake_floor_percent = -50.0f;
    settings.zero_snap_percent = 5.0f;
    state.reset(settings);
    command = up();
    assert(command.write && near(command.percent, 0.0f) && near(command.throttle, 0.0f));  // 5%: inside
    command = up();
    assert(near(command.percent, 10.0f) && near(command.throttle, 0.1f));                 // left the zone
    command = up();
    float lowest = 100.0f;
    for (int i = 0; i < 6; ++i) {                                                          // 15 -> -15 held
        command = down();
        assert(command.write && !command.brake_hold);
        lowest = std::min(lowest, command.percent);
    }
    assert(near(command.percent, -15.0f) && near(command.brake, 0.15f) && near(lowest, -15.0f));
    for (int i = 0; i < 2; ++i) command = up();
    assert(near(command.percent, 0.0f) && near(state.percent(), -5.0f));                  // zone edge
    command = idle();
    assert(near(command.percent, 0.0f) && near(state.percent(), 0.0f));                   // settles on 0
    command = down(0.05f);
    assert(near(command.percent, 0.0f) && near(command.brake, 0.0f));                      // -2.5: inside
    command = idle();
    assert(near(state.percent(), 0.0f));
    for (int i = 0; i < 2; ++i) command = up();
    command = idle();
    assert(near(command.percent, 10.0f));                                                   // kept outside
    for (int i = 0; i < 13; ++i) command = down();                                          // 10 -> -50, then floor
    assert(!command.write && command.brake_hold && near(command.percent, -50.0f));
    settings.zero_snap_percent = 0.0f;
    state.reset(settings);
    command = up(0.02f);
    assert(near(command.percent, 1.0f));                                                    // 0 disables it

    // Toggle mode.
    settings = {};
    settings.mode = ControlMode::toggle;
    state.reset(settings);
    command = state.update(settings, {.accelerate=true, .accelerate_pressed=true});
    assert(command.write && near(command.throttle, 1.0f) && near(command.percent, 100.0f));
    command = idle();
    assert(command.write && near(command.throttle, 1.0f));
    command = state.update(settings, {.decelerate=true, .decelerate_pressed=true});
    assert(command.write && near(command.brake, 1.0f) && near(command.percent, -100.0f));
    command = state.update(settings, {.neutral_pressed=true});
    assert(command.write && near(command.percent, 0.0f));
    command = state.update(settings, {.accelerate=true, .decelerate=true});
    assert(!command.write && command.high_g_yield);
    settings.toggle_decelerate = false;
    command = state.update(settings, {.decelerate=true});
    assert(!command.write && command.brake_hold);

    // Toggle W+S: the same result whichever key is pressed first.
    {
        struct Step { bool w, s, wp, sp; };
        // Runs presses from a start state and returns the final command after
        // all keys are released. start: 0 neutral, 1 accelerating.
        const auto run = [&](ControlSettings set, int start, std::initializer_list<Step> steps) {
            ControlState toggle;
            toggle.reset(set);
            ControlCommand last{};
            if (start == 1) {
                toggle.update(set, {.accelerate=true, .accelerate_pressed=true});
                toggle.update(set, {});
            }
            for (const Step& step : steps) {
                last = toggle.update(set, {.accelerate=step.w, .decelerate=step.s,
                                           .accelerate_pressed=step.wp, .decelerate_pressed=step.sp});
            }
            return last;
        };
        ControlSettings set{};
        set.mode = ControlMode::toggle;
        for (int decel_toggle = 0; decel_toggle < 2; ++decel_toggle) {
            set.toggle_decelerate = decel_toggle != 0;
            set.toggle_hold_to_neutral = true;
            for (int start = 0; start < 2; ++start) {
                // W then S, S then W, both at once; release S, keep W.
                const ControlCommand w_first = run(set, start, {{true,false,true,false},{true,true,false,true},
                                                                {true,false,false,false}});
                const ControlCommand s_first = run(set, start, {{false,true,false,true},{true,true,true,false},
                                                                {true,false,false,false}});
                const ControlCommand together = run(set, start, {{true,true,true,true},{true,false,false,false}});
                for (const ControlCommand& c : {w_first, s_first, together}) {
                    assert(c.write && near(c.throttle, 1.0f) && near(c.percent, 100.0f));    // W held: accelerating
                }
                // ...then release W too: still accelerating.
                const ControlCommand w_first_released = run(set, start, {{true,false,true,false},{true,true,false,true},
                                                                         {true,false,false,false},{false,false,false,false}});
                const ControlCommand s_first_released = run(set, start, {{false,true,false,true},{true,true,true,false},
                                                                         {true,false,false,false},{false,false,false,false}});
                assert(near(w_first_released.throttle, 1.0f) && near(s_first_released.throttle, 1.0f));
            }
        }
        set.toggle_decelerate = false;
        // Turning acceleration off acts on release; held W keeps accelerating.
        command = run(set, 1, {{true,false,true,false}});
        assert(near(command.throttle, 1.0f));
        command = run(set, 1, {{true,false,true,false},{false,false,false,false}});
        assert(command.write && near(command.percent, 0.0f));
        // toggle_accelerate_repress=keep: W again keeps accelerating; S hold
        // with after_hold=neutral, or the neutral key, still leaves it.
        set.toggle_accelerate_keep = true;
        command = run(set, 1, {{true,false,true,false},{false,false,false,false}});
        assert(command.write && near(command.throttle, 1.0f) && near(command.percent, 100.0f));
        command = run(set, 1, {{true,false,true,false},{false,false,false,false},{true,false,true,false},
                               {false,false,false,false}});
        assert(near(command.throttle, 1.0f));
        command = run(set, 1, {{false,true,false,true},{false,false,false,false}});
        assert(command.write && near(command.percent, 0.0f));
        set.toggle_decelerate = true;
        command = run(set, 1, {{false,true,false,true},{false,false,false,false}});
        assert(command.write && near(command.brake, 1.0f));
        set.toggle_decelerate = false;
        set.toggle_accelerate_keep = false;
        // Turning it on acts at once.
        command = run(set, 0, {{true,false,true,false}});
        assert(near(command.throttle, 1.0f));
        // Accelerating, W+S, release both: High-G only, acceleration kept.
        command = run(set, 1, {{true,true,true,true},{false,false,false,false}});
        assert(near(command.throttle, 1.0f));
        // Leaving W+S with S held: S is the vanilla hold, then neutral.
        command = run(set, 1, {{true,true,true,true},{false,true,false,false}});
        assert(!command.write && command.brake_hold);
        command = run(set, 1, {{true,true,true,true},{false,true,false,false},{false,false,false,false}});
        assert(command.write && near(command.percent, 0.0f));
    }

    // A decelerate hold during an acceleration toggle: resume or neutral.
    settings = {};
    settings.mode = ControlMode::toggle;
    settings.toggle_decelerate = false;
    state.reset(settings);
    command = state.update(settings, {.accelerate=true, .accelerate_pressed=true});
    command = idle();
    assert(command.write && near(command.throttle, 1.0f));
    command = state.update(settings, {.decelerate=true, .decelerate_pressed=true});
    assert(!command.write && command.brake_hold);
    command = idle();
    assert(command.write && near(command.throttle, 1.0f) && near(command.percent, 100.0f));  // resume
    settings.toggle_hold_to_neutral = true;
    command = state.update(settings, {.decelerate=true, .decelerate_pressed=true});
    assert(!command.write && command.brake_hold);
    command = idle();
    assert(command.write && near(command.throttle, 0.0f) && near(command.brake, 0.0f) &&
           near(command.percent, 0.0f));                                                    // neutral
    command = state.update(settings, {.accelerate=true, .accelerate_pressed=true});
    assert(command.write && near(command.throttle, 1.0f));                                  // toggles again
    settings.toggle_decelerate = true;
    settings.toggle_accelerate = false;
    command = state.update(settings, {.decelerate=true, .decelerate_pressed=true});
    command = idle();
    assert(near(command.brake, 1.0f));
    command = state.update(settings, {.accelerate=true, .accelerate_pressed=true});
    assert(!command.write);
    command = idle();
    assert(command.write && near(command.percent, 0.0f));                                   // symmetric

    // Air brake: hold and toggle, stored setting resumes afterwards.
    settings = {};
    settings.sensitivity_percent_per_second = 50.0f;
    settings.zero_snap_percent = 0.0f;
    state.reset(settings);
    for (int i = 0; i < 8; ++i) command = up();
    command = state.update(settings, {.airbrake=true, .airbrake_pressed=true});
    assert(command.write && command.airbrake && near(command.throttle, 0.0f) && near(command.brake, 1.0f) &&
           near(command.percent, 40.0f));
    command = state.update(settings, {.accelerate=true, .elapsed_seconds=0.1f, .airbrake=true});
    assert(command.airbrake && near(state.percent(), 40.0f));
    command = idle();
    assert(!command.airbrake && near(command.percent, 40.0f) && near(command.throttle, 0.4f));
    settings.airbrake_toggle = true;
    command = state.update(settings, {.airbrake=true, .airbrake_pressed=true});
    assert(command.airbrake && near(command.brake, 1.0f));
    command = idle();
    assert(command.airbrake && near(command.brake, 1.0f));
    command = state.update(settings, {.airbrake=true, .airbrake_pressed=true});
    assert(!command.airbrake && near(command.throttle, 0.4f));
    command = state.update(settings, {.airbrake=true, .airbrake_pressed=true});
    state.reset(settings);
    command = idle();
    assert(!command.airbrake);
    settings.mode = ControlMode::toggle;
    settings.airbrake_toggle = false;
    command = state.update(settings, {.airbrake=true});
    assert(command.write && command.airbrake && near(command.brake, 1.0f));
    std::cout << "PASS throttle control state\n";
}
