#pragma once
// Local diagnostic capture, enabled by the saved Developer setting.
// No process RAM is changed or restored. A dump freezes the game at its frame
// hook after the presentation worker drains; GPU/OS/DSP state is not atomic.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef __3DS__
#include <3ds.h>
#include "frame_profile.h"
#include "enhance_settings.h"
#include "dump_transfer.hpp"
#include "dump_screen.hpp"
#include "profile_probe.hpp"
#include "hitch_capture.hpp"
#include "dump_folder.hpp"
#include "sd_writer.hpp"
#include <atomic>
#include <ctime>
#include <malloc.h>
#endif

namespace native_diagnostics {
// Version 2: plain device memory (VRAM, DSP shared windows) carries payload
// with reason 4; register blocks and other I/O mappings are never read.
struct Header {
    char magic[8]={'S','M','6','4','V','M','1',0};
    uint32_t version=2,header_bytes=64,word_bytes=4,region_bytes=24;
    uint64_t begin_tick=0,end_tick=0;
    uint32_t scene=0,epoch=0,regions=0,complete=0;
    uint32_t scan_end=0x40000000,non_atomic_devices=1;
};
struct Region {uint32_t address,size,perm,state,payload_bytes,reason;};
static_assert(sizeof(Header)==64 && sizeof(Region)==24,"Diagnostic disk format");
constexpr uint32_t region_device_payload=4;
// Mapped memory without read side effects: both VRAM banks and the DSP
// shared-memory windows. Everything else in MEMSTATE_IO stays excluded.
inline bool plain_device_memory(uint32_t address,uint32_t size){
    const uint64_t end=uint64_t(address)+size;
    return (address>=0x1F000000u&&end<=0x1F600000u)||(address>=0x1FF00000u&&end<=0x1FF80000u);
}
// Physical LCD capture (.lcd): what the display controllers scan out, read
// from GSP before the dump progress screen replaces the lower image.
struct LcdHeader {
    char magic[8]={'S','M','6','4','L','C','D','1'};
    uint32_t version=1,header_bytes=64,record_bytes=64,records=3;
    uint64_t tick=0;
    uint32_t scene=0,epoch=0,register_blocks=4,register_block_bytes=256;
    uint32_t reserved[4]{};
};
struct LcdRecord {
    uint32_t screen=0,eye=0,valid=0,physical=0,virtual_address=0,format=0,select=0,stride=0;
    uint32_t rows=0,columns=0,payload_bytes=0,status=0;
    uint32_t reserved[4]{};
};
static_assert(sizeof(LcdHeader)==64 && sizeof(LcdRecord)==64,"LCD disk format");
// GSP register offsets (physical 0x10000000 + offset): GPU id/memory fill,
// PDC0 top LCD, PDC1 bottom LCD, display transfer engine.
constexpr uint32_t lcd_register_blocks[4]={0x400000,0x400400,0x400500,0x400C00};
struct TraceRecord {
    uint64_t tick,frame;
    uint32_t epoch,scene,held,reserved;
    uint64_t overflow_mask;
    uint32_t delta[48];
};
static_assert(sizeof(TraceRecord)==232,"Trace disk format");
struct TraceHeader {
    char magic[8]={'S','M','6','4','P','R','2',0};
    uint32_t version=2,record_bytes=232,count=0,overwritten=0;
    uint64_t tick_frequency=268111856;
};
static_assert(sizeof(TraceHeader)==32,"Trace header format");
struct Anchor {const char* name;const void* address;};
#ifdef __3DS__
// Developer > Full RAM dumps (default on). Partial dumps never need it.
inline bool enabled(){return enhance::live().developer_dumps.load(std::memory_order_relaxed);}
// Periodic SD telemetry lines cost SD writes every second. Only diagnostic
// acquisitions enable them through their marker file; dumps alone never do.
inline std::atomic<bool> verbose_flag{false};
inline bool verbose(){return verbose_flag.load(std::memory_order_relaxed);}
inline void configure_verbose(){
    bool on=false;
    if(FILE* f=std::fopen("sdmc:/3ds/sm64ds/native-diagnostics.enabled","rb")){
        char line[64]{};on=std::fgets(line,sizeof line,f)&&!std::strcmp(line,"SM64DS_NATIVE_DIAGNOSTICS=1\n");std::fclose(f);
    }
    verbose_flag.store(on,std::memory_order_relaxed);
}
inline bool requested=false;          // full dump: screens, RAM, VRAM
inline bool partial_requested=false;  // screens, VRAM, registers; no RAM
inline bool delete_requested=false;
inline char last_capture_stem[dump_folder::max_path]{}; // folder of the last dump
struct Trace {
    std::array<TraceRecord,4096> records{};
    std::array<uint64_t,48> previous{};
    uint64_t written=0;
    uint32_t previous_audio=0;
    bool hitch_checked=false,hitch_enabled=false;
    profile_probe::HitchCapture hitch;
    void clear(){written=0;for(unsigned i=0;i<48;++i)previous[i]=sm64ds_frame_prof[i];
        previous_audio=profile_probe::audio_ticks.load(std::memory_order_relaxed);hitch.reset_interval();}
    // Called only after wait_idle, when the worker's counter slots are stable.
    void record(uint64_t frame,unsigned epoch,unsigned scene,uint32_t context=0){
        if(!hitch_checked){
            hitch_checked=true;
            if(FILE* f=std::fopen("sdmc:/3ds/sm64ds/native-hitch-capture.enabled","rb")){
                char marker[64]{};const auto n=std::fread(marker,1,sizeof marker,f);
                const bool read_ok=!std::ferror(f),closed=std::fclose(f)==0;
                constexpr char expected[]="SM64DS_HITCH_CAPTURE_V1\n";
                hitch_enabled=read_ok&&closed&&n==sizeof(expected)-1&&!std::memcmp(marker,expected,n);
            }
        }
        auto& out=records[written++%records.size()];out={};
        out.tick=svcGetSystemTick();out.frame=frame;out.epoch=epoch;out.scene=scene;out.held=hidKeysHeld();out.reserved=context;
        for(unsigned i=0;i<48;++i){const uint64_t value=sm64ds_frame_prof[i],delta=value-previous[i];previous[i]=value;
            if(delta>UINT32_MAX){out.delta[i]=UINT32_MAX;out.overflow_mask|=uint64_t(1)<<i;}
            else out.delta[i]=uint32_t(delta);}
        const uint32_t audio=profile_probe::audio_ticks.load(std::memory_order_relaxed);
        out.delta[45]=audio-previous_audio;previous_audio=audio;
        const unsigned target_fps=enhance::live().fps_lock.load(std::memory_order_relaxed)==enhance::kFpsLock30?30:60;
        if(enabled() && hitch_enabled && hitch.observe(out.tick,scene==3 && (context&0xffff0007u)==0x534d0000u,target_fps)){
            requested=true;
            std::printf("HITCH captureRequested=%u frame=%llu epoch=%u ticks=%llu context=%08x thresholdMs=%u sustainedSamples=3 budget=2\n",
                hitch.captures(),(unsigned long long)frame,epoch,(unsigned long long)out.tick,context,profile_probe::HitchCapture::threshold_ms(target_fps));
        }
    }
    template<class Writer> bool save(Writer& w,const char* path){
        if(!w.open(path))return false;
        TraceHeader h;h.count=written<records.size()?uint32_t(written):uint32_t(records.size());
        h.overwritten=written>records.size()?uint32_t(written-records.size()):0;
        bool ok=w.write(&h,sizeof h);
        const uint64_t first=written-h.count;
        for(uint64_t i=first;ok&&i<written;++i)ok=w.write(&records[i%records.size()],sizeof(TraceRecord));
        return w.close()&&ok;
    }
};
inline Trace trace;
alignas(32) inline std::array<uint32_t,transfer_bytes/4> transfer_staging{};
inline SdWriter make_writer(){return SdWriter(transfer_staging.data(),transfer_bytes);}
inline bool join(char* out,size_t cap,const char* dir,const char* name){return std::snprintf(out,cap,"%s/%s",dir,name)<int(cap);}
inline bool readable_span(uint32_t address,uint32_t size){
    MemInfo memory{};PageInfo page{};
    if(!address||!size||R_FAILED(svcQueryMemory(&memory,&page,address)))return false;
    return (memory.perm&MEMPERM_READ)&&uint64_t(memory.base_addr)+memory.size>=uint64_t(address)+size;
}
// Scanout addresses are physical. Only mappings this process can read count.
inline uint32_t lcd_virtual(uint32_t physical,uint32_t size){
    const auto inside=[&](uint32_t base,uint32_t length){return physical>=base&&uint64_t(physical)+size<=uint64_t(base)+length;};
    uint32_t candidates[2]{};
    if(inside(OS_VRAM_PADDR,OS_VRAM_SIZE))candidates[0]=OS_VRAM_VADDR+(physical-OS_VRAM_PADDR);
    else if(inside(OS_FCRAM_PADDR,OS_FCRAM_SIZE)){
        candidates[0]=OS_FCRAM_VADDR+(physical-OS_FCRAM_PADDR);
        if(inside(OS_OLD_FCRAM_PADDR,OS_OLD_FCRAM_SIZE))candidates[1]=OS_OLD_FCRAM_VADDR+(physical-OS_OLD_FCRAM_PADDR);
    }
    else if(inside(OS_QTMRAM_PADDR,OS_QTMRAM_SIZE))candidates[0]=OS_QTMRAM_VADDR+(physical-OS_QTMRAM_PADDR);
    for(auto v:candidates)if(v&&readable_span(v,size))return v;
    return 0;
}
// One scanout pixel in screen orientation (x right, y down) as B,G,R.
inline void scanout_bgr(const uint8_t* fb,const LcdRecord& r,unsigned x,unsigned y,uint8_t* out){
    const unsigned format=r.format&7,bpp=format==0?4:format==1?3:2;
    const uint8_t* p=fb+size_t(x)*r.stride+size_t(r.rows-1-y)*bpp;
    if(format==0){out[0]=p[1];out[1]=p[2];out[2]=p[3];return;}
    if(format==1){out[0]=p[0];out[1]=p[1];out[2]=p[2];return;}
    const unsigned v=p[0]|p[1]<<8;unsigned red,green,blue;
    if(format==2){red=(v>>11)*255/31;green=((v>>5)&63)*255/63;blue=(v&31)*255/31;}
    else if(format==3){red=(v>>11)*255/31;green=((v>>6)&31)*255/31;blue=((v>>1)&31)*255/31;}
    else{red=(v>>12)*17;green=((v>>8)&15)*17;blue=((v>>4)&15)*17;}
    out[0]=uint8_t(blue);out[1]=uint8_t(green);out[2]=uint8_t(red);
}
inline bool write_bmp(const char* path,const uint8_t* fb,const LcdRecord& r){
    auto w=make_writer();
    const unsigned row_bytes=dump_folder::bmp_row_bytes(r.columns);
    if(!w.open(path,54+uint64_t(row_bytes)*r.rows))return false;
    uint8_t header[54];dump_folder::bmp_header(header,r.columns,r.rows);
    bool ok=w.write(header,sizeof header);
    uint8_t row[800*3+4]{};
    for(unsigned b=0;ok&&b<r.rows;++b){              // BMP rows are bottom-up
        const unsigned y=r.rows-1-b;
        for(unsigned x=0;x<r.columns;++x)scanout_bgr(fb,r,x,y,row+x*3);
        ok=w.write(row,row_bytes);
    }
    return w.close()&&ok;
}
// Physical screens: what the display controllers scan out right now, read
// through GSP after the worker drains and before any dump painting.
// lcd.bin keeps the raw scanout and GSP registers; the BMPs are for people.
// Record status: 0 ok, 1 unreadable registers, 2 unmapped/unsafe address,
// 3 implausible geometry, 4 copy/write failure.
inline bool capture_screens(const char* dir,unsigned scene,unsigned epoch){
    LcdHeader header;header.tick=svcGetSystemTick();header.scene=scene;header.epoch=epoch;
    std::array<uint32_t,64*4> registers{};bool ok=true;
    for(unsigned b=0;b<4;++b)for(unsigned part=0;part<2;++part)
        if(R_FAILED(GSPGPU_ReadHWRegs(lcd_register_blocks[b]+part*128,registers.data()+b*64+part*32,128)))ok=false;
    LcdRecord records[3];
    for(unsigned i=0;i<3;++i){
        auto& r=records[i];r.screen=i<2?0:1;r.eye=i==1;
        const uint32_t* pdc=registers.data()+(r.screen?2:1)*64;
        if(!ok){r.status=1;continue;}
        r.format=pdc[0x70/4];r.select=pdc[0x78/4];r.stride=pdc[0x90/4];
        const uint32_t size=pdc[0x5C/4];
        r.rows=size&0xffff;r.columns=size>>16;
        if(!r.rows||!r.columns){r.rows=240;r.columns=r.screen?320:400;}
        const unsigned active=r.select&1;
        r.physical=r.eye?pdc[(active?0x98:0x94)/4]:pdc[(active?0x6C:0x68)/4];
        const unsigned bpp=(r.format&7)==0?4:(r.format&7)==1?3:2;
        if(r.rows>240||r.columns>800||r.stride<r.rows*bpp||r.stride>4096||(r.stride&3)){r.status=3;continue;}
        r.payload_bytes=r.stride*r.columns;
        r.virtual_address=lcd_virtual(r.physical,r.payload_bytes);
        if(!r.virtual_address||(r.virtual_address&3)){r.status=2;r.payload_bytes=0;continue;}
        r.valid=1;
    }
    // Copy every scanout first (word loads: VRAM-safe), then write files.
    uint8_t* pixels[3]{};
    for(unsigned i=0;i<3;++i)if(records[i].valid){
        pixels[i]=static_cast<uint8_t*>(std::malloc(records[i].payload_bytes));
        if(!pixels[i]||!read_capture_words(records[i].virtual_address,pixels[i],records[i].payload_bytes)){records[i].status=4;records[i].valid=0;}
    }
    char path[dump_folder::max_path+16];
    auto lcd=make_writer();
    bool written=join(path,sizeof path,dir,"lcd.bin")&&lcd.open(path)&&lcd.write(&header,sizeof header)&&
        lcd.write(records,sizeof records)&&lcd.write(registers.data(),registers.size()*4);
    for(unsigned i=0;i<3;++i)if(written&&records[i].valid)written=lcd.write(pixels[i],records[i].payload_bytes);
    written=lcd.close()&&written;
    // The right eye is only meaningful while the 3D effect is shown.
    const bool stereo=(records[0].format&0x20)!=0;
    constexpr const char* names[3]={"top-screen.bmp","top-screen-right.bmp","bottom-screen.bmp"};
    for(unsigned i=0;i<3;++i){
        if(records[i].valid&&(i!=1||stereo)&&join(path,sizeof path,dir,names[i]))written=write_bmp(path,pixels[i],records[i])&&written;
        std::free(pixels[i]);
    }
    std::printf("Native diagnostic screens dir=%s ok=%u top=%u/%u bottom=%u formats=%x/%x\n",dir,unsigned(ok&&written),
        records[0].valid,records[1].valid,records[2].valid,records[0].format,records[2].format);
    return ok&&written&&records[0].valid&&records[2].valid;
}
// Raw copy of a mapped range (VRAM/DSP) through word loads.
inline bool write_range(const char* path,uint32_t address,uint32_t bytes,unsigned progress_from,unsigned progress_to){
    if(!readable_span(address,bytes))return false;
    auto w=make_writer();
    if(!w.open(path,bytes))return false;
    for(uint32_t done=0;done<bytes;){
        size_t n=bytes-done;uint8_t* dst=w.reserve(n);
        if(!dst||!read_capture_words(address+done,dst,n)||!w.commit(n))return false;
        done+=uint32_t(n);dump_progress(progress_from+unsigned(uint64_t(done)*(progress_to-progress_from)/bytes));
    }
    return w.close();
}
// Full process address space (SM64VM1 v2), plain device memory included.
inline bool write_memory(const char* path,Header& header,uint64_t& total){
    auto w=make_writer();
    if(!w.open(path))return false;
    bool ok=w.write(&header,sizeof header);
    uint64_t address=0;total=0;
    for(unsigned blocks=0;ok&&address<header.scan_end&&blocks<512;++blocks){
        MemInfo memory{};PageInfo page{};
        const Result result=svcQueryMemory(&memory,&page,uint32_t(address));
        const uint64_t end=uint64_t(memory.base_addr)+memory.size;
        if(R_FAILED(result)||memory.base_addr>address||end<=address){ok=false;break;}
        const uint64_t limit=end<header.scan_end?end:header.scan_end;
        Region region{uint32_t(address),uint32_t(limit-address),memory.perm,memory.state,0,0};
        const unsigned state=memory.state&0xff;
        if(!(memory.perm&MEMPERM_READ))region.reason=1;
        else if(state==MEMSTATE_IO&&plain_device_memory(region.address,region.size)){
            if(total+region.size>uint64_t(header.scan_end)){region.reason=3;ok=false;}
            else {region.reason=region_device_payload;region.payload_bytes=region.size;total+=region.size;}
        }
        else if(state==MEMSTATE_FREE||state==MEMSTATE_RESERVED||state==MEMSTATE_IO)region.reason=2;
        else if(total+region.size>uint64_t(header.scan_end)){region.reason=3;ok=false;}
        else {region.payload_bytes=region.size;total+=region.size;}
        if(!w.write(&region,sizeof region)){ok=false;break;}
        ++header.regions;
        for(uint32_t done=0;ok&&done<region.payload_bytes;){
            size_t n=region.payload_bytes-done;uint8_t* dst=w.reserve(n);
            ok=dst&&read_capture_words(region.address+done,dst,n)&&w.commit(n);
            done+=uint32_t(n);
            // Address-space progress; the last ten percent is file finalization.
            dump_progress(unsigned((address+done)*90/header.scan_end));
        }
        address=limit;
    }
    header.end_tick=svcGetSystemTick();header.complete=ok&&address==header.scan_end;
    ok=w.write_at(0,&header,sizeof header)&&ok;
    return w.close()&&ok&&header.complete;
}
inline void write_info(const char* dir,bool full,unsigned scene,unsigned epoch,uint64_t begin,bool screens,bool trace_ok,bool memory_ok,uint64_t memory_bytes){
    char path[dump_folder::max_path+16];
    if(!join(path,sizeof path,dir,"info.txt"))return;
    FILE* f=std::fopen(path,"w");if(!f)return;
    const auto& live=enhance::live();const auto s=enhance::snapshot();
    const struct mallinfo heap=mallinfo();
    const time_t now=std::time(nullptr);char when[32]="unknown";
    if(const struct tm* t=std::localtime(&now))std::strftime(when,sizeof when,"%Y-%m-%d %H:%M:%S",t);
    const char* slash=std::strrchr(dir,'/');
    std::fprintf(f,"Super Mario 64 DS 3DS - diagnostic dump\n");
    std::fprintf(f,"Dump: %s (%s)\nTime: %s\n",slash?slash+1:dir,full?"full: screens, RAM, VRAM":"partial: screens, VRAM, registers; no RAM",when);
    std::fprintf(f,"Console: %s 3DS, kernel %u.%u, firm %u.%u, app memory type %u, app region %u MiB\n",
        live.new_model.load(std::memory_order_relaxed)?"New":"Old",unsigned(OS_KernelConfig->kernel_ver>>24),unsigned((OS_KernelConfig->kernel_ver>>16)&255),
        unsigned(OS_KernelConfig->firm_ver>>24),unsigned((OS_KernelConfig->firm_ver>>16)&255),unsigned(OS_KernelConfig->app_memtype),unsigned(OS_KernelConfig->memregion_sz[0]>>20));
    constexpr const char* framing[]={"Original","Wide","Stretch"};constexpr const char* locks[]={"Off","30","60"};
    constexpr const char* buttons[]={"Standard","Touch","Dual-hand"};constexpr const char* control[]={"Original","Hybrid","Nintendo 64"};
    std::fprintf(f,"Display: resolution %s, screen %s, lock FPS %s, actual geometry %ux%u%s\n",s.res3d==enhance::Res3d::Native?"Native":"Original",
        framing[s.screen_mode%3],locks[live.fps_lock.load(std::memory_order_relaxed)%3],unsigned(live.actual_render_width.load(std::memory_order_relaxed)),
        unsigned(live.actual_render_height.load(std::memory_order_relaxed)),live.native_fallback.load(std::memory_order_relaxed)?" (DS fallback)":"");
    std::fprintf(f,"Controls: buttons %s, control %s, C-Stick %u, Circle Pad %u, invert X %s, invert Y %s\n",buttons[s.controller_mode%3],
        control[s.physics_mode%3],unsigned(s.camera_sensitivity),unsigned(s.move_sensitivity),s.camera_invert_x?"on":"off",s.camera_invert_y?"on":"off");
    std::fprintf(f,"Game: scene %u, update %u\n",scene,epoch);
    std::fprintf(f,"Memory: heap used %u KiB, heap arena %u KiB, linear free %u KiB, VRAM free %u KiB\n",unsigned(heap.uordblks/1024),
        unsigned(heap.arena/1024),unsigned(linearSpaceFree()/1024),unsigned(vramSpaceFree()/1024));
    std::fprintf(f,"Files: lcd.bin + screen BMPs %s, trace.bin %s, %s %s (%llu bytes)\n",screens?"ok":"FAILED",trace_ok?"ok":"FAILED",
        full?"memory.vm":"vram.bin + dsp.bin",memory_ok?"ok":"FAILED",(unsigned long long)memory_bytes);
    std::fprintf(f,"Duration: %.0f ms\n",double(svcGetSystemTick()-begin)*1000.0/SYSCLOCK_ARM11);
    std::fclose(f);
}
// One dump into sdmc:/3ds/sm64ds/dumps/NNN-dump-YYYYMMDD-HHMMSS. Full dumps
// need Developer > Full RAM dumps; partial dumps never read process RAM.
inline bool capture(bool full,unsigned scene,unsigned epoch,const Anchor* anchors,size_t anchor_count){
    if(full&&!enabled())return false;
    const uint64_t begin=svcGetSystemTick();
    char dir[dump_folder::max_path];
    if(!dump_folder::create(dir,sizeof dir)){
        std::printf("Native diagnostic failed: cannot create a folder in %s\n",dump_folder::root_directory);
        dump_progress_full=full;dump_progress(0,true,true);return false;
    }
    std::snprintf(last_capture_stem,sizeof last_capture_stem,"%s",dir);
    // The physical screens first: the progress screen repaints the lower LCD.
    const bool screens_ok=capture_screens(dir,scene,epoch);
    dump_progress_full=full;dump_progress(0,true);
    char path[dump_folder::max_path+16];
    auto trace_writer=make_writer();
    const bool trace_ok=join(path,sizeof path,dir,"trace.bin")&&trace.save(trace_writer,path);
    bool metadata_ok=false;
    if(join(path,sizeof path,dir,"anchors.json"))if(FILE* metadata=std::fopen(path,"w")){
        std::fprintf(metadata,"{\"scene\":%u,\"epoch\":%u,\"kind\":\"%s\",\"word_bytes\":4,\"GPU_OS_DSP_atomic\":false,\"worker_drained\":true,\"physical_lcd\":%s,\"anchors\":{",
            scene,epoch,full?"full":"partial",screens_ok?"true":"false");
        for(size_t i=0;i<anchor_count;++i)std::fprintf(metadata,"%s\"%s\":%u",i?",":"",anchors[i].name,unsigned(uintptr_t(anchors[i].address)));
        const auto& live=enhance::live();
        std::fprintf(metadata,"},\"console\":{\"new_model\":%s,\"fps_lock\":%u,\"kernel_version\":%u,\"firm_version\":%u,\"app_memtype\":%u,\"app_memregion_bytes\":%u}",
            live.new_model.load(std::memory_order_relaxed)?"true":"false",unsigned(live.fps_lock.load(std::memory_order_relaxed)),
            unsigned(OS_KernelConfig->kernel_ver),unsigned(OS_KernelConfig->firm_ver),unsigned(OS_KernelConfig->app_memtype),unsigned(OS_KernelConfig->memregion_sz[0]));
        std::fputs("}\n",metadata);metadata_ok=std::fclose(metadata)==0;
    }
    bool memory_ok=false;uint64_t memory_bytes=0;Header header;
    if(full){
        header.scene=scene;header.epoch=epoch;header.begin_tick=begin;
        memory_ok=join(path,sizeof path,dir,"memory.vm")&&write_memory(path,header,memory_bytes);
    }else{
        memory_ok=join(path,sizeof path,dir,"vram.bin")&&write_range(path,OS_VRAM_VADDR,OS_VRAM_SIZE,0,80);
        memory_bytes=memory_ok?OS_VRAM_SIZE:0;
        if(memory_ok&&join(path,sizeof path,dir,"dsp.bin")&&write_range(path,0x1FF50000,0x8000,80,85)&&
           join(path,sizeof path,dir,"dsp-2.bin")&&write_range(path,0x1FF70000,0x8000,85,90))memory_bytes+=0x10000;
    }
    write_info(dir,full,scene,epoch,begin,screens_ok,trace_ok,memory_ok,memory_bytes);
    const bool ok=memory_ok&&trace_ok&&metadata_ok;
    dump_progress(90,true,!ok);
    trace.clear(); // The next trace starts after capture I/O, not across it.
    std::printf("Native diagnostic path=%s/%s complete=%u regions=%u bytes=%llu elapsedMs=%.3f trace=%u anchors=%u lcd=%u kind=%s\n",dir,full?"memory.vm":"vram.bin",
        unsigned(ok),unsigned(header.regions),(unsigned long long)memory_bytes,
        double(svcGetSystemTick()-begin)*1000.0/SYSCLOCK_ARM11,unsigned(trace_ok),unsigned(metadata_ok),unsigned(screens_ok),full?"full":"partial");
    return ok;
}
#endif
}
