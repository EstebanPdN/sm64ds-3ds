#include <unistd.h>
#include <cerrno>
static unsigned sync_calls=0;static bool fail_sync=false;
static int builder_test_fsync(int fd){++sync_calls;if(fail_sync){errno=EIO;return -1;}return ::fsync(fd);}
#define fsync builder_test_fsync
#include "packed_builder.hpp"
#undef fsync
#include <cstdlib>
static unsigned checks=0;static bool forbid_new=false;
void* operator new(std::size_t n){if(forbid_new)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
static void check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL %u: %s\n",checks,why);std::abort();}}
static void copy(const char* from,const char* to){auto* a=std::fopen(from,"rb");auto* b=std::fopen(to,"wb");check(a&&b,"copy open");unsigned char buf[8192];std::size_t n;
    while((n=std::fread(buf,1,sizeof buf,a)))check(std::fwrite(buf,1,n,b)==n,"copy bytes");check(!std::ferror(a),"copy input");check(!std::fclose(a)&&!std::fclose(b),"copy close");}
static bool same(const char* first,const char* second){auto* a=std::fopen(first,"rb");auto* b=std::fopen(second,"rb");check(a&&b,"compare open");unsigned char x[8192],y[8192];bool equal=true;
    for(;;){const auto n=std::fread(x,1,sizeof x,a),m=std::fread(y,1,sizeof y,b);if(n!=m||std::memcmp(x,y,n)){equal=false;break;}if(!n)break;}
    check(!std::fclose(a)&&!std::fclose(b),"compare close");return equal;}
static bool exists(const char* path){struct stat st{};return !::lstat(path,&st);}
static void flip(const char* path,long offset){auto* f=std::fopen(path,"r+b");check(f&&!std::fseek(f,offset,SEEK_SET),"flip open/seek");const int c=std::fgetc(f);check(c!=EOF&&!std::fseek(f,offset,SEEK_SET)&&std::fputc(c^1,f)!=EOF&&!std::fclose(f),"flip byte");}
struct Hook{const char* rom=nullptr;bool mutate=false;bool cancel_write=false;bool cancel_readback=false;};
static bool hook(const rom_bootstrap::Progress& p,void* ctx){auto& h=*static_cast<Hook*>(ctx);
    if(h.mutate&&!std::strcmp(p.phase,"verify-rom")&&p.done==p.total){
        // Last ROM span is deliberately changed after whole-ROM verification;
        // the writer's per-member CRC must still reject it.
        std::size_t index=packed_assets::count;while(index&&rom_install_manifest::files[index-1].generated)--index;
        const auto& entry=rom_install_manifest::files[index-1];
        if(!entry.generated){flip(h.rom,long(entry.offset+entry.size-1));h.mutate=false;}
    }
    if(h.cancel_write&&!std::strcmp(p.phase,"extract")&&p.done>p.total/2)return false;
    if(h.cancel_readback&&!std::strcmp(p.phase,"verify-cache"))return false;
    return true;
}
int main(int argc,char** argv){
    check(argc==5,"usage ROM GOLDEN_PACKAGE CANONICAL_REPO WORK_DIRECTORY");
    char out[2048],part[2054],badrom[2048];std::snprintf(out,sizeof out,"%s/console.sm64ds",argv[4]);
    std::snprintf(part,sizeof part,"%s.part",out);std::snprintf(badrom,sizeof badrom,"%s/damaged.nds",argv[4]);
    check(!exists(out)&&!exists(part),"fresh private test directory");forbid_new=true;
    auto baseline=boot_memory::statistics();
    {sync_calls=0;const auto r=packed_builder::ensure(argv[1],out);check(r.ok,r.error.c_str());check(r.prepared&&r.written==1,"one container written");check(sync_calls==1,"exactly one fsync for complete container");}
    check(same(out,argv[2]),"entire container byte-identical to desktop golden");
    unsigned char a[8192],b[8192];uint64_t compared=0;
    for(unsigned i=0;i<packed_assets::count;++i){packed_assets::Stream stream;check(stream.open(out,i),"open member");const auto& e=rom_install_manifest::files[i];
        char path[2048];std::snprintf(path,sizeof path,"%s/%s",argv[3],e.path);auto* f=std::fopen(path,"rb");check(f,"open canonical");uint32_t done=0;
        while(done<e.size){auto n=e.size-done<sizeof a?e.size-done:sizeof a;check(stream.read(a,n)==n&&std::fread(b,1,n,f)==n&&!std::memcmp(a,b,n),"canonical member bytes");done+=uint32_t(n);compared+=n;}
        check(!stream.close()&&!std::fclose(f),"close member");}
    {sync_calls=0;const auto r=packed_builder::ensure("missing.nds",out);check(r.ok&&r.reused==packed_assets::count&&!r.prepared,"existing valid package preserved without ROM");check(sync_calls==0,"reuse has no fsync");}
    copy(argv[1],badrom);flip(badrom,rom_install_manifest::rom_size-1);
    check(!std::remove(out),"remove private output");
    {const auto r=packed_builder::ensure(badrom,out);check(!r.ok&&r.error.find("Unsupported or damaged ROM")==0,"bad ROM rejected");check(!exists(out)&&!exists(part),"bad ROM performs no writes");}
    {Hook h;h.cancel_write=true;const auto r=packed_builder::ensure(argv[1],out,hook,&h);check(!r.ok&&r.error.find("Cancelled during extract")==0,"cancel halfway");check(!exists(out)&&exists(part),"partial never published");}
    {const auto r=packed_builder::ensure(argv[1],out);check(r.ok,r.error.c_str());check(same(out,argv[2]),"truncated part repaired");}
    check(!std::rename(out,part),"simulate interruption before publish");
    {sync_calls=0;const auto r=packed_builder::ensure(argv[1],out);check(r.ok&&r.prepared,"valid temporary recovered");check(sync_calls==1&&same(out,argv[2]),"recovery reestablishes durability once");}
    check(!std::remove(out),"remove test output again");
    {Hook h;h.cancel_readback=true;const auto r=packed_builder::ensure(argv[1],out,hook,&h);check(!r.ok&&exists(part)&&!exists(out),"readback cancellation never publishes");}
    check(!std::remove(part),"remove verified temporary for mutation case");
    {fail_sync=true;const auto r=packed_builder::ensure(argv[1],out);fail_sync=false;
        check(!r.ok&&exists(part)&&!exists(out),"failed fsync never publishes");}
    {sync_calls=0;const auto r=packed_builder::ensure(argv[1],out);check(r.ok&&sync_calls==1,"failed-sync temporary recovered with successful sync");}
    check(!std::remove(out),"remove recovered private output");
    copy(argv[1],badrom);
    {Hook h;h.rom=badrom;h.mutate=true;const auto r=packed_builder::ensure(badrom,out,hook,&h);check(!h.mutate,"mutation hook reached ROM member");check(!r.ok&&r.error.find("ROM extraction checksum mismatch")==0,"post-SHA ROM mutation rejected by member CRC");check(!exists(out),"mutated extraction never published");}
    check(!std::remove(part),"remove failed private temporary");
    {auto* f=std::fopen(out,"wb");check(f&&std::fwrite("bad",1,3,f)==3&&!std::fclose(f),"create invalid prior output");
        Hook h;h.cancel_write=true;const auto r=packed_builder::ensure(argv[1],out,hook,&h);
        struct stat st{};check(!r.ok&&!::stat(out,&st)&&st.st_size==3,"invalid old output retained until new archive fully verified");
        const auto repaired=packed_builder::ensure(argv[1],out);check(repaired.ok&&same(out,argv[2]),"invalid prior replaced only by complete verified archive");}
    check(!std::remove(out),"remove repaired private output");copy(argv[2],out);
    {const auto r=packed_builder::ensure(badrom,out);check(r.ok&&same(out,argv[2]),"valid existing output never overwritten");}
    check(boot_memory::statistics().live_bytes==baseline.live_bytes&&boot_memory::statistics().live_blocks==baseline.live_blocks,"all temporary boot allocations released");
    std::printf("{\"status\":\"passed\",\"checks\":%u,\"members_compared\":%zu,\"bytes_compared\":%llu,\"container_fsyncs\":1,\"global_new_calls\":0}\n",checks,packed_assets::count,(unsigned long long)compared);
}
