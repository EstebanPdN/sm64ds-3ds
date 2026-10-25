#include "sm64ds/game_symbols.hpp"
#include <cstring>

// Signatures match the native core's reviewed ABI adapters. In particular the
// two ROM tail-call veneers carry their r0 argument explicitly under GCC, and
// the default allocation callback actually returns the allocated pointer.
extern "C" {
int func_ov007_020ccab4(int size);
int func_ov007_020cca98(int allocation);
unsigned int func_ov007_020cca80();
int func_ov007_020cca74(int handle);
void func_ov007_020cca68(void* allocation);
uint8_t func_ov007_020cc600(int32_t action);
void func_ov007_020c3e4c(void* allocation);
void* func_ov007_020c3e64(int requested_size);
}

namespace sm64ds {
bool resolve_native_game_symbol(const char* module,const char* symbol,
                                game_data::NativeSymbol& result) {
    if(!module || !symbol || std::strcmp(module,"ov007")) return false;
    struct Entry {const char* name;uintptr_t address;};
    const Entry entries[]={
        {"func_ov007_020ccab4",reinterpret_cast<uintptr_t>(&func_ov007_020ccab4)},
        {"func_ov007_020cca98",reinterpret_cast<uintptr_t>(&func_ov007_020cca98)},
        {"func_ov007_020cca80",reinterpret_cast<uintptr_t>(&func_ov007_020cca80)},
        {"func_ov007_020cca74",reinterpret_cast<uintptr_t>(&func_ov007_020cca74)},
        {"func_ov007_020cca68",reinterpret_cast<uintptr_t>(&func_ov007_020cca68)},
        {"func_ov007_020cc600",reinterpret_cast<uintptr_t>(&func_ov007_020cc600)},
        {"func_ov007_020c3e4c",reinterpret_cast<uintptr_t>(&func_ov007_020c3e4c)},
        {"func_ov007_020c3e64",reinterpret_cast<uintptr_t>(&func_ov007_020c3e64)},
    };
    for(const auto& entry:entries) if(!std::strcmp(symbol,entry.name)) {
        result={entry.address,0,game_data::SymbolKind::function};return true;
    }
    return false;
}
}
extern "C" bool sm64ds_prepare_game_data(sm64ds::AssetPack& pack,std::string& error) {
    return sm64ds::game_data::initialize_native_constants(pack,error) &&
        sm64ds::game_data::initialize_menu_callbacks(pack,sm64ds::resolve_native_game_symbol,error);
}
