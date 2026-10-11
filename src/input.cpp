#include "ps5_library/input.hpp"

#include <cmath>

namespace ps5_library {

const char* to_glyph_symbol(ButtonGlyph glyph) {
    switch (glyph) {
        case ButtonGlyph::Cross:          return "✕";
        case ButtonGlyph::Circle:         return "○";
        case ButtonGlyph::Square:         return "□";
        case ButtonGlyph::Triangle:       return "△";
        case ButtonGlyph::L1:             return "L1";
        case ButtonGlyph::R1:             return "R1";
        case ButtonGlyph::L2:             return "L2";
        case ButtonGlyph::R2:             return "R2";
        case ButtonGlyph::L3:             return "L3";
        case ButtonGlyph::R3:             return "R3";
        case ButtonGlyph::Options:        return "Options";
        case ButtonGlyph::Touchpad:       return "Touchpad";
        case ButtonGlyph::DPad:           return "D-Pad";
        case ButtonGlyph::DPadLeftRight:  return "◀ ▶";
        case ButtonGlyph::DPadUpDown:     return "▲ ▼";
        default:                          return "";
    }
}

const char* to_glyph_name(ButtonGlyph glyph) {
    switch (glyph) {
        case ButtonGlyph::Cross:          return "[X]";
        case ButtonGlyph::Circle:         return "[O]";
        case ButtonGlyph::Square:         return "[Square]";
        case ButtonGlyph::Triangle:       return "[Triangle]";
        case ButtonGlyph::L1:             return "[L1]";
        case ButtonGlyph::R1:             return "[R1]";
        case ButtonGlyph::L2:             return "[L2]";
        case ButtonGlyph::R2:             return "[R2]";
        case ButtonGlyph::L3:             return "[L3]";
        case ButtonGlyph::R3:             return "[R3]";
        case ButtonGlyph::Options:        return "[Options]";
        case ButtonGlyph::Touchpad:       return "[Touchpad]";
        case ButtonGlyph::DPad:           return "[D-Pad]";
        case ButtonGlyph::DPadLeftRight:  return "[Left/Right]";
        case ButtonGlyph::DPadUpDown:     return "[Up/Down]";
        default:                          return "[Button]";
    }
}

InputManager::InputManager() = default;

void InputManager::process_pad_input(const PadState& raw_state,
                                     std::chrono::milliseconds delta_time) {
    current_state_ = raw_state;
    current_state_.pressed = raw_state.buttons & ~previous_buttons_;
    current_state_.released = ~raw_state.buttons & previous_buttons_;
    previous_buttons_ = raw_state.buttons;

    // Check stick deflection to synthetic D-Pad bits if raw D-Pad not held
    constexpr float kStickDeadzone = 0.45f;
    if (raw_state.stick_left_x < -kStickDeadzone) {
        current_state_.buttons |= pad_bits::kLeft;
    } else if (raw_state.stick_left_x > kStickDeadzone) {
        current_state_.buttons |= pad_bits::kRight;
    }

    if (raw_state.stick_left_y < -kStickDeadzone) {
        current_state_.buttons |= pad_bits::kUp;
    } else if (raw_state.stick_left_y > kStickDeadzone) {
        current_state_.buttons |= pad_bits::kDown;
    }

    // Auto-repeat timing for navigation
    LogicalAction active_nav = LogicalAction::None;
    if (is_button_down(pad_bits::kLeft)) active_nav = LogicalAction::NavLeft;
    else if (is_button_down(pad_bits::kRight)) active_nav = LogicalAction::NavRight;
    else if (is_button_down(pad_bits::kUp)) active_nav = LogicalAction::NavUp;
    else if (is_button_down(pad_bits::kDown)) active_nav = LogicalAction::NavDown;

    if (active_nav != LogicalAction::None) {
        if (active_nav == repeating_action_) {
            repeat_timer_ += delta_time;
        } else {
            repeating_action_ = active_nav;
            repeat_timer_ = std::chrono::milliseconds(0);
        }
    } else {
        repeating_action_ = LogicalAction::None;
        repeat_timer_ = std::chrono::milliseconds(0);
    }
}

void InputManager::feed_raw_buttons(uint32_t raw_buttons, float lx, float ly) {
    PadState state;
    state.buttons = raw_buttons;
    state.stick_left_x = lx;
    state.stick_left_y = ly;
    process_pad_input(state);
}

bool InputManager::action_triggered(LogicalAction action) const {
    switch (action) {
        case LogicalAction::Confirm:
            return is_button_pressed(pad_bits::kCross);

        case LogicalAction::Cancel:
            return is_button_pressed(pad_bits::kCircle);

        case LogicalAction::Inspect:
            return is_button_pressed(pad_bits::kSquare);

        case LogicalAction::ToggleMode:
            return is_button_pressed(pad_bits::kTriangle);

        case LogicalAction::SwitchLayout:
            return is_button_pressed(pad_bits::kTouchpad) || is_button_pressed(pad_bits::kOptions);

        case LogicalAction::PrevFilter:
            return is_button_pressed(pad_bits::kL1);

        case LogicalAction::NextFilter:
            return is_button_pressed(pad_bits::kR1);

        case LogicalAction::OpenSearch:
            return is_button_pressed(pad_bits::kL3) || is_button_pressed(pad_bits::kR3);

        case LogicalAction::NavLeft:
            if (is_button_pressed(pad_bits::kLeft)) return true;
            if (repeating_action_ == LogicalAction::NavLeft && repeat_timer_ >= std::chrono::milliseconds(300)) {
                return (repeat_timer_.count() % 100) < 20;
            }
            return false;

        case LogicalAction::NavRight:
            if (is_button_pressed(pad_bits::kRight)) return true;
            if (repeating_action_ == LogicalAction::NavRight && repeat_timer_ >= std::chrono::milliseconds(300)) {
                return (repeat_timer_.count() % 100) < 20;
            }
            return false;

        case LogicalAction::NavUp:
            if (is_button_pressed(pad_bits::kUp)) return true;
            if (repeating_action_ == LogicalAction::NavUp && repeat_timer_ >= std::chrono::milliseconds(300)) {
                return (repeat_timer_.count() % 100) < 20;
            }
            return false;

        case LogicalAction::NavDown:
            if (is_button_pressed(pad_bits::kDown)) return true;
            if (repeating_action_ == LogicalAction::NavDown && repeat_timer_ >= std::chrono::milliseconds(300)) {
                return (repeat_timer_.count() % 100) < 20;
            }
            return false;

        default:
            return false;
    }
}

} // namespace ps5_library
