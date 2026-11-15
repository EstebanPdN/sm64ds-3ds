#pragma once
#include "citro3d_backend.hpp"
#include <cstdio>

namespace native_lab {
// Actual completed target transfer, with the existing complete-read/checksum
// export protocol. Earlier checksum-free export corruption remains unexplained.
inline bool capture(sm64ds::render::Citro3DBackend& renderer,C3D_RenderTarget* target,
                    std::uint8_t* pixels,FILE* log,const char* name) {
    using sm64ds::render::Error;
    C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(240,400),
        reinterpret_cast<u32*>(pixels),GX_BUFFER_DIM(240,400),
        GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if(renderer.begin(false)!=Error::none)return false;
    const auto invalidation=GSPGPU_InvalidateDataCache(pixels,288000);
    const auto ended=renderer.end();if(R_FAILED(invalidation)||ended!=Error::none)return false;
    std::uint32_t checksum=2166136261u;
    for(unsigned i=0;i<288000;++i)checksum=(checksum^pixels[i])*16777619u;
    std::fprintf(log,"readback=%s fnv1a=%08lx\n",name,static_cast<unsigned long>(checksum));std::fflush(log);
    char path[128];std::snprintf(path,sizeof(path),"sdmc:/sm64ds/%s.bgr",name);
    FILE* output=std::fopen(path,"wb");if(!output)return false;
    const bool written=std::fwrite(pixels,1,288000,output)==288000;
    const bool closed=std::fclose(output)==0;return written&&closed;
}
}
