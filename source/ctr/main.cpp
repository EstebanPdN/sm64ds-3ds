#include <3ds.h>
#include "sm64ds/assets.hpp"
#include "sm64ds/model.hpp"
#include "sm64ds/game_data.hpp"
#include "sm64ds/game_files.hpp"
#ifdef SM64DS_ORIGINAL_COLLISION
#include "sm64ds/collision_data.hpp"
#include "sm64ds/game_collision.hpp"
#endif
#ifdef SM64DS_ORIGINAL_HEAP
#include "sm64ds/game_heap.hpp"
#endif
#include "sm64ds/renderer.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>

extern "C" { u32 __stacksize__=128*1024; }
namespace {
void log(const std::string& text) {
    FILE* f=fopen("sdmc:/3ds/sm64ds/platform.log","a");
    if(f) {fprintf(f,"%s\n",text.c_str());fclose(f);}
}
void wait_exit() {
    puts("\nSTART: salir");
    while(aptMainLoop()) {hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
}
}
int main() {
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/sm64ds",0777);
    bool is_new=sm64ds::input_init();
    osSetSpeedupEnable(is_new);
    puts("SM64DS - Port en desarrollo\n\nPrueba de recursos y GPU nativa\n");
    printf("Consola: %s\n",is_new?"New 3DS":"Old 3DS / 2DS");
    log(std::string("boot platform-preview new3ds=")+(is_new?"1":"0"));
#ifdef SM64DS_ORIGINAL_HEAP
    if(!sm64ds::initialize_game_root_heap(4*1024*1024) || !sm64ds::verify_game_root_heap()) {
        puts("No se pudo iniciar la memoria original del juego.");
        log("original heap ERROR");wait_exit();sm64ds::input_exit();gfxExit();return 1;
    }
    log("original heap READY arena_bytes=4194304 allocation_round_trip=pass");
#endif
    std::string error;
    sm64ds::AssetPack pack;
    const std::string root="sdmc:/3ds/sm64ds/",pak=root+"assets.pak";
    if(!pack.open(pak,error) || !pack.verify(error)) {
        const std::string paths[]={root+"rom.nds",root+"Super Mario 64 DS.nds","sdmc:/Super Mario 64 DS.nds"};
        std::string rom;
        for(auto& path:paths) {FILE* f=fopen(path.c_str(),"rb");if(f) {fclose(f);rom=path;break;}}
        if(rom.empty()) {
            puts("\nCopia tu ROM de SM64DS a:\n/3ds/sm64ds/rom.nds\n\nSe extraera al iniciar.");
            wait_exit();sm64ds::input_exit();gfxExit();return 1;
        }
        puts("\nExtrayendo recursos...\nB: cancelar\n");
        int last=-1;
        bool ok=sm64ds::import_rom(rom,pak,error,[&](uint32_t done,uint32_t total) {
            if(!aptMainLoop())return false;
            hidScanInput();if(hidKeysDown()&KEY_B)return false;
            int percent=int(uint64_t(done)*100/total);
            if(percent!=last) {printf("\x1b[12;1H%3d%%  Verificacion al terminar.",percent);last=percent;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
            return true;
        });
        if(!ok || !pack.open(pak,error)) {
            printf("\n%s\n",error.c_str());log("import ERROR "+error);wait_exit();sm64ds::input_exit();gfxExit();return 1;
        }
        log("import OK");
    }
    log("assets verified code="+pack.info().code+" entries="+std::to_string(pack.info().files.size()));
    sm64ds::GameFiles game_files(pack);
    if(!game_files.initialize_common_archive(error)) {
        printf("\nRecursos del jugador: %s\n",error.c_str());log("player archive ERROR "+error);
        wait_exit();sm64ds::input_exit();gfxExit();return 1;
    }
    sm64ds::bind_native_game_files(&game_files);
    log("player archive READY verified_members=198 gameplay_executed=0");
    // Exercise the same checked ROM data bridge the original game will use.
    // Initializing data does not yet dispatch any original scene callbacks.
    {
        puts("\nInicializando datos originales...");
        sm64ds::game_data::HandleTable handles;
        sm64ds::game_data::LevelData castle_data;
        if(!sm64ds::game_data::initialize_native_constants(pack,error) ||
           !sm64ds::game_data::load_handles(pack,handles,error) ||
           !sm64ds::game_data::load_level(pack,9,handles,
               "data/stage/main_castle/main_castle_all.bmd",castle_data,error)) {
            printf("\nDatos de juego: %s\n",error.c_str());log("game data ERROR "+error);
            wait_exit();sm64ds::input_exit();gfxExit();return 1;
        }
        log("game data READY handles="+std::to_string(handles.asset_indices.size())+
            " castle_groups="+std::to_string(castle_data.groups.size())+
            " castle_entrances="+std::to_string(castle_data.entrances.size())+
            " gameplay_executed=0");
#ifdef SM64DS_ORIGINAL_COLLISION
        sm64ds::CollisionData collision;
        alignas(8) unsigned char collider[sm64ds::collision_storage_size]{};
        if(!sm64ds::load_collision_data(pack,handles,castle_data,collision,error)) {
            printf("\nColisiones: %s\n",error.c_str());log("collision data ERROR "+error);
            wait_exit();sm64ds::input_exit();gfxExit();return 1;
        }
        sm64ds::collision_initialize(collider,collision.kcl.data(),collision.clps.data());
        unsigned hits=0;
        bool valid=castle_data.entrances.size()>=4;
        for(size_t i=0;i<4 && valid;++i) {
            const auto& entrance=castle_data.entrances[i];
            const int32_t pos[]={int32_t(entrance.position[0])*4096,
                (int32_t(entrance.position[1])+500)*4096,int32_t(entrance.position[2])*4096};
            sm64ds::GroundHit hit;
            valid=sm64ds::collision_ground(collider,pos,-20000*4096,hit) &&
                hit.y==int32_t(entrance.position[1])*4096 && hit.normal[1]==4096;
            hits+=valid;
        }
        sm64ds::collision_destroy(collider);
        log(std::string("original collision ")+(valid?"READY":"ERROR")+
            " triangles="+std::to_string(collision.triangles)+" entrance_floor_checks="+
            std::to_string(hits)+" player_executed=0");
        if(!valid) {
            puts("Colision original: comprobacion de entradas fallida.");
            wait_exit();sm64ds::input_exit();gfxExit();return 1;
        }
#endif
    }
    sm64ds::ModelPreview model;
    const char* castle="data/stage/main_castle/main_castle_all.bmd";
    if(!model.load(pack,castle,error)) {
        printf("\n%s\n",error.c_str());log("model ERROR "+error);wait_exit();sm64ds::input_exit();gfxExit();return 1;
    }
    sm64ds::Renderer renderer;
    if(!renderer.init(model,error)) {
        printf("\n%s\n",error.c_str());log("GPU ERROR "+error);wait_exit();sm64ds::input_exit();gfxExit();return 1;
    }
    consoleClear();
    printf("SM64DS - Port en desarrollo\n\nVisor del castillo original\nAventura aun no integrada\n\n%s rev.%u | %zu recursos\n%zu triangulos\n\nCircle Pad: desplazar vista\nC-stick / cruceta: camara\nL/R: alejar/acercar\nA: centrar | START: salir\nSlider 3D: profundidad\n",pack.info().code.c_str(),pack.info().revision,pack.info().files.size(),model.triangles.size());
    // Citro3D submits the top-screen GPU targets. Make the CPU-written bottom
    // console visible too, including with New 3DS's enabled CPU cache.
    gfxFlushBuffers();
    log("render READY triangles="+std::to_string(model.triangles.size()));
    model = {}; // GPU owns the static preview; release duplicate CPU geometry/textures.
    ntr::gx_reset();
    uint64_t previous=svcGetSystemTick(),begin=previous;unsigned frames=0;
    while(aptMainLoop()) {
        auto input=sm64ds::input_poll();if(input.pressed&KEY_START)break;
        uint64_t now=svcGetSystemTick();float dt=float(double(now-previous)/SYSCLOCK_ARM11);previous=now;
        const float slider=osGet3DSliderState();
        renderer.update(input,dt);renderer.draw(slider);
        if(++frames==120)log("render 120 frames elapsed_ms="+std::to_string(uint64_t((double(svcGetSystemTick()-begin)/SYSCLOCK_ARM11)*1000))+" slider="+std::to_string(slider));
    }
    renderer.exit();log("exit clean");sm64ds::input_exit();gfxExit();return 0;
}
