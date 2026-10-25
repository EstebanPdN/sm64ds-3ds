// Full native backend instantiation: compile boundary, not gameplay proof.
#include "ds_world_source.hpp"
template class movement_geometry::DsWorldSource<1024,32>;
extern "C" unsigned fixture_native_world_source_bytes() {
    return sizeof(movement_geometry::DsWorldSource<1024,32>);
}
extern "C" {
unsigned char fixture_native_world_source_storage[sizeof(movement_geometry::DsWorldSource<1024,32>)];
unsigned char fixture_native_platform_owner_storage[sizeof(movement_geometry::StablePlatforms<32>)];
}
