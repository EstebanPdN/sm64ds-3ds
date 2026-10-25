#include "sm64ds/collision_data.hpp"
#include "sm64ds/game_files.hpp"
#include <cstring>
#include <functional>
#include <set>
#include <utility>

namespace sm64ds {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p+2)) << 16; }
}
bool validate_collision_data(CollisionData& data, std::string& error) {
    auto fail=[&](const char* reason) { error=reason; return false; };
    const auto& b=data.kcl;
    const auto& c=data.clps;
    if(b.size()<0x38 || b.size()>16*1024*1024) return fail("Invalid KCL size");
    if(c.size()<8 || std::memcmp(c.data(),"CLPS",4) || u16(c.data()+4)!=8 ||
       c.size()!=8u+8u*u16(c.data()+6)) return fail("Invalid CLPS block");
    const uint32_t p=u32(b.data()),n=u32(b.data()+4),t=u32(b.data()+8),o=u32(b.data()+12);
    // The triangle pointer is biased by -16; index zero is a sentinel and
    // overlaps the tail of the normal array. The first prism is tris[1].
    if(p<0x38 || p%4 || n%4 || t%4 || o%4 || p>=n || n>t ||
       uint64_t(t)+16>=o || o>b.size() || (n-p)%12 || (o-t)%16)
        return fail("Invalid KCL section offsets");
    const uint32_t vertices=(n-p)/12,normals=(t+16-n)/6,triangles=(o-t)/16;
    if(triangles>32768) return fail("KCL triangle index exceeds signed game index");
    for(uint32_t i=1;i<triangles;++i) {
        const auto* tri=b.data()+t+i*16;
        if(u16(tri+4)>=vertices || u16(tri+14)>=u16(c.data()+6))
            return fail("KCL vertex or surface index outside section");
        for(unsigned off=6;off<=12;off+=2)
            if(u16(tri+off)>=normals) return fail("KCL normal index outside section");
    }
    const uint32_t shift=u32(b.data()+0x2c),ys=u32(b.data()+0x30),zs=u32(b.data()+0x34);
    if(shift>24 || ys>24 || zs>24) return fail("Invalid KCL octree shift");
    uint32_t roots[3]{};
    for(unsigned i=0;i<3;++i) {
        uint32_t extent=~u32(b.data()+0x20+i*4)+1;
        if(!extent || (extent&(extent-1)) || extent>(1u<<24) || extent<(1u<<shift))
            return fail("Invalid KCL octree extent");
        roots[i]=extent>>shift;
    }
    if((uint64_t(1)<<ys)!=roots[0] || (uint64_t(1)<<zs)!=uint64_t(roots[0])*roots[1])
        return fail("Overlapping KCL octree root indices");
    const uint64_t root_count=uint64_t(roots[0])*roots[1]*roots[2];
    if(root_count>(b.size()-o)/4) return fail("KCL root table outside file");
    std::set<std::pair<uint32_t,uint32_t>> nodes;
    std::set<uint32_t> leaves;
    std::function<bool(uint32_t,uint32_t,uint32_t)> visit;
    visit=[&](uint32_t base,uint32_t count,uint32_t depth) {
        if(base<o || base%4 || uint64_t(base)+uint64_t(count)*4>b.size())
            return fail("KCL node table outside file");
        if(!nodes.emplace(base,depth).second) return true;
        for(uint32_t i=0;i<count;++i) {
            const uint32_t word=u32(b.data()+base+i*4);
            const uint64_t next=uint64_t(base)+(word&0x7fffffffu);
            if(word&0x80000000u) {
                if(next%2 || next+4>b.size()) return fail("KCL triangle list outside file");
                if(!leaves.insert(uint32_t(next)).second) continue;
                uint64_t at=next+2;
                for(;;at+=2) {
                    if(at+2>b.size()) return fail("Unterminated KCL triangle list");
                    const uint16_t index=u16(b.data()+at);
                    if(!index) break;
                    if(index>=triangles) return fail("KCL leaf triangle outside section");
                }
            } else {
                if(!depth || !word || next>b.size()) return fail("Invalid KCL child depth or offset");
                if(!visit(uint32_t(next),8,depth-1)) return false;
            }
        }
        return true;
    };
    if(!visit(o,uint32_t(root_count),shift)) return false;
    data.triangles=triangles-1; data.octree_nodes=uint32_t(nodes.size());
    data.octree_lists=uint32_t(leaves.size()); error.clear(); return true;
}

bool load_collision_data(AssetPack& pack,const game_data::HandleTable& handles,
                         const game_data::LevelData& level,CollisionData& output,
                         std::string& error) {
    CollisionData candidate;
    const auto handle=level.handles[1];
    if(handle>=handles.asset_indices.size() || handles.asset_indices[handle]>=pack.info().files.size()) {
        error="Collision handle outside verified catalog"; return false;
    }
    GameFiles files(pack);
    if(!files.read_path(pack.info().files[handles.asset_indices[handle]].path,candidate.kcl,error)) return false;
    size_t at;
    if(!level.image.range(level.clps_address,8,at)) {error="CLPS header outside level";return false;}
    const auto size=8u+8u*u16(level.image.bytes.data()+at+6);
    if(!level.image.range(level.clps_address,size,at)) {error="CLPS entries outside level";return false;}
    candidate.clps.assign(level.image.bytes.begin()+at,level.image.bytes.begin()+at+size);
    if(!validate_collision_data(candidate,error)) return false;
    output=std::move(candidate);return true;
}
}
