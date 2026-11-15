#include "model_components_hooks.hpp"
// Model::LoadFile proves the resource's type, including archive members not
// present in the top-level ROM model roster. Observe its published pointer
// after original relocation/shrink, not a guessed header or VRAM address.
extern "C" void* __real__ZN5Model8LoadFileER13SharedFilePtr(void*);
extern "C" void* __wrap__ZN5Model8LoadFileER13SharedFilePtr(void* handle){
    void* file=__real__ZN5Model8LoadFileER13SharedFilePtr(handle);
    sm64ds::render::game_hooks::model_loaded(handle,file);return file;
}
