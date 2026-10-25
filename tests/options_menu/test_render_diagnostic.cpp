#include "render_diagnostic.hpp"
#include "diagnostic_files.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
void v3ds_gpu_candidate_geometry(uint32_t* l,uint32_t* r){std::fill_n(l,256*192,0xff123456);std::fill_n(r,256*192,0xff654321);}
void v3ds_gpu_candidate_panel(unsigned n,uint32_t* out){assert(n==0||n==2);std::fill_n(out,256*192,0xffabcdef+n);}
void v3ds_read_bottom(uint32_t* out){std::fill_n(out,320*240,0xff314159);}
bool final_readback_ok=true;
bool v3ds_diagnostic_final_top(uint32_t* out){std::fill_n(out,400*240,0xff271828);return final_readback_ok;}
bool bottom_readback_ok=true;
bool v3ds_diagnostic_final_bottom(uint32_t* out){std::fill_n(out,320*240,0xffc0ffee);return bottom_readback_ok;}
std::vector<uint32_t> words(const std::string& p){std::ifstream f(p,std::ios::binary|std::ios::ate);assert(f);auto bytes=f.tellg();assert(bytes%4==0);std::vector<uint32_t> r(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(r.data()),bytes);assert(f);return r;}
int main(int argc,char** argv){
 assert(argc==1||argc==2);char temporary[]="/tmp/sm64ds-render-XXXXXX";
 const char* directory=argc==2?argv[1]:mkdtemp(temporary);assert(directory);
 const std::string dump=std::string(directory)+"/000-dump-20261024-101500";
 assert(mkdir(dump.c_str(),0777)==0);
 assert(native_diagnostics::capture_render(dump.c_str()));
 auto r=words(dump+"/render.bin"),u=words(dump+"/upper.bin"),l=words(dump+"/lower.bin");
 assert(r.size()*4==897040&&u.size()*4==580624);
 assert(r[0]==0x34524d53&&r[1]==1&&u[0]==0x31554d53&&u[1]==2&&u[2]==400&&u[3]==240);
 assert(r[4]==0xff123456&&r[4+256*192]==0xff654321&&r.back()==0xff314159);
 assert(u[4]==0xffabcdef&&u[4+256*192]==0xff271828&&u.back()==0xff271828);
 assert(l.size()*4==307216&&l[0]==0x314c4d53&&l[1]==1&&l[2]==320&&l[3]==240);
 assert(l[4]==0xffc0ffee&&l.back()==0xffc0ffee);
 assert(native_diagnostics::dump_name("native-0000000000000001.upper"));
 assert(native_diagnostics::dump_name("native-0000000000000001.lower"));
 assert(!native_diagnostics::dump_name("save.lower")&&!native_diagnostics::dump_name("native-0000000000000001.lower.backup"));
 assert(!native_diagnostics::dump_name("save.upper")&&!native_diagnostics::dump_name("native-0000000000000001.upper.backup"));
 assert(native_diagnostics::dump_name("native-0000000000000001.lcd"));
 assert(!native_diagnostics::capture_render((dump+"/missing").c_str()));
 final_readback_ok=false;assert(!native_diagnostics::capture_render(dump.c_str()));
 final_readback_ok=true;bottom_readback_ok=false;assert(!native_diagnostics::capture_render(dump.c_str()));
 // Clean Dumps / L+R+X: numbered folders under dumps/ and legacy loose files.
 const std::string dumps=std::string(directory)+"/dumps";assert(mkdir(dumps.c_str(),0777)==0);
 assert(std::rename(dump.c_str(),(dumps+"/000-dump-20261024-101500").c_str())==0);
 {FILE* f=std::fopen((std::string(directory)+"/native-0000000000000002.vm").c_str(),"wb");assert(f);std::fclose(f);}
 {FILE* f=std::fopen((std::string(directory)+"/keep.txt").c_str(),"wb");assert(f);std::fclose(f);}
 const auto deleted=native_diagnostics::delete_dumps(directory);
 assert(deleted.failed==0&&deleted.removed==4); // render/upper/lower + legacy .vm
 assert(access((dumps+"/000-dump-20261024-101500").c_str(),F_OK)!=0);
 assert(access((std::string(directory)+"/keep.txt").c_str(),F_OK)==0);
 if(argc==1){assert(std::remove((std::string(directory)+"/keep.txt").c_str())==0);assert(rmdir(dumps.c_str())==0);assert(rmdir(directory)==0);}
}
