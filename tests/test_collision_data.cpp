#include "sm64ds/collision_data.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <cstdio>
using sm64ds::CollisionData;
static void put32(std::vector<uint8_t>& b,size_t at,uint32_t v) {
    for(unsigned i=0;i<4;++i) b.at(at+i)=uint8_t(v>>(i*8));
}
static CollisionData fixture() {
    CollisionData c; c.kcl.resize(134); c.clps.resize(16);
    std::memcpy(c.clps.data(),"CLPS",4); c.clps[4]=8; c.clps[6]=1;
    put32(c.kcl,0,56); put32(c.kcl,4,68); put32(c.kcl,8,92); put32(c.kcl,12,124);
    for(unsigned i=0;i<3;++i) put32(c.kcl,32+4*i,0xffffffffu);
    put32(c.kcl,124,0x80000004); c.kcl[130]=1;
    return c;
}
int main(int argc,char** argv) {
    std::string error;
    auto valid=fixture(); assert(sm64ds::validate_collision_data(valid,error));
    assert(valid.triangles==1 && valid.octree_nodes==1 && valid.octree_lists==1);
    auto reject=[&](CollisionData bad) {
        bad.triangles=123; assert(!sm64ds::validate_collision_data(bad,error));
        assert(!error.empty() && bad.triangles==123);
    };
    auto bad=valid; bad.kcl.resize(55); reject(bad);
    bad=valid; put32(bad.kcl,4,0xfffffff8); reject(bad);
    bad=valid; bad.kcl[112]=1; reject(bad); // only one vertex
    bad=valid; bad.kcl[114]=7; reject(bad); // normal outside section
    bad=valid; bad.kcl[122]=1; reject(bad); // surface outside CLPS
    bad=valid; put32(bad.kcl,44,32); reject(bad);
    bad=valid; put32(bad.kcl,32,0xfffffffau); reject(bad);
    bad=valid; put32(bad.kcl,48,1); reject(bad);
    bad=valid; put32(bad.kcl,124,0); reject(bad); // cycle/depth zero
    bad=valid; put32(bad.kcl,124,0x80000005); reject(bad);
    bad=valid; put32(bad.kcl,124,0xfffffff0); reject(bad);
    bad=valid; bad.kcl[130]=2; reject(bad);
    bad=valid; bad.kcl[132]=1; reject(bad); // missing terminator
    bad=valid; bad.clps[4]=4; reject(bad);
    bad=valid; bad.clps[6]=2; reject(bad);
    if(argc>1) {
        sm64ds::AssetPack pack; assert(pack.open(argv[1],error));
        sm64ds::game_data::HandleTable handles;
        sm64ds::game_data::LevelData level;
        assert(sm64ds::game_data::load_handles(pack,handles,error));
        assert(sm64ds::game_data::load_level(pack,9,handles,
            "data/stage/main_castle/main_castle_all.bmd",level,error));
        CollisionData castle;
        if(!sm64ds::load_collision_data(pack,handles,level,castle,error)) {
            std::fprintf(stderr,"%s\n",error.c_str()); return 1;
        }
        assert(castle.triangles==1243);
        std::printf("%s castle: %u triangles, %u nodes, %u lists, %zu entrances\n",
            pack.info().code.c_str(),castle.triangles,castle.octree_nodes,
            castle.octree_lists,level.entrances.size());
        if(argc>2) {
            auto save=[&](const char* name,const std::vector<uint8_t>& bytes) {
                std::ofstream out(std::string(argv[2])+name,std::ios::binary);
                out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(out.good());
            };
            save("/castle.kcl",castle.kcl);save("/castle.clps",castle.clps);
            std::ofstream out(std::string(argv[2])+"/entrances.csv");
            for(const auto& e:level.entrances)
                out<<e.position[0]<<','<<e.position[1]<<','<<e.position[2]<<'\n';
            assert(out.good());
        }
    }
}
