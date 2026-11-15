#pragma once
#include "image_catalog.hpp"
#include "game_particle_geometry.hpp"

namespace sm64ds::render::game_hooks {
struct ParticleDraw {
    game::ParticleBillboardGeometry geometry;
    Material material;
    std::uint32_t bank_id=0;
    std::uint16_t image=0;
    std::uint8_t polygon_id=63;
    bool fog=false;
};
using SubmitParticle=bool (*)(const ParticleDraw&,void*);
bool install_particles(ImageCatalog,SubmitParticle,void*) noexcept;
void particle_clock(std::uint64_t (*)()) noexcept;
// Original resource/manager lifetime notifications, before source deallocation.
// Descriptor owners retain only compiled identities, not source image pointers.
void particle_file_loaded(const void* file,std::uint32_t bank_id) noexcept;
void particle_file_retiring(const void* file) noexcept;
void particle_manager_retiring(const void* manager) noexcept;
struct ParticleStatistics {
    std::uint64_t resource_loads=0,manager_loads=0,bindings=0,calls=0,draws=0,failures=0,flushes=0;
    std::uint64_t capture_ticks=0;
    const char* last_issue="ok";
    std::uint32_t last_detail=0;
};
const ParticleStatistics& particle_statistics() noexcept;
}
