// cstick-camera: see cstick_camera.h.
#include "cstick_camera.h"
#include "enhance_settings.h"
#include "native_gameplay_camera.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __3DS__
#include <3ds.h>
#endif

extern "C" {
// Command records (Stage::CheckInput), 4 x 0x18 bytes: +4 held, +6 pressed,
// +0x16 control mode (0 Standard, 1 Touch, 2 Dual-hand).
extern unsigned char data_0209f498[];
extern unsigned char data_0209f2d8, data_0209d660, data_0209f21c, data_020a0e40;
extern unsigned char data_0209f294, data_0209f2c4, data_0209f20c;
extern unsigned char data_ov002_02111180;   // zoom touch button highlight timer
extern int data_0209caa0[];
extern int data_0209fc48;
extern unsigned data_0209b454;              // retail HUD/script hide flags
extern void* data_0209f318;                 // the Camera
}

namespace cstick_camera {
namespace {
constexpr int kRecord = 0x18;
constexpr unsigned kRight = 0x100, kLeft = 0x200, kBehind = 0x4000, kZoom = 0x8000;
constexpr float kAngleToRadians = 6.2831853071795864769f / 65536.f;

int g_env = -1;              // SM64DS_CSTICK_CAMERA: -1 unset, else forced mode
bool g_init = false, g_trace = false;
bool g_inject = false;
int g_ix = 0, g_iy = 0;
unsigned g_applied = 0;

// Per-update state (game thread only).
void* g_cam = nullptr;       // camera object the state belongs to
unsigned g_prev = 0;         // command bits the C-Stick wrote last update
int g_hdir = 0, g_vdir = 0;  // DsArrows digital directions (hysteresis)
float g_speed = 0.f;         // Free: yaw speed, s16 units/update (+ = 0x100)
bool g_free_active = false;  // Free: this update's arrow bit is the C-Stick's
int g_step = kDsStep;        // Free: magnitude handed to the camera
int g_pitch = 0;             // Free: orbit angle, same 65536-units/turn as yaw
bool g_pitch_seeded = false;
const void* g_zone = nullptr;
bool g_engaged = false;
float g_pitch_speed = 0.f;
native_gameplay_camera::Touch g_touch;
bool g_stylus_eligible=false;
bool g_touch_gameplay=false;
bool g_touch_inject=false,g_touch_old=false,g_touch_down=false;
int g_touch_x=0,g_touch_y=0;
const void* g_touch_cam=nullptr;

void init() {
    if (g_init) return;
    g_init = true;
    if (const char* e = std::getenv("SM64DS_CSTICK_CAMERA")) g_env = std::atoi(e);
    g_trace = std::getenv("SM64DS_CSTICK_TRACE") != nullptr;
}

struct Sample { int x, y; };
Sample sample() {
    if (g_inject) return {g_ix, g_iy};
    if (enhance::live().overlay_open.load(std::memory_order_relaxed)) return {0, 0};
#ifdef __3DS__
    // hidScanInput() ran for this update; on New 3DS libctru's hidInit also
    // started ir:rst and hidScanInput scans it. Old 3DS: stays {0, 0}.
    circlePosition c{};
    hidCstickRead(&c);
    return {c.dx, c.dy};
#else
    return {0, 0};
#endif
}

// Same gate as the original Stage::CheckInput main part.
bool check_input_ran() {
    const unsigned m = data_0209f2d8;
    if (m == 1) return true;
    if (m == 2 || !(data_0209caa0[2] & 0x80)) return false;
    return data_0209d660 != 0 || data_0209fc48 == 0;
}

// Digital direction with hysteresis; dominant axis only.
int digital(int prev, int v, int other) {
    const int a = std::abs(v);
    const int s = v > 0 ? 1 : -1;
    if (prev != 0 && s == prev && a >= kDigitalOff && a >= std::abs(other) / 2) return prev;
    return (a >= kDigitalOn && a > std::abs(other)) ? s : 0;
}

float curve(int v) {
    const int a = std::abs(v);
    const int dead = enhance::live().camera_deadzone.load(std::memory_order_relaxed);
    if (a <= dead) return 0.f;
    float t = float(a - dead) / float(kFull - dead);
    if (t > 1.f) t = 1.f;
    return 0.3f * t + 0.7f * t * t;   // fine control near the centre
}

void reset_motion() { g_prev = 0; g_hdir = g_vdir = 0; g_speed = 0.f; g_pitch_speed = 0.f; g_free_active = false; }

inline unsigned rd16(const unsigned char* p) { unsigned short v; std::memcpy(&v, p, 2); return v; }
inline void wr16(unsigned char* p, unsigned v) { const unsigned short s = (unsigned short)v; std::memcpy(p, &s, 2); }
}  // namespace

Mode mode() {
    init();
    if (g_env >= 0 && g_env <= 2) return Mode(g_env);
    const unsigned v = enhance::live().camera_cstick.load(std::memory_order_relaxed);
    return v <= 2 ? Mode(v) : Mode::DsArrows;
}

void set_mode(Mode m) { init(); g_env = int(m); }

bool gameplay_camera_available(bool gameplay){
 if(!gameplay||!data_0209f318||!check_input_ran()||
    (data_0209f294|data_0209f2c4|data_0209f20c|data_0209d660)||data_0209fc48||
    (data_0209b454&0x40000000u)||enhance::live().overlay_open.load(std::memory_order_relaxed))return false;
 const unsigned slot=data_020a0e40;
 if(slot>=4||slot>=data_0209f21c||data_0209f498[slot*kRecord+0x16]>2)return false;
 unsigned flags=0;std::memcpy(&flags,static_cast<unsigned char*>(data_0209f318)+0x154,4);
 return (flags&0x1000)!=0;
}

bool capture_stylus(bool gameplay,bool arrows_visible){
 bool old=false,down=false;int x=0,y=0;
#ifdef __3DS__
 static const bool old_model=[](){bool newer=true;return R_SUCCEEDED(APT_CheckNew3DS(&newer))&&!newer;}();
 old=old_model;touchPosition p{};hidTouchRead(&p);x=p.px;y=p.py;down=(hidKeysHeld()&KEY_TOUCH)!=0;
#endif
 if(g_touch_inject){old=g_touch_old;down=g_touch_down;x=g_touch_x;y=g_touch_y;}
 g_touch_gameplay=gameplay;g_touch_cam=data_0209f318;
 const bool enabled=gameplay_camera_available(gameplay);
 g_stylus_eligible=old&&enabled;
 return g_touch.sample(enabled,arrows_visible,old,down,x,y);
}

float free_target_speed(int x) {
    const float c = curve(x);
    const float rate = float(kDsStep) * enhance::live().camera_sensitivity.load(std::memory_order_relaxed) / 100.f;
    return x > 0 ? c * rate : -c * rate;
}

float free_smooth(float speed, float target) {
    const bool accelerating = (target > 0.f && speed >= 0.f && target > speed) ||
                              (target < 0.f && speed <= 0.f && target < speed);
    speed += (target - speed) * (accelerating ? 0.30f : 0.45f);
    if (target == 0.f && std::fabs(speed) < 12.f) speed = 0.f;
    return speed;
}

void after_check_input() {
    g_free_active = false;
    const Mode md = g_stylus_eligible?Mode::Free:mode();
    void* cam = data_0209f318;
    if (cam != g_cam) { g_cam = cam; reset_motion(); g_pitch = 0; g_pitch_seeded = false; g_zone = nullptr; g_engaged = false; }
    const void* zone = nullptr;
    if (cam) std::memcpy(&zone, static_cast<unsigned char*>(cam) + 0x13c, sizeof zone);
    if (zone != g_zone || md != Mode::Free) { g_zone = zone; g_pitch = 0; g_pitch_seeded = false; g_engaged = false; reset_motion(); }
    if (md != Mode::Free) { g_pitch = 0; g_pitch_seeded = false; }
    if (!cam || !check_input_ran()) { reset_motion(); return; }
    const unsigned index = data_020a0e40;
    if (index >= 4 || index >= data_0209f21c) { reset_motion(); return; }
    unsigned char* rec = data_0209f498 + index * kRecord;
    const unsigned ctl = rec[0x16];
    const Sample s = sample();

    // Gates of the original paths.
    const bool ui_free = (unsigned char)(data_0209f294 | data_0209f2c4 | data_0209f20c) == 0 && data_0209d660 == 0;
    const bool keys_ok = data_0209fc48 == 0;   // Stage::CheckInput key remap
    unsigned cam_flags;
    std::memcpy(&cam_flags, static_cast<unsigned char*>(cam) + 0x154, 4);
    // Arrows: touch buttons (Standard, Dual-hand) need camera flag 0x1000 and
    // no pause/message (Stage::CheckCameraInput); Touch mode turns with X/Y.
    const bool arrows_ok = ctl == 1 ? keys_ok : (ctl <= 2 && ui_free && (cam_flags & 0x1000));

    unsigned held = rd16(rec + 4), pressed = rd16(rec + 6);
    // Same command API as the ROM's touch arrows. Independent of the C-Stick
    // preference, with no synthetic hardware key or hidden zoom/recenter.
    const unsigned touch_pressed=g_touch.take_pressed();
    const bool touch_allowed=cam==g_touch_cam&&gameplay_camera_available(g_touch_gameplay);
    const unsigned touch_bits=touch_allowed?g_touch.buttons():0;
    if(touch_bits){
        held|=touch_bits;pressed|=touch_pressed;
        wr16(rec+4,held);wr16(rec+6,pressed);++g_applied;
    }
    if(md==Mode::Off){reset_motion();return;}
    unsigned cur = 0;
    // Player axis options. Free yaw follows the stick (right turns the view
    // right); its previous sign was reversed. DS arrows keep their mapping.
    const bool invert_x=enhance::live().camera_invert_x.load(std::memory_order_relaxed);
    const bool invert_y=enhance::live().camera_invert_y.load(std::memory_order_relaxed);
    if (md == Mode::DsArrows) {
        g_hdir = digital(g_hdir, invert_x ? -s.x : s.x, s.y);
        g_vdir = digital(g_vdir, s.y, s.x);
        if (g_hdir && arrows_ok) cur |= g_hdir > 0 ? kRight : kLeft;
        // Down = L ("behind Mario"; Dual-hand: Y), a key in every mode.
        if (g_vdir < 0 && keys_ok) cur |= kBehind;
        // Up = zoom: X in Standard mode, the zoom touch button otherwise.
        if (g_vdir > 0 && (ctl == 0 ? keys_ok : ui_free)) {
            cur |= kZoom;
            if (ctl != 0 && !(g_prev & kZoom)) data_ov002_02111180 = 0x10;
        }
    } else {   // Free: no DS command bits, pressed edges, zoom or sound path.
        const bool allowed = arrows_ok && ui_free && keys_ok && !(data_0209b454&0x40000000u) &&
            (!g_stylus_eligible||touch_allowed) &&
            !enhance::live().overlay_open.load(std::memory_order_relaxed);
        int tx=0,ty=0;if(g_stylus_eligible)g_touch.consume_stroke(tx,ty);
        const float drag_rate=128.f*enhance::live().camera_sensitivity.load(std::memory_order_relaxed)/100.f;
        const int stick_x = invert_x ? s.x : -s.x, stick_y = invert_y ? -s.y : s.y;
        g_speed = allowed ? (g_stylus_eligible?tx*drag_rate:free_smooth(g_speed, free_target_speed(stick_x))) : 0.f;
        const float vertical = curve(stick_y) * (stick_y >= 0 ? 1.f : -1.f) * float(kDsStep) *
            enhance::live().camera_sensitivity.load(std::memory_order_relaxed) / 100.f;
        g_pitch_speed = allowed ? (g_stylus_eligible?ty*drag_rate:free_smooth(g_pitch_speed, vertical)) : 0.f;
        if (allowed && (g_speed != 0.f || g_pitch_speed != 0.f)) {
            g_engaged = true;
            g_pitch += int(std::lround(g_pitch_speed));
            if (g_pitch > kMaxPitch) g_pitch = kMaxPitch;
            if (g_pitch < -kMaxPitch) g_pitch = -kMaxPitch;
            ++g_applied;
        }
        if (pressed & kBehind) { g_engaged = false; g_pitch = 0; g_pitch_seeded = false; reset_motion(); }
        g_step = int(std::lround(g_speed));
        g_free_active = allowed && g_engaged && !(held & (kBehind | kRight | kLeft));
    }
    if (cur) {
        held |= cur;
        pressed |= cur & ~g_prev;
        // Stage::CheckInput: both arrows held = behind Mario (not Dual-hand).
        if (keys_ok && ctl != 2 && (held & 0x300) == 0x300) {
            held |= kBehind;
            if (pressed & 0x300) pressed |= kBehind;
        }
        wr16(rec + 4, held);
        wr16(rec + 6, pressed);
        ++g_applied;
    }
    if (pressed & kBehind) { g_pitch = 0; g_pitch_seeded = false; }   // L / C-Stick down: back to the DS view
    if (g_trace && (cur != g_prev || s.x || s.y)) {
        static int px = 1 << 20, py, pp;
        if (s.x != px || s.y != py || g_pitch != pp || cur != g_prev)
            std::printf("C-Stick camera: raw=%d,%d mode=%d ctl=%u bits=%04x speed=%.1f pitch=%d\n", s.x, s.y,
                        int(md), ctl, cur, double(g_speed), g_pitch);
        px = s.x; py = s.y; pp = g_pitch;
    }
    g_prev = cur;
}

}  // namespace cstick_camera

