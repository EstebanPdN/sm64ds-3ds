"""Guarded native options hooks, only for disposable generated decomp trees."""
from pathlib import Path

CALLBACK = 'sm64ds_options_request'
SOURCE = 'src/_ZN5Stage9PS_UpdateEv.cpp'


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Options source contract changed: ' + before[:90])
    return text.replace(before, after, 1)


def patch_options_sources(decomp):
    """Replace retail pause-page entries before their DS VRAM/backlight code.

    The original selection and background remain intact. Returning from the
    native overlay resumes the existing normal (1) or VS (5) pause parent.
    Caller copies pristine pinned sources before applying this once.
    """
    root = Path(decomp).resolve()
    vendor = (Path(__file__).resolve().parents[1] / 'third_party').resolve()
    path = root / SOURCE
    if root.is_relative_to(vendor) or path.resolve().is_relative_to(vendor):
        raise ValueError('Refusing to modify pinned vendor dependency')
    if not path.resolve().is_relative_to(root) or path.is_symlink():
        raise ValueError('Options generated source must stay inside its own tree')
    text = path.read_text()
    if CALLBACK in text:
        raise ValueError('Options source is already patched')
    text = once(text, 'struct Stage {\n    static void PS_Update();\n};', '''#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
extern "C" void sm64ds_options_request(int page);
#endif

struct Stage {
    static void PS_Update();
};''')
    for entry, page in ((7, 1), (9, 0)):
        anchor = f'    case {entry}: {{\n        REG16(0x400100a)'
        hook = f'''    case {entry}: {{
#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
        /* Native page; keep the original pause selection and both state
           shadows consistent before handing input to the native overlay. */
        data_0209f1ec = data_0209f218 != 0 ? 5 : 1;
        data_0209f248 = data_0209f1ec;
        sm64ds_options_request({page});
        return;
#endif
        REG16(0x400100a)'''
        text = once(text, anchor, hook)
    # All anchors are checked before any mutation is written.
    title_source = 'src/func_ov007_020b63e4.c'
    title_path = root / title_source
    if title_path.resolve().is_relative_to(vendor):
        raise ValueError('Refusing to modify pinned vendor dependency')
    if not title_path.resolve().is_relative_to(root) or title_path.is_symlink():
        raise ValueError('Options generated title source must stay inside its own tree')
    title_text = title_path.read_text()
    if CALLBACK in title_text:
        raise ValueError('Options title source is already patched')
    declaration = '#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)\n#ifdef __cplusplus\nextern "C" {\n#endif\nvoid sm64ds_options_request(int page);\n#ifdef __cplusplus\n}\n#endif\n#endif\n\n'
    title_text = declaration + title_text
    title_text = once(title_text,
                      '    unsigned short v = *(unsigned short *)a[0];',
                      "    unsigned short v = *(unsigned short *)a[0];\n#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)\n    /* Retail options element 7 would animate into substate 8 (the sound\n       and backlight panel). Its real hit test has already accepted this\n       element. Keep the title's states and selection unchanged instead. */\n    if (v == 7) {\n        sm64ds_options_request(0);\n        return;\n    }\n#endif")
    # Validate both translation units before writing either generated copy.
    path.write_text(text)
    title_path.write_text(title_text)
    return [SOURCE, title_source]
