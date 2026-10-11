#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace ps5_library {

namespace pad_bits {
constexpr uint32_t kL3        = 0x00000002;
constexpr uint32_t kR3        = 0x00000004;
constexpr uint32_t kOptions   = 0x00000008;
constexpr uint32_t kUp        = 0x00000010;
constexpr uint32_t kRight     = 0x00000020;
constexpr uint32_t kDown      = 0x00000040;
constexpr uint32_t kLeft      = 0x00000080;
constexpr uint32_t kL2        = 0x00000100;
constexpr uint32_t kR2        = 0x00000200;
constexpr uint32_t kL1        = 0x00000400;
constexpr uint32_t kR1        = 0x00000800;
constexpr uint32_t kTriangle  = 0x00001000;
constexpr uint32_t kCircle    = 0x00002000;
constexpr uint32_t kCross     = 0x00004000;
constexpr uint32_t kSquare    = 0x00008000;
constexpr uint32_t kTouchpad  = 0x00100000;
} // namespace pad_bits

enum class ButtonGlyph {
    Cross,
    Circle,
    Square,
    Triangle,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    Options,
    Touchpad,
    DPad,
    DPadLeftRight,
    DPadUpDown
};

const char* to_glyph_symbol(ButtonGlyph glyph);
const char* to_glyph_name(ButtonGlyph glyph);

struct ButtonPrompt {
    ButtonGlyph glyph{ButtonGlyph::Cross};
    std::string action_label;
    bool enabled{true};
    bool primary{false};
};

struct PadState {
    uint32_t buttons{0};
    uint32_t pressed{0};   // Buttons that went down this frame
    uint32_t released{0};  // Buttons that went up this frame
    float stick_left_x{0.0f};  // -1.0 (left) to +1.0 (right)
    float stick_left_y{0.0f};  // -1.0 (up) to +1.0 (down)
    float stick_right_x{0.0f};
    float stick_right_y{0.0f};
};

enum class LogicalAction {
    None,
    NavUp,
    NavDown,
    NavLeft,
    NavRight,
    Confirm,        // Cross
    Cancel,         // Circle
    Inspect,        // Square
    ToggleMode,     // Triangle (Installed vs Library Browser)
    SwitchLayout,   // Touchpad / Options (Coverflow vs Block Grid)
    PrevFilter,     // L1
    NextFilter,     // R1
    OpenSearch,     // L3 / R3
};

class InputManager {
public:
    InputManager();

    // Updates pad transitions and generates logical actions
    void process_pad_input(const PadState& raw_state,
                           std::chrono::milliseconds delta_time = std::chrono::milliseconds(16));

    bool is_button_down(uint32_t bit) const { return (current_state_.buttons & bit) != 0; }
    bool is_button_pressed(uint32_t bit) const { return (current_state_.pressed & bit) != 0; }
    bool is_button_released(uint32_t bit) const { return (current_state_.released & bit) != 0; }

    const PadState& current_state() const { return current_state_; }

    // Check if an action was triggered (with repeat support for navigation)
    bool action_triggered(LogicalAction action) const;

    // Direct helper for feeding raw bitmask
    void feed_raw_buttons(uint32_t raw_buttons, float lx = 0.0f, float ly = 0.0f);

private:
    PadState current_state_;
    uint32_t previous_buttons_{0};

    // Auto-repeat state for analog/D-Pad navigation
    std::chrono::milliseconds repeat_timer_{0};
    LogicalAction repeating_action_{LogicalAction::None};
};

} // namespace ps5_library
