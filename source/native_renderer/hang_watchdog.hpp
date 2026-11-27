#pragma once
// BUILD137 hang watchdog (always on). A high-priority thread on the
// application core checks the game's heartbeats every 250 ms. When the game,
// the picture or the system (HOME/POWER) handling stops advancing it writes
// sdmc:/3ds/sm64ds/cuelgues/cuelgue-NN.txt (what stopped, every thread's
// last stage, breadcrumbs, audio and GPU state), the unwritten part of the
// log (cuelgue-NN-log.txt) and the game's global memory
// (cuelgue-NN-memoria.bin), lights the notification LED red, and, when HOME
// or POWER stays unanswered for kHomeExitSeconds, closes the game to the HOME
// Menu instead of leaving the console to be switched off.
// It never takes the stdio, newlib or heap locks a stuck thread may hold:
// files are written through FS:USER directly, text with snprintf (integers).
// Every function here is lock-free and callable from any thread.
#include <cstdint>
extern "C" {
// Stages: where a thread was last seen (see kStageNames in hang_watchdog.cpp).
enum Sm64dsWatchStage : std::uint8_t {
    SM64DS_WATCH_NONE=0,
    // Host (main) thread, between game updates.
    SM64DS_WATCH_HOST_HOOK,SM64DS_WATCH_HOST_APT,SM64DS_WATCH_HOST_PRESENT,SM64DS_WATCH_HOST_AUDIO,
    SM64DS_WATCH_HOST_CADENCE,SM64DS_WATCH_HOST_LIFECYCLE,SM64DS_WATCH_HOST_REPORT,
    // Game thread (the DS game's update).
    SM64DS_WATCH_GAME_UPDATE,SM64DS_WATCH_GAME_WAIT_WORKER,
    // Render worker.
    SM64DS_WATCH_WORKER_IDLE,SM64DS_WATCH_WORKER_JOB,SM64DS_WATCH_WORKER_FRAME_BEGIN,SM64DS_WATCH_WORKER_DRAW,
    SM64DS_WATCH_WORKER_SUBMIT,SM64DS_WATCH_WORKER_GPU_WAIT,SM64DS_WATCH_WORKER_PRESENT_WAIT,
    SM64DS_WATCH_STAGE_COUNT
};
// Main thread, once the render worker exists. Idempotent.
void sm64ds_watchdog_start(void);
// Game thread, at the start of every original game update.
void sm64ds_watchdog_game_update(unsigned scene);
// Render worker: an image job finished (presented or refused).
void sm64ds_watchdog_presented(void);
void sm64ds_watchdog_host_stage(unsigned stage);
void sm64ds_watchdog_game_stage(unsigned stage);
void sm64ds_watchdog_worker_stage(unsigned stage);
// HOME/sleep suspension and test-report pauses: no detection meanwhile.
void sm64ds_watchdog_pause(int paused);
// Test reports and developer dumps: a pause that becomes a hang after 60 s.
void sm64ds_watchdog_report_pause(int paused);
// Close to the HOME Menu (APT close, then the raw process exit). No return.
void sm64ds_watchdog_emergency_exit(void);
// Breadcrumb ring (64 entries). tag must be a string literal.
void sm64ds_watchdog_crumb(const char* tag,std::uint32_t a,std::uint32_t b);
// A GPU wait gave up (bounded gxCmdQueueWait / present fence).
void sm64ds_watchdog_gpu_stall(std::uint32_t milliseconds);
// The GPU must not be used: a GX wait gave up (sticky) or the render worker
// was still running when HOME/sleep suspended the game (until it resumes).
int sm64ds_gpu_unusable(void);
int sm64ds_gpu_hung(void);
void sm64ds_gpu_block(int blocked);
// Menu state of the title overlay (state machine value), for the report.
void sm64ds_watchdog_menu_state(int state);
// Hangs detected in this boot (the test mode records them).
unsigned sm64ds_watchdog_hangs(void);
}
