// Private host fixtures only. The caller supplies its own EU ROM and canonical
// extracted repo. Never mutate the source ROM or ship any generated game data.
#include "rom_discovery.hpp"
#include "rom_bootstrap.hpp"
#include <cstdlib>
#include <fcntl.h>
#include <new>

static bool forbid_global_new=false;
static std::size_t global_new_calls=0;
void* operator new(std::size_t n){++global_new_calls;if(forbid_global_new)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using String=boot_memory::String;
static unsigned checks=0;
static void check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"check %u failed: %s\n",checks,what);std::abort();}}
static bool exists(const String& path){struct stat st{};return ::lstat(path.c_str(),&st)==0;}
static void directory(const String& path){check(::mkdir(path.c_str(),0700)==0,"create fresh private directory");}
static bool same_live(boot_memory::Counts a,boot_memory::Counts b){return a.live_bytes==b.live_bytes&&a.live_blocks==b.live_blocks;}
static unsigned open_files(){unsigned total=0;for(int fd=0;fd<256;++fd)if(::fcntl(fd,F_GETFD)!=-1)++total;return total;}
static void write_header(const String& path,const char* code="ASMP",unsigned revision=0,std::size_t size=rom_install_manifest::rom_size){
    rom_discovery::detail::File file;file.handle=std::fopen(path.c_str(),"wb");check(file.handle!=nullptr,"create private header fixture");
    uint8_t header[0x20]{};std::memcpy(header+0x0c,code,4);header[0x1e]=uint8_t(revision);
    const std::size_t n=size<sizeof header?size:sizeof header;
    check(std::fwrite(header,1,n,file.handle)==n,"write private header fixture");
    check(::ftruncate(::fileno(file.handle),off_t(size))==0,"size sparse private ROM fixture");
    check(file.close(),"close private header fixture");
}
static void empty_file(const String& path){rom_discovery::detail::File file;file.handle=std::fopen(path.c_str(),"wb");check(file.handle&&file.close(),"create empty private file");}
static void copy_rom(const char* source,const String& path){
    rom_discovery::detail::File input,output;input.handle=std::fopen(source,"rb");output.handle=std::fopen(path.c_str(),"wb");
    check(input.handle&&output.handle,"open private ROM copy");uint8_t buffer[65536];uint64_t size=0;
    for(;;){const std::size_t n=std::fread(buffer,1,sizeof buffer,input.handle);if(!n)break;
        check(std::fwrite(buffer,1,n,output.handle)==n,"copy user's ROM without modifying source");size+=n;}
    check(!std::ferror(input.handle)&&size==rom_install_manifest::rom_size,"complete supported-size private ROM copy");
    check(input.close()&&output.close(),"close private ROM copy");
}
static uint64_t compare_file(const String& output,const String& canonical){
    rom_discovery::detail::File a,b;a.handle=std::fopen(output.c_str(),"rb");b.handle=std::fopen(canonical.c_str(),"rb");
    check(a.handle&&b.handle,"open extracted/canonical comparison");uint8_t x[65536],y[65536];uint64_t bytes=0;
    for(;;){const std::size_t n=std::fread(x,1,sizeof x,a.handle),m=std::fread(y,1,sizeof y,b.handle);
        check(n==m&&!std::memcmp(x,y,n),"all extracted bytes equal canonical after renamed-ROM discovery");
        bytes+=n;if(!n)break;}
    check(!std::ferror(a.handle)&&!std::ferror(b.handle),"canonical comparison read complete");
    check(a.close()&&b.close(),"close canonical comparison");return bytes;
}
static void primitives(){
    for(const char* name:{"a.nds","ROM.NDS","Spaced ROM.nDs",".nds","é_日本.NdS"})check(rom_discovery::detail::nds_extension(name),"ASCII-case-insensitive NDS extension");
    for(const char* name:{"","nds",".nd","file.nds.bak","file.nds.","file.txt","fileＮＤＳ"})check(!rom_discovery::detail::nds_extension(name),"other suffixes excluded");
    check(!rom_discovery::detail::nds_extension(nullptr),"null filename excluded");
    uint8_t h[0x20]{};std::memcpy(h+0x0c,"ASMP",4);
    check(rom_discovery::detail::compatible(h,sizeof h),"EU revision zero header");
    check(!rom_discovery::detail::compatible(nullptr,sizeof h)&&!rom_discovery::detail::compatible(h,31),"short/null header excluded");
    h[0x1e]=1;check(!rom_discovery::detail::compatible(h,sizeof h),"other revision excluded");h[0x1e]=0;h[0xf]='E';
    check(!rom_discovery::detail::compatible(h,sizeof h),"other region excluded");
}
static void checked_result(const rom_discovery::Result& result,const String& expected){
    check(result.failure==rom_discovery::Failure::None&&result.found&&result.path==expected,"deterministic smallest compatible path after cursor");
    check(result.nds_files==9&&result.compatible_headers==5,"whole-directory regular/header counters");
    check(result.error[0]==0,"successful discovery error empty");
}
static std::size_t scan_peak(const String& dir,unsigned count){
    const auto before=boot_memory::statistics();const std::size_t saved_peak=boot_memory::counts.peak_bytes;
    boot_memory::counts.peak_bytes=before.live_bytes;
    {const auto result=rom_discovery::next(dir.c_str());
        check(!result.found&&result.failure==rom_discovery::Failure::None&&result.nds_files==count&&result.compatible_headers==0,"stress scan of only wrong-size NDS files");}
    const std::size_t bytes=boot_memory::statistics().peak_bytes-before.live_bytes;
    if(saved_peak>boot_memory::counts.peak_bytes)boot_memory::counts.peak_bytes=saved_peak;
    check(same_live(before,boot_memory::statistics()),"stress scan releases its paths");return bytes;
}
int main(int argc,char** argv){
    if(argc!=3){std::fprintf(stderr,"usage: %s ROM CANONICAL_REPO\n",argv[0]);return 2;}
    const std::size_t prior_new=global_new_calls;forbid_global_new=true;
    const auto baseline=boot_memory::statistics();const unsigned prior_files=open_files();
    unsigned ensure_calls=0,rejected_SHA=0;uint64_t compared=0;std::size_t small_peak=0,large_peak=0;
    char evidence[]="build/integrated-native/rom-discovery/run-XXXXXX";
    if(::mkdir("build/integrated-native/rom-discovery",0700)!=0)check(errno==EEXIST,"create discovery evidence parent");
    check(::mkdtemp(evidence)!=nullptr,"create unique immutable private evidence root");
    {
        primitives();const String root(evidence),scan=root+"/scan",empty=root+"/empty",stress=root+"/stress",renamed=root+"/renamed",cache=root+"/cache";
        directory(scan);directory(empty);directory(stress);directory(renamed);
        const String real=scan+"/02 Mi copia de Mario.NdS";copy_rom(argv[1],real);
        for(const char* name:{".nds","01 False match.NDS","03-é_東京.nds","04-compatible.nDS"})write_header(scan+"/"+name);
        write_header(scan+"/05-region.nds","ASME");write_header(scan+"/06-revision.NDS","ASMP",1);
        write_header(scan+"/07-short.nds","ASMP",0,31);write_header(scan+"/08-zero.NdS","0000");
        for(const char* name:{"no extension","other.txt","extension.nds.bak","trailing.nds."})empty_file(scan+"/"+name);
        directory(scan+"/subfolder.nds");write_header(scan+"/subfolder.nds/nested.nds");
        check(::symlink("02 Mi copia de Mario.NdS",(scan+"/symlink.NDS").c_str())==0,"create symlink fixture");
        {struct stat target{};check(::stat((scan+"/symlink.NDS").c_str(),&target)==0&&S_ISREG(target.st_mode),"link fixture really points to valid regular ROM");}
        check(::symlink("missing-target.nds",(scan+"/dangling.nds").c_str())==0,"create dangling-link fixture");
        check(::mkfifo((scan+"/namedpipe.nds").c_str(),0600)==0,"create special-file fixture");
        const auto before_scans=boot_memory::statistics();
        {
            String after;
            for(const char* name:{".nds","01 False match.NDS","02 Mi copia de Mario.NdS","03-é_東京.nds","04-compatible.nDS"}){
                errno=EIO;const auto result=rom_discovery::next(scan.c_str(),after.c_str());checked_result(result,scan+"/"+name);after=result.path;
            }
            const auto exhausted=rom_discovery::next(scan.c_str(),after.c_str());
            check(!exhausted.found&&exhausted.failure==rom_discovery::Failure::None&&exhausted.path.empty()&&exhausted.nds_files==9&&exhausted.compatible_headers==5,"cursor exhaustion remains clean EOF with counters");
            const auto null_cursor=rom_discovery::next(scan.c_str(),nullptr);checked_result(null_cursor,scan+"/.nds");
            const auto trailing=rom_discovery::next((scan+"/").c_str());checked_result(trailing,scan+"/.nds");
        }
        check(same_live(before_scans,boot_memory::statistics()),"all repeated discovery result paths released");
        const String missing=root+"/missing";
        for(const char* path:{static_cast<const char*>(nullptr),"",missing.c_str(),real.c_str()}){
            const auto result=rom_discovery::next(path);check(!result.found&&result.failure==rom_discovery::Failure::Directory&&result.path.empty()&&result.error[0],"invalid/missing/not-directory error contract");
        }
        errno=EIO;{const auto result=rom_discovery::next(empty.c_str());check(!result.found&&result.failure==rom_discovery::Failure::None&&result.nds_files==0&&!result.error[0],"empty directory is successful clean EOF despite incoming errno");}
        // Hundreds of entries must not create a vector/list of file paths.
        for(unsigned i=0;i<256;++i){char name[180];std::snprintf(name,sizeof name,"/long-private-filename-for-a-user-ROM-with-spaces-and-letters-abcdefghijklmnopqrstuvwxyz-%03u.NdS",i);empty_file(stress+name);
            if(i==0)small_peak=scan_peak(stress,1);}
        large_peak=scan_peak(stress,256);check(small_peak==large_peak&&large_peak<4096,"scan path memory independent of entry count");
        // Simulate the wrapper: full SHA rejection moves to the next header
        // candidate before any cache directory or extracted file is written.
        {
            String after;bool installed=false;
            for(unsigned i=0;i<6&&!installed;++i){
                const auto candidate=rom_discovery::next(scan.c_str(),after.c_str());check(candidate.found&&candidate.failure==rom_discovery::Failure::None,"integration candidate discovered");
                ++ensure_calls;const auto prepared=rom_bootstrap::ensure(candidate.path.c_str(),cache.c_str());
                if(!prepared.ok){++rejected_SHA;check(prepared.error.find("Unsupported or damaged ROM")!=String::npos,"false compatible header rejected by exact ROM validation");check(!exists(cache),"SHA rejection precedes every cache write");after=candidate.path;continue;}
                check(candidate.path==real&&prepared.prepared&&prepared.written==rom_install_manifest::file_count,"arbitrary-name real ROM installs all canonical files");
                for(const auto& file:rom_install_manifest::files)compared+=compare_file(prepared.root+"/"+file.path,String(argv[2])+"/"+file.path);
                check(compared==rom_install_manifest::total_bytes,"entire extracted repo equals canonical byte count");installed=true;
            }
            check(installed&&rejected_SHA==2,"two earlier false headers skipped before valid freely named ROM");
        }
        // Rename only the private copy, exercising ordinary names, spaces,
        // Unicode and all extension cases with no fixed rom.nds dependency.
        String previous=real;
        for(const char* name:{"Super Mario 64 DS (Europe).nds","Juego de Esteban.NDS","Mário_ñ_日本.NdS"}){
            const String target=renamed+"/"+name;check(std::rename(previous.c_str(),target.c_str())==0,"rename private ROM copy");
            const auto candidate=rom_discovery::next(renamed.c_str());check(candidate.found&&candidate.path==target&&candidate.nds_files==1&&candidate.compatible_headers==1,"freely named real ROM discovered after rename");
            previous=target;
        }
        ++ensure_calls;const auto reused=rom_bootstrap::ensure(previous.c_str(),cache.c_str());
        check(reused.ok&&!reused.prepared&&reused.reused==rom_install_manifest::file_count&&reused.written==0,"Unicode renamed ROM validates and reuses exact canonical cache");
    }
    check(same_live(baseline,boot_memory::statistics()),"discovery/integration leaves zero boot allocation");
    check(open_files()==prior_files,"directory and ROM handles close on every tested path");
    check(global_new_calls==prior_new,"global new untouched during discovery and installation");forbid_global_new=false;
    std::printf("{\"status\":\"passed\",\"checks\":%u,\"ensure_calls\":%u,\"rejected_SHA_before_writes\":%u,\"canonical_files\":%zu,\"canonical_bytes_compared\":%llu,\"stress_entries\":256,\"scan_peak_bytes_1_entry\":%zu,\"scan_peak_bytes_256_entries\":%zu,\"global_new_calls\":%zu,\"live_bytes_after\":%zu,\"live_blocks_after\":%zu,\"private_evidence\":\"%s\"}\n",
        checks,ensure_calls,rejected_SHA,sizeof rom_install_manifest::files/sizeof rom_install_manifest::files[0],static_cast<unsigned long long>(compared),small_peak,large_peak,global_new_calls-prior_new,boot_memory::statistics().live_bytes,boot_memory::statistics().live_blocks,evidence);
}
