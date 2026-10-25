#include "sm64ds/game_data.hpp"
#include <limits>
#include <unordered_set>

namespace sm64ds::game_data {
bool relocate_block(const std::vector<uint8_t>& source,
                    const std::vector<Relocation>& relocations,
                    const NativeResolver& resolver,
                    std::vector<uint8_t>& output,std::string& error) {
    if(!resolver) {error="Falta el resolvedor de simbolos nativos";return false;}
    struct Patch {size_t offset;uint32_t pointer;};
    std::vector<Patch> patches;
    std::unordered_set<size_t> written;
    for(const auto& r:relocations) {
        if(r.offset%4 || r.offset>source.size() || source.size()-r.offset<4 ||
           !written.insert(r.offset).second || !r.module || !r.symbol ||
           !*r.module || !*r.symbol) {
            error="Registro de relocalizacion invalido o duplicado";return false;
        }
        const auto* p=source.data()+r.offset;
        const uint32_t old=uint32_t(p[0])|uint32_t(p[1])<<8|
                           uint32_t(p[2])<<16|uint32_t(p[3])<<24;
        if(old!=r.original_pointer) {error="La tabla ROM no coincide con su perfil de punteros";return false;}
        NativeSymbol native;
        if(!resolver(r.module,r.symbol,native)) {
            error=std::string("Simbolo nativo no resuelto: ")+r.module+":"+r.symbol;return false;
        }
        if(!native.address || native.kind!=r.kind ||
           (r.kind==SymbolKind::function && r.addend!=0) ||
           (r.kind==SymbolKind::data && r.addend>=native.size) ||
           native.address>std::numeric_limits<uint32_t>::max() ||
           r.addend>std::numeric_limits<uint32_t>::max()-native.address) {
            error=std::string("Destino nativo fuera de limites: ")+r.symbol;return false;
        }
        const uint32_t pointer=uint32_t(native.address+r.addend);
        // Catch the most common accidental "resolver": returning the DS word
        // unchanged. No fall-through to unresolved cartridge pointers exists.
        if(pointer==old) {error="El resolvedor devolvio la direccion DS original";return false;}
        patches.push_back({r.offset,pointer});
    }
    auto result=source;
    for(const auto& patch:patches)
        for(unsigned byte=0;byte<4;++byte)
            result[patch.offset+byte]=uint8_t(patch.pointer>>(byte*8));
    output=std::move(result);
    return true;
}
} // namespace sm64ds::game_data
