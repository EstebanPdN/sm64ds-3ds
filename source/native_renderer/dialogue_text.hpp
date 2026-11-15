#pragma once
#include "scene_frame.hpp"

namespace sm64ds::render {
struct DialogueCharacter {
    std::uint8_t code=0,advance=0;
    bool white=true;
};
struct DialogueGlyph {
    std::int16_t x=0,y=0;
    std::uint8_t code=0;
    bool white=true;
};
// Retained ink for the ordinary Message font. The source parser owns its
// strings, choices and timing; this owner stores glyphs rather than DS tiles.
class DialogueText {
public:
    static constexpr std::size_t capacity=2048;
    void reset() noexcept {used_=0;}
    Error line(const DialogueCharacter*,std::size_t count,std::uint8_t x,unsigned row) noexcept;
    Error scroll(const DialogueCharacter*,std::size_t count,std::uint8_t x,unsigned rows,unsigned phase) noexcept;
    const DialogueGlyph* glyphs() const noexcept {return glyph_.data();}
    std::size_t size() const noexcept {return used_;}
    Error capture(SceneFrame&,Screen,float x,float y,const ScreenClip&,unsigned rows,unsigned width) const noexcept;
private:
    std::array<DialogueGlyph,capacity> glyph_{};
    std::size_t used_=0;
    void append(const DialogueCharacter*,std::size_t,std::uint8_t x,int y) noexcept;
};
}
