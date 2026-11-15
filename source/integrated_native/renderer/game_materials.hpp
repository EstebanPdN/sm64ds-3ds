#pragma once
#include "game_material.hpp"
struct ModelComponents;

namespace sm64ds::render::game_hooks {
// Resource owner validates bank/bindings once at load, then may free the CPU
// bank. Dimensions come from the compact immutable catalog. Output is borrowed
// frame scratch, indexed by ORIGINAL material ID, not native colour-role runs.
// Ignore all scratch after failure; publish only a successful capture_instance.
// No GPU calls, allocation, vertex processing or virtual VRAM lookup occurs.
// sine_cosine contains the original 4096 interleaved pairs. It is optional
// unless a visible material actually needs the original UV matrix.
Error capture_materials(const ModelBindings&,const ::ModelComponents&,
                        const std::int16_t* sine_cosine,std::size_t pairs,
                        LiveMaterial* scratch,std::size_t capacity,
                        std::uint8_t alpha_cutoff=0) noexcept;
}