extern "C" {
int sm64ds_cstick_cam_step(int ds_step) {
    return ds_step; // Legacy DS arrows retain their original step.
}

namespace {
bool pitch_active(const void* cam) {
    return cam && cam == cstick_camera::g_cam && cstick_camera::g_free_active;
}
void seed_pitch(int distance, int vertical) {
    using namespace cstick_camera;
    if(g_pitch_seeded || distance <= 0)return;
    const float ratio = std::fmax(-1.f,std::fmin(1.f,float(vertical)/float(distance)));
    g_pitch += int(std::lround(std::asin(ratio)/kAngleToRadians));
    g_pitch = g_pitch > kMaxPitch ? kMaxPitch : g_pitch < -kMaxPitch ? -kMaxPitch : g_pitch;
    g_pitch_seeded = true;
}
}

int sm64ds_cstick_cam_height(int distance, int original_vertical, const void* cam) {
    using namespace cstick_camera;
    if(!pitch_active(cam) || distance <= 0)return original_vertical;
    seed_pitch(distance,original_vertical);
    return int(std::lround(float(distance)*std::sin(float(g_pitch)*kAngleToRadians)));
}

// Retained for older external diagnostic harnesses; new source hooks pass the
// original vertical component and use an absolute pitch target instead.
int sm64ds_cstick_cam_pitch(int distance, const void* cam) {
    return sm64ds_cstick_cam_height(distance,0,cam);
}

void sm64ds_cstick_cam_orbit(int distance, int* vertical, int* horizontal, const void* cam) {
    using namespace cstick_camera;
    if(!pitch_active(cam) || distance <= 0 || !vertical || !horizontal)return;
    seed_pitch(distance,*vertical);
    *vertical = int(std::lround(float(distance)*std::sin(float(g_pitch)*kAngleToRadians)));
    *horizontal = int(std::lround(float(distance)*std::cos(float(g_pitch)*kAngleToRadians)));
}

int sm64ds_cstick_cam_direct(int distance, int clamped_vertical, const void* cam) {
    using namespace cstick_camera;
    if(!pitch_active(cam) || distance <= 0)return 0;
    // Read back the game's mode/wall/floor/ceiling-limited target, preventing
    // stick wind-up at a bound. Do not add a second pitch-only easing filter.
    const int desired=sm64ds_cstick_cam_height(distance,clamped_vertical,cam);
    if(std::abs(desired-clamped_vertical)>1){
        const float ratio=std::fmax(-1.f,std::fmin(1.f,float(clamped_vertical)/float(distance)));
        g_pitch=int(std::lround(std::asin(ratio)/kAngleToRadians));
    }
    return 1;
}

int sm64ds_cstick_cam_offset(short* yaw_offset) {
    using namespace cstick_camera;
    if (!g_free_active) return 0;
    *yaw_offset = short(*yaw_offset + g_step);
    return 1;
}

// Called only from the normal follow-camera branch, after scripted/special
// modes dispatch, before its automatic/DS-key rotation. Collision remains below.
int sm64ds_cstick_cam_follow(short* yaw, const void* cam) {
    using namespace cstick_camera;
    if (!g_free_active || cam != g_cam) return 0;
    *yaw = short(*yaw + g_step);
    return 1;
}

void sm64ds_cstick_inject(int active, int x, int y) {
    cstick_camera::g_inject = active != 0;
    cstick_camera::g_ix = x;
    cstick_camera::g_iy = y;
}

void sm64ds_camera_touch_inject(int active,int old_model,int down,int x,int y){
    using namespace cstick_camera;
    g_touch_inject=active!=0;g_touch_old=old_model!=0;g_touch_down=down!=0;
    g_touch_x=x;g_touch_y=y;
}

unsigned sm64ds_cstick_applied() { return cstick_camera::g_applied; }
}
