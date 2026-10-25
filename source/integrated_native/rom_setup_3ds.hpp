#pragma once
#include <cerrno>
#include <sys/stat.h>
// First-run installation precedes every game/audio/renderer allocation. The
// loading screen borrows gfx's existing framebuffers and owns no worker thread.
#include "rom_bootstrap.hpp"
#include "rom_discovery.hpp"
#include "dsp_bootstrap.hpp"
#include "rom_art.hpp"
#include "packed_assets_3ds.hpp"
#include "packed_builder.hpp"
#include "loading_screen_3ds.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace rom_setup_3ds {
inline void log_memory(const char* phase) {
    const auto heap=mallinfo();
    const auto boot=boot_memory::statistics();
    std::printf("ROM-only memory phase=%s heapUsed=%d heapFree=%d linearFree=%lu bootLive=%lu bootBlocks=%lu bootPeak=%lu\n",
        phase,heap.uordblks,heap.fordblks,(unsigned long)linearSpaceFree(),
        (unsigned long)boot.live_bytes,(unsigned long)boot.live_blocks,(unsigned long)boot.peak_bytes);
}
struct Timing { uint64_t start=0,phase_start=0,last_log=0;const char* phase=nullptr; };
inline Timing timing;
inline bool progress(const rom_bootstrap::Progress& value,void* context) {
    const uint64_t now=svcGetSystemTick();
    if(!timing.start)timing.start=now;
    const bool changed=!timing.phase||std::strcmp(timing.phase,value.phase)!=0;
    if(changed){timing.phase=value.phase;timing.phase_start=now;}
    if(changed||now-timing.last_log>=uint64_t(SYSCLOCK_ARM11)*5||value.done==value.total){
        const double seconds=double(now-timing.phase_start)/SYSCLOCK_ARM11;
        std::printf("Setup I/O phase=%s elapsed=%.3f phase_seconds=%.3f bytes=%llu/%llu members=%u/%u phase_KiB_s=%.1f\n",
            value.phase,double(now-timing.start)/SYSCLOCK_ARM11,seconds,
            (unsigned long long)value.done,(unsigned long long)value.total,value.files_done,value.files_total,
            seconds>0?double(value.done)/1024/seconds:0);
        timing.last_log=now;
    }
    return static_cast<loading_screen::Session*>(context)->update(
        value.phase,value.done,value.total,value.files_done,value.files_total);
}
inline bool build_progress(const rom_bootstrap::Progress& value,void* context){
    auto mapped=value;
    if(!std::strcmp(value.phase,"extract"))mapped.phase="write-package";
    else if(!std::strcmp(value.phase,"verify-cache"))mapped.phase="verify-package";
    return progress(mapped,context);
}
inline bool failure(loading_screen::Session& screen,loading_screen::Error kind,const char* detail) {
    // Keep the precise operation/path/result in the log; the on-screen title
    // and recovery hint come from the same system-language dictionary.
    std::fprintf(stderr,"FATAL: ROM-only setup error=%u: %s\n",unsigned(kind),detail);
    screen.error(kind);
    loading_screen::CtrBackend input;input.error_mode=true;
    for(;;) {
        if(!input.poll()||(hidKeysDown()&KEY_START))break;
        svcSleepThread(50000000);
    }
    screen.close();dsp_bootstrap::release_after_audio_shutdown();return false;
}
// A fresh SD card has no game folder. Create it so the ROM-missing screen
// points at a folder that exists; an existing folder is left untouched.
inline void ensure_game_directory(){
    for(const char* path:{"sdmc:/3ds","sdmc:/3ds/sm64ds","sdmc:/3ds/sm64ds/dumps"}){
        if(::mkdir(path,0777)==0)std::printf("ROM-only setup created %s\n",path);
        else if(errno!=EEXIST)std::printf("ROM-only setup cannot create %s errno=%d\n",path,errno);
    }
}
inline bool prepare() {
    timing={};
    ensure_game_directory();
    setup_power::Scope power;
    log_memory("before-art");
    const auto language=loading_screen::detect_language();
    auto selected=rom_discovery::next("sdmc:/3ds/sm64ds");
    auto package=packed_assets::next("sdmc:/3ds/sm64ds");
    rom_art::Resources artwork;
    const bool art_ready=artwork.load_embedded();
    // Always borrow the same owner view, including when it is initially empty.
    // A later candidate may supply valid artwork after a damaged copy is skipped.
    loading_screen::Session screen(&artwork.views(),language);
    std::printf("ROM-only artwork loaded=%u loading_language=%u\n",unsigned(art_ready),unsigned(language));
    if(selected.found&&!art_ready)std::printf("ROM-only artwork unavailable: %s\n",artwork.error());
    log_memory("before-ROM");
    char rejected[384]{};
    for(;;){
        if(package.failure!=rom_discovery::Failure::None)
            return failure(screen,loading_screen::Error::Storage,package.error);
        if(!package.found&&selected.failure!=rom_discovery::Failure::None)
            return failure(screen,selected.failure==rom_discovery::Failure::Memory?loading_screen::Error::Memory:loading_screen::Error::Storage,selected.error);
        if(!package.found&&!selected.found)
            return failure(screen,selected.nds_files||rejected[0]?loading_screen::Error::RomRevision:loading_screen::Error::RomMissing,
                rejected[0]?rejected:"No compatible .nds ROM or .sm64ds package in sdmc:/3ds/sm64ds/");
        rom_bootstrap::Result assets;
        if(package.found){
            std::printf("Packed setup started; package=%s schema=%s\n",package.path.c_str(),rom_install_manifest::schema_id);
            assets=packed_assets::verify(package.path.c_str(),progress,&screen);
            if(assets.ok&&!packed_assets::mount(package.path.c_str())){
                return failure(screen,loading_screen::Error::Storage,"Cannot mount verified .sm64ds package");
            }
            if(!assets.ok&&assets.error.compare(0,16,"Cancelled during")!=0){
                std::snprintf(rejected,sizeof(rejected),"%s",assets.error.c_str());
                std::printf("Packed setup rejected: %s: %s\n",package.path.c_str(),rejected);
                package=packed_assets::next("sdmc:/3ds/sm64ds",package.path.c_str());continue;
            }
        }else{
            std::printf("ROM-only setup started; ROM=%s schema=%s scratch=%u discovery=.nds\n",
                selected.path.c_str(),rom_install_manifest::schema_id,unsigned(rom_bootstrap::detail::buffer_bytes));
            constexpr const char* output="sdmc:/3ds/sm64ds/prepared.sm64ds";
            assets=packed_builder::ensure(selected.path.c_str(),output,build_progress,&screen);
            if(assets.ok){
                if(!packed_assets::mount(output))return failure(screen,loading_screen::Error::Storage,"Cannot mount prepared .sm64ds package");
                std::printf("ROM preparation format=SM64DSP1 SD_files_written=%u members=%zu output=%s\n",assets.written,packed_assets::count,output);
            }
        }
        if(!assets.ok){
            if(assets.error.compare(0,16,"Cancelled during")==0){
                std::puts("ROM-only setup cancelled; ROM and old cache retained; a complete temporary package can resume at next boot");
                screen.close();return false;
            }
            if(assets.error.find("Incorrect ROM size")==0||assets.error.find("Unsupported or damaged ROM")==0){
                // The complete SHA/CRC check precedes every cache write. Keep trying
                // later filenames; header checks alone do not establish a valid ROM.
                std::snprintf(rejected,sizeof(rejected),"%s",assets.error.c_str());
                std::printf("ROM-only rejected candidate: %s: %s\n",selected.path.c_str(),rejected);
                selected=rom_discovery::next("sdmc:/3ds/sm64ds",selected.path.c_str());
                if(selected.found){
                    screen.stage(loading_screen::translated(language,loading_screen::Label::VerifyRom),screen.state().percent);
                }
                continue;
            }
            auto kind=loading_screen::Error::Rom;
            if(assets.error.find("Open ROM")==0)kind=loading_screen::Error::RomMissing;
            else if(assets.error.find("Insufficient memory")==0)kind=loading_screen::Error::Memory;
            return failure(screen,kind,assets.error.c_str());
        }
        log_memory("after-ROM");
        if(setenv("SM64DS_ASSET_ROOT",assets.root.c_str(),1)!=0)
            return failure(screen,loading_screen::Error::Memory,"Could not set the game asset directory");
        // Firmware is read from this console's own system content when the usual
        // SD/launcher component is absent. No firmware is shipped or downloaded.
        screen.stage(loading_screen::Stage::Audio);
        const auto audio=dsp_bootstrap::ensure();
        std::printf("ROM-only DSP setup ok=%u prepared=%u source=%s result=%08lx\n",
            unsigned(audio.ok),unsigned(audio.prepared),audio.source?audio.source:"none",(unsigned long)audio.code);
        if(!audio.ok)return failure(screen,loading_screen::Error::Audio,audio.error);
        log_memory("after-DSP");
        std::printf("ROM-only setup complete; root=%s written=%u reused=%u files=%zu bytes=%llu\n",
            assets.root.c_str(),assets.written,assets.reused,rom_install_manifest::file_count,
            (unsigned long long)rom_install_manifest::total_bytes);
        screen.stage(loading_screen::Stage::Ready);
        if(!power.completed()){screen.close();return false;}
        screen.close();return true;
    }
}
}
