#include "model_bank.hpp"
#include "mesh_groups.hpp"
#include "native_batch.hpp"
#include <cstring>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace sm64ds::render;
static void put(std::vector<std::uint8_t>& data,unsigned at,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)data[at+i]=std::uint8_t(value>>(8*i));
}
static void seal(std::vector<std::uint8_t>& data) { put(data,52,ModelBank::checksum(data.data(),data.size())); }
static std::vector<std::uint8_t> fixture() {
    // Independent C++ authoring: one untextured three-vertex mesh. Python's
    // cross-language fixture has different tables and image/quad payloads.
    std::vector<std::uint8_t> b(336);
    const char* magic="SM64NMD2";for(unsigned i=0;i<8;++i)b[i]=magic[i];
    for(auto p:{std::pair<unsigned,unsigned>{8,2},{12,336},{16,1},{24,1},{28,1},
                {32,128},{36,160},{40,160},{44,224},{48,1},
                {128,256},{132,72},{136,328},{140,6},
                {160,0xffffffffu},{168,0},{172,3},{180,0x3f800000u},{196,0x3f800000u},{204,0xffffffffu},
                {240,3}})put(b,p.first,p.second);
    b[330]=1;b[332]=2;seal(b);return b;
}
static std::vector<std::uint8_t> pair_fixture(float second_uv,unsigned indices=3,bool normal_second=false) {
    const unsigned first_vertex=384,first_index=first_vertex+72;
    const unsigned second_vertex=(first_index+indices*2+3)&~3u;
    const unsigned second_index=second_vertex+(normal_second?96:72);
    const unsigned size=(second_index+indices*2+3)&~3u;
    std::vector<std::uint8_t> b(size);
    const char* magic=normal_second?"SM64NMD3":"SM64NMD2";std::memcpy(b.data(),magic,8);
    for(auto p:{std::pair<unsigned,unsigned>{8,normal_second?3u:2u},{12,size},{16,2},{24,1},{28,2},
                {32,128},{36,192},{40,192},{44,256},{48,1},
                {128,first_vertex},{132,72},{136,first_index},{140,indices*2},
                {160,second_vertex},{164,normal_second?96u:72u},{168,second_index},{172,indices*2},{176,normal_second?1u:0u},
                {192,0xffffffffu},{204,3},{212,0x3f800000u},{228,0x3f800000u},{236,0xffffffffu},
                {272,3},{288,1},{304,3}})put(b,p.first,p.second);
    std::uint32_t uv;std::memcpy(&uv,&second_uv,4);put(b,second_vertex+12,uv);
    for(unsigned i=0;i<indices;++i){b[first_index+i*2]=i%3;b[second_index+i*2]=i%3;}
    seal(b);return b;
}
static void grouped_geometry(){
    auto bytes=pair_fixture(.5f);ModelBank bank;
    assert(ModelBank::open(bytes.data(),bytes.size(),bank)==Error::none);
    MeshGroups plan(bank);assert(plan.size()==1);
    assert(plan.placement(0).vertex==0&&plan.placement(0).index==0);
    assert(plan.placement(1).vertex==3&&plan.placement(1).index==3);
    const auto& g=plan.group(0);assert(g.vertex_bytes==144&&g.index_bytes==12&&!g.normals);
    std::vector<std::uint8_t> vb(g.vertex_bytes+16,0xca),ib(g.index_bytes+16,0xca);
    plan.copy(0,vb.data()+8,ib.data()+8);
    assert(!std::memcmp(vb.data()+8,bank.mesh(0).vertices,72));
    assert(!std::memcmp(vb.data()+80,bank.mesh(1).vertices,72));
    for(unsigned i=0;i<6;++i)assert(ib[8+i*2]==i&&ib[9+i*2]==0);
    for(unsigned i=0;i<8;++i)assert(vb[i]==0xca&&vb[vb.size()-1-i]==0xca&&ib[i]==0xca&&ib[ib.size()-1-i]==0xca);
    for(unsigned w=8;w<=1024;w*=2)for(unsigned h=8;h<=1024;h*=2)assert(g.uv.accepts(w,h));
    MeshGroups separate(bank,false);assert(separate.size()==2&&separate.placement(1).index==0);
    // Never narrow another part's dynamic fixed-UV domain or change whether
    // a mesh supports normals. Exceeding the index ceiling creates a new group.
    for(auto input:{pair_fixture(100.f),pair_fixture(0.f,3,true),pair_fixture(0.f,65535)}){
        assert(ModelBank::open(input.data(),input.size(),bank)==Error::none);
        MeshGroups split(bank);assert(split.size()==2);
    }
}
static void compact_topology(){
    auto input=pair_fixture(.5f,12,true);put(input,272,12);put(input,300,3);put(input,304,9);seal(input);ModelBank bank;
    assert(ModelBank::open(input.data(),input.size(),bank)==Error::none);
    BatchGeometry cache;
    assert(cache.prepare(bank,1)==Error::budget&&cache.bytes()==0&&!cache.source(0));
    assert(cache.prepare(bank,4096)==Error::none&&cache.bytes()>0&&cache.bytes()<4096);
    assert(!cache.source(2));
    assert(cache.prepare(bank,4096)==Error::invalid_phase);
    BatchGeometry tight;assert(tight.prepare(bank,cache.bytes()-1)==Error::budget&&tight.bytes()==0&&!tight.source(0));
    for(unsigned p=0;p<bank.parts();++p){const auto part=bank.part(p);const auto original=bank.mesh(part.mesh);const auto compact=cache.source(p);
        assert(compact.vertex_count()==3&&compact.index_count()==part.count&&compact.normals()==original.normals);
        const auto stride=original.normals?sizeof(NormalVertex):sizeof(Vertex);
        for(unsigned i=0;i<part.count;++i){const auto* index=original.indices+(part.first+i)*2;
            const unsigned source=index[0]|(unsigned(index[1])<<8),dest=compact.indices()[i];
            assert(dest<compact.vertex_count());
            assert(!std::memcmp(&compact.vertices()[dest].source,original.vertices+source*stride,stride));
            assert((compact.vertices()[dest].rows==std::array<std::uint8_t,4>{}));
        }
    }
    input.clear();input.shrink_to_fit();const auto bytes=cache.bytes();BatchGeometry moved(std::move(cache));
    assert(cache.bytes()==0&&!cache.source(0)&&moved.bytes()==bytes);
    assert(moved.source(0).vertex_count()==3&&moved.source(0).index_count()==12);
    moved.reset();assert(!moved.bytes()&&!moved.source(0));
}
int main(int argc,char** argv) {
    grouped_geometry();compact_topology();
    auto bytes=fixture();ModelBank bank;
    assert(ModelBank::open(bytes.data(),bytes.size(),bank)==Error::none);
    assert(bank.meshes()==1 && bank.parts()==1 && bank.matrices()==1 && bank.images()==0);
    assert(bank.mesh(0).vertex_bytes==72 && bank.part(0).count==3);
    assert(bank.has_distinct_index_triangle(0));
    auto degenerate=bytes;degenerate[330]=0;seal(degenerate);ModelBank placeholder;
    assert(ModelBank::open(degenerate.data(),degenerate.size(),placeholder)==Error::none);
    assert(!placeholder.has_distinct_index_triangle(0));
    // No collinearity heuristic: distinct indices remain drawable even when
    // authoring places the vertices at the same position.
    assert(bank.has_distinct_index_triangle(0));
    const auto* retained=bank.mesh(0).vertices;
    // v1 used the opposite vertical image/UV convention. Its valid CRC must
    // not allow it to enter the v2 sampler contract.
    auto legacy=bytes;legacy[7]='1';put(legacy,8,1);seal(legacy);
    assert(ModelBank::open(legacy.data(),legacy.size(),bank)==Error::invalid_data);
    assert(bank.mesh(0).vertices==retained);
    auto broken=bytes;broken[270]^=1;
    assert(ModelBank::open(broken.data(),broken.size(),bank)==Error::invalid_data);
    assert(bank.mesh(0).vertices==retained); // failed open is transactional
    for(auto p:{std::pair<unsigned,unsigned>{16,0xffffffffu},{32,0xfffffffcu},{128,128},{136,0xfffffffcu},
                {132,71},{140,4},{144,1},{160,999},{184,0x7fc00000u},{232,1},
                {164,2},{172,12},{172,48},{172,64},{88,2},{92,1},
                {236,3},{240,6},{272,0x7f800000u},{328,65535}}){
        broken=bytes;put(broken,p.first,p.second);seal(broken);
        ModelBank invalid;
        assert(ModelBank::open(broken.data(),broken.size(),invalid)!=Error::none);
        assert(!invalid);
    }
    std::vector<std::uint8_t> unaligned(bytes.size()+1);
    auto wrapped=bytes;put(wrapped,172,3|(1<<2)|(2<<4));seal(wrapped);
    assert(ModelBank::open(wrapped.data(),wrapped.size(),bank)==Error::none);
    assert(bank.surface(0).material.wrap_s==Wrap::repeat && bank.surface(0).material.wrap_t==Wrap::mirror);
    std::copy(bytes.begin(),bytes.end(),unaligned.begin()+1);
    assert(ModelBank::open(unaligned.data()+1,bytes.size(),bank)==Error::none);
    if(argc==2){
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<std::uint8_t> other((std::istreambuf_iterator<char>(input)),{});
        assert(!other.empty() && ModelBank::open(other.data(),other.size(),bank)==Error::none);
        const MeshGroups groups(bank);
        for(unsigned g=0;g<groups.size();++g){const auto& shape=groups.group(g);
            std::vector<std::uint8_t> vertices(shape.vertex_bytes),indices(shape.index_bytes);
            groups.copy(g,vertices.data(),indices.data());
            for(unsigned m=0;m<bank.meshes();++m){const auto placed=groups.placement(m);if(placed.group!=g)continue;
                const auto mesh=bank.mesh(m);UvDomain domain;
                assert(validate(mesh,&domain)==Error::none);
                for(unsigned w=8;w<=1024;w*=2)for(unsigned h=8;h<=1024;h*=2)
                    assert(domain.accepts(w,h)==shape.uv.accepts(w,h));
                const unsigned stride=mesh.normals?sizeof(NormalVertex):sizeof(Vertex);
                assert(mesh.normals==shape.normals);
                assert(!std::memcmp(vertices.data()+placed.vertex*stride,mesh.vertices,mesh.vertex_bytes));
                for(std::size_t i=0;i<mesh.index_bytes;i+=2){
                    const unsigned source=mesh.indices[i]|(unsigned(mesh.indices[i+1])<<8);
                    const unsigned output=indices[placed.index*2+i]|(unsigned(indices[placed.index*2+i+1])<<8);
                    assert(output==source+placed.vertex);
                }
            }
        }
        std::printf("native grouping: source=%u groups=%u\n",bank.meshes(),groups.size());
        std::printf("native bank accepted: meshes=%u images=%u materials=%u parts=%u\n",
            bank.meshes(),bank.images(),bank.materials(),bank.parts());
    }
    std::puts("native bank bounds, CRC, references, finite values and unaligned input passed");
}
