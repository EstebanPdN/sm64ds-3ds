#include "stable_surfaces.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>
using movement_geometry::SurfaceKey;
using movement_geometry::StableSurfaces;
static SurfaceKey key(unsigned n) {return {0x9000000,0x9100000,n,0x9200000,uint16_t(n%30000+1),1};}
int main() {
    static_assert(!std::is_move_constructible<StableSurfaces<8>>::value,"No moving retained addresses");
    StableSurfaces<8> cache;
    assert(!cache.acquire(key(0)));
    assert(!cache.begin_tick(nullptr,1));
    assert(cache.begin_tick(nullptr,0));
    auto invalid=key(0);invalid.slot=24;assert(!cache.acquire(invalid));
    invalid=key(0);invalid.triangle=32768;assert(!cache.acquire(invalid));
    SM64SurfaceCollisionData* contacts[8];
    for (unsigned i=0;i<8;++i) {
        contacts[i]=cache.acquire(key(i));assert(contacts[i]);
        contacts[i]->isValid=1;contacts[i]->originOffset=float(100+i);
        assert(cache.acquire(key(i))==contacts[i]);
    }
    // Every issued contact is protected for the rest of the current tick.
    assert(!cache.acquire(key(99)) && cache.exhaustions()==1);
    assert(!cache.begin_tick(nullptr,0));cache.end_tick();
    SM64SurfaceCollisionData foreign{};
    const SM64SurfaceCollisionData* bad[]={&foreign};
    assert(!cache.begin_tick(bad,1));
    const SM64SurfaceCollisionData* pins[]={contacts[0],contacts[3],contacts[7],nullptr};
    assert(cache.begin_tick(pins,4));
    // Recycled collider/actor pointers with new owner IDs must not overwrite
    // retained old-generation floor/wall/ceiling records.
    for (unsigned i=100;i<105;++i) assert(cache.acquire(key(i)));
    assert(!cache.acquire(key(105)));
    assert(contacts[0]->originOffset==100 && contacts[3]->originOffset==103 && contacts[7]->originOffset==107);
    cache.end_tick();
    // Sustained churn: three old contacts remain pinned while five slots are
    // reused on each tick. Check retained contents and address stability.
    for (unsigned tick=0;tick<10000;++tick) {
        assert(cache.begin_tick(pins,4));
        for (unsigned j=0;j<5;++j) {
            auto* s=cache.acquire(key(1000+tick*5+j));assert(s);
            assert(s!=contacts[0] && s!=contacts[3] && s!=contacts[7]);
            s->originOffset=-float(tick+1);s->isValid=1;
        }
        assert(contacts[0]->originOffset==100 && contacts[3]->originOffset==103 && contacts[7]->originOffset==107);
        cache.end_tick();
    }
    // Existing keys keep their addresses and payload when acquired again.
    assert(cache.begin_tick(pins,4));assert(cache.acquire(key(0))==contacts[0]);cache.end_tick();
    cache.reset_after_solver_deleted();assert(!cache.live() && !cache.exhaustions());
    assert(cache.begin_tick(nullptr,0));auto* fresh=cache.acquire(key(0));assert(fresh && !fresh->isValid && fresh->originOffset==0);cache.end_tick();
    std::printf("stable surfaces: 10000 churn ticks, protected contacts and explicit reset passed; 256-slot host bytes=%zu\n",sizeof(StableSurfaces<256>));
}
