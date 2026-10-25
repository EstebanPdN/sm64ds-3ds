#pragma once
// visual-enhancements: user-facing display options, persisted in
// sdmc:/3ds/sm64ds/settings.ini (read at boot by game_entry.cpp, edited by the
// in-game settings overlay, enhance_overlay.h). The user build ignores
// env.txt, so this file is the only way to configure a user build.
//
// Two kinds of options:
//  * boot options, applied ONCE before the renderer starts by exporting the
//    existing environment switches (SM64DS_SCREEN_FILTER, SM64DS_TOP_LAYOUT,
//    SM64DS_TOP_SKY); changing them in the overlay says "au prochain
//    démarrage". An explicit env.txt value (dev builds) always wins.
//  * live options, read every frame by the renderer through enhance::live()
//    (atomics: written by the overlay thread, read by the present worker).
//
// Pure C++ + stdio: also compiled into host tests.
#include <atomic>
#include <cstdint>

namespace enhance {

enum class TopLayout : uint8_t { Auto = 0, Ds = 1, Stretch = 2 };          // boot
enum class ScreenFilter : uint8_t { Nearest = 0, Sharp = 1 };              // boot
enum class Res3d : uint8_t { Ds = 0, Native = 1, Ss2x = 2 };               // live (Phase 2)
enum class TexFilter : uint8_t { Nearest = 0, Auto = 1, Linear = 2 };      // live (Phase 2)
// Display > Lock FPS: presentation cadence only; the original game keeps its
// own 30 Hz update clock. Off = no LCD wait, 30 = one image per update,
// 60 = interpolated midpoint plus exact image. Auto = console default.
enum FpsLock : uint8_t { kFpsLockOff = 0, kFpsLock30 = 1, kFpsLock60 = 2, kFpsLockAuto = 3 };
constexpr uint8_t default_fps_lock(bool new_model) { return new_model ? kFpsLock60 : kFpsLock30; }
constexpr uint8_t resolve_fps_lock(uint8_t value, bool new_model) {
    return value < kFpsLockAuto ? value : default_fps_lock(new_model);
}

struct Settings {
    // 2D
    bool logo_hd = false;                 // HD title logo (enhance_logo.h)
    uint8_t logo_method = 7;              // enhance::Method (7 = Xbr)
    bool ui_hd = false;                   // HD menu 2D layers (future)
    TopLayout top_layout = TopLayout::Auto;
    ScreenFilter screen_filter = ScreenFilter::Sharp;
    bool full_width_sky = true;
    ScreenFilter gameplay_filter = ScreenFilter::Nearest;   // in-game top presentation
    // 3D
    Res3d res3d = Res3d::Native;
    TexFilter tex_filter = TexFilter::Nearest;
    // misc
    uint8_t audio_volume = 100; // percent, 0..200
    bool show_fps = false;
    bool gpu_timing = false;
    // input (analog-pad, analog_pad.h): Circle Pad drives the DS touch-stick
    // analog path (speed follows tilt, no Y needed) instead of the DS
    // digital D-pad. Default ON at the user's request; kept by safe() and
    // recommended() (not a display option).
    bool circle_pad_analog = true;
    // input (cstick-camera, cstick_camera.h): New 3DS C-Stick camera.
    // 0 = off, 1 = DS arrows (same commands as the touch-screen camera
    // arrows / L / X), 2 = free analog camera. Kept by safe()/recommended().
    uint8_t camera_cstick = 2;

    uint8_t screen_mode = 1;
    uint8_t fps_lock = kFpsLockAuto;
    uint8_t controller_mode = 0;
    uint8_t physics_mode = 0; // Original, Hybrid, Nintendo 64; independent of DS buttons
    uint8_t camera_sensitivity = 100;
    uint8_t camera_pitch_sensitivity = 100;
    uint8_t camera_deadzone = 16;
    uint8_t move_sensitivity = 100;
    uint8_t run_threshold = 70;
    // Free C-Stick camera axes, relative to the corrected default direction.
    bool camera_invert_x = false;
    bool camera_invert_y = false;
    bool developer_dumps = true;          // Developer > Full RAM dumps (L+R+A)
    bool developer_overlay = false;

