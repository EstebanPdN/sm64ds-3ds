#include "intro_skip.hpp"
#include <cassert>
int main(){
 enhance::IntroSkip s;
 assert(!s.update(true,true));s.begin();
 assert(!s.update(true,true)&&s.hint);
 for(int i=0;i<120;++i)assert(!s.update(true,i%10==0));
 assert(!s.update(false,false));assert(s.update(true,true));
 assert(!s.active&&!s.hint);assert(!s.update(true,true));
 s.begin();s.update(true,true);s.end();assert(!s.update(true,true));
 s.begin();assert(!s.hint);assert(!s.update(false,false));assert(!s.update(true,true));
 assert(!s.update(false,false));assert(s.update(true,true));
}
