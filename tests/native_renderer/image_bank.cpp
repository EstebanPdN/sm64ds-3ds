#include "image_bank.hpp"
#include "image_catalog.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace sm64ds::render;
static void put(std::vector<std::uint8_t>& b,unsigned at,std::uint32_t v){
    for(unsigned i=0;i<4;++i)b[at+i]=std::uint8_t(v>>(8*i));
}
static void seal(std::vector<std::uint8_t>& b){put(b,28,ImageBank::checksum(b.data(),b.size()));}
static std::vector<std::uint8_t> fixture(){
    std::vector<std::uint8_t> b(512);
    std::copy_n("SM64NIM1",8,b.begin());
    for(auto p:{std::pair<unsigned,unsigned>{8,1},{12,512},{16,1},{20,128},{24,256},
                {128,256},{132,256},{136,8},{140,8},{144,1}})put(b,p.first,p.second);
    b[32]=77;std::fill(b.begin()+256,b.end(),137);seal(b);return b;
}
int main(int argc,char** argv){
    auto b=fixture();ImageBank bank;
    assert(ImageBank::open(b.data(),b.size(),bank)==Error::none);
    assert(bank.images()==1&&bank.gpu_bytes()==256&&bank.source_sha256()[0]==77);
    assert(bank.image(0).data==b.data()+256&&bank.image(0).width==8&&bank.image(0).height==8);
    assert(bank.image(0).alpha==ImageBank::Alpha::palette_zero_mask&&!bank.image(1).data);
    const auto* retained=bank.image(0).data;
    assert(ImageBank::open(nullptr,b.size(),bank)==Error::invalid_data);
    for(unsigned n=0;n<b.size();++n){
        assert(ImageBank::open(b.data(),n,bank)!=Error::none);
        assert(bank.image(0).data==retained);
    }
    for(auto p:{std::pair<unsigned,unsigned>{0,0},{8,2},{12,511},{16,0},{16,129},{16,0xffffffffu},
                {20,160},{24,255},{64,1},{128,128},{128,0xffffff80u},{132,0xffffffffu},
                {136,0},{136,9},{136,2048},{140,16},{144,2},{148,1},{160,1}}){
        auto bad=b;put(bad,p.first,p.second);seal(bad);
        assert(ImageBank::open(bad.data(),bad.size(),bank)!=Error::none);
        assert(bank.image(0).data==retained);
    }
    // Payload tampering, trailing bytes and overlap remain errors even with
    // otherwise legal metadata or a recomputed checksum.
    auto bad=b;bad.back()^=1;assert(ImageBank::open(bad.data(),bad.size(),bank)!=Error::none);
    bad=b;bad.push_back(0);put(bad,12,bad.size());seal(bad);
    assert(ImageBank::open(bad.data(),bad.size(),bank)!=Error::none);
    std::vector<std::uint8_t> unaligned(b.size()+1);
    std::copy(b.begin(),b.end(),unaligned.begin()+1);
    assert(ImageBank::open(unaligned.data()+1,b.size(),bank)==Error::none);
    ImageAsset assets[]={{8,8,ImageBank::Alpha::palette_zero_mask}};
    ImageBankBinding bindings[]={{1,{},assets,1},{3,{},assets,1}};
    bindings[0].source_sha256[0]=77;bindings[1].source_sha256[0]=77;
    ImageCatalog catalog;
    assert(ImageCatalog::open(bindings,2,catalog)==Error::none&&catalog.size()==2);
    assert(catalog.find(1)==bindings&&catalog.find(3)==bindings+1&&!catalog.find(2));
    assert(validate(bank,bindings[0])==Error::none);
    bindings[0].source_sha256[1]=1;assert(validate(bank,bindings[0])==Error::invalid_data);bindings[0].source_sha256[1]=0;
    assets[0].alpha=ImageBank::Alpha::independent;assert(validate(bank,bindings[0])==Error::invalid_data);
    assets[0].alpha=static_cast<ImageBank::Alpha>(2);
    assert(ImageCatalog::open(bindings,2,catalog)==Error::invalid_data&&catalog.size()==2);
    assets[0].alpha=ImageBank::Alpha::palette_zero_mask;
    bindings[1].bank_id=1;assert(ImageCatalog::open(bindings,2,catalog)==Error::invalid_data);
    assert(ImageCatalog::open(nullptr,1,catalog)==Error::invalid_data);
    auto a4=fixture();a4.resize(384);std::fill(a4.begin()+256,a4.end(),0);
    std::copy_n("SM64NIM2",8,a4.begin());put(a4,8,2);put(a4,12,a4.size());put(a4,24,32);put(a4,132,32);put(a4,148,1);
    std::fill(a4.begin()+256,a4.begin()+288,0x8f);seal(a4);
    ImageBank mask;assert(ImageBank::open(a4.data(),a4.size(),mask)==Error::none&&mask.gpu_bytes()==32);
    assert(mask.image(0).format==TextureFormat::alpha4&&mask.image(0).bytes==32);
    assets[0]={8,8,ImageBank::Alpha::palette_zero_mask,TextureFormat::alpha4};
    assert(validate(mask,bindings[0])==Error::none&&validate(bank,bindings[0])==Error::invalid_data);
    for(auto p:{std::pair<unsigned,unsigned>{148,2},{132,31},{288,1},{24,128}}){
        auto bad=a4;put(bad,p.first,p.second);seal(bad);assert(ImageBank::open(bad.data(),bad.size(),mask)!=Error::none);
    }
    assert(texture_bytes(8,8,TextureFormat::alpha4)==32&&texture_bytes(8,8,TextureFormat::rgba8)==256);
    assert(!texture_bytes(8,8,static_cast<TextureFormat>(2)));
    if(argc==3&&!std::strcmp(argv[1],"--catalog")){
        catalog=prepared_image_catalog();assert(catalog.size());
        for(std::size_t i=0;i<catalog.size();++i){
            const auto* binding=catalog.at(i);assert(binding);
            const auto path=std::string(argv[2])+"/"+std::string(5-std::to_string(binding->bank_id).size(),'0')+std::to_string(binding->bank_id)+".nim";
            std::ifstream input(path,std::ios::binary);std::vector<std::uint8_t> external((std::istreambuf_iterator<char>(input)),{});
            assert(ImageBank::open(external.data(),external.size(),bank)==Error::none&&validate(bank,*binding)==Error::none);
        }
        std::printf("image-only catalog validated: banks=%zu\n",catalog.size());
    }else if(argc>=2){
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<std::uint8_t> external((std::istreambuf_iterator<char>(input)),{});
        assert(ImageBank::open(external.data(),external.size(),bank)==Error::none);
        if(argc==3){
            catalog=prepared_image_catalog();const auto* binding=catalog.find(std::strtoul(argv[2],nullptr,10));
            assert(binding&&validate(bank,*binding)==Error::none);
        }
        std::printf("images=%u gpuBytes=%zu\n",bank.images(),bank.gpu_bytes());
    }
    std::puts("image-only bank bounds, CRC, canonical ranges and transactional borrow passed");
}
