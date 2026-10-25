#pragma once
#include "sm64ds/game_geometry.hpp"
#include <map>
#include <tuple>
#include <vector>
namespace sm64ds {
class GameTextures {
public:
    explicit GameTextures(const GameGeometry& geometry):geometry_(geometry) {}
    // Call only after the previous frame's triangles have been consumed. Old
    // decoded versions remain alive for an entire frame, even across uploads.
    void begin_frame();
    bool resolve(uint32_t image,uint32_t palette,const uint32_t*& pixels,int& width,int& height);
    // A decoded version is immutable until erased at begin_frame. Tokens are
    // process-unique, so allocator address reuse cannot certify older texels.
    // Zero means the pointer/dimensions have no certified decoded owner.
    uint64_t revision(const uint32_t* pixels,int width,int height) const;
    static bool resolver(void*,uint32_t,uint32_t,const uint32_t*&,int&,int&);
    std::size_t cached_textures() const {return cache_.size();}
private:
    using Key=std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>;
    struct Texture {std::vector<uint32_t> pixels;int width,height;uint64_t revision=0;};
    const GameGeometry& geometry_;
    std::map<Key,Texture> cache_;
};
}
