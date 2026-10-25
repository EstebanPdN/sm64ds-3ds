#pragma once
#include "enhance_settings.h"
#include <algorithm>
#include <cstdio>
// Controller page rows, shared by the pause/front Options model and its art.
// Calibration (pitch sensitivity, dead zone, run threshold) stays internal.
namespace enhance::controller_options {
enum Row : unsigned { Buttons, Control, CStickSensitivity, CirclePadSensitivity, InvertX, InvertY, Count };
constexpr unsigned count = Count;

inline const char* label(unsigned row) {
    constexpr const char* labels[] = {"Buttons", "Control", "C-Stick Sensitivity",
                                      "Circle Pad Sensitivity", "Invert X Axis", "Invert Y Axis"};
    return row < count ? labels[row] : "";
}
// Native art: original menu letters, optional second line.
inline const char* art_label(unsigned row) {
    constexpr const char* labels[] = {"BUTTONS", "CONTROL", "C-STICK", "CIRCLE PAD", "INVERT X AXIS", "INVERT Y AXIS"};
    return row < count ? labels[row] : "";
}
inline const char* art_second_line(unsigned row) {
    return row == CStickSensitivity || row == CirclePadSensitivity ? "SENSITIVITY" : nullptr;
}
inline bool toggle(unsigned row) { return row == InvertX || row == InvertY; }
inline bool toggle_on(const Settings& s, unsigned row) {
    return row == InvertX ? s.camera_invert_x : row == InvertY ? s.camera_invert_y : false;
}
inline void value(const Settings& s, unsigned row, char* out, unsigned cap) {
    if (!cap) return;
    constexpr const char* buttons[] = {"Standard", "Touch", "Dual-hand"};
    constexpr const char* control[] = {"Original", "Hybrid", "Nintendo 64"};
    switch (row) {
    case Buttons: std::snprintf(out, cap, "%s", buttons[s.controller_mode % 3]); break;
    case Control: std::snprintf(out, cap, "%s", control[s.physics_mode % 3]); break;
    case CStickSensitivity: std::snprintf(out, cap, "%u", unsigned(s.camera_sensitivity)); break;
    case CirclePadSensitivity: std::snprintf(out, cap, "%u", unsigned(s.move_sensitivity)); break;
    case InvertX: case InvertY: std::snprintf(out, cap, "%s", toggle_on(s, row) ? "On" : "Off"); break;
    default: out[0] = 0;
    }
}
// Left/right arrows cycle values in place; toggles flip.
inline void adjust(Settings& s, unsigned row, int direction) {
    const auto cycle = [direction](uint8_t v) { return uint8_t((v + direction + 3) % 3); };
    const auto sensitivity = [direction](unsigned v) { return uint8_t(std::max(25, std::min(200, int(v) + direction * 5))); };
    switch (row) {
    case Buttons: s.controller_mode = cycle(s.controller_mode); break;
    case Control: s.physics_mode = cycle(s.physics_mode); break;
    case CStickSensitivity:
        s.camera_sensitivity = sensitivity(s.camera_sensitivity);
        s.camera_pitch_sensitivity = s.camera_sensitivity;
        break;
    case CirclePadSensitivity: s.move_sensitivity = sensitivity(s.move_sensitivity); break;
    case InvertX: s.camera_invert_x = !s.camera_invert_x; break;
    case InvertY: s.camera_invert_y = !s.camera_invert_y; break;
    default: break;
    }
}
inline void set_toggle(Settings& s, unsigned row, bool on) {
    if (row == InvertX) s.camera_invert_x = on;
    else if (row == InvertY) s.camera_invert_y = on;
}
}
