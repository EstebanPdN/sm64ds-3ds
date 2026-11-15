#pragma once
#include "scene_frame.hpp"
#include <limits>
#include <algorithm>

namespace sm64ds::render {
// Match semantic instances across two immutable captures. Draw order, matrix
// values and equal mesh topology never establish identity. Storage is bounded
// and reusable; callers keep it off the small render-thread stack.
class OccurrenceMatcher {
public:
    static constexpr std::uint16_t missing=std::numeric_limits<std::uint16_t>::max();
    static constexpr std::size_t capacity=768;
    struct Result {
        std::array<std::uint16_t,capacity> previous{};
        std::size_t matched=0,unmatched=0,ambiguous=0;
        bool continuous=false;
    };
    Error match(const SceneView& previous,const SceneView& current) noexcept {
        if(previous.occurrences>capacity||current.occurrences>capacity)return Error::capacity;
        if((previous.occurrences&&!previous.occurrence)||(current.occurrences&&!current.occurrence))return Error::invalid_data;
        result_={};result_.previous.fill(missing);result_.unmatched=current.occurrences;
        if(previous.scene!=current.scene||previous.serial==std::numeric_limits<std::uint64_t>::max()||
           current.serial!=previous.serial+1||previous.scene_screen!=current.scene_screen||previous.scene_order!=current.scene_order)return Error::none;
        result_.continuous=true;slots_.fill({});
        for(std::size_t i=0;i<previous.occurrences;++i){const auto& item=previous.occurrence[i];
            if(!item.identity.tracked())continue;
            auto& slot=lookup(item,previous,current);
            if(slot.previous==missing)slot.previous=std::uint16_t(i);
            else slot.flags|=1; // a reused model rendered twice is ambiguous
        }
        for(std::size_t i=0;i<current.occurrences;++i){const auto& item=current.occurrence[i];
            if(!item.identity.tracked())continue;
            auto& slot=lookup(item,previous,current);
            if(slot.current==missing)slot.current=std::uint16_t(i);
            else slot.flags|=2;
        }
        for(const auto& slot:slots_){
            if(slot.current==missing)continue;
            if(slot.flags){++result_.ambiguous;continue;}
            if(slot.previous==missing)continue;
            const auto& a=previous.occurrence[slot.previous];const auto& b=current.occurrence[slot.current];
            // A changing primitive count is a discontinuity within this draw,
            // even when the owning actor and model component survived.
            if(a.count!=b.count)continue;
            result_.previous[slot.current]=slot.previous;++result_.matched;--result_.unmatched;
        }
        return Error::none;
    }
    const Result& result() const noexcept {return result_;}
private:
    struct Slot {std::uint16_t previous=missing,current=missing;std::uint8_t flags=0;};
    std::array<Slot,2048> slots_{};
    Result result_{};
    static bool same_key(const Occurrence& a,const Occurrence& b) noexcept {
        return a.identity.owner==b.identity.owner&&a.identity.component==b.identity.component&&
            a.identity.piece==b.identity.piece&&a.file_id==b.file_id&&a.kind==b.kind;
    }
    Slot& lookup(const Occurrence& item,const SceneView& previous,const SceneView& current) noexcept {
        auto h=item.identity.owner;
        h^=item.identity.component+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
        h^=std::uint64_t(item.file_id)<<32|item.identity.piece;
        h^=std::uint64_t(item.kind)*0x9e3779b97f4a7c15ull;
        h^=h>>33;h*=0xff51afd7ed558ccdull;h^=h>>33;
        auto index=std::size_t(h)&(slots_.size()-1);
        for(;;){auto& slot=slots_[index];
            if(slot.previous==missing&&slot.current==missing)return slot;
            const auto& stored=slot.previous!=missing?previous.occurrence[slot.previous]:current.occurrence[slot.current];
            if(same_key(item,stored))return slot;
            index=(index+1)&(slots_.size()-1);
        }
    }
};
}

