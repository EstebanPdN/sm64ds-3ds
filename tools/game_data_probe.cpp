#include "sm64ds/game_data.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>

int main(int argc,char** argv) {
    if(argc<2 || argc>3) {
        std::fprintf(stderr,"Usage: game_data_probe assets.pak [output-directory]\n");
        return 2;
    }
    sm64ds::AssetPack pack;
    std::string error;
    if(!pack.open(argv[1],error)) { std::fprintf(stderr,"%s\n",error.c_str()); return 1; }
    sm64ds::game_data::HandleTable handles;
    if(!sm64ds::game_data::initialize_native_constants(pack,error)) {
        std::fprintf(stderr,"native constants: %s\n",error.c_str()); return 1;
    }
    std::puts("native initialized constants: verified and loaded");
    if(!sm64ds::game_data::load_handles(pack,handles,error)) {
        std::fprintf(stderr,"handles: %s\n",error.c_str()); return 1;
    }
    std::printf("ROM %s rev%u; handles=%zu at=%08x\n",pack.info().code.c_str(),
                pack.info().revision,handles.asset_indices.size(),handles.original_address);
    sm64ds::game_data::LevelData level;
    if(!sm64ds::game_data::load_level(pack,9,handles,
            "data/stage/main_castle/main_castle_all.bmd",level,error)) {
        std::fprintf(stderr,"castle: %s\n",error.c_str()); return 1;
    }
    std::printf("castle descriptor=%08x groups=%zu entrances=%zu\n",level.original_address,
                level.groups.size(),level.entrances.size());
    for(unsigned i=0;i<4;++i)
        std::printf("handle %u => %s\n",level.handles[i],
            pack.info().files[handles.asset_indices[level.handles[i]]].path.c_str());
    for(const auto& e:level.entrances)
        std::printf("entrance object=%u param=%u xyz=%d,%d,%d rot=%d,%d,%d area=%d scenario=%u\n",
            e.object,e.parameter,e.position[0],e.position[1],e.position[2],
            e.rotation[0],e.rotation[1],e.rotation[2],e.area,e.scenario);
    // Every ARM9 overlay is read from the actual cartridge table. There are 103
    // in the supported SM64DS reference; the command is a developer probe.
    for(uint32_t id=0;id<103;++id) {
        sm64ds::game_data::OverlayImage image;
        if(!sm64ds::game_data::load_overlay(pack,id,image,error)) {
            std::fprintf(stderr,"overlay %u: %s\n",id,error.c_str());return 1;
        }
        std::printf("overlay %u base=%08x bytes=%zu crc32=%08x\n",id,image.base,
                    image.bytes.size(),sm64ds::crc32(image.bytes.data(),image.bytes.size()));
        if(argc==3) {
            char name[80];std::snprintf(name,sizeof name,"/overlay_%04u.bin",id);
            std::ofstream out(std::string(argv[2])+name,std::ios::binary);
            out.write(reinterpret_cast<const char*>(image.bytes.data()),image.bytes.size());
            if(!out) return 1;
        }
    }
    return 0;
}
