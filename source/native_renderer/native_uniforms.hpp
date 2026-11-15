#pragma once
// Configure between GPU frames. Only the float dirty-range search changes;
// both modes retain the detected SDK1.7.1 state and restoration semantics.
void sm64ds_native_uniform_ranges(bool enabled) noexcept;
// BUILD134 cost cut (perf_switch.hpp), off by default = the SDK's emission.
// On: a program's constant table and the boolean word are not sent again
// while the GPU provably still holds them. The SDK re-sends both on every
// C3D_BindProgram, including a bind of the program already bound (74 command
// words per pass and per batch/rigid program switch here). "Still holds":
// this file emitted exactly that table / word last, no float range emitted
// since overlapped the table's rows, and the SDK has not declared the GPU
// state lost (applet restore). Each register keeps the value the SDK would
// have written; the SDK's dirty-flag side effects are reproduced.
void sm64ds_native_uniform_residency(bool enabled) noexcept;
// The next update sends constants and booleans again (start of an image).
void sm64ds_native_uniform_forget() noexcept;
