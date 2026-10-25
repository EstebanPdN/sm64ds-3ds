#pragma once
#include "sm64ds/game_data.hpp"

namespace sm64ds {
// Explicit addresses of the compiled native functions used by DSMT. It returns
// false for every unimplemented symbol; no address/delta fallback exists.
bool resolve_native_game_symbol(const char* module,const char* symbol,
                                game_data::NativeSymbol& result);
}
// Data-only startup: verifies constants and relocates all supported menu hooks
// to native function addresses. It does not call any of those game functions.
extern "C" bool sm64ds_prepare_game_data(sm64ds::AssetPack& pack,std::string& error);
