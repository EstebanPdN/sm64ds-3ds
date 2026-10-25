#include "ntr/bmd.h"
#include <cassert>
#include <cstring>
#include <vector>

// Original synthetic container, with material order deliberately different
// from polygon order. No Nintendo data is needed to reproduce this regression.
static std::vector<uint8_t> fixture;
namespace ntr {
int32_t fs_size(int32_t id) {return id==0 ? int32_t(fixture.size()) : -1;}
int32_t fs_read(int32_t id,void* out,int32_t size) {
    if(id!=0 || size!=int32_t(fixture.size()))return -1;
    std::memcpy(out,fixture.data(),fixture.size());return size;
}
int32_t fs_id_by_path(const char*) {return 0;}
}
static void p32(size_t offset,uint32_t value) {
    for(unsigned i=0;i<4;++i)fixture[offset+i]=uint8_t(value>>(8*i));
}
int main() {
    fixture.assign(0x180,0);
    p32(0x04,1);p32(0x08,0x40); // one bone
    p32(0x0c,2);p32(0x10,0x80); // two polygon groups
    p32(0x24,2);p32(0x28,0xc0); // two materials, no textures
    p32(0x70,2);p32(0x74,0x120);p32(0x78,0x122);
    fixture[0x120]=0;fixture[0x121]=1;
    fixture[0x122]=1;fixture[0x123]=0; // reverse group/material relationship
    p32(0x80,1);p32(0x84,0x90);p32(0x88,1);p32(0x8c,0xa0);
    p32(0x98,4);p32(0x9c,0x130);p32(0xa8,4);p32(0xac,0x134);
    p32(0xe0,0x50000);p32(0xe4,0x1f0000);p32(0xec,0x87654321);
    ntr::BmdModel model;
    assert(ntr::bmd_load(0,model));
    assert(model.dlists.size()==2);
    assert(model.material_ids[0]==1 && model.material_ids[1]==0);
    assert(model.materials[0].wrap==5);
    assert(model.materials[0].polygon==0x1f0000);
    assert(model.materials[0].spe_emi==0x87654321);
    fixture[0x122]=2; // invalid group must fail, never silently use material 0
    assert(!ntr::bmd_load(0,model));
    fixture[0x122]=0; // conflicting materials on the same group must fail
    assert(!ntr::bmd_load(0,model));
    fixture[0x122]=1;fixture[0x121]=2;
    assert(!ntr::bmd_load(0,model));
}
