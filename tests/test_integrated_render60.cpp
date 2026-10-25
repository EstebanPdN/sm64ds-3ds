#include "render_60.hpp"
#include "native_diagnostics.hpp"
#include <cassert>
#include <climits>
#include <cstring>
#include <iostream>
#include <unistd.h>
using namespace render60;
static std::shared_ptr<Frame> frame(const std::shared_ptr<prepared_mesh::Topology>& topology,int value){
    if(topology->layouts.empty()){
        prepared_mesh::LayoutRef ref;ref.owner=std::make_shared<int>(1);ref.identity=1;ref.matrix_count=1;
        topology->layouts.push_back(ref);
    }
    auto f=std::make_shared<Frame>();f->topology=topology;f->matrices.resize(1);
    f->temporal_owners.push_back(11);
    for(unsigned i=0;i<4;++i)f->matrices[0].fixed[i*5]=4096;
    f->matrices[0].fixed[12]=value;return f;
}
int main(){
    Interpolator p;auto topology=std::make_shared<prepared_mesh::Topology>();
    auto a=frame(topology,100),b=frame(topology,300);
    assert(p.select(a)==a && !p.pending());
    auto midpoint=p.select(b);assert(midpoint!=b && p.pending());
    assert(midpoint->matrices[0].fixed[12]==200 && midpoint->matrices[0].fixed[0]==4096);
    assert(a->matrices[0].fixed[12]==100 && b->matrices[0].fixed[12]==300);
    assert(p.select(b)==b && !p.pending());
    // A retained GPU job may keep a slot alive; never overwrite it.
    auto c=frame(topology,500);auto midpoint2=p.select(c);
    assert(midpoint2!=c && midpoint2!=midpoint && midpoint->matrices[0].fixed[12]==200);
    auto d=frame(topology,700);assert(p.select(d)==d && !p.pending());
    midpoint2.reset();midpoint.reset();
    auto e=frame(topology,INT_MAX-1);auto f=frame(topology,INT_MAX-3);
    p.reset();assert(p.select(e)==e);auto wide=p.select(f);
    assert(wide->matrices[0].fixed[12]==INT_MAX-2);
    // Identical dimensions do not prove identity across scene cuts/reloads.
    auto replacement=frame(std::make_shared<prepared_mesh::Topology>(),19);
    assert(p.select(replacement)==replacement && !p.pending());
    p.reset();assert(p.select(b)==b);
    assert(p.select(c,false)==c && !p.pending());
    assert(p.select({})==nullptr);
    uint64_t ids[]={0x100000010ull,0x200000020ull};
    p.observe_owners(ids,2);assert(p.select(a)==a);assert(p.select(b)!=b);
    p.observe_owners(ids,2);assert(p.select(b)==b);
    std::swap(ids[0],ids[1]);p.observe_owners(ids,2);
    assert(p.select(c)==c); // Identical geometry belongs to different actors.
    // Transient unowned particles must not prevent stable actor interpolation.
    Interpolator q;
    auto particles=std::make_shared<prepared_mesh::Topology>(*topology);
    prepared_mesh::LayoutRef legacy;legacy.owner=std::make_shared<int>(8);legacy.identity=8;legacy.matrix_count=1;
    particles->layouts.push_back(legacy);
    auto water=frame(particles,300);water->matrices.resize(2);water->matrices[1].fixed[0]=777;
    water->temporal_owners.push_back(0);
    assert(q.select(a,true,10)==a);
    auto mixed=q.select(water,true,12);
    assert(mixed!=water && mixed->matrices[0].fixed[12]==200 && mixed->matrices[1].fixed[0]==777);
    assert(q.pending()); // Scheduler must present this exact endpoint even when late.
    assert(q.select(water,false,13)==water && !q.pending() && q.lost_exact()==0);
    auto next=std::make_shared<Frame>(*water);next->matrices[0].fixed[12]=700;
    assert(q.select(next,false,14)==next && !q.pending()); // Optional work loses priority.
    auto later=std::make_shared<Frame>(*next);later->matrices[0].fixed[12]=900;
    assert(q.select(later,true,40)==later && !q.pending()); // No blend across skipped source poses.
    auto other=std::make_shared<Frame>(*later);other->temporal_owners[0]=22;
    assert(q.select(other,true,42)==other); // Reused display list, different actor.
    assert(q.lost_exact()==0);
    Interpolator epoch;
    assert(epoch.select(a,true,10,20)==a);
    assert(epoch.select(b,true,12,21)!=b && epoch.pending());
    auto refreshed=std::make_shared<Frame>(*b);
    assert(epoch.select(refreshed,true,13,21)==refreshed && !epoch.pending());
    assert(epoch.lost_exact()==0);
    assert(epoch.select(c,true,14,22)!=c);
    assert(epoch.select(c,false,15,22)==c && !epoch.pending());
    assert(epoch.select(d,true,16,23)!=d);
    Interpolator held;
    assert(held.select(a,true,10,20)==a);
    assert(held.select(a,true,12,21)==a); // Same immutable source, new game update.
    assert(held.select(b,true,14,22)!=b && held.pending());
    assert(held.select(b,true,15,22)==b && !held.pending());
    assert(held.select(c,true,16,23)!=c); // Endpoint replay did not move source tick.
    // A 180-degree turn must never average into a collapsed object.
    Interpolator rotation;
    auto turned=frame(topology,300);turned->matrices[0].fixed[0]=-4096;turned->matrices[0].fixed[10]=-4096;
    assert(rotation.select(a)==a);assert(rotation.select(turned)==turned && !rotation.pending());
    auto small=frame(topology,500);const float angle=.1f;
    small->matrices[0].fixed[0]=small->matrices[0].fixed[10]=int32_t(4096*std::cos(angle));
    small->matrices[0].fixed[2]=int32_t(-4096*std::sin(angle));small->matrices[0].fixed[8]=int32_t(4096*std::sin(angle));
    rotation.reset();assert(rotation.select(a)==a);assert(rotation.select(small)!=small);
    // Two equal submodels owned by one actor are ambiguous: both stay exact,
    // while a uniquely identified actor in the same frame can still blend.
    auto repeated_topology=std::make_shared<prepared_mesh::Topology>(*topology);
    repeated_topology->layouts.push_back(topology->layouts[0]);
    repeated_topology->layouts.push_back(topology->layouts[0]);
    auto repeated_a=frame(repeated_topology,100),repeated_b=frame(repeated_topology,300);
    repeated_a->temporal_owners={11,11,22};repeated_b->temporal_owners={11,11,22};
    repeated_a->matrices.resize(3,a->matrices[0]);repeated_b->matrices.resize(3,b->matrices[0]);
    Interpolator duplicates;duplicates.select(repeated_a);auto safe=duplicates.select(repeated_b);
    assert(safe!=repeated_b && safe->matrices[0].fixed[12]==300 && safe->matrices[1].fixed[12]==300);
    assert(safe->matrices[2].fixed[12]==200);
    // Actors entering/leaving view change only their own spans. Every matched
    // actor still blends; unmatched spans keep exact current matrices.
    {
        prepared_mesh::LayoutRef model_a=topology->layouts[0],model_b;
        model_b.owner=std::make_shared<int>(2);model_b.identity=2;model_b.matrix_count=1;
        auto scene=[&](std::initializer_list<std::pair<uint64_t,prepared_mesh::LayoutRef>> spans,int value){
            auto t=std::make_shared<prepared_mesh::Topology>();auto f=std::make_shared<Frame>();f->topology=t;
            for(const auto& span:spans){
                t->layouts.push_back(span.second);f->temporal_owners.push_back(span.first);
                native_mesh::Matrix m{};for(unsigned i=0;i<4;++i)m.fixed[i*5]=4096;
                m.fixed[12]=value+int(span.first);f->matrices.push_back(m);
            }
            return f;
        };
        Interpolator entering;
        auto before=scene({{11,model_a}},100),after=scene({{11,model_a},{33,model_b}},300);
        assert(entering.select(before)==before);
        auto blended=entering.select(after);
        assert(blended!=after && entering.pending());
        assert(blended->matrices[0].fixed[12]==211 && blended->matrices[1].fixed[12]==333);
        assert(entering.partial_frames()==1 && entering.unmatched_spans()==1);
        Interpolator leaving;
        auto both=scene({{11,model_a},{33,model_b}},100),alone=scene({{11,model_a}},300);
        assert(leaving.select(both)==both);
        auto kept=leaving.select(alone);
        assert(kept!=alone && kept->matrices.size()==1 && kept->matrices[0].fixed[12]==211);
        assert(leaving.partial_frames()==0);
        // Draw order changes pair spans by actor, never by position.
        Interpolator reordered;
        auto first=scene({{11,model_a},{33,model_b}},100),swapped=scene({{33,model_b},{11,model_a}},300);
        assert(reordered.select(first)==first);
        auto pairs=reordered.select(swapped);
        assert(pairs!=swapped && pairs->matrices[0].fixed[12]==233 && pairs->matrices[1].fixed[12]==211);
        // A changed display list on the same actor is a different occurrence.
        Interpolator switched;
        auto old_model=scene({{11,model_a},{33,model_a}},100),new_model=scene({{11,model_b},{33,model_a}},300);
        assert(switched.select(old_model)==old_model);
        auto partial=switched.select(new_model);
        assert(partial!=new_model && partial->matrices[0].fixed[12]==311 && partial->matrices[1].fixed[12]==233);
        // No common actor: the exact source is presented.
        Interpolator disjoint;
        auto only_a=scene({{11,model_a}},100),only_b=scene({{44,model_a}},300);
        assert(disjoint.select(only_a)==only_a && disjoint.select(only_b)==only_b && !disjoint.pending());
    }
    // Lock FPS 30: no midpoint, exact source each update; 60 resumes blending.
    Interpolator locked;
    locked.set_enabled(false);
    assert(locked.select(a,true,10,20)==a && locked.select(b,true,12,21)==b && !locked.pending());
    locked.set_enabled(true);
    assert(locked.select(c,true,14,22)!=c && locked.pending());
    JobMetrics metrics;metrics.completed(true,100,Selection::Midpoint);
    metrics.completed(false,200,Selection::Endpoint);
    assert(metrics.midpoint_accepted==1 && metrics.endpoint_rejected==1 && metrics.endpoint_accepted==0);
    // User-visible contract: disabled telemetry writes no SD-bound stdout,
    // and toggling diagnostics cannot report samples from a disabled window.
    std::fflush(stdout);FILE* capture=std::tmpfile();assert(capture);
    const int saved_stdout=dup(fileno(stdout));assert(saved_stdout>=0);
    assert(dup2(fileno(capture),fileno(stdout))>=0);
    JobMetrics quiet;
    for(unsigned i=0;i<1000;++i){quiet.completed(true,100+i*100,Selection::Endpoint);quiet.report(false);}
    std::fflush(stdout);assert(std::ftell(capture)==0 && quiet.count==0 && quiet.last==0 && quiet.successful==1000);
    quiet.report(true);std::fflush(stdout);assert(std::ftell(capture)==0);
    for(unsigned i=0;i<121;++i)quiet.completed(true,200000+i*100,Selection::Endpoint);
    quiet.report(true);std::fflush(stdout);assert(std::ftell(capture)>0 && quiet.count==0);
    assert(dup2(saved_stdout,fileno(stdout))>=0);close(saved_stdout);std::fclose(capture);
    std::cout<<"PASS Render60: immutable midpoint/endpoint ownership, busy slots, overflow, topology/actor cuts, held epochs, ambiguous submodels, rotation discontinuity, exact fallback and renderer result accounting\n";
}
