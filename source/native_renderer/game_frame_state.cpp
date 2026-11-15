#include "game_frame_state.hpp"
namespace sm64ds::render::game {
Error capture_clear(std::uint16_t rgb,int alpha,int depth,int id,bool fog,SceneClear& output) noexcept {
    if(rgb>32767||alpha<0||alpha>31||id<0||id>63)return Error::invalid_data;
    if(depth!=32767)return Error::unsupported;
    const auto channel=[rgb](unsigned shift){const unsigned v=(rgb>>shift)&31;return (v<<3)|(v>>2);};
    output={(channel(0)<<24)|(channel(5)<<16)|(channel(10)<<8)|unsigned(alpha*255/31),std::uint8_t(id),fog};
    return Error::none;
}
}
