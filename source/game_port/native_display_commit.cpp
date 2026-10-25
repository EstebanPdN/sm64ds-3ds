// Original func_02019144 with only bus reads/writes routed to native_video.
#include "sm64ds/native_video.hpp"
#include <cstdint>
extern "C" {
using sm64ds::native_video_read;
using sm64ds::native_video_write;


typedef struct UnkObj UnkObj;
typedef struct UnkVt {
    void *pad0;
    void *pad4;
    int (*func8)(UnkObj *);
} UnkVt;
struct UnkObj {
    UnkVt *vt;
};

extern UnkObj *data_0209d4a8;
extern unsigned char data_0209d45c;
extern unsigned char data_0209d454;
extern short data_0209d468;
extern short data_0209d46c;
extern short data_0209d4a4;
extern short data_0209d4a0;
extern short data_0209d49c;
extern short data_0209d478;
extern short data_0209d48c;
extern short data_0209d490;
extern short data_0209d494;
extern short data_0209d498;
extern short data_0209d484;
extern short data_0209d480;
extern short data_0209d47c;
extern short data_0209d470;
extern short data_0209d474;
extern short data_0209d488;

void _ZN3OAM5FlushEv(void);
void _ZN3OAM4LoadEv(void);


void func_02019144(void)
{
    UnkObj *p = data_0209d4a8;
    if (p != 0) {
        if (p->vt->func8(p) == 0)
            return;
    }
    _ZN3OAM5FlushEv();
    _ZN3OAM4LoadEv();
    native_video_write(0x4000000,(uint32_t(native_video_read(0x4000000,4)) & ~0x1f00) | (data_0209d45c << 8),4);
    native_video_write(0x4001000,(uint32_t(native_video_read(0x4001000,4)) & ~0x1f00) | (data_0209d454 << 8),4);
    if (!(uint32_t(native_video_read(0x4000000,4)) & 8)) {
        native_video_write(0x4000010,(data_0209d468 & 0x1ff) | (0x1ff0000 & (data_0209d46c << 16)),4);
    } else {
        native_video_write(0x04000010,uint32_t(data_0209d468),4);
    }
    native_video_write(0x4000014,(data_0209d4a4 & 0x1ff) | (0x1ff0000 & (data_0209d4a0 << 16)),4);
    native_video_write(0x4000018,(data_0209d49c & 0x1ff) | (0x1ff0000 & (data_0209d478 << 16)),4);
    native_video_write(0x400001c,(data_0209d48c & 0x1ff) | (0x1ff0000 & (data_0209d490 << 16)),4);

    native_video_write(0x4001010,(data_0209d494 & 0x1ff) | (0x1ff0000 & (data_0209d498 << 16)),4);
    native_video_write(0x4001014,(data_0209d484 & 0x1ff) | (0x1ff0000 & (data_0209d480 << 16)),4);
    native_video_write(0x4001018,(data_0209d47c & 0x1ff) | (0x1ff0000 & (data_0209d470 << 16)),4);
    native_video_write(0x400101c,(data_0209d474 & 0x1ff) | (0x1ff0000 & (data_0209d488 << 16)),4);
}

} // extern C
