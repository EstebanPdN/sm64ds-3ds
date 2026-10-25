#pragma once
// Simulation-thread storage for the original N64 solver's retained contacts.
// No heap allocation, SD access or moving addresses. The caller pins all
// retained floor/wall/ceiling/platform contacts before each simulation tick.
#include "libsm64.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace movement_geometry {
struct SurfaceKey {
    uint32_t collider, actor, owner_id, kcl;
    uint16_t triangle, slot;
    bool operator==(const SurfaceKey& b) const {
        return collider==b.collider && actor==b.actor && owner_id==b.owner_id &&
               kcl==b.kcl && triangle==b.triangle && slot==b.slot;
    }
};

template<std::size_t Capacity> class StableSurfaces {
    static_assert(Capacity>0 && Capacity<=32768,"Bounded contact storage");
    struct Entry {
        SM64SurfaceCollisionData surface{};
        SurfaceKey key{};
        uint64_t used=0;
        bool occupied=false, retained=false, issued=false;
    };
    std::array<Entry,Capacity> entries{};
    uint64_t clock=0;
    bool open=false;
    uint32_t exhausted_count=0;
public:
    StableSurfaces()=default;
    StableSurfaces(const StableSurfaces&)=delete;
    StableSurfaces& operator=(const StableSurfaces&)=delete;
    StableSurfaces(StableSurfaces&&)=delete;
    StableSurfaces& operator=(StableSurfaces&&)=delete;

    // Only after destroying/resetting every solver that could retain a pointer.
    // Scene/actor changes must never free storage behind an active Mario state.
    void reset_after_solver_deleted() {
        for (auto& e:entries) e=Entry{};
        clock=0;open=false;exhausted_count=0;
    }
    bool begin_tick(const SM64SurfaceCollisionData* const* pins,std::size_t count) {
        if (open || count>Capacity || (count && !pins)) return false;
        // Validate first, preserving all old contacts if the caller missed an
        // ownership transition. A foreign mesh pointer cannot silently pass.
        for (std::size_t i=0;i<count;++i) {
            if (!pins[i]) continue;
            bool found=false;
            for (const auto& e:entries) if (e.occupied && &e.surface==pins[i]) {found=true;break;}
            if (!found) return false;
        }
        for (auto& e:entries) {e.retained=false;e.issued=false;}
        for (std::size_t i=0;i<count;++i) for (auto& e:entries)
            if (pins[i] && e.occupied && &e.surface==pins[i]) e.retained=true;
        open=true;return true;
    }
    void end_tick() {open=false;}
    SM64SurfaceCollisionData* acquire(const SurfaceKey& key) {
        if (!open || !key.collider || !key.kcl || !key.triangle || key.triangle>=32768 || key.slot>=24) return nullptr;
        // Monotonic lifetime ordering. On the theoretical clock wrap, preserve
        // pin/issued protection and make old entries equally eligible.
        if (clock==std::numeric_limits<uint64_t>::max()) {
            for (auto& e:entries) e.used=0;
            clock=0;
        }
        ++clock;
        for (auto& e:entries) if (e.occupied && e.key==key) {
            e.used=clock;e.issued=true;return &e.surface;
        }
        Entry* available=nullptr;
        for (auto& e:entries) {
            if (!e.occupied) {available=&e;break;}
            if (!e.retained && !e.issued && (!available || e.used<available->used)) available=&e;
        }
        if (!available) {++exhausted_count;return nullptr;}
        *available=Entry{};available->key=key;available->occupied=true;
        available->used=clock;available->issued=true;
        return &available->surface;
    }
    uint32_t exhaustions() const {return exhausted_count;}
    std::size_t live() const {
        std::size_t result=0;for (const auto& e:entries) result+=e.occupied;return result;
    }
};
}
