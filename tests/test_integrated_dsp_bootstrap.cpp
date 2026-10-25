// Synthetic parser/BLZ fixtures only. No DSP firmware or signed system code.
#include "dsp_bootstrap.hpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

using Bytes=std::vector<uint8_t>;
// Model the actual pre-game condition: the port's global C++ allocator is
// unavailable until DS Heap::SetupRootHeap. Boot vectors must bypass it.
static bool forbid_global_new=false;
static std::size_t global_new_calls=0;
void* operator new(std::size_t size) {
    ++global_new_calls;
    if(forbid_global_new)throw std::bad_alloc();
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static unsigned checks=0;
static void check(bool value) { ++checks;assert(value); }
static void put32(Bytes& p,std::size_t at,uint32_t v) {
    for(unsigned i=0;i<4;++i)p.at(at+i)=uint8_t(v>>(i*8));
}
static std::string hex(const std::array<uint8_t,32>& hash) {
    static const char digits[]="0123456789abcdef";
    std::string result;for(uint8_t x:hash){result+=digits[x>>4];result+=digits[x&15];}return result;
}
static Bytes component() {
    Bytes p(0x303);
    std::memcpy(p.data()+0x100,"DSP1",4);put32(p,0x104,uint32_t(p.size()));
    p[0x108]=p[0x109]=0xff;p[0x10e]=1;p[0x10f]=1;
    put32(p,0x120,0x300);put32(p,0x124,0x100);put32(p,0x128,3);
    // Independent standard SHA256("abc") test vector.
    static constexpr uint8_t digest[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,
        0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    std::memcpy(p.data()+0x130,digest,32);std::memcpy(p.data()+0x300,"abc",3);return p;
}
static void parser_tests() {
    using namespace dsp_bootstrap;
    check(hex(sha256(nullptr,0))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const uint8_t abc[]={'a','b','c'};
    check(hex(sha256(abc,sizeof abc))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string million(1000000,'a');
    check(hex(sha256(reinterpret_cast<const uint8_t*>(million.data()),million.size()))==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    Sha256 incremental;incremental.update(abc,1);incremental.update(abc+1,2);
    check(hex(incremental.finish())==hex(sha256(abc,sizeof abc)));
    const Bytes good=component();Component view;char error[192]{};
    check(validate_component(good.data(),good.size(),view,error,sizeof error));
    check(view.offset==0 && view.size==good.size() && view.sections==1);
    check(!validate_component(nullptr,good.size(),view));
    check(!validate_component(good.data(),0x2ff,view));
    check(!validate_component(good.data(),MaxComponent+1,view));
    for(auto which:{0,1,2,3,4,5,6,7,8,9,10}) {
        Bytes bad=good;
        switch(which) {
        case 0:bad[0x100]='X';break;
        case 1:put32(bad,0x104,0xffffffffu);break;
        case 2:bad[0x10e]=0;break;
        case 3:bad[0x10e]=11;break;
        case 4:bad[0x10f]=4;break;
        case 5:bad[0x118]=1;break;
        case 6:put32(bad,0x120,0xffffffffu);break;
        case 7:put32(bad,0x124,0xffffffffu);break;
        case 8:put32(bad,0x128,0xffffffffu);break;
        case 9:bad[0x12f]=3;break;
        default:bad[0x300]^=1;break;
        }
        check(!validate_component(bad.data(),bad.size(),view,error,sizeof error));
        check(error[0]!=0);
    }
    Bytes special=good;special[0x10f]=3;special[0x10d]=2;
    put32(special,0x110,0xffff);put32(special,0x114,2);
    check(validate_component(special.data(),special.size(),view));
    put32(special,0x114,3);check(!validate_component(special.data(),special.size(),view));
    Bytes overlap=good;overlap[0x10e]=2;
    std::memcpy(overlap.data()+0x150,overlap.data()+0x120,0x30);
    check(!validate_component(overlap.data(),overlap.size(),view));
    Bytes code(89+good.size()+333,0x5a);
    std::memcpy(code.data()+89,good.data(),good.size());
    std::memcpy(code.data()+0x100,"DSP1",4);put32(code,0x104,1);
    // The deliberately invalid earlier magic changes signature bytes only.
    check(find_component(code.data(),code.size(),view,error,sizeof error));
    check(view.offset==89 && view.size==good.size());
    Bytes duplicate=good;duplicate.insert(duplicate.end(),good.begin(),good.end());
    check(!find_component(duplicate.data(),duplicate.size(),view,error,sizeof error));
    check(!find_component(nullptr,999,view));
    check(!find_component(good.data(),MaxCode+1,view));
    check(home_menu_title(0)==0x0004003000008202ull);
    check(home_menu_title(1)==0x0004003000008f02ull);
    check(home_menu_title(2)==home_menu_title(3));
    check(home_menu_title(6)==0x000400300000b102ull);
    check(home_menu_title(7)==0);
}

static void blz_tests() {
    using namespace dsp_bootstrap;
    // Reverse BLZ: three literal 'A' bytes followed by three length-18,
    // distance-3 copies. Eighteen stored bytes expand to 57 'A' bytes.
    const Bytes packed={0x00,0xf0,0x00,0xf0,0x00,0xf0,'A','A','A',0x1c,
                        18,0,0,8,39,0,0,0};
    BlzLayout layout;char error[192]{};
    check(blz_layout(packed.data()+packed.size()-8,packed.size(),layout));
    check(layout.decoded==57 && layout.stop==0 && layout.input==10);
    Bytes buffer(layout.decoded);std::memcpy(buffer.data(),packed.data(),packed.size());
    check(decompress_blz_in_place(buffer.data(),buffer.size(),layout,error,sizeof error));
    for(uint8_t b:buffer)check(b=='A');
    Bytes prefix(20);for(unsigned i=0;i<20;++i)prefix[i]=uint8_t(i);
    prefix.insert(prefix.end(),packed.begin(),packed.end());
    check(blz_layout(prefix.data()+prefix.size()-8,prefix.size(),layout));
    buffer.assign(layout.decoded,0);std::memcpy(buffer.data(),prefix.data(),prefix.size());
    check(decompress_blz_in_place(buffer.data(),buffer.size(),layout));
    for(unsigned i=0;i<20;++i)check(buffer[i]==i);
    for(unsigned i=20;i<buffer.size();++i)check(buffer[i]=='A');
    check(!blz_layout(nullptr,18,layout));
    check(!blz_layout(packed.data()+10,7,layout));
    check(!blz_layout(packed.data()+10,MaxCode+1,layout));
    for(unsigned which=0;which<5;++which) {
        Bytes bad=packed;
        switch(which) {
        case 0:bad[13]=7;break; // footer shorter than eight bytes
        case 1:put32(bad,14,0xffffffffu);break;
        case 2:bad[9]=0x80;break; // reference before any output exists
        case 3:bad[9]=0;break; // truncated literal stream
        default:bad[14]=40;break; // output one byte longer than stream
        }
        bool accepted=false;
        if(blz_layout(bad.data()+bad.size()-8,bad.size(),layout)) {
            buffer.assign(layout.decoded,0);std::memcpy(buffer.data(),bad.data(),bad.size());
            accepted=decompress_blz_in_place(buffer.data(),buffer.size(),layout);
        }
        check(!accepted);
    }
    // Bounded mutation sweep exercises malformed footer/backref combinations
    // under sanitizers. Mutated valid streams may decode to different bytes.
    for(std::size_t at=0;at<packed.size();++at)for(unsigned bit=0;bit<8;++bit) {
        Bytes mutated=packed;mutated[at]^=uint8_t(1u<<bit);
        if(!blz_layout(mutated.data()+mutated.size()-8,mutated.size(),layout) || layout.decoded>1024)continue;
        buffer.assign(layout.decoded,0);std::memcpy(buffer.data(),mutated.data(),mutated.size());
        decompress_blz_in_place(buffer.data(),buffer.size(),layout,error,sizeof error);
        ++checks;
    }
}

static void allocator_tests() {
    using namespace dsp_bootstrap;
    const Bytes good=component();
    bool detected=false;forbid_global_new=true;
    try {void* p=::operator new(17);::operator delete(p);}
    catch(const std::bad_alloc&){detected=true;}
    forbid_global_new=false;check(detected);
    const std::size_t before=global_new_calls;
    forbid_global_new=true;
    {
        boot_memory::Vector<uint8_t> image(64);
        image.resize(MaxCode);
        std::memcpy(image.data()+64,good.data(),good.size());
        Result result;Component view{64,good.size(),1};
        check(detail::copy_component(image,view,"synthetic-test",result));
        check(detail::retained.size()==good.size());
        check(std::memcmp(detail::retained.data(),good.data(),good.size())==0);
        // Image growth, retained growth, a replacement copy and teardown all
        // execute the same allocator type and retention helper as the device.
        image.resize(128);image.shrink_to_fit();image.resize(MaxCode);
        detail::retained.resize(4096);
        check(detail::retained.size()==4096);
        check(std::memcmp(detail::retained.data(),good.data(),good.size())==0);
        std::memcpy(image.data()+64,good.data(),good.size());
        check(detail::copy_component(image,view,"synthetic-replacement",result));
        check(std::strcmp(detail::retained_source,"synthetic-replacement")==0);
        check(detail::retained.size()==good.size());
        Component rejected{image.size(),1,1};
        check(!detail::copy_component(image,rejected,"invalid",result));
        check(result.code==-1 && result.error[0]);
        check(detail::retained.size()==good.size());
        release_after_audio_shutdown();
        check(detail::retained.empty() && detail::retained.capacity()==0);
        release_after_audio_shutdown();
        check(global_new_calls==before);
    }
    forbid_global_new=false;check(global_new_calls==before);
}

int main(int argc,char** argv) {
    parser_tests();blz_tests();allocator_tests();
    if(argc==3) {
        auto load=[](const char* path){
            std::FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);
            const long size=std::ftell(f);assert(size>=8 && std::size_t(size)<=dsp_bootstrap::MaxCode);
            std::rewind(f);Bytes data(static_cast<std::size_t>(size));
            assert(std::fread(data.data(),1,data.size(),f)==data.size());std::fclose(f);return data;
        };
        const Bytes compressed=load(argv[1]),expected=load(argv[2]);
        dsp_bootstrap::BlzLayout layout;
        check(dsp_bootstrap::blz_layout(compressed.data()+compressed.size()-8,compressed.size(),layout));
        Bytes work(layout.decoded);std::memcpy(work.data(),compressed.data(),compressed.size());
        check(dsp_bootstrap::decompress_blz_in_place(work.data(),work.size(),layout));check(work==expected);
    } else check(argc==1);
#ifndef __3DS__
    check(!dsp_bootstrap::ensure().ok);dsp_bootstrap::release_after_audio_shutdown();
#endif
    std::printf("{\"status\":\"passed\",\"checks\":%u,\"ASan_UBSan_required\":true,"
                "\"fixture\":\"synthetic_only_no_firmware\",\"boot_allocations_without_global_new\":true,"
                "\"physical_DSP\":false}\n",checks);
}
