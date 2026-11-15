#include "canvas_catalog.hpp"
#include <cassert>
#include <fstream>
#include <vector>
#include <iterator>
#include <cstring>
using namespace sm64ds::render;
namespace {
std::uint32_t u32(const std::vector<std::uint8_t>& b,unsigned at){return b[at]|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;}
void put(std::vector<std::uint8_t>& b,unsigned at,std::uint32_t n){for(unsigned i=0;i<4;++i)b[at+i]=std::uint8_t(n>>(8*i));}
void seal(std::vector<std::uint8_t>& b){put(b,52,ModelBank::checksum(b.data(),b.size()));}
}
int main(int argc,char** argv){
    assert(argc==2);std::ifstream file(argv[1],std::ios::binary);const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    ModelBank bank;assert(ModelBank::open(bytes.data(),bytes.size(),bank)==Error::none);
    CanvasBinding binding{5001,{},1,1};for(unsigned i=0;i<32;++i)binding.source_sha256[i]=i;
    assert(validate(bank,binding)==Error::none);
    CanvasBinding entries[]={binding,binding};entries[1].bank_id=5002;CanvasCatalog catalog;
    assert(CanvasCatalog::open(entries,2,catalog)==Error::none&&catalog.find(5001)==entries&&catalog.find(5002)==entries+1&&!catalog.find(1));
    entries[1].bank_id=5001;assert(CanvasCatalog::open(entries,2,catalog)==Error::invalid_data&&catalog.size()==2);entries[1].bank_id=5002;
    assert(CanvasCatalog::open(nullptr,1,catalog)==Error::invalid_data);
    binding.bank_id=65536;assert(validate(bank,binding)==Error::invalid_data);binding.bank_id=5001;
    binding.source_sha256[0]=1;assert(validate(bank,binding)==Error::invalid_data);binding.source_sha256[0]=0;
    binding.images=2;assert(validate(bank,binding)==Error::invalid_data);binding.images=1;
    binding.parts=2;assert(validate(bank,binding)==Error::invalid_data);binding.parts=1;
    const auto mt=u32(bytes,40),mesh=u32(bytes,32),vertex=u32(bytes,mesh);
    const auto policy=[&](unsigned at,std::uint32_t value){auto changed=bytes;put(changed,at,value);seal(changed);ModelBank source;
        assert(ModelBank::open(changed.data(),changed.size(),source)==Error::none&&validate(source,binding)==Error::invalid_data);};
    policy(mt+4,0); // opaque material is not a prepared alpha-masked canvas
    policy(mt+8,1); // backface culling is not a screen-space contract
    policy(mt+12,1);policy(mt+16,1);policy(mt+44,0x888888ff);
    float f=1.25f;std::uint32_t bits;std::memcpy(&bits,&f,4);policy(vertex,bits);
    f=.25f;std::memcpy(&bits,&f,4);policy(vertex+8,bits);
    auto matrices=bytes;put(matrices,48,2);seal(matrices);assert(ModelBank::open(matrices.data(),matrices.size(),bank)==Error::none&&validate(bank,binding)==Error::invalid_data);
}
