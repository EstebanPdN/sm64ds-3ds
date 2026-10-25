// CPU texture-binding tests consume the installed SDK's public GPU structs
// without pulling ARM-only TLS/synchronization intrinsics into the host.
#pragma once
#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/gpu/enums.h>
#include <3ds/gpu/gx.h>
#include <3ds/gpu/shbin.h>
#include <3ds/gpu/shaderProgram.h>
#include <3ds/services/gspgpu.h>
#include <3ds/gfx.h>
#include <3ds/allocator/linear.h>
