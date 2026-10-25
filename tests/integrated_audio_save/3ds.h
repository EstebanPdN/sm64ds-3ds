// Narrow host NDSP adapter. Production output code is compiled unchanged
// after the generated-source patch; no DSP timing or hardware is emulated.
#pragma once
#include <cstddef>
#include <cstdint>
using s16 = int16_t;
using Result = int;
struct ndspWaveBuf { s16* data_pcm16 = nullptr; unsigned nsamples = 0; int status = 0; };
struct RecursiveLock {};
constexpr int NDSP_WBUF_DONE = 3, NDSP_INTERP_NONE = 0, NDSP_FORMAT_STEREO_PCM16 = 2;
constexpr float NDSP_SAMPLE_RATE = 32728.498f;
#define R_FAILED(result) ((result) < 0)
Result ndspInit();
void ndspExit();
void ndspChnReset(int);
void ndspChnSetInterp(int, int);
void ndspChnSetRate(int, float);
void ndspChnSetFormat(int, int);
void ndspChnSetMix(int, float*);
void ndspChnWaveBufAdd(int, ndspWaveBuf*);
void ndspChnWaveBufClear(int);
void* linearAlloc(size_t);
void linearFree(void*);
void DSP_FlushDataCache(void*, size_t);
void RecursiveLock_Lock(RecursiveLock*);
void RecursiveLock_Unlock(RecursiveLock*);
void svcSleepThread(long long);
