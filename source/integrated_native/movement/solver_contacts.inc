// Included only by the generated headless engine TU, where MarioInstance and
// its owning pool are defined. No copied/guessed MarioState byte offsets.
#include "solver_contacts.h"
#include "decomp/include/object_fields.h"
extern struct SM64SurfaceCollisionData gWaterSurfacePseudoFloor;
int motion64_get_contacts(int32_t mario_id,Motion64Contacts* out) {
    if (!out || mario_id<0 || (size_t)mario_id>=s_mario_instance_pool.size ||
        !s_mario_instance_pool.objects[mario_id]) return 0;
    const struct MarioInstance* instance=s_mario_instance_pool.objects[mario_id];
    const struct MarioState* state=&instance->globalState->mgMarioStateVal;
    Motion64Contacts contacts={0};
    contacts.mario_id=mario_id;
    contacts.floor=state->floor;contacts.ceil=state->ceil;contacts.wall=state->wall;
    if (state->marioObj) {
        contacts.object_floor=state->marioObj->oFloor;
        contacts.platform=state->marioObj->platform;
    }
    *out=contacts;return 1;
}
int motion64_visit_contacts(Motion64ContactsVisitor visitor,void* context) {
    if (!visitor) return -1;
    int count=0;
    for (size_t i=0;i<s_mario_instance_pool.size;++i) {
        if (!s_mario_instance_pool.objects[i]) continue;
        if (i>INT32_MAX) return -1;
        Motion64Contacts contacts;
        if (!motion64_get_contacts((int32_t)i,&contacts) || !visitor(&contacts,context)) return -1;
        ++count;
    }
    return count;
}
int motion64_permanent_surface(const struct SM64SurfaceCollisionData* surface) {
    return surface==&gWaterSurfacePseudoFloor;
}
int motion64_seed_buttons(int32_t mario_id,int jump,int attack,int crouch) {
    if(mario_id<0 || (size_t)mario_id>=s_mario_instance_pool.size || !s_mario_instance_pool.objects[mario_id])return 0;
    const struct MarioInstance* instance=s_mario_instance_pool.objects[mario_id];
    struct Controller* controller=&instance->globalState->mgController;
    const u16 mask=A_BUTTON|B_BUTTON|Z_TRIG;
    const u16 held=(jump?A_BUTTON:0)|(attack?B_BUTTON:0)|(crouch?Z_TRIG:0);
    controller->buttonDown=(controller->buttonDown&~mask)|held;
    controller->buttonPressed&=~mask;
    return 1;
}
