#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// Existing renderer readback APIs. Caller drains the presentation worker first.
void v3ds_gpu_candidate_geometry(uint32_t*,uint32_t*);
void v3ds_gpu_candidate_panel(unsigned,uint32_t*);
void v3ds_read_bottom(uint32_t*);
bool v3ds_diagnostic_final_top(uint32_t*);
bool v3ds_diagnostic_final_bottom(uint32_t*);
namespace native_diagnostics {
// Renderer readbacks inside one dump folder: render.bin, upper.bin, lower.bin.
inline bool capture_render(const char* directory){
    auto* left=static_cast<uint32_t*>(std::calloc(400*240,sizeof(uint32_t)));
    auto* right=static_cast<uint32_t*>(std::calloc(256*192,sizeof(uint32_t)));
    if(!left||!right){std::free(left);std::free(right);return false;}
    char path[224];std::snprintf(path,sizeof path,"%s/render.bin",directory);
    FILE* f=std::fopen(path,"wb");
    if(!f){std::free(left);std::free(right);return false;}
    bool ok=true;
    // Fixed little-endian layout: 16-byte header, left/right/panel DS ARGB32
    // arrays 256x192, then displayed bottom ARGB32 320x240.
    const uint32_t header[]={0x34524d53,1,256,192};
    if(ok)ok=std::fwrite(header,sizeof header,1,f)==1;
    v3ds_gpu_candidate_geometry(left,right);
    if(ok)ok=std::fwrite(left,256*192*4,1,f)==1;
    if(ok)ok=std::fwrite(right,256*192*4,1,f)==1;
    std::memset(left,0,320*240*4);v3ds_gpu_candidate_panel(2,left);
    if(ok)ok=std::fwrite(left,256*192*4,1,f)==1;
    std::memset(left,0,320*240*4);v3ds_read_bottom(left);
    if(ok)ok=std::fwrite(left,320*240*4,1,f)==1;
    if(f && std::fclose(f)!=0)ok=false;
    // Keep the established .render ABI and add an explicitly sized upper
    // companion v2: composed panel downsampled256x192, then synchronized
    // final400x240 GPU target. v1 CPU framebuffer reads can be stale.
    // Both allocations are bounded and released on every success/error path.
    if(ok){
        std::snprintf(path,sizeof path,"%s/upper.bin",directory);f=std::fopen(path,"wb");
        if(!f)ok=false;
        else{
            const uint32_t upper_header[]={0x31554d53,2,400,240};
            ok=std::fwrite(upper_header,sizeof upper_header,1,f)==1;
            std::memset(left,0,400*240*4);v3ds_gpu_candidate_panel(0,left);
            if(ok)ok=std::fwrite(left,256*192*4,1,f)==1;
            std::memset(left,0,400*240*4);
            if(ok)ok=v3ds_diagnostic_final_top(left);
            if(ok)ok=std::fwrite(left,400*240*4,1,f)==1;
            if(std::fclose(f)!=0)ok=false;
        }
    }
    // Preserve .render v1 (including the LCD/progress framebuffer) and record
    // an explicitly sized game-target companion for native-pixel inspection.
    if(ok){
        std::snprintf(path,sizeof path,"%s/lower.bin",directory);f=std::fopen(path,"wb");
        if(!f)ok=false;
        else{
            const uint32_t lower_header[]={0x314c4d53,1,320,240};
            ok=std::fwrite(lower_header,sizeof lower_header,1,f)==1;
            if(ok)ok=v3ds_diagnostic_final_bottom(left);
            if(ok)ok=std::fwrite(left,320*240*4,1,f)==1;
            if(std::fclose(f)!=0)ok=false;
        }
    }
    std::free(left);std::free(right);
    std::printf("Render diagnostic path=%s/render.bin upper=%u complete=%u\n",directory,unsigned(ok),unsigned(ok));return ok;
}
}
