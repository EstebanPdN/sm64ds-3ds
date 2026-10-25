#pragma once
#include "ntr/gx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sm64ds {
using GameProjectionMatrix=std::array<float,16>;
struct GameProjection {
    float vertical_fov_degrees=60,near_plane=0.1f,far_plane=32768;
    float convergence=150,eye_separation=1;
    // Keep the original vertical framing and scale both axes equally when
    // presenting the DS image on either 3DS panel.
    bool native_matrix=false;
    float source_aspect=256.0f/192.0f;
    GameProjectionMatrix matrix{};
};

inline bool game_projection_finite(const GameProjectionMatrix& matrix) {
    return std::all_of(matrix.begin(),matrix.end(),[](float value) {return std::isfinite(value);});
}

// A batch owns its original matrix: the decoder and caller may retire their
// frame after draw() has copied the vertices. Conventional projection ignores
// the original matrices and keeps its existing FOV/stereo path.
struct GameBatchKey {
    const void* texture=nullptr;
    std::uint8_t wrap=0;
    bool translucent=false,native_matrix=false;
    GameProjectionMatrix matrix{};
    bool operator==(const GameBatchKey& other) const {
        return texture==other.texture && wrap==other.wrap && translucent==other.translucent &&
            native_matrix==other.native_matrix && (!native_matrix || matrix==other.matrix);
    }
    bool operator!=(const GameBatchKey& other) const {return !(*this==other);}
};

inline bool game_batch_key(const ntr::GxTriangle& triangle,const GameProjection& projection,
                           const void* texture,bool translucent,GameBatchKey& result) {
    GameBatchKey key;
    key.texture=texture;key.wrap=triangle.wrap;key.translucent=translucent;
    key.native_matrix=projection.native_matrix;
    if(projection.native_matrix) {
        if(triangle.native_projection)std::copy_n(triangle.projection,16,key.matrix.begin());
        else key.matrix=projection.matrix;
        if(!game_projection_finite(key.matrix))return false;
    }
    result=key;
    return true;
}

// Return PICA matrix rows. DS uses row vectors and [-w,w] clip depth. PICA
// takes column vectors, a sideways panel and [-w,0] depth. The capture target
// retains 256x192 framing; panel output preserves equal scale on both axes.
inline GameProjectionMatrix game_projection_for_target(const GameProjectionMatrix& m,
    const GameProjection& settings,bool bottom,bool capture,float separation) {
    const float horizontal=capture?1.0f:settings.source_aspect/((bottom?320.0f:400.0f)/240.0f);
    GameProjectionMatrix rows={
        m[1],m[5],m[9],m[13],
        -m[0]*horizontal,-m[4]*horizontal,-m[8]*horizontal,-m[12]*horizontal,
        (m[2]-m[3])*0.5f,(m[6]-m[7])*0.5f,(m[10]-m[11])*0.5f,(m[14]-m[15])*0.5f,
        m[3],m[7],m[11],m[15]};
    // Preserve the renderer's existing off-axis stereo convention. Flat
    // orthographic layers stay at screen depth without a second camera step.
    if(std::fabs(m[11])>0.00001f && std::fabs(m[15])<0.00001f) {
        rows[7]+=separation*0.5f;
        rows[6]-=m[11]*(separation/(2*settings.convergence))*m[0]*horizontal;
    }
    return rows;
}
}
