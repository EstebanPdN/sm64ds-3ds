// Complete independent Python-golden comparison. No ROM/artwork is embedded.
#include "rom_art.hpp"
#include <cstdlib>
#include <new>

static bool forbid_global_new=false;
static std::size_t global_new_calls=0;
void* operator new(std::size_t size){
    ++global_new_calls;
    if(forbid_global_new)throw std::bad_alloc();
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
static unsigned checks=0;
static void check(bool good,const char* what){
    ++checks;if(!good){std::fprintf(stderr,"check %u failed: %s\n",checks,what);std::abort();}
}
static bool same_live(boot_memory::Counts a,boot_memory::Counts b){return a.live_bytes==b.live_bytes&&a.live_blocks==b.live_blocks;}
static void parser_tests(){
    uint8_t decoded[8]={0x55,0,0,0,0,0,0,0x66};
    uint8_t packed[]={0x10,6,0,0,0x10,'a','b','c',0,2};
    using namespace rom_art::detail;
    check(lz10(packed,sizeof packed,decoded+1,6),"valid overlapping LZ10 back reference");
    check(!std::memcmp(decoded+1,"abcabc",6)&&decoded[0]==0x55&&decoded[7]==0x66,"LZ10 content and output guards");
    uint8_t wrapped[14]={'L','Z','7','7'};std::memcpy(wrapped+4,packed,sizeof packed);
    check(lz10(wrapped,sizeof wrapped,decoded+1,6),"LZ77 wrapper");
    check(!lz10(nullptr,1,decoded,1),"null compressed input");
    check(!lz10(packed,sizeof packed,nullptr,6),"null output");
    for(std::size_t n=0;n<sizeof packed;++n)check(!lz10(packed,n,decoded+1,6),"every truncated LZ10 input");
    check(!lz10(packed,sizeof packed,decoded+1,5),"decoded size mismatch");
    packed[0]=0x11;check(!lz10(packed,sizeof packed,decoded+1,6),"unsupported compression");packed[0]=0x10;
    packed[9]=4;check(!lz10(packed,sizeof packed,decoded+1,6),"back reference before output start");packed[9]=2;
    packed[8]=0x10;check(!lz10(packed,sizeof packed,decoded+1,6),"back reference beyond expected size");
    check(color(0)==0x8000&&color(0x1f)==0xfc00&&color(0x7c00)==0x801f,"DS RGB555 to A1R5G5B5 channels and opaque black");
    check(crc32("123456789",9)==0xcbf43926u,"standard CRC32 test vector");
    check(blend(0,0x7fff,0,31)==0&&blend(0,0x7fff,31,31)==0x7fff,"gradient endpoints");
    check(!span(8,9,0)&&!span(8,7,2)&&span(8,7,1),"overflow-safe span bounds");
}
static void path(char* out,std::size_t capacity,const char* dir,const char* name){
    const int n=std::snprintf(out,capacity,"%s/%s",dir,name);check(n>0&&std::size_t(n)<capacity,"private fixture path size");
}
static void golden(const loading_screen::Image& image,const char* dir,const char* name){
    char full[4096],filename[128];std::snprintf(filename,sizeof filename,"%s.a1r5g5b5",name);path(full,sizeof full,dir,filename);
    rom_art::detail::File file(full);check(file.handle!=nullptr,"open independent Python golden");
    check(image.valid(),"runtime image view valid");
    const uint8_t* bytes=reinterpret_cast<const uint8_t*>(image.pixels);
    const std::size_t size=std::size_t(image.width)*image.height*2;
    uint8_t chunk[4096];std::size_t done=0;
    while(done<size){const std::size_t count=size-done>sizeof chunk?sizeof chunk:size-done;
        check(std::fread(chunk,1,count,file.handle)==count,"golden read complete");
        check(std::memcmp(bytes+done,chunk,count)==0,"all decoded pixels equal independent Python golden");done+=count;}
    check(std::fgetc(file.handle)==EOF,"no trailing golden pixels");
}
static void compare(const rom_art::Resources& art,const char* dir){
    const auto& a=art.views();
    golden(a.castle,dir,"castle");golden(a.sky,dir,"sky");golden(a.logo,dir,"logo");golden(a.pattern,dir,"pattern");
    golden(a.frame,dir,"frame");golden(a.star,dir,"star");golden(a.title_font.image,dir,"title_font");golden(a.body_font.image,dir,"body_font");
    check(a.sky.pixels==a.castle.pixels,"sky reuses original castle rows");
    check(a.title_font.valid()&&a.body_font.valid(),"original font views valid");
    check(a.title_font.cell_width==16&&a.body_font.cell_width==8&&a.title_font.cell_height==16&&a.body_font.cell_height==16,"original glyph cell dimensions");
    unsigned count=0,ascii=0,latin1=0;for(unsigned c=0;c<256;++c){
        const unsigned cell=a.title_font.glyphs[c];
        check(cell==a.body_font.glyphs[c],"font cell mappings agree");
        if(cell!=0xffff){++count;if(c<128)++ascii;else ++latin1;
            check(cell<166,"canonical glyph in compact atlas");
            if(c>=128){check(rom_art_manifest::codes[cell]==c-0x30,"original canonical Latin1 to CP1252 glyph mapping");
                for(const auto* font:{&a.title_font,&a.body_font}){
                    unsigned ink=0;const unsigned x=(cell%16)*font->cell_width,y=(cell/16)*16;
                    for(unsigned v=0;v<16;++v)for(unsigned u=0;u<font->cell_width;++u)ink+=font->image.pixels[(y+v)*font->image.width+x+u]!=0;
                    check(ink>0,"every exposed Latin1 glyph contains original atlas pixels");}}
            check(a.title_font.advances[c]>0&&a.body_font.advances[c]>0,"original positive glyph advances");}}
    check(count==166&&ascii==74&&latin1==92,"canonical printable ASCII and Latin1 repertoire");
    const uint8_t accents[]={0xc9,0xc8,0xca,0xc0,0xc7,0xd1,0xc3,0xd5,0xdc,0xdf,0xe9,0xe8,0xea,0xe0,0xe7,0xf1,0xe3,0xf5,0xfc};
    for(const auto c:accents)check(a.title_font.glyphs[c]!=0xffff&&a.body_font.glyphs[c]!=0xffff,"loading-language accent present in both original fonts");
    check(a.title_font.image.height==176&&a.body_font.image.height==176,"compact Latin1 font row count");
    check(a.body_font.glyphs[0xa4]==0xffff&&a.body_font.glyphs[0xb8]==0xffff,"original empty Latin1 slots remain absent");
    check(a.body_font.glyphs['%']==0xffff&&a.body_font.glyphs['"']==0xffff,"unsupported glyphs are explicitly absent");
    check(a.logo.width==240&&a.logo.height==128,"logo excludes adjacent sky tiles while retaining registered mark");
    check(a.frame.width==24&&a.frame.height==24,"original menu button nine-slice");
    check(!a.mario.valid()&&!a.yoshi.valid(),"optional character art remains absent");
}
static void make_corrupt(const char* rom,const char* output){
    rom_art::detail::File input(rom);check(input.handle!=nullptr,"open private ROM for corrupt fixture");
    std::FILE* file=std::fopen(output,"wb");check(file!=nullptr,"create private corrupt ROM fixture");
    uint8_t buffer[65536];
    for(;;){const std::size_t n=std::fread(buffer,1,sizeof buffer,input.handle);if(!n)break;
        check(std::fwrite(buffer,1,n,file)==n,"copy corrupt fixture");}
    check(!std::ferror(input.handle)&&std::fclose(file)==0,"finish private corrupt fixture");
    file=std::fopen(output,"r+b");check(file!=nullptr,"open private corrupt fixture for mutation");
    check(std::fseek(file,long(rom_art_manifest::overlay7.offset),SEEK_SET)==0,"seek checksum mutation");
    const int old=std::fgetc(file);check(old!=EOF,"read checksum mutation");
    check(std::fseek(file,-1,SEEK_CUR)==0&&std::fputc(old^1,file)!=EOF&&std::fclose(file)==0,"mutate one artwork span byte");
}
int main(int argc,char** argv){
    if(argc!=3){std::fprintf(stderr,"usage: %s ROM PRIVATE_PREVIEW_DIR\n",argv[0]);return 2;}
    const std::size_t before_new=global_new_calls;forbid_global_new=true;parser_tests();
    const auto baseline=boot_memory::statistics();std::size_t peak=0,retained=0;
    {
        rom_art::Resources art;
        check(art.load_embedded(),"embedded artwork loads without a ROM");compare(art,argv[2]);
        check(same_live(boot_memory::statistics(),baseline),"embedded artwork has zero heap allocations");
        art.release();
        check(art.load(argv[1]),art.error());compare(art,argv[2]);
        peak=art.peak_bytes();retained=art.retained_bytes();
        check(retained==329344,"retained Latin1 art byte budget");check(peak<=1024*1024,"peak artwork allocation budget");
        check(boot_memory::statistics().live_bytes-baseline.live_bytes==retained&&boot_memory::statistics().live_blocks-baseline.live_blocks==1,"only one pixel allocation survives loading");
        check(art.load(argv[1]),"repeat loading with live art");compare(art,argv[2]);
        check(boot_memory::statistics().live_bytes-baseline.live_bytes==retained,"repeat load replaces previous pixel allocation");
        art.release();check(same_live(boot_memory::statistics(),baseline),"release returns all boot allocations");
        check(!art.views().castle.valid()&&!art.views().title_font.valid()&&art.retained_bytes()==0,"release invalidates all views");
        art.release();check(same_live(boot_memory::statistics(),baseline),"release is idempotent");
        check(!art.load(nullptr)&&art.error()[0],"null ROM path rejects without allocations");
        check(!art.load("/does-not-exist-sm64ds-art-rom.nds")&&art.error()[0],"missing ROM path rejects");
        char wrong[4096];path(wrong,sizeof wrong,argv[2],"castle.a1r5g5b5");
        check(!art.load(wrong)&&art.error()[0],"wrong ROM size rejects");
        check(same_live(boot_memory::statistics(),baseline),"pre-read failures leak no boot allocations");
        char corrupt[4096];path(corrupt,sizeof corrupt,argv[2],"host-corrupt-art-rom.nds");make_corrupt(argv[1],corrupt);
        check(art.load(argv[1]),"good load before CRC failure");
        check(!art.load(corrupt)&&std::strstr(art.error(),"CRC"),"changed artwork span rejects by CRC");
        check(!art.views().castle.valid()&&same_live(boot_memory::statistics(),baseline),"CRC failure clears previous art and temporary allocations");
        check(std::remove(corrupt)==0,"private corrupt fixture removed");
        check(art.load(argv[1]),"recover loading after failed ROM");art.release();
    }
    check(same_live(boot_memory::statistics(),baseline),"Resources destructor leaves no allocation");
    check(global_new_calls==before_new,"no global new calls anywhere in pre-heap artwork path");forbid_global_new=false;
    std::printf("{\"status\":\"passed\",\"checks\":%u,\"schema_id\":\"%s\",\"retained_bytes\":%zu,\"peak_bytes\":%zu,\"global_new_calls\":%zu,\"live_bytes_after\":%zu,\"live_blocks_after\":%zu}\n",
        checks,rom_art_manifest::schema_id,retained,peak,global_new_calls,boot_memory::statistics().live_bytes,boot_memory::statistics().live_blocks);
}
