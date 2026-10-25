#pragma once
// The first numbered build follows 22 distinct archived ELF candidates.
// tools/build_integrated_native.py reserves the next number before building;
// interrupted/failed builds may leave gaps, but numbers are never reused.
#define SM64DS_BUILD_NUMBER 24
#define SM64DS_BUILD_STRING_INNER(value) #value
#define SM64DS_BUILD_STRING(value) SM64DS_BUILD_STRING_INNER(value)
namespace sm64ds_build {
inline constexpr unsigned number = SM64DS_BUILD_NUMBER;
inline constexpr char label[] = "BUILD " SM64DS_BUILD_STRING(SM64DS_BUILD_NUMBER);
}
