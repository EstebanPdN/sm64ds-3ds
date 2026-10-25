#include "loading_screen.hpp"
#ifdef SM64DS_TEST_ROM_ART
#include "rom_art.hpp"
#endif
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace {
size_t allocations=0;
bool count_allocations=false;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
constexpr size_t top_bytes=400*240*3,bottom_bytes=320*240*3;
struct GuardedTop {std::array<unsigned char,64> before;std::array<unsigned char,top_bytes*4/3> pixels;std::array<unsigned char,64> after;};
struct GuardedBottom {std::array<unsigned char,64> before;std::array<unsigned char,bottom_bytes*4/3> pixels;std::array<unsigned char,64> after;};
static GuardedTop top;
static GuardedBottom bottom;
void fill_guards(){top.before.fill(0xa7);top.after.fill(0x6d);bottom.before.fill(0xa7);bottom.after.fill(0x6d);}
void check_guards(){
    for(auto b:top.before)check(b==0xa7,"top prefix canary");for(auto b:top.after)check(b==0x6d,"top suffix canary");
    for(auto b:bottom.before)check(b==0xa7,"bottom prefix canary");for(auto b:bottom.after)check(b==0x6d,"bottom suffix canary");
}
struct MockBackend {
    inline static std::array<std::array<unsigned char,top_bytes>,2> top_buffers{};
    inline static std::array<std::array<unsigned char,bottom_bytes>,2> bottom_buffers{};
    uint64_t now=0;
    unsigned frames=0,clears=0,buffer=0,polls=0,top_paints=0;
    bool allowed=true;
    uint64_t milliseconds()const{return now;}
    bool poll(){++polls;return allowed;}
    void paint(const loading_screen::State& state,uint64_t time){
        loading_screen::Surface a{top_buffers[buffer].data(),top_bytes,400,240,loading_screen::Format::Bgr8,true};
        loading_screen::Surface b{bottom_buffers[buffer].data(),bottom_bytes,320,240,loading_screen::Format::Bgr8,true};
        if(top_paints<2){loading_screen::paint_top(a,state.artwork,state.language);++top_paints;}
        loading_screen::paint_bottom(b,state,time);++frames;buffer^=1;
    }
    void clear_both(){for(unsigned i=0;i<2;++i){top_buffers[buffer].fill(0);bottom_buffers[buffer].fill(0);buffer^=1;++clears;}}
};
unsigned progress_tests(){
    using loading_screen::ratio;
    check(ratio(0,0,100)==0,"unknown total never invents progress");
    check(ratio(1,1,0)==0,"zero span");check(ratio(1,1,100)==100,"completed work");
    check(ratio(110,100,85)==85,"over-completed clamped");
    for(unsigned total=1;total<1000;++total)for(unsigned done=0;done<=total;++done)
        check(ratio(done,total,85)==uint64_t(done)*85/total,"ratio exact against widened arithmetic");
    check(ratio(UINT64_MAX-1,UINT64_MAX,100)==99,"ratio handles maximum total");
    check(ratio(UINT64_MAX/2,UINT64_MAX,100)==49,"ratio handles maximum half total");
    check(ratio(UINT64_MAX,UINT64_MAX,100)==100,"ratio handles maximum completed");
    loading_screen::State state;
    state.work("verify-rom",0,16777216,0,2450);check(state.percent==0,"hash starts zero");
    state.work("verify-rom",16777216,16777216,0,2450);check(state.percent==10,"hash ends ten");
    unsigned previous=state.percent;
    for(unsigned file=0;file<=2450;++file){
        state.work(file%2?"extract":"verify-cache",file,2450,file,2450);
        check(state.percent>=previous,"cache progress monotonic");check(state.percent<=95,"cache not complete before marker/audio");previous=state.percent;
    }
    state.work("finish",2450,2450,2450,2450);check(state.percent==95,"ROM finish remains 95");
    state.stage(loading_screen::Stage::Audio);check(state.percent==97,"audio explicit stage");
    state.work("verify-cache",0,2450,0,2450);check(state.percent==97,"revalidation cannot move backwards");
    state.stage(loading_screen::Stage::Ready);check(state.percent==100,"only explicit final stage completes");
    loading_screen::State packed;
    packed.work("verify-rom",100,100,0,2450);check(packed.percent==10,"packed ROM check ends at ten");
    packed.work("write-package",100,100,2450,2450);check(packed.percent==75,"packed write reserves readback progress");
    packed.work("verify-package",50,100,1000,2450);check(packed.percent==85,"packed readback advances independently");
    packed.work("verify-package",100,100,2450,2450);check(packed.percent==95,"packed readback ends at95");
    return 500000;
}
void packing_tests(){
    std::array<unsigned char,64> bytes{};
    loading_screen::Surface rgb{bytes.data(),12,2,2,loading_screen::Format::Rgb8,false};
    rgb.pixel(1,0,{0x12,0x34,0x56});check(bytes[3]==0x12&&bytes[4]==0x34&&bytes[5]==0x56,"RGB row-major packing");
    bytes.fill(0);rgb.format=loading_screen::Format::Bgr8;rgb.rotated=true;
    rgb.pixel(1,0,{0x12,0x34,0x56});check(bytes[9]==0x56&&bytes[10]==0x34&&bytes[11]==0x12,"3DS rotated BGR packing");
    bytes.fill(0);rgb.format=loading_screen::Format::Rgba8;rgb.bytes=16;rgb.rotated=false;
    rgb.pixel(0,0,{0x12,0x34,0x56});check(bytes[0]==255&&bytes[1]==0x56&&bytes[2]==0x34&&bytes[3]==0x12,"3DS RGBA packing");
    bytes.fill(0);rgb.format=loading_screen::Format::Rgb565;rgb.bytes=8;
    rgb.pixel(0,0,{255,0,0});check(bytes[0]==0&&bytes[1]==0xf8,"RGB565 packing");
    bytes.fill(0);rgb.bytes=7;rgb.pixel(0,0,{255,0,0});
    check(bytes[0]==0&&bytes[1]==0,"undersized framebuffer rejected");
}
void blit_tests(){
    std::array<uint16_t,35> source{};
    for(unsigned i=0;i<source.size();++i)source[i]=uint16_t((i%4?0x8000u:0u)|((i*79u)&0x7fffu));
    const loading_screen::Image image{source.data(),7,5};
    const loading_screen::Format formats[]={loading_screen::Format::Rgb8,loading_screen::Format::Bgr8,
        loading_screen::Format::Rgba8,loading_screen::Format::Rgb565,loading_screen::Format::Rgb5A1,loading_screen::Format::Rgba4};
    struct Case{int x,y,w,h;unsigned sx,sy,sw,sh;};
    const Case cases[]={{2,3,7,5,0,0,7,5},{-3,-2,11,9,0,0,7,5},{9,8,3,2,1,1,5,3},{-1,6,9,8,1,1,5,3}};
    for(auto format:formats)for(bool rotated:{false,true})for(const auto& c:cases){
        std::array<unsigned char,12*11*4+64> actual{},expected{};actual.fill(0x6b);expected.fill(0x6b);
        const unsigned stride=format==loading_screen::Format::Rgb8||format==loading_screen::Format::Bgr8?3u:format==loading_screen::Format::Rgba8?4u:2u;
        const loading_screen::Surface output{actual.data()+32,size_t(12*11*stride),12,11,format,rotated};
        const loading_screen::Surface oracle{expected.data()+32,size_t(12*11*stride),12,11,format,rotated};
        loading_screen::blit(output,image,c.x,c.y,c.w,c.h,c.sx,c.sy,c.sw,c.sh);
        for(int y=0;y<11;++y)for(int x=0;x<12;++x){
            if(x<c.x||y<c.y||x>=c.x+c.w||y>=c.y+c.h)continue;
            const unsigned sx=c.sx+unsigned(uint64_t(x-c.x)*c.sw/unsigned(c.w));
            const unsigned sy=c.sy+unsigned(uint64_t(y-c.y)*c.sh/unsigned(c.h));
            const uint16_t pixel=source[sy*7+sx];
            if(pixel&0x8000)oracle.pixel(x,y,loading_screen::image_color(pixel));
        }
        check(actual==expected,"nearest blit, alpha and clipping match independent pixel oracle");
        const auto previous=actual;
        loading_screen::blit(output,image,0,0,10,10,6,4,2,2);
        check(actual==previous,"invalid source rectangle rejected");
        const loading_screen::Color fill{71,147,231};
        loading_screen::rect(output,c.x,c.y,c.w,c.h,fill);
        for(int y=0;y<11;++y)for(int x=0;x<12;++x)
            if(x>=c.x&&y>=c.y&&x<c.x+c.w&&y<c.y+c.h)oracle.pixel(x,y,fill);
        check(actual==expected,"clipped fast fill matches independent pixel oracle");
    }
}
void renderer_tests(const loading_screen::Artwork* artwork=nullptr){
    fill_guards();loading_screen::State state;state.artwork=artwork;
    const loading_screen::Format formats[]={loading_screen::Format::Rgb8,loading_screen::Format::Bgr8,
        loading_screen::Format::Rgba8,loading_screen::Format::Rgb565,loading_screen::Format::Rgb5A1,loading_screen::Format::Rgba4};
    for(auto format:formats)for(bool rotated:{false,true}){
        const unsigned stride=format==loading_screen::Format::Rgb8||format==loading_screen::Format::Bgr8?3u:format==loading_screen::Format::Rgba8?4u:2u;
        top.pixels.fill(0xda);bottom.pixels.fill(0xda);
        const loading_screen::Surface a{top.pixels.data(),size_t(400*240*stride),400,240,format,rotated};
        const loading_screen::Surface b{bottom.pixels.data(),size_t(320*240*stride),320,240,format,rotated};
        state.work("extract",1535,2450,1535,2450);loading_screen::paint(a,b,state,950);
        state.error(loading_screen::Error::Rom);
        loading_screen::paint(a,b,state,999);
        loading_screen::rect(b,-20,-20,40,40,{255,255,255});loading_screen::rect(b,319,239,50,50,{255,255,255});
        a.pixel(-1,0,{255,255,255});a.pixel(400,240,{255,255,255});
        check_guards();
        for(size_t n=size_t(400*240*stride);n<top.pixels.size();++n)check(top.pixels[n]==0xda,"top beyond declared pixel extent");
        for(size_t n=size_t(320*240*stride);n<bottom.pixels.size();++n)check(bottom.pixels[n]==0xda,"bottom beyond declared pixel extent");
        state=loading_screen::State{};state.artwork=artwork;
    }
    // Painter also clips safely to partial/failed surfaces.
    loading_screen::Surface tiny{bottom.pixels.data(),3,1,1,loading_screen::Format::Rgb8,true};
    loading_screen::paint(tiny,{},state);check_guards();
}
unsigned session_tests(const loading_screen::Artwork* artwork=nullptr,loading_screen::Language language=loading_screen::Language::English){
    using Session=loading_screen::BasicSession<MockBackend>;
    const size_t before=allocations;count_allocations=true;
    unsigned rendered=0;
    {
        Session session(artwork,language);
        for(unsigned i=0;i<500;++i){session.backend().now=i;
            check(session.update(i<100?"verify-rom":"extract",i,500,i,500),"session update accepted");}
        check(session.backend().polls==500,"cancellation polled each callback");
        rendered=session.backend().frames;
        check(rendered==2,"500 one-millisecond callbacks cause only two rendered frames");
        check(session.backend().top_paints==2,"static top rendered once in each buffer");
        session.stage(loading_screen::Stage::Audio);session.stage(loading_screen::Stage::Ready);
        check(session.state().percent==100,"session ready state");
        session.backend().allowed=false;check(!session.update("extract",500,500,500,500),"B or app exit cancels");
        session.error(loading_screen::Error::Preparation);
        check(session.state().failed,"session error displayed");
        session.close();session.close();check(session.backend().clears==2,"close clears each buffer exactly once");
        check(!session.update("extract",0,0,0,0),"closed session rejects updates");
    }
    {
        Session unchanged(artwork,language);
        for(unsigned i=0;i<500;++i){unchanged.backend().now=uint64_t(i)*400;
            check(unchanged.update(i%2?"verify-cache":"extract",0,500,i,500),"unchanged work accepted");}
        check(unchanged.backend().frames==2,"slow per-file callbacks with same percentage do not keep repainting");
        check(unchanged.backend().polls==500,"unchanged work still polls input");
    }
    count_allocations=false;check(allocations==before,"500 updates, stages, error and close allocate no C++ heap");
    for(const auto& bank:MockBackend::top_buffers)for(auto byte:bank)check(byte==0,"both top buffers cleared");
    for(const auto& bank:MockBackend::bottom_buffers)for(auto byte:bank)check(byte==0,"both bottom buffers cleared");
    return rendered;
}
void preview(const std::string& path,const loading_screen::State& state){
    loading_screen::Surface a{top.pixels.data(),top_bytes,400,240,loading_screen::Format::Rgb8,false};
    loading_screen::Surface b{bottom.pixels.data(),bottom_bytes,320,240,loading_screen::Format::Rgb8,false};
    loading_screen::paint(a,b,state,950);
    FILE* file=std::fopen(path.c_str(),"wb");check(file!=nullptr,"preview file open");
    check(std::fprintf(file,"P6\n400 500\n255\n")>0,"preview header");
    check(std::fwrite(top.pixels.data(),1,top_bytes,file)==top_bytes,"preview top pixels");
    std::array<unsigned char,400*3> row{};
    for(unsigned y=0;y<20;++y)check(std::fwrite(row.data(),1,row.size(),file)==row.size(),"preview screen gap");
    for(unsigned y=0;y<240;++y){row.fill(0);std::memcpy(row.data()+40*3,bottom.pixels.data()+y*320*3,320*3);
        check(std::fwrite(row.data(),1,row.size(),file)==row.size(),"preview bottom pixels");}
    check(std::fclose(file)==0,"preview close");
}
struct LocaleProof {unsigned service_cases=0,strings=0,glyphs=0,states=0;};
void check_utf8_coverage(const loading_screen::Font& font,const char* value,LocaleProof& proof){
    while(*value){const uint8_t code=loading_screen::next_latin1(value);check(code!='?',"dictionary decodes to supported Latin1 without replacement");
        unsigned accent=0;check(loading_screen::fallback_base(code,accent)!='?',"fallback contains every translated letter and accent");
        if(font.valid()&&code!='\n')check(font.glyphs[code]!=0xffff,"original ROM font contains translated glyph");
        ++proof.glyphs;
    }
    ++proof.strings;
}
void check_wrapped(const loading_screen::Font& font,const char* value,unsigned width,unsigned lines){
    for(unsigned line=0;*value&&line<lines;++line){
        while(*value==' ')++value;
        const size_t length=loading_screen::line_bytes(font,value,width);check(length>0,"wrapped text always advances");
        char buffer[80];std::memcpy(buffer,value,length);buffer[length]=0;
        check(loading_screen::menu_width(font,buffer)<=width,"translated line fits its actual font width");
        check(!value[length]||(static_cast<unsigned char>(value[length])&0xc0)!=0x80,"wrap never splits UTF-8 continuation bytes");
        value+=length;
    }
    while(*value==' ')++value;check(!*value,"translated text fits permitted line count");
}
LocaleProof locale_tests(const loading_screen::Artwork* artwork){
    using namespace loading_screen;LocaleProof proof;
    struct Service {bool init_ok=true,read_ok=true;unsigned raw=1,opens=0,reads=0,closes=0;
        bool open(){++opens;return init_ok;}bool read(unsigned& value){++reads;value=raw;return read_ok;}void close(){++closes;}};
    constexpr Language expected[]={Language::English,Language::English,Language::French,Language::German,
        Language::Italian,Language::Spanish,Language::English,Language::English,Language::English,Language::Portuguese,Language::English,Language::English};
    for(unsigned raw=0;raw<12;++raw){Service service;service.raw=raw;check(query_language(service)==expected[raw],"CFGU 0..11 language mapping");
        check(service.opens==1&&service.reads==1&&service.closes==1,"CFGU successful reference balanced");++proof.service_cases;}
    for(unsigned raw:{12u,255u,UINT_MAX}){Service service;service.raw=raw;check(query_language(service)==Language::English,"unknown CFGU language uses English");++proof.service_cases;}
    Service failed_init;failed_init.init_ok=false;check(query_language(failed_init)==Language::English,"CFGU open failure uses English");
    check(failed_init.opens==1&&failed_init.reads==0&&failed_init.closes==0,"failed CFGU open does not close someone else's reference");++proof.service_cases;
    Service failed_read;failed_read.raw=5;failed_read.read_ok=false;check(query_language(failed_read)==Language::English,"CFGU read failure uses English");
    check(failed_read.opens==1&&failed_read.reads==1&&failed_read.closes==1,"CFGU read failure still closes owned reference");++proof.service_cases;
    check(normalize(static_cast<Language>(255))==Language::English,"invalid language enum normalized");
    const char* sample="ÉÃÜçñßàêõ";const uint8_t codes[]={0xc9,0xc3,0xdc,0xe7,0xf1,0xdf,0xe0,0xea,0xf5};
    for(auto code:codes)check(next_latin1(sample)==code,"UTF-8 accent consumes exactly one original glyph");check(!*sample,"UTF-8 sample completely consumed");
    const char* bad="\xc3";check(next_latin1(bad)=='?'&&!*bad,"truncated UTF-8 is one replacement");
    const char* beyond="\xf0\x9f\x8c\x9f";check(next_latin1(beyond)=='?'&&!*beyond,"unsupported Unicode is one replacement");
    char clipped[4];copy_text(clipped,sizeof(clipped),"aaÉbb");check(std::strcmp(clipped,"aa")==0,"bounded text does not retain partial accent bytes");
    Font empty{};const Font& title=artwork?artwork->title_font:empty;const Font& body=artwork?artwork->body_font:empty;
    if(title.valid()){check(menu_width(title,"É")==font_advance(title,0xc9),"title measure uses one Latin1 glyph");
        check(menu_width(body,"ÃÜ")==font_advance(body,0xc3)+font_advance(body,0xdc),"body measure matches Latin1 advances");}
    const Surface target{bottom.pixels.data(),bottom_bytes,320,240,Format::Bgr8,true};fill_guards();
    char malformed[513];std::memset(malformed,0x80,sizeof(malformed)-1);malformed[0]=char(0xf0);malformed[512]=0;
    for(size_t capacity:{size_t(0),size_t(1),size_t(3),size_t(4),size_t(79),std::numeric_limits<size_t>::max()}){
        const size_t length=line_bytes(empty,malformed,0,capacity);
        check(length<=79&&length<=capacity,"malformed UTF-8 cannot exceed caller line capacity");
        char line[80];std::memcpy(line,malformed,length);line[length]=0;
    }
    phase_text(target,empty,112,malformed,256);menu_wrap(target,empty,20,151,malformed,256,3);
    char long_text[1025];std::memset(long_text,'X',1024);long_text[1024]=0;
    phase_text(target,title,112,long_text,256);menu_wrap(target,body,20,151,long_text,256,3);check_guards();
    for(unsigned which=0;which<language_count;++which){const auto language=static_cast<Language>(which);
        for(unsigned key=0;key<unsigned(Label::Count);++key){const auto* value=translated(language,static_cast<Label>(key));
            check(*value,"translated labels present");check_utf8_coverage(title,value,proof);check_utf8_coverage(body,value,proof);}
        check(menu_width(body,translated(language,Label::Cancel))<=unsigned(cancel_button.width-24),"compact Cancel keeps twelve pixels of side padding");
        check(menu_width(body,translated(language,Label::Exit))<=unsigned(exit_button.width-24),"separate Exit keeps twelve pixels of side padding");
        for(Label label:{Label::Initial,Label::VerifyRom,Label::VerifyCache,Label::Extract,Label::Finish,Label::Audio,Label::Ready}){
            const auto line=phase_line(title,translated(language,label),240);
            check(line.count>0&&line.width<=240&&line.height<=(title.valid()?title.cell_height:11),"phase is one bounded line in every language");
            check(line.height==uint64_t(title.valid()?title.cell_height:11)*line.width/line.natural_width,"phase shrink preserves uniform aspect ratio");
        }
        char giant[24001];std::memset(giant,'X',24000);giant[24000]=0;
        const auto giant_line=phase_line(title,giant,240);check(giant_line.count<=79&&giant_line.width<=240,"huge phase input stays bounded to eighty-byte stack buffer");
        phase_text(target,title,83,giant,240);check_guards();
        State state(language);state.artwork=artwork;check_wrapped(title,state.headline,256,2);paint_bottom(target,state);++proof.states;
        const char* phases[]={"verify-rom","verify-cache","extract","finish"};const Label keys[]={Label::VerifyRom,Label::VerifyCache,Label::Extract,Label::Finish};
        for(unsigned i=0;i<4;++i){state.work(phases[i],56,100,7,100);check(!std::strcmp(state.headline,translated(language,keys[i])),"phase uses selected console language");
            check_wrapped(title,state.headline,256,2);paint_bottom(target,state);++proof.states;}
        state.stage(Stage::Audio);check(state.percent==97,"localized audio progress remains 97");check_wrapped(title,state.headline,256,2);paint_bottom(target,state);++proof.states;
        state.stage(Stage::Ready);check(state.percent==100,"localized ready progress remains 100");check_wrapped(title,state.headline,256,2);paint_bottom(target,state);++proof.states;
        for(unsigned i=0;i<8;++i){const auto error=static_cast<Error>(i);
            check_utf8_coverage(body,error_title(language,error),proof);check_utf8_coverage(body,error_help(language,error),proof);
            state.error(error);check(!std::strcmp(state.headline,error_title(language,error)),"error title follows console language");
            check(!std::strcmp(state.detail,error_help(language,error)),"error help follows console language");
            check_wrapped(body,state.headline,256,2);check_wrapped(body,state.detail,256,3);paint_bottom(target,state);++proof.states;}
        check_guards();session_tests(artwork,language);
    }
    return proof;
}
} // namespace
void* operator new(std::size_t bytes){if(count_allocations)++allocations;if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void loading_interaction_tests(){
    using namespace loading_screen;
    check(cancel_button_hit(112,204)&&cancel_button_hit(207,231),"cancel contains inclusive upper-left and last pixel");
    check(!cancel_button_hit(111,204)&&!cancel_button_hit(208,204)&&!cancel_button_hit(112,203)&&!cancel_button_hit(112,232),"cancel excludes all four outside edges");
    check(cancel_button_hit(92,204,exit_button)&&cancel_button_hit(227,231,exit_button)&&!cancel_button_hit(228,204,exit_button),"Exit uses its separately drawn hit rectangle");
    CancelTouch touch;
    check(!touch.update(true,true,true,150,210),"touch already held on entering dialog does not cancel");
    check(!touch.update(false,false,true,0,0),"release arms next touch");
    check(touch.update(true,true,true,150,210),"fresh touch inside button cancels");
    check(!touch.update(false,true,true,150,210),"held touch alone never cancels");
    check(!touch.update(true,true,false,150,210),"closed lid suppresses touch");
    check(!touch.update(true,true,true,150,210),"reopening with held touch stays suppressed");
    touch.update(false,false,true,0,0);check(!touch.update(true,true,true,0,0),"fresh outside touch does not cancel");
    for(unsigned which=0;which<language_count;++which){
        State state{Language(which)};
        for(const char* phase:{"verify-rom","verify-cache","verify-package"}){
            state=State{Language(which)};state.work(phase,1,100,1,100);
            check(!state.preparing_files&&std::strcmp(progress_hint(state),translated(state.language,Label::LongWait)),"normal checking never shows forever text");}
        for(const char* phase:{"extract","write-package"}){
            state=State{Language(which)};state.work(phase,1,100,1,100);
            check(state.preparing_files&&!std::strcmp(progress_hint(state),translated(state.language,Label::LongWait)),"extraction alone shows wait personality");
            state.percent=90;check(!std::strcmp(progress_hint(state),translated(state.language,Label::LongWait)),"90 percent still long wait");
            state.percent=91;check(!std::strcmp(progress_hint(state),translated(state.language,Label::AlmostThere)),"91 percent nearly there");
            state.stage(Stage::Audio);check(!state.preparing_files&&!*progress_hint(state),"audio clears extracting hint");
            state.stage(Stage::Ready);check(!state.preparing_files&&!*progress_hint(state),"ready clears hint");
            state.error(Error::Rom);check(!state.preparing_files&&!*progress_hint(state),"failure clears hint");}
        state=State{Language(which)};state.work("write-package",100,100,100,100);check(state.percent==75,"package generation retains 75 percent boundary");
        state.work("verify-package",100,100,100,100);check(state.percent==95&&state.phase_group==7,"package verification retains 95 percent boundary and group");
    }
}
int main(int argc,char** argv){
    try{
        loading_interaction_tests();
#ifdef SM64DS_TEST_ROM_ART
        check(argc==2||argc==3,"Usage: test-integrated-loading OUTPUT_DIRECTORY [ROM_PATH]");
        const auto memory_before=boot_memory::statistics();
        rom_art::Resources resources;
        if(argc==3){const size_t before_new=allocations;count_allocations=true;
            const bool loaded=resources.load_embedded();count_allocations=false;
            check(loaded,resources.error());check(allocations==before_new,"ROM artwork never uses unavailable game/global heap");}
        const auto memory_loaded=boot_memory::statistics();
#else
        check(argc==2,"Usage: test-integrated-loading OUTPUT_DIRECTORY");
#endif
        check(::mkdir(argv[1],0777)==0||errno==EEXIST,"create preview directory");
        const loading_screen::Artwork* artwork=nullptr;
#ifdef SM64DS_TEST_ROM_ART
        if(argc==3)artwork=&resources.views();
#endif
        const unsigned ratio_cases=progress_tests();packing_tests();blit_tests();renderer_tests(artwork);
#ifdef SM64DS_TEST_ROM_ART
        const auto memory_before_paint=boot_memory::statistics();
#endif
        const unsigned frames=session_tests(artwork);
        const LocaleProof locale=locale_tests(artwork);
#ifdef SM64DS_TEST_ROM_ART
        const auto memory_after_paint=boot_memory::statistics();
        check(memory_after_paint.live_bytes==memory_before_paint.live_bytes&&memory_after_paint.live_blocks==memory_before_paint.live_blocks,"loading frames keep ROM artwork libc allocation count stable");
#endif
        loading_screen::State state;state.artwork=artwork;
        state.work("verify-rom",5033164,16777216,0,2450);preview(std::string(argv[1])+"/verify-rom.ppm",state);
        state.work("extract",9840000,15686745,1535,2450);preview(std::string(argv[1])+"/extract.ppm",state);
        state.stage(loading_screen::Stage::Ready);preview(std::string(argv[1])+"/ready.ppm",state);
        state.error(loading_screen::Error::RomMissing);
        preview(std::string(argv[1])+"/error.ppm",state);
        for(unsigned which=0;which<loading_screen::language_count;++which){
            const auto language=static_cast<loading_screen::Language>(which);loading_screen::State localized(language);localized.artwork=artwork;
            localized.work("extract",9840000,15686745,1535,2450);
            const std::string base=std::string(argv[1])+"/locale-"+loading_screen::language_tag(language);
            preview(base+".ppm",localized);localized.error(loading_screen::Error::Rom);preview(base+"-error.ppm",localized);
        }
        size_t artwork_retained=0,artwork_peak=0,artwork_after_release=0;
#ifdef SM64DS_TEST_ROM_ART
        artwork_retained=resources.retained_bytes();artwork_peak=resources.peak_bytes();resources.release();
        const auto memory_released=boot_memory::statistics();artwork_after_release=memory_released.live_bytes-memory_before.live_bytes;
        check(memory_released.live_bytes==memory_before.live_bytes&&memory_released.live_blocks==memory_before.live_blocks,"all ROM artwork libc allocations released before game startup");
        check(memory_loaded.live_bytes>=memory_before.live_bytes,"ROM artwork owned memory accounted");
#endif
        std::printf("{\"status\":\"passed\",\"ratio_cases_at_least\":%u,\"format_layout_cases\":12,\"blit_oracle_cases\":48,\"controller_updates\":500,"
            "\"throttled_frames\":%u,\"constant_percent_slow_callbacks\":500,\"CXX_heap_allocations_updates_stages_close\":%zu,\"buffer_canaries\":true,\"both_buffers_cleared\":true,"
            "\"extra_framebuffer_allocations\":0,\"artwork_retained_bytes\":%zu,\"artwork_peak_bytes\":%zu,\"artwork_after_release_bytes\":%zu,"
            "\"locale_languages\":6,\"locale_service_cases\":%u,\"locale_strings\":%u,\"locale_glyph_checks\":%u,\"locale_states_painted\":%u,"
            "\"locale_controller_updates\":6000,\"preview_width\":400,\"preview_height\":500,\"physical_hardware\":false}\n",ratio_cases,frames,allocations,artwork_retained,artwork_peak,artwork_after_release,
            locale.service_cases,locale.strings,locale.glyphs,locale.states);
        return 0;
    }catch(const std::exception& error){count_allocations=false;std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
