#include "packed_assets.hpp"
#include <cstdlib>
#include <new>
#include <unistd.h>
static unsigned checks=0;
static bool forbid_new=false;
void* operator new(std::size_t n){if(forbid_new)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
static void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL %u %s\n",checks,message);std::abort();}}
static bool cancel(const rom_bootstrap::Progress&,void*){return false;}
int main(int argc,char** argv){
    if(argc==2){const auto result=packed_assets::verify(argv[1]);std::printf("%s\n",result.ok?"accepted":result.error.c_str());return result.ok?0:2;}
    check(argc==3,"usage PACKAGE CANONICAL_REPO");forbid_new=true;const auto baseline=boot_memory::statistics();
    check(packed_assets::extension("Mário.SM64DS")&&!packed_assets::extension("foo.sm64ds.part"),"extension");
    check(packed_assets::lookup("../build/assets/files.tsv")==-1,"traversal refused");
    {const auto result=packed_assets::verify(argv[1]);check(result.ok,result.error.c_str());check(result.root=="s64:","mounted asset root");}
    {const auto result=packed_assets::verify(argv[1],cancel);check(!result.ok&&result.error.find("Cancelled during")==0,"cancel verification");}
    unsigned char a[8192],b[8192];
    for(unsigned i=0;i<packed_assets::count;++i){
        const auto& entry=rom_install_manifest::files[i];check(packed_assets::lookup(entry.path)==int(i),"lookup all entries");
        packed_assets::Stream stream{},other{};check(stream.open(argv[1],i)&&other.open(argv[1],i),"two independent streams");
        char path[2048];std::snprintf(path,sizeof(path),"%s/%s",argv[2],entry.path);auto* file=std::fopen(path,"rb");check(file,"open canonical file");
        uint32_t done=0;
        while(done<entry.size){const auto size=entry.size-done<sizeof(a)?entry.size-done:sizeof(a);
            check(stream.read(a,sizeof(a))==size&&std::fread(b,1,size,file)==size&&!std::memcmp(a,b,size),"all member bytes match canonical");done+=uint32_t(size);}
        check(stream.read(a,1)==0,"EOF stops at member boundary");
        check(stream.seek(-1,SEEK_SET)==-1&&stream.seek(INT64_MAX,SEEK_CUR)==-1,"negative and overflow seek rejected");
        check(stream.seek(0,SEEK_END)==entry.size&&stream.seek(1,SEEK_END)==entry.size+1&&stream.read(a,1)==0,"seek beyond EOF read zero");
        if(entry.size){check(other.read(a,1)==1,"second stream position independent");check(stream.seek(-1,SEEK_END)==entry.size-1&&stream.read(a,2)==1,"last byte isolated");}
        check(!std::fclose(file)&&!stream.close()&&!other.close(),"all streams close");
    }
    // Shared reader (3DS: one SD handle): members read at package offsets match too.
    static int shared_fd=-1;shared_fd=::open(argv[1],O_RDONLY);check(shared_fd>=0,"open shared package");
    packed_assets::shared_read=[](uint64_t offset,void* output,std::size_t amount)->std::size_t{
        const auto got=::pread(shared_fd,output,amount,off_t(offset));return got<0?0:std::size_t(got);};
    for(unsigned i=0;i<packed_assets::count;++i){
        const auto& entry=rom_install_manifest::files[i];
        packed_assets::Stream stream{},other{};check(stream.open(argv[1],i)&&other.open(argv[1],i)&&!stream.file,"shared streams open without a file");
        char path[2048];std::snprintf(path,sizeof(path),"%s/%s",argv[2],entry.path);auto* file=std::fopen(path,"rb");check(file,"open canonical file");
        uint32_t done=0;
        while(done<entry.size){const auto size=entry.size-done<sizeof(a)?entry.size-done:sizeof(a);
            check(stream.read(a,sizeof(a))==size&&std::fread(b,1,size,file)==size&&!std::memcmp(a,b,size),"shared member bytes match canonical");done+=uint32_t(size);}
        check(stream.read(a,1)==0,"shared EOF stops at member boundary");
        if(entry.size){check(other.read(a,1)==1,"shared second stream position independent");check(stream.seek(-1,SEEK_END)==entry.size-1&&stream.read(a,2)==1,"shared last byte isolated");}
        check(!std::fclose(file)&&!stream.close()&&!other.close(),"shared streams close");
    }
    packed_assets::shared_read=nullptr;check(!::close(shared_fd),"close shared package");
    unsigned char h[128]{};auto* file=std::fopen(argv[1],"rb");check(file&&std::fread(h,1,sizeof(h),file)==sizeof(h)&&!std::fclose(file),"header read");
    check(packed_assets::header_valid(h,sizeof(h)),"canonical header");
    for(unsigned i=0;i<sizeof(h);++i){h[i]^=1;check(!packed_assets::header_valid(h,sizeof(h)),"every mutated header byte rejected");h[i]^=1;}
    check(boot_memory::statistics().live_bytes==baseline.live_bytes&&boot_memory::statistics().live_blocks==baseline.live_blocks,"temporary bootstrap allocations released");
    std::printf("{\"status\":\"passed\",\"checks\":%u,\"files_compared\":%zu,\"bytes_compared\":%llu,\"global_new_calls\":0}\n",checks,packed_assets::count,(unsigned long long)rom_install_manifest::total_bytes);
}
