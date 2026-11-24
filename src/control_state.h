#pragma once

#include <algorithm>
#include <cmath>

namespace ac8throttle {

// percent: one signed scale, -100% full brake .. 0% neutral .. +100% full
// throttle, limited below by brake_floor. toggle: per-direction latches.
// The status channel and the HUD use these numeric values.
enum class ControlMode {
    percent = 0,
    toggle = 1,
};

enum class BothKeysMode {
    neutral,
    high_g_only,
};

struct ControlSettings {
    ControlMode mode{ControlMode::percent};
    BothKeysMode both_keys{BothKeysMode::high_g_only};
    float sensitivity_percent_per_second{50.0f};
    // Neutral deadzone: held input is never stopped; a value released inside
    // +/-zero_snap becomes 0%, and from 0% the command stays 0% until held
    // input carries the value outside the zone. 0 disables it.
    float zero_snap_percent{2.0f};
    bool toggle_accelerate{true};
    bool toggle_decelerate{true};
    // toggle: after a hold of a direction whose toggle is disabled, false
    // resumes the opposite latch, true clears it (neutral).
    bool toggle_hold_to_neutral{false};
    // toggle: pressing accelerate again while accelerating. false selects
    // neutral on release, true keeps accelerating (leave it with S or the
    // neutral key).
    bool toggle_accelerate_keep{false};
    float brake_floor_percent{-100.0f};  // -100..0
    bool airbrake_toggle{false};         // airbrake key: false = hold, true = toggle
};

// Lowest stored percentage. Above -100%, holding decelerate at the floor is the
// game's own full brake for as long as it is held; releasing returns to the floor.
inline float brake_floor(const ControlSettings& settings) {
    return std::clamp(settings.brake_floor_percent, -100.0f, 0.0f);
}

struct ControlInput {
    bool accelerate{};
    bool decelerate{};
    bool combined{};
    bool neutral_pressed{};
    bool accelerate_pressed{};
    bool decelerate_pressed{};
    float elapsed_seconds{};
    bool airbrake{};          // airbrake key held now
    bool airbrake_pressed{};  // airbrake key pressed since the previous update
};

struct ControlCommand {
    bool write{};
    bool brake_hold{};
    bool high_g_yield{};
    float percent{};
    float throttle{};
    float brake{};
    bool airbrake{};
};

class ControlState {
public:
    void reset(const ControlSettings&) {
        percent_ = 0.0f;
        accelerate_latched_ = false;
        decelerate_latched_ = false;
        accelerate_release_clears_ = 0;
        decelerate_release_clears_ = 0;
        previous_accelerate_ = false;
        previous_decelerate_ = false;
        previous_both_ = false;
        airbrake_latched_ = false;
    }

    // Stored value (inside the deadzone it may differ from the command).
    [[nodiscard]] float percent() const { return percent_; }

    ControlCommand update(const ControlSettings& settings, const ControlInput& input) {
        // Air brake: full brake while active, in every mode. The stored
        // throttle setting is untouched and resumes when the air brake ends.
        if (settings.airbrake_toggle) {
            if (input.airbrake_pressed) airbrake_latched_ = !airbrake_latched_;
        } else {
            airbrake_latched_ = false;
        }
        if (settings.airbrake_toggle ? airbrake_latched_ : input.airbrake) {
            const float shown = settings.mode == ControlMode::toggle ? percent_ : output(settings);
            return {true, false, false, shown, 0.0f, 1.0f, true};
        }
        if (settings.mode == ControlMode::toggle) return update_toggle(settings, input);
        return update_percent(settings, input);
    }

private:
    // The commanded percentage: a stored value inside the deadzone commands neutral.
    [[nodiscard]] float output(const ControlSettings& settings) const {
        const float zone = settings.zero_snap_percent;
        return zone > 0.0f && std::abs(percent_) <= zone ? 0.0f : percent_;
    }

    ControlCommand update_percent(const ControlSettings& settings, const ControlInput& input) {
        const bool both = input.combined || (input.accelerate && input.decelerate);
        if (input.neutral_pressed) percent_ = 0.0f;

        if (both) {
            if (settings.both_keys == BothKeysMode::high_g_only) {
                return {false, false, true, output(settings), 0.0f, 0.0f};
            }
            percent_ = 0.0f;
        } else if (input.accelerate || input.decelerate) {
            const float elapsed = std::clamp(input.elapsed_seconds, 0.0f, 0.1f);
            const float step = settings.sensitivity_percent_per_second * elapsed;
            if (input.accelerate) {
                percent_ = std::min(100.0f, percent_ + step);
            } else {
                const float floor = brake_floor(settings);
                if (floor > -100.0f && percent_ <= floor + 0.0001f) {
                    percent_ = floor;
                    return {false, true, false, percent_, 0.0f, 0.0f};
                }
                percent_ = std::max(floor, percent_ - step);
            }
        } else if (std::abs(percent_) <= settings.zero_snap_percent) {
            // Released inside the deadzone: settle on neutral.
            percent_ = 0.0f;
        }

        percent_ = std::max(brake_floor(settings), percent_);
        const float shown = output(settings);
        return {true, false, false, shown, std::max(0.0f, shown) / 100.0f,
                std::max(0.0f, -shown) / 100.0f};
    }

