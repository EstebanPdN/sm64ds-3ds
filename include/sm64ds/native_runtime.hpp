#pragma once
#include "sm64ds/assets.hpp"
#include <cstdint>

namespace sm64ds {
class GameFiles;
bool native_runtime_load(AssetPack& pack, std::string& error);
bool native_runtime_bind_archives(GameFiles& files,std::string& error);
bool native_runtime_activate(uint32_t module);
bool native_runtime_initialize_main();
uint32_t native_runtime_unresolved_data();
}
extern "C" [[noreturn]] void sm64ds_native_missing(const char* name);
