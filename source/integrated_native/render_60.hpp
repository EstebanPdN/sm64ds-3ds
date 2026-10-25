#pragma once
// Original SM64DS integration code. Only immutable presentation snapshots are
// blended. The original loop, actor state, collision, audio and clocks are not
// advanced here. A midpoint is followed by the exact current source frame.
#include "platform/3ds/prepared_mesh_view.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

namespace render60 {
using Frame=prepared_mesh::Frame;
using Ref=std::shared_ptr<const Frame>;
enum class Selection {Source,Midpoint,Endpoint};
class Interpolator {
    Ref source_;
    std::array<std::shared_ptr<Frame>,2> slots_{};
    bool endpoint_pending_=false;
    std::array<uint64_t,512> owners_{};size_t owner_count_=0;
    uint64_t midpoints_=0,endpoints_=0,refused_=0,resets_=0,changed_=0;
    uint64_t last_source_tick_=0,budget_refused_=0,source_gaps_=0,lost_exact_=0;
    uint64_t last_epoch_=0,publication_refreshes_=0;
    uint64_t cut_endpoints_=0,ambiguous_spans_=0,unsafe_matrices_=0;
    uint64_t partial_frames_=0,unmatched_spans_=0,refused_no_source_=0,refused_no_match_=0,refused_budget_=0,refused_slots_=0;
    uint64_t locked30_=0;bool enabled_=true;
    // New poses blended into an unchanged frame: every matched span kept its
    // matrices (or none matched). Counted with their span totals.
    uint64_t still_frames_=0,still_matched_=0,still_unmatched_=0,poses_=0;
    Selection selection_=Selection::Source;
    // One compiled occurrence: render-list actor (unique ID + address) and its
    // display-list layout. Distinct actors never share a key.
    struct SpanKey {
        uint64_t owner;const void* resource;const native_mesh::Planner* plan;uint64_t identity;size_t matrix_count;
        static SpanKey of(const Frame& f,size_t i){const auto& l=f.topology->layouts[i];
            return {f.temporal_owners[i],l.owner.get(),l.plan,l.identity,l.matrix_count};}
        bool operator<(const SpanKey& b)const{
            if(owner!=b.owner)return owner<b.owner;
            if(resource!=b.resource)return std::less<const void*>{}(resource,b.resource);
            if(plan!=b.plan)return std::less<const native_mesh::Planner*>{}(plan,b.plan);
            if(identity!=b.identity)return identity<b.identity;
            return matrix_count<b.matrix_count;
        }
        bool operator==(const SpanKey& b)const{return !(*this<b)&&!(b<*this);}
    };
    // Actor-owned spans sorted by key. A repeated display list inside one actor
    // has no stable submodel identity: mark those spans exact instead of
    // guessing an occurrence correspondence; they are left out of the order.
    static bool ordered(const Frame& f,std::array<uint16_t,512>& order,size_t& count,std::array<bool,512>& flags){
        if(f.temporal_owners.size()>flags.size())return false;
        count=0;
        for(size_t i=0;i<f.temporal_owners.size();++i)if(f.temporal_owners[i])order[count++]=uint16_t(i);
        auto less=[&](size_t x,size_t y){return SpanKey::of(f,x)<SpanKey::of(f,y);};
        std::sort(order.begin(),order.begin()+count,less);
        for(size_t i=1;i<count;++i){auto x=order[i-1],y=order[i];
            if(SpanKey::of(f,x)==SpanKey::of(f,y))flags[x]=flags[y]=true;
        }
        size_t kept=0;
        for(size_t i=0;i<count;++i)if(!flags[order[i]])order[kept++]=order[i];
        count=kept;return true;
    }
    // These are final projected matrices, not decomposed rigid poses. Admit
    // only small basis changes; large turns and degenerate midpoints stay exact.
    static bool safe_basis(const native_mesh::Matrix& a,const native_mesh::Matrix& b){
        float columns[3][3]{};float da[3][3]{},db[3][3]{};
        constexpr float scale=1.f/2147483648.f;
        for(unsigned c=0;c<3;++c){float aa=0,bb=0,ab=0;
            for(unsigned r=0;r<4;++r){float x=float(a.fixed[c*4+r])*scale,y=float(b.fixed[c*4+r])*scale;
                aa+=x*x;bb+=y*y;ab+=x*y;
                if(r<3){da[c][r]=x;db[c][r]=y;columns[c][r]=(x+y)*.5f;}}
            // cos(15 degrees)^2, and at most 25% change in basis magnitude.
            if(aa<=0||bb<=0||ab<=0||ab*ab<.9330127f*aa*bb||aa>1.5625f*bb||bb>1.5625f*aa)return false;
        }
        auto det=[](const float m[3][3]){return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-
            m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);};
        const float x=det(da),y=det(db),z=det(columns);
        if(x==0||y==0||z==0||(x<0)!=(y<0)||(x<0)!=(z<0))return false;
        return std::abs(z)>=.5f*std::min(std::abs(x),std::abs(y));
    }
    static bool spans_valid(const Frame& f){
        if(!f.topology || f.temporal_owners.size()!=f.topology->layouts.size())return false;
        size_t total=0;
        for(const auto& l:f.topology->layouts){if(l.matrix_count>f.matrices.size()-total)return false;total+=l.matrix_count;}
        return total==f.matrices.size();
    }
    // Pair each actor-owned span of the current pose with the previous span of
    // the same actor and display list. An actor entering or leaving the view,
    // spawning or despawning changes only its own spans: those and legacy
    // particle/water spans keep their exact current matrices while every
    // matched actor still blends. Keys never pair two actors' identical models.
    static size_t match(const Frame& a,const Frame& b,std::array<int16_t,512>& previous_span,size_t& unmatched,size_t& ambiguous){
        std::array<uint16_t,512> order_a{},order_b{};size_t count_a=0,count_b=0;
        std::array<bool,512> ambiguous_a{},ambiguous_b{};
        previous_span.fill(-1);unmatched=ambiguous=0;
        if(!ordered(a,order_a,count_a,ambiguous_a)||!ordered(b,order_b,count_b,ambiguous_b))return 0;
        for(size_t i=0;i<b.temporal_owners.size();++i)ambiguous+=ambiguous_b[i];
        size_t x=0,matched=0;
        for(size_t j=0;j<count_b;++j){
            const auto key=SpanKey::of(b,order_b[j]);
            while(x<count_a && SpanKey::of(a,order_a[x])<key)++x;
            if(x<count_a && SpanKey::of(a,order_a[x])==key){previous_span[order_b[j]]=int16_t(order_a[x++]);++matched;}
            else ++unmatched;
        }
        return matched;
    }
    static size_t footprint(const Frame& f){
        return sizeof(Frame)+f.attributes.size()*sizeof(native_mesh::Attribute)+
            f.matrices.size()*sizeof(native_mesh::Matrix)+f.batches.size()*sizeof(V3dsBatch)+
            f.texture_owners.size()*sizeof(std::shared_ptr<const void>)+
            f.batch_env.size()*sizeof(int16_t)+f.envs.size()*sizeof(ntr::gx_gpu::Env)+
            f.temporal_owners.size()*sizeof(uint64_t);
    }
public:
    bool pending()const{return endpoint_pending_;}
    uint64_t midpoints()const{return midpoints_;}
    uint64_t refused()const{return refused_;}
    uint64_t changed()const{return changed_;}
    uint64_t lost_exact()const{return lost_exact_;}
    uint64_t partial_frames()const{return partial_frames_;}
    uint64_t unmatched_spans()const{return unmatched_spans_;}
    Selection selection()const{return selection_;}
    // Display Lock FPS 30: present each source pose once. History keeps
    // tracking, so returning to 60 blends from the next update.
    void set_enabled(bool enabled){enabled_=enabled;}
    void reset(){if(endpoint_pending_)++cut_endpoints_;source_.reset();endpoint_pending_=false;last_source_tick_=last_epoch_=0;++resets_;}
    void observe_owners(const uint64_t* ids,size_t count){
        if(count>owners_.size() || (!ids && count)){reset();owner_count_=0;return;}
        bool same=count==owner_count_;
        if(same)for(size_t i=0;i<count;++i)if(ids[i]!=owners_[i]){same=false;break;}
        if(!same)reset();
        if(count)std::copy(ids,ids+count,owners_.begin());owner_count_=count;
    }
    Ref select(const Ref& frame,bool allow=true,uint64_t tick=0,uint64_t epoch=0) noexcept {
        selection_=Selection::Source;
        if(!frame){reset();return frame;}
        if(frame==source_){
            if(endpoint_pending_){endpoint_pending_=false;++endpoints_;selection_=Selection::Endpoint;}
            if(epoch && epoch!=last_epoch_){last_source_tick_=tick;last_epoch_=epoch;}
            return frame;
        }
        // Raw GX publication can change during the held VBlank without a new
        // game update. That is an exact endpoint refresh, not another pose.
        if(source_ && epoch && epoch==last_epoch_){
            if(endpoint_pending_){endpoint_pending_=false;++endpoints_;selection_=Selection::Endpoint;}
            source_=frame;++publication_refreshes_;return frame;
        }
        if(endpoint_pending_)++lost_exact_;
        ++poses_;
        const bool gap=(tick && last_source_tick_ && tick-last_source_tick_!=2) ||
            (epoch && last_epoch_ && epoch!=last_epoch_+1);
        last_source_tick_=tick;last_epoch_=epoch;
        auto previous=source_;source_=frame;endpoint_pending_=false;
        if(!enabled_){++locked30_;return frame;}
        if(!allow){++budget_refused_;return frame;}
        if(gap){++source_gaps_;return frame;}
        if(!previous){++refused_;++refused_no_source_;return frame;}
        if(!spans_valid(*previous)||!spans_valid(*frame)){++refused_;++refused_no_match_;return frame;}
        // Explicit additional snapshot budget; no unbounded history or texture
        // copies. Both slots own leases until the existing GPU fence retires.
        if(footprint(*frame)>1024*1024){++refused_;++refused_budget_;return frame;}
        std::array<int16_t,512> previous_span{};size_t unmatched=0,ambiguous=0;
        const size_t matched=match(*previous,*frame,previous_span,unmatched,ambiguous);
        if(!matched){++refused_;++refused_no_match_;return frame;}
        try {
            std::shared_ptr<Frame>* slot=nullptr;
            for(auto& candidate:slots_)if(!candidate || candidate.use_count()==1){slot=&candidate;break;}
            if(!slot){++refused_;++refused_slots_;return frame;}
            if(!*slot)*slot=std::allocate_shared<Frame>(ntr::HostAllocator<Frame>{});
            **slot=*frame;
            std::array<uint32_t,512> previous_base{};
            {size_t total=0;for(size_t x=0;x<previous->temporal_owners.size();++x){previous_base[x]=uint32_t(total);total+=previous->topology->layouts[x].matrix_count;}}
            bool moved=false;
            size_t yb=0;
            for(size_t y=0;y<frame->temporal_owners.size();++y){
                const size_t count=frame->topology->layouts[y].matrix_count;
                if(previous_span[y]<0){yb+=count;continue;}
                const size_t xb=previous_base[size_t(previous_span[y])];
                for(size_t i=0;i<count;++i){
                    if(!safe_basis(previous->matrices[xb+i],frame->matrices[yb+i])){++unsafe_matrices_;continue;}
                    auto& out=(**slot).matrices[yb+i].fixed;
                    const auto& a=previous->matrices[xb+i].fixed;const auto& b=frame->matrices[yb+i].fixed;
                    for(unsigned k=0;k<16;++k){
                        // Widen before adding. Only renderer snapshots change.
                        out[k]=int32_t((int64_t(a[k])+int64_t(b[k]))/2);
                        moved|=out[k]!=b[k];
                    }
                }
                yb+=count;
            }
            ambiguous_spans_+=ambiguous;
            if(!moved){++still_frames_;still_matched_+=matched;still_unmatched_+=unmatched;return frame;}
            if(unmatched){++partial_frames_;unmatched_spans_+=unmatched;}
            ++midpoints_;++changed_;endpoint_pending_=true;selection_=Selection::Midpoint;return *slot;
        }catch(const std::bad_alloc&){++refused_;++refused_slots_;return frame;}
    }
    void report()const{
        std::printf("Render60 midpointSelections=%llu endpointSelections=%llu refused=%llu resets=%llu changed=%llu pending=%u budgetRejected=%llu sourceGaps=%llu endpointReplacedBeforeSelection=%llu heldPublicationRefreshes=%llu endpointDiscardedByCut=%llu ambiguousSpans=%llu unsafeMatrices=%llu partialFrames=%llu unmatchedSpans=%llu refusedNoSource=%llu refusedNoMatch=%llu refusedFootprint=%llu refusedSlots=%llu locked30=%llu stillFrames=%llu stillMatched=%llu stillUnmatched=%llu poses=%llu\n",
            (unsigned long long)midpoints_,(unsigned long long)endpoints_,
            (unsigned long long)refused_,(unsigned long long)resets_,
            (unsigned long long)changed_,unsigned(endpoint_pending_),
            (unsigned long long)budget_refused_,(unsigned long long)source_gaps_,(unsigned long long)lost_exact_,
            (unsigned long long)publication_refreshes_,(unsigned long long)cut_endpoints_,
            (unsigned long long)ambiguous_spans_,(unsigned long long)unsafe_matrices_,
            (unsigned long long)partial_frames_,(unsigned long long)unmatched_spans_,
            (unsigned long long)refused_no_source_,(unsigned long long)refused_no_match_,
            (unsigned long long)refused_budget_,(unsigned long long)refused_slots_,(unsigned long long)locked30_,
            (unsigned long long)still_frames_,(unsigned long long)still_matched_,(unsigned long long)still_unmatched_,(unsigned long long)poses_);
    }
};
inline Interpolator interpolation;
// Consumed on the game thread after wait_idle(). These are job result handoff
// timestamps, not worker submit, GPU completion or physical LCD timestamps.
struct JobMetrics {
    uint64_t last=0,successful=0,failed=0;
    uint64_t midpoint_accepted=0,midpoint_rejected=0,endpoint_accepted=0,endpoint_rejected=0;
    std::array<uint64_t,120> intervals{};size_t count=0;
    void completed(bool ok,uint64_t tick,Selection selection){
        if(selection==Selection::Endpoint){if(ok)++endpoint_accepted;else ++endpoint_rejected;}
        if(selection==Selection::Midpoint){if(ok)++midpoint_accepted;else ++midpoint_rejected;}
        if(!ok){++failed;last=0;return;}
        ++successful;
        if(last && tick>=last && count<intervals.size())intervals[count++]=tick-last;
        last=tick;
    }
    void report(bool enabled=true){
        // Discard the disabled window before sorting or formatting. Enabling
        // diagnostics later must start a fresh timing interval.
        if(!enabled){count=0;last=0;return;}
        if(count<intervals.size())return;
        auto sorted=intervals;std::sort(sorted.begin(),sorted.end());
        uint64_t sum=0;for(auto v:intervals)sum+=v;
        const double scale=1000.0/268111856.0;
        std::printf("Render60 jobsAccepted=%llu jobsRejected=%llu handoffMeanMs=%.4f p50=%.4f p95=%.4f p99=%.4f max=%.4f samples=%u midpointJobAccepted=%llu midpointJobRejected=%llu endpointJobAccepted=%llu endpointJobRejected=%llu\n",
            (unsigned long long)successful,(unsigned long long)failed,double(sum)*scale/count,
            double(sorted[59])*scale,double(sorted[113])*scale,double(sorted[118])*scale,
            double(sorted[119])*scale,unsigned(count),(unsigned long long)midpoint_accepted,
            (unsigned long long)midpoint_rejected,(unsigned long long)endpoint_accepted,(unsigned long long)endpoint_rejected);
        interpolation.report();count=0;
    }
};
inline JobMetrics jobs;
}
