"""Guarded camera hooks for a disposable generated decompilation tree only."""
from prepare_integrated_native_pause import GeneratedTree, block


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Camera source contract changed: ' + before[:90])
    return text.replace(before, after, 1)


def patch_camera_sources(decomp):
    """Call after copying pristine pinned source into the GENERATED tree.

    Caller owns restoration of its generated tree before another preparation.
    Never pass the actual third_party dependency checkout.
    """
    tree = GeneratedTree(decomp)
    path = 'src/func_02009e70.cpp'
    text = tree.read_text(path)
    if 'sm64ds_cstick_cam_follow' in text:
        raise ValueError('Camera source is already patched')
    text = once(text, 'extern int sm64ds_cstick_cam_step(int ds_step);',
                'extern int sm64ds_cstick_cam_step(int ds_step);\nextern int sm64ds_cstick_cam_follow(short *yaw, const void *cam);')
    text = once(text, '    if (r5 != 0) goto L_A5F8;', '''#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
    /* Free orbit bypasses DS arrow/automatic yaw only. Everything after
       L_A814 (distance, wall, floor, ceiling and target clamps) stays original. */
    sb = *(s16 *)(self + 0x17c);
    if (sm64ds_cstick_cam_follow((s16 *)(self + 0x17c), self)) goto L_A814;
#endif
    if (r5 != 0) goto L_A5F8;''')
    text = once(text, 'extern int sm64ds_cstick_cam_pitch(int distance, const void *cam);',
                'extern int sm64ds_cstick_cam_height(int distance,int vertical,const void *cam);\nextern void sm64ds_cstick_cam_orbit(int distance,int *vertical,int *horizontal,const void *cam);\nextern int sm64ds_cstick_cam_direct(int distance,int vertical,const void *cam);')
    text = once(text, '    r7h += sm64ds_cstick_cam_pitch(sp28, self);   /* before the mode/ceiling/floor clamps */',
                '    r7h = *(s32 *)(self + 0x84) + sm64ds_cstick_cam_height(sp28,(s32)r7h - *(s32 *)(self + 0x84),self);')
    text = once(text, '    sp40.z = t0;\n    sp40.x = 0;', """    sp40.z = t0;
#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
    sm64ds_cstick_cam_orbit(sp28,&sp40.y,&sp40.z,self);
#endif
    sp40.x = 0;""")
    text = once(text, '    Math_Function_0203b14c((s32 *)(self + 0x90), r7h, 0x300, 0x14000, 0x100);', """#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
    if(sm64ds_cstick_cam_direct(sp28,(s32)r7h - *(s32 *)(self + 0x84),self))
        *(s32 *)(self + 0x90) = (s32)r7h;
    else
#endif
    Math_Function_0203b14c((s32 *)(self + 0x90), r7h, 0x300, 0x14000, 0x100);""")
    follow_path, follow_text = path, text
    path = 'src/func_0200bb28.c'
    text = tree.read_text(path)
    text = once(text, 'extern int sm64ds_cstick_cam_offset(short *yaw_offset);',
                'extern int sm64ds_cstick_cam_offset(short *yaw_offset);\nextern int sm64ds_cstick_cam_height(int distance,int vertical,const void *cam);\nextern int sm64ds_cstick_cam_direct(int distance,int vertical,const void *cam);')
    text = once(text, '    vsrc.y = comp;', '''#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
    /* Original eight-direction mode has a fixed elevation. Tilt the orbit
       before its sqrt/translation, keeping a real horizontal radius. */
    comp = sm64ds_cstick_cam_height(tbl[0], comp, c);
    if (comp > tbl[0] * 3 / 4) comp = tbl[0] * 3 / 4;
    if (comp < -tbl[0] * 3 / 4) comp = -tbl[0] * 3 / 4;
#endif
    vsrc.y = comp;''')
    text = once(text, '    Math_Function_0203b14c((s32*)(c + 0x90), vout.y, 0x300, 0x18000, 0x100);', """#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
    if(sm64ds_cstick_cam_direct(tbl[0],comp,c)) *(s32*)(c + 0x90) = vout.y;
    else
#endif
    Math_Function_0203b14c((s32*)(c + 0x90), vout.y, 0x300, 0x18000, 0x100);""")
    tree.write_text(follow_path, follow_text)
    tree.write_text(path, text)
    meter='src/actors/dMeter_c.cpp';text=tree.read_text(meter)
    # sm64ds_map deliberately has no SM64DS_PLATFORM_PC definition. Its
    # original 32-bit ABI objects still compile with __3DS__; use that actual
    # target contract or the old buttons survive in the linked game.
    text=once(text,'void dMeter_c::RenderCameraButtons()\n{','void dMeter_c::RenderCameraButtons()\n{\n#if defined(__3DS__)\n    return; // Camera HUD is separate; retain no invisible retail buttons.\n#endif')
    tree.write_text(meter,text)
    touch='src/_ZN5Stage16CheckCameraInputEv.cpp';text=tree.read_text(touch)
    signature='void Stage::CheckCameraInput()'
    start,end,brace=block(text,signature)
    body=text[brace+1:end-1]
    # The old arrow-only opt-out left the separate zoom hitbox active on New3DS.
    # Native input owns the entire pen camera path. Keep the original physical
    # key mapping in CheckInput, and clear stale retail pen repeats/highlights.
    text=text[:brace+1]+'''
#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
    for (unsigned i = 0; i < data_0209f21c && i < 4; ++i) {
        data_0209f498[i].pressed = 0;
        data_0209f498[i].held = 0;
        data_0209f350[i] = 0;
        data_0209f368[i] = 0;
    }
    data_ov002_02111180 = 0;
#else'''+body+'#endif\n'+text[end-1:]
    tree.write_text(touch,text)
    tree.commit()
