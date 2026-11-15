#pragma once
#include "scene_frame.hpp"
#include <cstdio>

namespace sm64ds::render {
// Diagnostic native component placements only (3D and prepared 2D layers).
// This format cannot claim complete original-scene or physical acceptance.
// No source addresses, command streams, pixels or GPU handles are serialized.
struct ComponentCaptureInfo {
    std::uint64_t frame=0,legacy_words=0;
    bool clear_known=false,unmigrated_2d=true;
};
// Validates the entire borrowed snapshot before writing its first byte. A
// failed IO may leave a partial stream: the caller must use temp/rename and
// check fflush/fclose before publishing. No allocations or ownership changes.
bool write_component_capture(FILE*,const SceneView&,const ComponentCaptureInfo&) noexcept;
}
