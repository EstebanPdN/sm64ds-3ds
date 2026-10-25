#pragma once
#include "sm64ds/game_data.hpp"

namespace sm64ds {
struct CollisionData {
    std::vector<uint8_t> kcl, clps;
    uint32_t triangles=0, octree_nodes=0, octree_lists=0;
};
// Validates every triangle, surface reference, octree child and terminated
// triangle list before original game code receives unchecked native pointers.
bool validate_collision_data(CollisionData& data, std::string& error);
bool load_collision_data(AssetPack& pack, const game_data::HandleTable& handles,
                         const game_data::LevelData& level, CollisionData& output,
                         std::string& error);
}
