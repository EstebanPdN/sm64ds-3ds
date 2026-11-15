#include "writer_provenance.hpp"
#include <cassert>
using namespace sm64ds::render;
int main(){
    WriterProvenance trace;
    for(unsigned i=0;i<10000;++i)trace.record(0x12345,1,0,4,5);
    assert(trace.size()==1&&trace.entries()[0].calls==10000&&trace.entries()[0].units==50000&&!trace.dropped());
    trace.record(0x12345,2,0,4,1);trace.record(0x12345,2,1,4,1);trace.record(0x12345,2,1,2,1);
    assert(trace.size()==4);
    trace.reset();assert(!trace.size()&&!trace.dropped());
    for(unsigned i=0;i<WriterProvenance::capacity;++i)trace.record(i,1,2,4,0xffffffffu);
    for(unsigned i=0;i<WriterProvenance::capacity;++i)trace.record(i,1,2,4,0xffffffffu);
    assert(trace.size()==WriterProvenance::capacity&&!trace.dropped());
    for(unsigned i=0;i<trace.size();++i)assert(trace.entries()[i].caller==i&&trace.entries()[i].calls==2&&trace.entries()[i].units==0x1fffffffeull);
    trace.record(9999,1,2,4,1);assert(trace.dropped()==1&&trace.size()==WriterProvenance::capacity);
    trace.record(0,1,2,4,1);assert(trace.entries()[0].calls==3&&trace.entries()[0].units==0x1ffffffffull);
}
