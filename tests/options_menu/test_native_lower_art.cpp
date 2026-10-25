#include "native_lower_painter.hpp"
#include <array>
#include <cassert>
#include <cstdio>
struct Surface {
 std::array<uint32_t,320*240> pixels{};
 unsigned writes=0;
 void pixel(int x,int y,uint32_t c){assert(x>=0&&x<320&&y>=0&&y<240);pixels[y*320+x]=c;++writes;}
};
int main(int argc,char** argv){
 Surface out;
 for(unsigned p:{0u,35u,90u,100u,200u}){
  out.writes=0;native_lower::dump(out,p);assert(out.writes>288*128);
 }
 native_lower::dump(out,0,true);
 assert(native_lower::text_width("Saving complete process memory")<=256);
 assert(native_lower::text_width("Do not turn off the console")<=256);
 native_lower::dump(out,35);
 if(argc>1){FILE* f=std::fopen(argv[1],"wb");assert(f);std::fprintf(f,"P6\n320 240\n255\n");for(auto c:out.pixels){unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};assert(std::fwrite(rgb,3,1,f)==1);}assert(std::fclose(f)==0);}
}
