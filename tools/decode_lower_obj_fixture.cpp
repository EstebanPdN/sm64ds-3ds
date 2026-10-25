// Offline original-plan inspection. No game/emulator runs or RAM writes.
#include "native_obj_plan.h"
#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
static void read(const std::string& path,void* bytes,size_t size){
 std::ifstream input(path,std::ios::binary);
 if(!input.read(static_cast<char*>(bytes),size)||input.peek()!=std::char_traits<char>::eof())throw std::runtime_error(path);
}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 const std::string directory=argv[1];
 std::array<uint8_t,1024> oam{};std::array<uint8_t,512> palette{};
 ntr::HostVector<uint8_t> vram(0x20000);uint32_t display=0;
 read(directory+"/oam.bin",oam.data(),oam.size());read(directory+"/palette.bin",palette.data(),palette.size());
 read(directory+"/vram.bin",vram.data(),vram.size());read(directory+"/display.bin",&display,4);
 ds_gpu::ObjPlan plan;ds_gpu::ObjInput input{display,oam.data(),vram.data(),palette.data(),0x1ffff};
 if(!plan.prepare(input)){std::cerr<<"Original plan refused: "<<unsigned(plan.fallback)<<'\n';return 1;}
 std::cout<<"{\"display\":"<<display<<",\"sprites\":[";
 for(unsigned i=0;i<plan.count;++i){
  const auto& s=plan.sprites[plan.order[i]];ntr::HostVector<uint32_t> pixels;ds_gpu::decode_obj_texture(s,pixels);
  const std::string stem=directory+"/sprite-"+std::to_string(s.index);
  std::ofstream(stem+".argb",std::ios::binary).write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4);
  std::ofstream(stem+".encoded",std::ios::binary).write(reinterpret_cast<const char*>(s.encoded.data()),s.encoded.size());
  std::ofstream(stem+".palette",std::ios::binary).write(reinterpret_cast<const char*>(s.palette.data()),s.palette_bytes);
  if(i)std::cout<<',';
  std::cout<<"{\"index\":"<<s.index<<",\"x\":"<<s.x<<",\"y\":"<<s.y
   <<",\"width\":"<<s.width<<",\"height\":"<<s.height<<",\"priority\":"<<s.priority
   <<",\"affine\":"<<(s.affine?"true":"false")<<",\"hflip\":"<<(s.hflip?"true":"false")
   <<",\"vflip\":"<<(s.vflip?"true":"false")<<'}';
 }
 std::cout<<"]}\n";
}
