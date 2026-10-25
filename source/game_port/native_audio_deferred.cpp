#include "sm64ds/native_platform.hpp"

// Explicit temporary policy for the native integration build: no sound heap,
// ARM7 sound driver, or playback is initialized. These narrowly owned entry
// points preserve requested game sound settings and return failure/no handle
// for playback, instead of pretending the DS firmware is download-played.
// Remove this object when the real native audio backend is connected.
namespace {
void deferred() {
    sm64ds::native_platform_defer_audio("native audio: request deferred; playback backend unavailable");
}
}

extern "C" {
extern std::uint8_t data_0209d4b4;
extern std::uint8_t data_0209b478;
extern std::uint8_t data_0209b47c;
extern std::uint8_t data_0208e428;

void SetSoundMode(int mode) {
    data_0209d4b4 = static_cast<std::uint8_t>(mode);
    deferred();
}

void func_02011f7c(std::uintptr_t voice_group) {
    data_0209b478 = static_cast<std::uint8_t>(voice_group);
    deferred();
}

void _ZN5Sound21ResetPlayerVoiceGroupEv() {
    data_0209b478 = 0;
    deferred();
}

void _ZN5Sound19LoadGroupAndSetBankEii(int group, int bank) {
    data_0208e428 = static_cast<std::uint8_t>(bank);
    data_0209b47c = static_cast<std::uint8_t>(group);
    data_0209b478 = 0;
    deferred();
}

// The original returns the group load result. Zero is failure; no group has
// been loaded into an audio heap by this temporary policy.
int func_02011ee4(void*) { deferred(); return 0; }

// Sequence start, stop and volume service boundaries. The original higher
// level music/secret state machines still run; these do not manufacture a
// successful voice, sound handle, or completion callback.
int func_02011dcc(void*, int) { deferred(); return 0; }
int func_0204fa2c(void*) { deferred(); return 0; }
void func_0204f8cc(std::uintptr_t, int, int) { deferred(); }

// The title's OV007 sound callbacks and Player_PlaySoundEffect enter the
// sequence-archive SDK directly, bypassing Sound::Play. Its original lookup
// dereferences data_020a5bb8, owned by the audio initialization deliberately
// deferred above. Keep the complete public load/start boundary under the same
// no-backend policy: zero is the SDK's failure result, and no handle, archive,
// heap or callback is manufactured. The original callers retain their state.
int func_020518dc(std::uintptr_t, void*) { deferred(); return 0; }
int func_02051f1c(void*, std::uint32_t, int) { deferred(); return 0; }
int func_02051e60(void*, int, int, int, std::uint32_t, int) {
    deferred(); return 0;
}

// Sound::Play's original first operation dereferences sequence-bank metadata
// that only the uninitialized sound backend owns. Defer at its public edge.
std::uint32_t _ZN5Sound4PlayEjjRK7Vector3(std::uint32_t, std::uint32_t, const void*) {
    deferred();
    return 0;
}
std::uint32_t _ZN5Sound6Play2DEjj(std::uint32_t, std::uint32_t) {
    deferred();
    return 0;
}
int _ZN5Sound8PlayLongEjjjRK7Vector3s(std::uint32_t, std::uint32_t,
                                   std::uint32_t, const void*, std::int16_t) {
    deferred();
    return 0;
}
}  // extern "C"
