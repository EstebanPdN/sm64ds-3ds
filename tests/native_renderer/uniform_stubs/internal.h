#pragma once
#include "citro3d.h"
#include <stddef.h>
// Host oracle fields consumed by drawElements.c; not a private ABI declaration.
typedef struct {struct {u32 base_paddr;} bufInfo;u32 flags;} C3D_Context;
enum {C3DiF_DrawUsed=2};
#ifdef __cplusplus
extern "C" {
#endif
C3D_Context* C3Di_GetContext(void);
void C3Di_UpdateContext(void);
u32 osConvertVirtToPhys(const void*);
#ifdef __cplusplus
}
#endif
