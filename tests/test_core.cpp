#include "sm64ds/assets.hpp"
#include "sm64ds/input.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

static void p16(std::vector<uint8_t>& b,size_t o,uint16_t v) {b[o]=uint8_t(v);b[o+1]=uint8_t(v>>8);}
static void p32(std::vector<uint8_t>& b,size_t o,uint32_t v) {for(int i=0;i<4;++i)b[o+i]=uint8_t(v>>(8*i));}
static void header_crc(std::vector<uint8_t>& b) {
    uint16_t c=0xffff;for(size_t i=0;i<0x15e;++i) {c^=b[i];for(int bit=0;bit<8;++bit)c=(c>>1)^((c&1)?0xa001:0);}
    p16(b,0x15e,c);
}
static void write(const std::string& p,const std::vector<uint8_t>& b) {std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f.good());}
static std::vector<uint8_t> load(const std::string& p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main() {
    assert(sm64ds::crc32("123456789",9)==0xcbf43926);
    auto a=sm64ds::normalize_stick(156,156);assert(std::abs(std::hypot(a.x,a.y)-1)<1e-6);
    assert(sm64ds::normalize_stick(7,-8).x==0);
    assert(sm64ds::normalize_stick(156,0).x==1);
    assert(sm64ds::normalize_stick(60,0).x>0 && sm64ds::normalize_stick(60,0).x<1);
    assert(sm64ds::touch_to_ds(319,320,256)==255);
    char temp[]="/tmp/sm64ds-test-XXXXXX";assert(mkdtemp(temp));
    std::string base=temp,rom=base+"/rom.nds",pak=base+"/assets.pak",error;
    std::vector<uint8_t> b(1024,0);memcpy(b.data(),"S.MARIO64DS",10);memcpy(b.data()+12,"ASME",4);
    p32(b,0x40,0x200);p32(b,0x44,15);p32(b,0x48,0x220);p32(b,0x4c,8);
    p32(b,0x200,8);p16(b,0x206,1);b[0x208]=5;memcpy(b.data()+0x209,"a.bin",5);
    p32(b,0x220,0x240);p32(b,0x224,0x244);memcpy(b.data()+0x240,"TEST",4);header_crc(b);write(rom,b);
    sm64ds::RomInfo info;assert(sm64ds::inspect_rom(rom,info,error));assert(info.files.size()==2);
    assert(sm64ds::import_rom(rom,pak,error));auto original=load(pak);
    {sm64ds::AssetPack p;assert(p.open(pak,error));assert(p.verify(error));std::vector<uint8_t> d;assert(p.read(p.find("a.bin"),d,error));assert(std::string(d.begin(),d.end())=="TEST");}
    assert(!sm64ds::import_rom(rom,pak,error,[](uint32_t,uint32_t){return false;}));assert(load(pak)==original);
    // A rejected import must preserve the existing complete package.
    p32(b,0x224,0xffffffffu);write(rom,b);assert(!sm64ds::import_rom(rom,pak,error));assert(load(pak)==original);
    p32(b,0x224,0x244);b[0x209]='.';b[0x20a]='.';b[0x20b]='/';write(rom,b);assert(!sm64ds::inspect_rom(rom,info,error));
    memcpy(b.data()+0x209,"a.bin",5);write(rom,b);
    assert(!sm64ds::import_rom(rom,rom,error));assert(load(rom)==b);
    assert(link(rom.c_str(),(pak+".part").c_str())==0);
    assert(!sm64ds::import_rom(rom,pak,error));assert(load(rom)==b);unlink((pak+".part").c_str());
    auto corrupt=original;corrupt.back()^=1;write(pak,corrupt);
    {sm64ds::AssetPack p;assert(p.open(pak,error));assert(!p.verify(error));}
    corrupt=original;corrupt[64+4]^=1;write(pak,corrupt);
    {sm64ds::AssetPack p;assert(!p.open(pak,error));}
    unlink(rom.c_str());unlink(pak.c_str());rmdir(base.c_str());
    puts("core: CRC, analog, touch, extraction, cancellation, aliasing, malformed ROM, corrupt pack OK");
}
