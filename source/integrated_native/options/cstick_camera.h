#pragma once
// cstick-camera: New 3DS C-Stick camera control (port feature, 3DS only).
//
// DS original: the camera is turned with the two "camera arrow" buttons drawn
// on the touch screen (dMeter_c::RenderCameraButtons). Stage::CheckCameraInput
// turns a touch on them into game-level command bits of the local command
// record data_0209f498[i] (+4 held, +6 pressed): 0x200 = left arrow,
// 0x100 = right arrow (both = 0x4000, "behind Mario", added by
// Stage::CheckInput). The follow camera (func_02009e70) then turns its yaw
// (+0x17c) by 0x400 per update while an arrow is held (0x200: -, 0x100: +);
// the 8-direction camera (func_0200bb28) steps 0x2000 per press and repeats
// every 20 updates. The physical keys feed the same command record through
// the per-control-mode key tables (Standard: L = 0x4000 behind Mario,
// X = 0x8000 "zoom" = cycles far camera / first person; Touch mode: X/Y =
// right/left arrows).
//
// Modes ([input] camera_cstick in settings.ini, overlay "Caméra (C-Stick)"):
//  * Off       : the C-Stick does nothing (DS behaviour).
//  * DsArrows  : after the original Stage::CheckInput (linker --wrap shared
//                with analog_pad.cpp) the C-Stick writes exactly what the DS
//                arrows / keys write: left/right = arrow bits 0x200/0x100
//                (held + pressed edge), down = 0x4000 (as L: camera behind
//                Mario), up = 0x8000 (as X: zoom button). Same gates as the
//                original touch path (camera flag 0x1000, no pause/message...),
//                so step size, repeat rate and sounds are the DS ones.
//  * Free: C-Stick updates yaw and pitch directly in normal camera branches.
//    No command bits are injected, so no DS arrow repeat, highlights or sounds.
//    Collision, scripted mode dispatch and camera-relative player movement stay
//    original. Recenter (L), changing zone/camera or mode releases free orbit.
//
// Old 3DS has no C-Stick sample. During ordinary gameplay its stylus strokes
// use Free orbit; menus and dialogue retain the retail pen. Without an active
// stick/stroke, automatic camera behavior remains original until free orbit
// has been engaged; recenter releases that orbit.
// The lower overlay contains only the two original 32x32 arrows. Their native
// touch regions use the same DS command bits in all three controller modes,
// independently of the C-Stick preference. Old3DS strokes start outside them.
#include <cstdint>

namespace cstick_camera {

enum class Mode : uint8_t { Off = 0, DsArrows = 1, Free = 2 };

// Raw libctru C-Stick units (irrstCstickRead; about +-146 on hardware).
constexpr int kDigitalOn = 48;    // DsArrows: direction pressed above this
constexpr int kDigitalOff = 32;   // ... released below this (hysteresis)
constexpr int kDeadZone = 16;     // Free: no motion below
constexpr int kFull = 104;        // Free: full speed / tilt rate at or above
constexpr int kDsStep = 0x400;    // DS follow-camera yaw step per update
constexpr int kMaxPitch = 0x3000; // Free: absolute pitch limit, +/-67.5 degrees

Mode mode();                 // env SM64DS_CSTICK_CAMERA (0/1/2) > settings
void set_mode(Mode m);       // explicit override (tests)

// Pure helpers (host-testable).
// Free mode: target yaw speed (s16 angle units per update, signed: + = right
// arrow direction) for a raw C-Stick x.
float free_target_speed(int x);
// One smoothing step of the yaw speed towards the target.
float free_smooth(float speed, float target);

// Called once per game update right after the original Stage::CheckInput.
void after_check_input();
// Called once after HID scan, before publishing the DS pen record. Old3DS
// gameplay strokes belong to the free camera; visible arrows own their native
// touch rectangles on either model. Returns whether the pen must be swallowed.
bool capture_stylus(bool gameplay,bool arrows_visible=false);
// Shared gates for drawing and taking the two native camera buttons. This
// reads the retail camera/input state and does not change physical key maps.
bool gameplay_camera_available(bool gameplay);

}  // namespace cstick_camera

extern "C" {
// Hooks called from the decomp (func_02009e70 / func_0200bb28, guarded by
// SM64DS_PLATFORM_PC && __3DS__). Identity when the free camera is inactive.
int sm64ds_cstick_cam_step(int ds_step);
int sm64ds_cstick_cam_follow(short* yaw, const void* cam);
int sm64ds_cstick_cam_pitch(int distance, const void* cam_mode);
// Angle-based orbit target; distance/vertical/horizontal are DS Fix12 lengths.
int sm64ds_cstick_cam_height(int distance,int original_vertical,const void* cam);
void sm64ds_cstick_cam_orbit(int distance,int* vertical,int* horizontal,const void* cam);
int sm64ds_cstick_cam_direct(int distance,int clamped_vertical,const void* cam);
int sm64ds_cstick_cam_offset(short* yaw_offset);
// Test harness: replaces the HID C-Stick sample (raw units); active=0 = HID.
void sm64ds_cstick_inject(int active, int x, int y);
// Host harness: native touch sample; active=0 restores the real HID/model.
void sm64ds_camera_touch_inject(int active,int old_model,int down,int x,int y);
// Diagnostics: updates where the C-Stick wrote command bits / analog steps.
unsigned sm64ds_cstick_applied();
}
