#include "../../source/integrated_native/movement/retry_gate.hpp"
#include <cassert>
int main(){
 native_motion::RetryGate g;native_motion::RetryGate::Signature s{{1,2,3,4,5,6}};
 assert(g.ready(s,false));g.defer(s);
 for(int i=0;i<15;++i)assert(!g.ready(s,false));
 assert(g.ready(s,false));
 for(unsigned i=0;i<6;++i){g.defer(s);auto t=s;++t[i];assert(g.ready(t,false));}
 g.defer(s);assert(g.ready(s,true));assert(g.ready(s,false));
 g.defer(s);g.clear();assert(g.ready(s,false));
 unsigned attempts=0;
 for(int i=0;i<1600;++i)if(g.ready(s,false)){++attempts;g.defer(s);}
 assert(attempts==100);
}