namespace sm64ds::render {
// Worker-owned pose history. The simulation's sealed buffers are never edited
// and no borrowed game/frame pointer survives remember(). Static geometry and
// current materials remain shared; only live pose/skin arrays are copied.
class PoseHistory {
public:
    static constexpr std::size_t max_parts=4096,max_skin_vertices=4095*3;
    Error remember(const SceneView& frame) noexcept {
        const auto error=admit(frame);if(error!=Error::none)return error;
        std::copy_n(frame.occurrence,frame.occurrences,occurrence_.data());
        std::copy_n(frame.part,frame.parts,part_.data());
        std::copy_n(frame.skin_vertex,frame.skin_vertices,skin_.data());
        previous_={};previous_.occurrence=occurrence_.data();previous_.occurrences=frame.occurrences;
        previous_.part=part_.data();previous_.parts=frame.parts;previous_.skin_vertex=skin_.data();previous_.skin_vertices=frame.skin_vertices;
        previous_.scene=frame.scene;previous_.serial=frame.serial;previous_.scene_screen=frame.scene_screen;previous_.scene_order=frame.scene_order;
        ready_=true;return Error::none;
    }
    void reset() noexcept {ready_=false;}
    Error midpoint(const SceneView& frame,SceneView& output) noexcept {
        const auto error=admit(frame);if(error!=Error::none)return error;
        output=frame;blended_=0;
        if(!ready_)return Error::none;
        const auto match_error=matcher_.match(previous_,frame);if(match_error!=Error::none)return match_error;
        if(!matcher_.result().continuous)return Error::none;
        std::copy_n(frame.part,frame.parts,middle_part_.data());
        std::copy_n(frame.skin_vertex,frame.skin_vertices,middle_skin_.data());
        for(std::size_t i=0;i<frame.occurrences;++i){
            const auto index=matcher_.result().previous[i];if(index==OccurrenceMatcher::missing)continue;
            const auto& a=occurrence_[index];const auto& b=frame.occurrence[i];
            if(b.kind==Occurrence::Kind::model){
                for(std::size_t j=0;j<b.count;++j){const auto& before=part_[a.first+j];const auto& after=frame.part[b.first+j];
                    if(!before.visible||!after.visible)continue;
                    auto& matrix=middle_part_[b.first+j].object_to_clip;
                    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
                        matrix.row[r][c]=before.object_to_clip.row[r][c]*.5f+after.object_to_clip.row[r][c]*.5f;
                    ++blended_;
                }
            }else if(b.kind==Occurrence::Kind::skin){
                for(std::size_t j=0;j<b.count;++j){const auto& before=skin_[a.first+j].base;const auto& after=frame.skin_vertex[b.first+j].base;
                    auto& vertex=middle_skin_[b.first+j].base;
                    vertex.x=before.x*.5f+after.x*.5f;vertex.y=before.y*.5f+after.y*.5f;vertex.z=before.z*.5f+after.z*.5f;
                }
            }
        }
        output.part=middle_part_.data();output.skin_vertex=middle_skin_.data();return Error::none;
    }
    std::size_t blended_parts() const noexcept {return blended_;}
private:
    SceneView previous_{};
    bool ready_=false;
    std::size_t blended_=0;
    OccurrenceMatcher matcher_;
    std::array<Occurrence,OccurrenceMatcher::capacity> occurrence_{};
    std::array<PreparedPart,max_parts> part_{},middle_part_{};
    std::array<NormalVertex,max_skin_vertices> skin_{},middle_skin_{};
    static Error admit(const SceneView& s) noexcept {
        if(s.parts>max_parts||s.skin_vertices>max_skin_vertices||s.occurrences>OccurrenceMatcher::capacity)return Error::capacity;
        if((s.parts&&!s.part)||(s.skin_vertices&&!s.skin_vertex)||(s.occurrences&&!s.occurrence))return Error::invalid_data;
        for(std::size_t i=0;i<s.occurrences;++i){const auto& o=s.occurrence[i];
            if(o.kind==Occurrence::Kind::model&&(o.first>s.parts||o.count>s.parts-o.first))return Error::invalid_data;
            if(o.kind==Occurrence::Kind::skin&&(o.first>s.skin_vertices||o.count>s.skin_vertices-o.first||o.state>=s.parts))return Error::invalid_data;
        }
        return Error::none;
    }
};
}
