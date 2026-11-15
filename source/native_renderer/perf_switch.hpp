#pragma once
// BUILD134 console A/B switch for the cost cuts that keep an old path.
// sdmc:/3ds/sm64ds/sin-recortes.txt present = the BUILD 133 paths (slow,
// same image); absent = the cuts. Read once. Every cut is exact (the image
// does not change); the switch exists to measure them on the console and to
// rule one out if a hardware run ever disagrees with the emulator.
// Host tests: always on, or forced with set_cuts().
#ifdef __3DS__
#include <sys/stat.h>
#endif

namespace sm64ds::perf {
inline int& cuts_override() noexcept {static int value=-1;return value;}
inline void set_cuts(int value) noexcept {cuts_override()=value;} // -1 = default
inline bool cuts() noexcept {
    static const bool from_file=[]() noexcept {
#ifdef __3DS__
        struct stat st;return stat("sdmc:/3ds/sm64ds/sin-recortes.txt",&st)!=0;
#else
        return true;
#endif
    }();
    const int forced=cuts_override();
    return forced<0?from_file:forced!=0;
}
}
