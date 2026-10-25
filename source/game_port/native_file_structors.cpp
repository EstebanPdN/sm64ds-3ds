// The port's existing structor forwarders, with ARM11's native 32-bit ABI.
// Source: pinned port/hal/dtor_forwarders_gen_w9c_structors.cpp. The recovered
// common file-pointer constructor/destructor bodies retain resource ownership.
#include "daStarGate_c.h"

extern "C" {
void func_02017acc(void*,unsigned);
void func_02017ab4(void*);
void func_02017b4c(void*,unsigned);
void SharedFilePtr_Destruct_Clsn(void*);
void _ZN18BattanModelFilePtrC1Ej(void* self,unsigned id) {func_02017acc(self,id);}
void _ZN18BattanModelFilePtrD1Ev(void* self) {func_02017ab4(self);}
void _ZN22BattanCollisionFilePtrC1Ej(void* self,unsigned id) {func_02017b4c(self,id);}
void _ZN22BattanCollisionFilePtrD1Ev(void* self) {SharedFilePtr_Destruct_Clsn(self);}
void _ZN24Kb1BillboardModelFilePtrC1Ej(void* self,unsigned id) {func_02017acc(self,id);}
void _ZN24Kb1BillboardModelFilePtrD1Ev(void* self) {func_02017ab4(self);}
void _ZN20StarGateModelFilePtrC1Ej(void* self,unsigned id) {func_02017acc(self,id);}
void _ZN20StarGateModelFilePtrD1Ev(void* self) {func_02017ab4(self);}
}

namespace {
// These are compiler-owned registration nodes, with the original three-word
// layout. The original shutdown list links them through func_020731dc; they
// need no fixed cartridge address and stay resident with their native module.
struct DestructorNode {void* next;void (*destroy)(void*);void* object;};
static_assert(sizeof(DestructorNode)==12);
static_assert(sizeof(daStarGate_c::State)==16);
DestructorNode battan_model_node{},battan_collision_node{};
DestructorNode kb1_billboard_node{},star_gate_node{};
}

extern "C" {
void func_020731dc(void* object,void (*destroy)(void*),void* node);
extern unsigned char data_ov023_02112088[8],data_ov023_02112080[8];
extern unsigned char data_ov044_02111680[8],data_ov100_02148934[8];

// GCC emits its own local _GLOBAL__sub_I_* entry for these three recovered
// C++ translation units, not their configured MWCC __sinit_* name. The native
// runtime deliberately removes process-start init_array calls and activates
// the original module table after ROM/catalog restoration. Restore these
// three table seats with all original effects and exact engine callees.
void sm64ds_sinit_battan() asm("__sinit_d_a_obj_fm_battan.cpp");
void sm64ds_sinit_kb1_billboard() asm("__sinit_d_a_obj_kb1_billboard.cpp");
void sm64ds_sinit_star_gate() asm("__sinit_d_a_star_gate.cpp");

void sm64ds_sinit_battan() {
    func_02017acc(data_ov023_02112088,1558);
    func_020731dc(data_ov023_02112088,func_02017ab4,&battan_model_node);
    func_02017b4c(data_ov023_02112080,1559);
    func_020731dc(data_ov023_02112080,SharedFilePtr_Destruct_Clsn,&battan_collision_node);
}
void sm64ds_sinit_kb1_billboard() {
    func_02017acc(data_ov044_02111680,1570);
    func_020731dc(data_ov044_02111680,func_02017ab4,&kb1_billboard_node);
}
void sm64ds_sinit_star_gate() {
    func_02017acc(data_ov100_02148934,0x4a1);
    func_020731dc(data_ov100_02148934,func_02017ab4,&star_gate_node);
    // Use the actual ARM C++ member-pointer representation, preserving every
    // original init/main pair and its null entries. The POD vector is already
    // zero-initialized storage; the original initializer leaves it untouched.
    using Gate=daStarGate_c;
    Gate::ST_OPEN_CLOSE={&Gate::St_OpenClose_Init,&Gate::St_OpenClose_Main};
    Gate::ST_STAY_CLOSED={nullptr,&Gate::St_StayClosed_Main};
    Gate::ST_WAIT={nullptr,&Gate::St_Wait_Main};
    Gate::ST_TALKING_TO_PLAYER={&Gate::St_TalkingToPlayer_Init,&Gate::St_TalkingToPlayer_Main};
    Gate::ST_UNLOCKING={&Gate::St_Unlocking_Init,&Gate::St_Unlocking_Main};
}
}
