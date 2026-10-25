#include <3ds.h>
#include "sm64ds/game_renderer.hpp"
#include "sm64ds/game_textures.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
// GRAPHICS CHECK ONLY. Replays locally captured commands, not gameplay. Never
// package this target as the game, or count its frame rate as game performance.
extern "C" {u32 __stacksize__=128*1024;}
static bool read(const char* path,std::vector<uint8_t>& bytes,size_t limit) {
    FILE* file=fopen(path,"rb");if(!file)return false;
    fseek(file,0,SEEK_END);const long size=ftell(file);rewind(file);
    if(size<=0 || size>long(limit)) {fclose(file);return false;}
    bytes.resize(size);const bool ok=fread(bytes.data(),1,size,file)==size_t(size);fclose(file);return ok;
}
static uint32_t word(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
int main() {
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);osSetSpeedupEnable(false);
    const char* root="sdmc:/3ds/sm64ds/render-check";
    FILE* log=fopen("sdmc:/3ds/sm64ds/render-check/result.log","w");
    if(log) {fprintf(log,"scope=recorded_player_graphics gameplay_executed=0 physical_hardware=unverified\n");fflush(log);}
    std::string error;sm64ds::GameRenderer renderer;
    auto& geometry=sm64ds::game_geometry();sm64ds::GameTextures textures(geometry);
    bool okay=renderer.init(error) && geometry.set_texture_banks(3) && geometry.set_palette_banks(0x30);
    std::vector<ntr::GxTriangle> triangles;
    const unsigned frames[]={0,90,179};unsigned previous=99;
    sm64ds::GameProjection projection;projection.vertical_fov_degrees=40;
    unsigned rendered=0;
    while(okay && aptMainLoop() && rendered<3600) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        const unsigned selected=(rendered/180)%3;
        if(selected!=previous) {
            char path[256];std::vector<uint8_t> bytes,commands;
            snprintf(path,sizeof path,"%s/frame-%03u/commands.bin",root,frames[selected]);
            okay=read(path,commands,65536*8) && commands.size()%8==0;
            snprintf(path,sizeof path,"%s/frame-%03u/textures.bin",root,frames[selected]);
            okay=okay && read(path,bytes,sizeof geometry.textures) && bytes.size()==sizeof geometry.textures;
            if(okay) {memcpy(geometry.textures,bytes.data(),bytes.size());++geometry.texture_generation;}
            snprintf(path,sizeof path,"%s/frame-%03u/palettes.bin",root,frames[selected]);
            okay=okay && read(path,bytes,sizeof geometry.palettes) && bytes.size()==sizeof geometry.palettes;
            if(!okay) {error="Faltan las capturas locales del jugador";break;}
            memcpy(geometry.palettes,bytes.data(),bytes.size());++geometry.palette_generation;
            triangles.clear();textures.begin_frame();ntr::gx_reset();
            ntr::gx_set_texture_resolver(sm64ds::GameTextures::resolver,&textures);
            ntr::gx_set_light(0,.2f,-.8f,-.5f,0x7fff);ntr::gx_enable_lights(1);
            for(size_t i=0;i<commands.size();i+=8) {
                const auto port=word(commands.data()+i),value=word(commands.data()+i+4);
                if(!port)ntr::gx_write_fifo(value);
                else if(port>=0x10 && port<=0x72)ntr::gx_write_port(0x04000400+port*4,value);
                else {okay=false;break;}
            }
            size_t count=0;const auto* decoded=ntr::gx_polygons(count);
            okay=okay && ntr::gx_asset_stream_valid() && count==456;
            if(!okay) {error="El dibujo no coincide con la captura verificada";break;}
            triangles.assign(decoded,decoded+count);
            consoleClear();printf("SM64DS - PRUEBA DE GPU\n\nPoses grabadas de Yoshi\nNo ejecuta una partida\n\nCiclo de origen: %u\n456 triangulos\n400x240 por ojo\n\nSTART: salir\nSlider: profundidad 3D\n",frames[selected]);gfxFlushBuffers();
            previous=selected;
        }
        okay=renderer.draw(triangles.data(),triangles.size(),projection,osGet3DSliderState(),error);
        if(log && (rendered%180==0 || rendered%180==1)) {
            fprintf(log,"frame=%u source=%u triangles=%u textures=%u uploads=%u eyes=%u okay=%d\n",rendered,frames[selected],
                renderer.stats.triangles,renderer.stats.textures,renderer.stats.texture_uploads,renderer.stats.eyes,okay);fflush(log);
        }
        ++rendered;
    }
    if(log) {fprintf(log,"exit=%s rendered=%u error=%s\n",okay?"clean":"error",rendered,error.c_str());fclose(log);}
    renderer.exit();ntr::gx_set_texture_resolver(nullptr,nullptr);gfxExit();return okay?0:1;
}
