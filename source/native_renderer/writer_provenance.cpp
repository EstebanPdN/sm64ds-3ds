#include "writer_provenance.hpp"
namespace sm64ds::render {
void WriterProvenance::reset() noexcept {used_=0;dropped_=0;index_.fill(0);}
void WriterProvenance::record(std::uintptr_t caller,std::uint32_t kind,std::uint32_t target,
                              std::uint32_t width,std::uint32_t units) noexcept {
    std::uint64_t code=caller;std::uint32_t hash=2166136261u;
    for(unsigned i=0;i<8;++i){hash=(hash^std::uint32_t(code&255u))*16777619u;code>>=8;}
    for(auto value:{kind,target,width})for(unsigned i=0;i<4;++i){hash=(hash^(value&255u))*16777619u;value>>=8;}
    unsigned at=hash&(index_.size()-1);
    while(index_[at]){
        auto& entry=entries_[index_[at]-1];
        if(entry.caller==caller&&entry.kind==kind&&entry.target==target&&entry.width==width){
            ++entry.calls;entry.units+=units;return;
        }
        at=(at+1)&(index_.size()-1);
    }
    if(used_==entries_.size()){++dropped_;return;}
    entries_[used_]={caller,target,kind,width,1,units};index_[at]=std::uint16_t(++used_);
}
}