    // Toggle rules, the same whichever of W and S is pressed first:
    // - A press that turns a direction on acts at once.
    // - Turning a direction off acts when the key is released: W pressed while
    //   accelerating keeps accelerating while held and selects neutral on
    //   release (unless toggle_accelerate_keep). A hold of a direction whose toggle is false clears the other
    //   latch on release when toggle_hold_to_neutral is set.
    // - W+S (High-G) during a hold cancels that release action, and leaving
    //   W+S with one key still held turns that key's direction on.
    // Presses during W+S change nothing else.
    ControlCommand update_toggle(const ControlSettings& settings, const ControlInput& input) {
        if (!settings.toggle_accelerate && !settings.toggle_decelerate) {
            percent_ = 0.0f;
            return {false, input.decelerate, false, percent_, 0.0f, 0.0f};
        }
        const bool both = input.combined || (input.accelerate && input.decelerate);
        if (input.neutral_pressed) {
            accelerate_latched_ = false;
            decelerate_latched_ = false;
            accelerate_release_clears_ = 0;
            decelerate_release_clears_ = 0;
        }

        if (previous_accelerate_ && !input.accelerate) {
            apply_clears(accelerate_release_clears_);
            accelerate_release_clears_ = 0;
        }
        if (previous_decelerate_ && !input.decelerate) {
            apply_clears(decelerate_release_clears_);
            decelerate_release_clears_ = 0;
        }
        if (previous_both_ && !both) {
            if (input.accelerate) {
                if (settings.toggle_accelerate) select(true);
                else if (settings.toggle_hold_to_neutral) accelerate_release_clears_ |= kDecelerate;
            } else if (input.decelerate) {
                if (settings.toggle_decelerate) select(false);
                else if (settings.toggle_hold_to_neutral) decelerate_release_clears_ |= kAccelerate;
            }
        }
        previous_accelerate_ = input.accelerate;
        previous_decelerate_ = input.decelerate;
        previous_both_ = both;

        if (both) {
            accelerate_release_clears_ = 0;
            decelerate_release_clears_ = 0;
            percent_ = accelerate_latched_ ? 100.0f : decelerate_latched_ ? -100.0f : 0.0f;
            return {false, false, true, percent_, 0.0f, 0.0f};
        }

        if (input.accelerate_pressed) {
            if (!settings.toggle_accelerate) {
                if (settings.toggle_hold_to_neutral) accelerate_release_clears_ |= kDecelerate;
            } else if (!accelerate_latched_) {
                select(true);
            } else if (!settings.toggle_accelerate_keep) {
                accelerate_release_clears_ |= kAccelerate;
            }
        }
        if (input.decelerate_pressed) {
            if (!settings.toggle_decelerate) {
                if (settings.toggle_hold_to_neutral) decelerate_release_clears_ |= kAccelerate;
            } else if (!decelerate_latched_) {
                select(false);
            } else {
                decelerate_release_clears_ |= kDecelerate;
            }
        }

        // A direction whose toggle is disabled remains the game's ordinary
        // hold control. Yield both raw axes while it is held; on release the
        // other direction's latch resumes, or is cleared as set above.
        if ((input.accelerate && !settings.toggle_accelerate) ||
            (input.decelerate && !settings.toggle_decelerate)) {
            percent_ = input.accelerate ? 100.0f : -100.0f;
            return {false, input.decelerate, false, percent_, 0.0f, 0.0f};
        }

        if (accelerate_latched_) {
            percent_ = 100.0f;
            return {true, false, false, percent_, 1.0f, 0.0f};
        }
        if (decelerate_latched_) {
            percent_ = -100.0f;
            return {true, false, false, percent_, 0.0f, 1.0f};
        }
        percent_ = 0.0f;
        // Write neutral rather than yield, so a held toggle key does not
        // reach the game. This also keeps a stable neutral between latches.
        return {true, false, false, percent_, 0.0f, 0.0f};
    }

    static constexpr unsigned kAccelerate = 1;
    static constexpr unsigned kDecelerate = 2;
    void select(bool accelerate) {
        accelerate_latched_ = accelerate;
        decelerate_latched_ = !accelerate;
    }
    void apply_clears(unsigned clears) {
        if (clears & kAccelerate) accelerate_latched_ = false;
        if (clears & kDecelerate) decelerate_latched_ = false;
    }

    float percent_{};
    bool accelerate_latched_{};
    bool decelerate_latched_{};
    unsigned accelerate_release_clears_{};  // latches cleared when W is released
    unsigned decelerate_release_clears_{};  // latches cleared when S is released
    bool previous_accelerate_{};
    bool previous_decelerate_{};
    bool previous_both_{};
    bool airbrake_latched_{};
};

} // namespace ac8throttle
