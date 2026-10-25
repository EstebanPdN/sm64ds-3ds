#include "SaveData.h"
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" { SaveData data_0209caa0; }
int main(){
    // Execute the original implementation, not a duplicate flag expression.
    for(unsigned flags=0;flags<256;++flags){
        std::memset(&data_0209caa0,0xa5,sizeof data_0209caa0);
        data_0209caa0.flags2=static_cast<int>(0x12340000u|flags);
        SaveData expected=data_0209caa0;
        expected.flags2|=0x80;
        SaveData::SetCharacterIntro(7);
        assert(std::memcmp(&data_0209caa0,&expected,sizeof expected)==0);
        assert((unsigned(data_0209caa0.flags2)&15)==(flags&15));
        SaveData::SetCharacterIntro(7);
        assert(std::memcmp(&data_0209caa0,&expected,sizeof expected)==0);
    }
    std::puts("PASS original intro-seen API: only flag7 changes; character unlocks and every other byte preserved, no cart writer linked");
}
