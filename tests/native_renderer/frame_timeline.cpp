#include "frame_timeline.hpp"
#include <cassert>
#include <cstring>
using namespace sm64ds::render;
int main(){
    FrameTimeline trace;assert(trace.size()==0);
    for(unsigned i=0;i<FrameTimeline::capacity+17;++i){FrameTiming r;r.frame=i;r.scene=i/200;
        r.start=1000ull*i;r.end=r.start+(i==1017?900:100);r.error=i==1000?4:0;r.endpoint=i%2;
        trace.push(r);}
    assert(trace.size()==FrameTimeline::capacity&&trace.at(0).frame==17&&trace.at(trace.size()-1).frame==1040);
    assert(trace.at(1000-17).error==4&&trace.at(1017-17).end-trace.at(1017-17).start==900);
    FILE* file=std::tmpfile();assert(file&&trace.write_csv(file));std::rewind(file);
    char line[512];assert(std::fgets(line,sizeof line,file)&&std::strstr(line,"source_2d_ticks"));
    unsigned lines=0;while(std::fgets(line,sizeof line,file))++lines;
    assert(lines==FrameTimeline::capacity&&std::fclose(file)==0);
}
