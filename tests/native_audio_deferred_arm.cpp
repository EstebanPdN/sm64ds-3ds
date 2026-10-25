#include "sm64ds/native_platform.hpp"

// Test-owned storage only. The production audio policy never seats a SDAT
// root or manufactures sequence metadata in these records.
extern "C" {
std::uint8_t data_0209d4b4, data_0209b478, data_0209b47c, data_0208e428;
std::uint8_t data_0209b480;
void* data_020a5bb8;
std::uint32_t* data_ov007_02104bbc;
std::uint32_t data_ov007_02103134[128];
std::uint32_t audio_deferred_count;
const char* audio_deferred_operation;

int func_02051e60(void*, int, int, int, std::uint32_t, int);
int func_02051f1c(void*, std::uint32_t, int);
int func_020518dc(std::uintptr_t, void*);

// This actual ARM11 caller forces arguments five and six onto the stack.
// The Python observer checks the delivered words before the provider runs.
int audio_six_argument_caller(void* handle, const std::uint32_t* values) {
    return func_02051e60(handle, static_cast<int>(values[0]),
        static_cast<int>(values[1]), static_cast<int>(values[2]), values[3],
        static_cast<int>(values[4]));
}
void audio_test_entry() {}
}

namespace sm64ds {
void native_platform_defer_audio(const char* operation) {
    ++audio_deferred_count;
    audio_deferred_operation = operation;
}
}