    // Current behaviour of the port (every new feature off). Used until the
    // features are validated on real hardware.
    static Settings safe();
    // Target defaults once validated (best quality within the frame budget).
    static Settings recommended();
    bool operator==(const Settings& o) const;
};

// Parse an INI text. Unknown keys / bad values are counted and skipped.
struct ParseReport { int lines = 0, applied = 0, unknown = 0, invalid = 0; };
ParseReport parse_ini(const char* text, Settings& inout);
// Serialize (stable order, French comments). Like snprintf, returns required
// bytes excluding NUL (>=cap means truncation); -1 for an invalid buffer.
int write_ini(const Settings& s, char* out, int cap);

// File helpers (stdio). Missing file/read error/oversize/NUL -> false,
// settings/report unchanged. A missing primary may recover a complete .bak.
bool load_file(const char* path, Settings& inout, ParseReport* report = nullptr);
// Checked temp write, flush/fsync/close. 3DS always moves an existing primary
// to .bak first (libctru replacement can unlink before failing), with rollback
// and read recovery. Host POSIX prefers replacement rename. No pre-unlink.
bool save_file(const char* path, const Settings& s);

constexpr const char* kSettingsPath = "sdmc:/3ds/sm64ds/settings.ini";

// Boot: load kSettingsPath (if present) over Settings::safe(), publish to
// live(), and export boot options as env switches unless already set.
// Call from main() before any renderer option is read. Returns the loaded set.
Settings boot_apply(const char* path = kSettingsPath);

// Live view shared with the renderer (lock-free reads).
struct Live {
    std::atomic<bool> logo_hd{false};
    std::atomic<uint8_t> logo_method{7};
    std::atomic<bool> ui_hd{false};
    std::atomic<uint8_t> gameplay_filter{0};
    std::atomic<uint8_t> res3d{1};
    std::atomic<uint8_t> tex_filter{0};
    std::atomic<uint8_t> audio_volume{100};
    std::atomic<bool> show_fps{false};
    std::atomic<bool> gpu_timing{false};
    std::atomic<bool> circle_pad_analog{true};
    std::atomic<uint8_t> camera_cstick{2};
    std::atomic<uint8_t> screen_mode{1};
    std::atomic<uint8_t> fps_lock{kFpsLock60};   // resolved: never kFpsLockAuto
    std::atomic<bool> new_model{false};          // set once at boot
    std::atomic<uint8_t> controller_mode{0};
    std::atomic<uint8_t> physics_mode{0};
    std::atomic<uint8_t> camera_sensitivity{100};
    std::atomic<uint8_t> camera_pitch_sensitivity{100};
    std::atomic<uint8_t> camera_deadzone{16};
    std::atomic<uint8_t> move_sensitivity{100};
    std::atomic<uint8_t> run_threshold{70};
    std::atomic<bool> camera_invert_x{false};
    std::atomic<bool> camera_invert_y{false};
    std::atomic<bool> developer_dumps{true};
    std::atomic<bool> developer_overlay{false};
    // Settings screen open (enhance_runtime): the game sees no input.
    std::atomic<bool> overlay_open{false};
    std::atomic<uint16_t> actual_render_width{256},actual_render_height{192};
    std::atomic<bool> native_fallback{false};
    std::atomic<bool> pause_scene{false};
    std::atomic<uint32_t> generation{0};   // bumped on every publish
};
Live& live();
// publish()/snapshot(): game thread only (boot + overlay). Renderer threads
// read live() only.
void publish(const Settings& s);
Settings snapshot();   // last published full Settings (incl. boot options)
// Console model for Auto defaults. boot_apply() queries it on 3DS; host tests
// set it explicitly. Republishes the current snapshot.
void set_console_model(bool new_model);
// Effective Lock FPS of a settings value on this console.
inline uint8_t effective_fps_lock(const Settings& s) {
    return resolve_fps_lock(s.fps_lock, live().new_model.load(std::memory_order_relaxed));
}

// Texture filter policy for a DS TEXIMAGE_PARAM (Phase 2 hook for the 3D
// texture cache): true = GPU_LINEAR. Auto = linear only for textures that
// cannot show cut-out fringes: no colour-0 transparency (bit 29) and not a
// translucent format (A3I5=1, A5I3=6); direct colour (7) always eligible.
bool texture_wants_linear(uint32_t teximage_param, TexFilter policy);

}  // namespace enhance
