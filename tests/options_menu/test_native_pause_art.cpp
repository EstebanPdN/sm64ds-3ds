#include "native_pause_art.hpp"
#include "native_lower_state.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
using enhance::native_pause::Menu;
struct Surface {
 std::array<uint32_t,320*240> pixels{};
 void pixel(int x,int y,uint32_t c){assert(x>=0&&x<320&&y>=0&&y<240);pixels[y*320+x]=c;}
};
void key(Menu& m,unsigned k){m.update({k,false,0,0});}
int main(int argc,char** argv){
 constexpr uint32_t original=0xffffffff;
 const auto filtered=native_lower::lower_dispcnt(original,true);
 assert(native_lower::lower_dispcnt(original,false)==original);
 assert((filtered&((1u<<8)|(1u<<9)|(1u<<12)))==0);
 assert((filtered&~((1u<<8)|(1u<<9)|(1u<<12)))==(original&~((1u<<8)|(1u<<9)|(1u<<12))));
 assert((filtered&(3u<<10))==(3u<<10)); // preserve BG2/BG3 map, display mode, windows, brightness controls
 assert(native_lower::text_width("Changes apply when you resume")<=280);
 Surface field;
 native_lower::button(field,48,40,224,40,"Resolution: Native",true);
 for(auto c:field.pixels)assert(c!=0xff529400&&c!=0xff80bf21&&c!=0xffb6df42); // field font stays brown for either value
 Surface out;uint16_t positions[]={0x80,0x120,0x1c0,0x260};native_lower::root(out,2,4,positions);
 Surface version;native_lower::version(version);
 unsigned version_pixels=0;
 for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)if(version.pixels[y*320+x]){
  ++version_pixels;assert(x>=6&&x<110&&y>=231&&y<239);
  assert(out.pixels[y*320+x]==version.pixels[y*320+x]);
 }
 assert(version_pixels>20&&sm64ds_build::number>0);
 assert(out.pixels[0]==0&&out.pixels[30*320+160]==0); // transparent original map remains outside UI
 Menu m;const auto settings=enhance::Settings::safe();
 for(unsigned category=0;category<4;++category){
  m.begin(settings,0,false);for(unsigned i=0;i<category;++i)key(m,128);key(m,1);
  for(unsigned row=0;row<=m.count();++row){out.pixels.fill(0);native_lower::menu(out,m);key(m,128);}
 }
 m.begin(settings,0,true);native_lower::menu(out,m);key(m,1);for(unsigned i=0;i<4;++i){native_lower::menu(out,m);key(m,128);}
 m.begin(settings,0,false);key(m,128);key(m,128);key(m,1);key(m,128);key(m,128);key(m,1);native_lower::menu(out,m);
 out.pixels.fill(0);native_lower::menu(out,m);
 assert(out.pixels[210*320+150]==0); // confirmation has only KEEP/DELETE, no redundant OK
 assert(out.pixels[108*320+160]==0); // real gap between the horizontal actions
 assert(out.pixels[100*320+40]!=0&&out.pixels[100*320+180]!=0);
 m.begin_general(settings,0);out.pixels.fill(0);native_lower::menu(out,m);
 assert(out.pixels[52*320+40]==0xffde8400); // orange action face replaces separate outlined label
 assert(out.pixels[52*320+300]!=0); // full-width action reaches the right bevel
 auto dumping=settings;dumping.developer_dumps=true;
 m.begin(dumping,0,false);key(m,128);key(m,128);key(m,1);
 out.pixels.fill(0);native_lower::menu(out,m); // L+R+A sprite layout must remain inside320x240
 for(unsigned volume:{0u,100u,200u}){
  auto s=settings;s.audio_volume=volume;m.begin(s,0,false);key(m,1);out.pixels.fill(0);native_lower::menu(out,m);
  assert(out.pixels[174*320+160]==0); // transparent map gap below the volume card
 }
 if(argc>1){FILE* f=std::fopen(argv[1],"wb");assert(f);std::fprintf(f,"P6\n320 240\n255\n");for(auto c:out.pixels){unsigned char rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};assert(std::fwrite(rgb,3,1,f)==1);}assert(std::fclose(f)==0);}
}
