"""Extend retail Stage pause states in a disposable generated source tree."""
from pathlib import Path

GUARD='defined(SM64DS_PLATFORM_PC) && defined(__3DS__)'
FILES=['src/_ZN5Stage9PS_UpdateEv.cpp','src/_ZN5Stage7PS_InitEv.cpp','src/_ZN5Stage9PS_RenderEv.cpp','src/_ZN7Message7DisplayEj.cpp','src/_ZN7Message16DisplayPauseTextEth.cpp','src/func_ov007_020b63e4.c','src/func_ov007_020b7090.c']


class GeneratedTree:
    """Validate paths and stage all source contracts before the first write.

    Shared by the UI preparers. This protects against contract failures and
    aliases into the pinned dependency; it is not a filesystem transaction.
    """
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.vendor = (Path(__file__).resolve().parents[1] / 'third_party').resolve()
        if self.root.is_relative_to(self.vendor):
            raise ValueError('Refusing pinned vendor dependency')
        self.pending = {}
        self.originals = {}

    def path(self, name):
        path = self.root / name
        relative = path.relative_to(self.root)
        if '..' in relative.parts or any((self.root / Path(*relative.parts[:i])).is_symlink()
                                         for i in range(1, len(relative.parts) + 1)):
            raise ValueError('Generated source escapes tree through symlink: ' + str(name))
        resolved = path.resolve()
        if not resolved.is_relative_to(self.root) or resolved.is_relative_to(self.vendor):
            raise ValueError('Generated source escapes tree: ' + str(name))
        return path

    def read_text(self, name):
        if name in self.pending:
            return self.pending[name].decode()
        path = self.path(name)
        raw = path.read_bytes()
        self.originals.setdefault(name, raw)
        return raw.decode()

    def write_bytes(self, name, raw):
        path = self.path(name)
        self.originals.setdefault(name, path.read_bytes() if path.exists() else None)
        self.pending[name] = raw

    def write_text(self, name, text):
        self.write_bytes(name, text.encode())

    def commit(self):
        # Revalidate every destination, including files read early in a prepare.
        for name, original in self.originals.items():
            path = self.path(name)
            if (path.read_bytes() if path.exists() else None) != original:
                raise ValueError('Concurrent generated source edit: ' + str(name))
        for name, raw in self.pending.items():
            self.path(name).write_bytes(raw)


def once(text,before,after):
    if text.count(before)!=1:raise ValueError('Native pause contract changed: '+before[:90])
    return text.replace(before,after,1)


def block(text,anchor):
    start=text.index(anchor);brace=text.index('{',start);level=0;i=brace
    quote=None;comment=None
    while i<len(text):
        ch=text[i];nxt=text[i:i+2]
        if comment=='line':
            if ch=='\n':comment=None
        elif comment=='block':
            if nxt=='*/':comment=None;i+=1
        elif quote:
            if ch=='\\':i+=1
            elif ch==quote:quote=None
        elif nxt=='//':comment='line';i+=1
        elif nxt=='/*':comment='block';i+=1
        elif ch in "\"'":quote=ch
        elif ch=='{':level+=1
        elif ch=='}':
            level-=1
            if level==0:return start,i+1,brace
        i+=1
    raise ValueError('Unbalanced native pause source')


def patch_sources(decomp):
    tree=GeneratedTree(decomp)
    contents={}
    for name in FILES:
        contents[name]=tree.read_text(name)
        if 'sm64ds_native_pause_' in contents[name] or 'sm64ds_options_request' in contents[name] or 'sm64ds_front_options_' in contents[name]:raise ValueError('Native pause source already patched or old bypass present')
    name=FILES[0];text=contents[name]
    text=once(text,'struct Stage {',f'''#if {GUARD}
extern "C" {{
void sm64ds_native_pause_parent(unsigned state);
void sm64ds_native_pause_begin(int controller);
void sm64ds_native_pause_update();
void sm64ds_native_pause_restore_parent();
}}
#endif

struct Stage {{''')
    for entry,controller in [(7,1),(9,0)]:
        anchor=f'    case {entry}: {{';start,end,brace=block(text,anchor);chunk=text[start:end]
        pos=chunk.rfind('        return;')
        if pos<0:raise ValueError('Missing native pause entry return')
        chunk=chunk[:pos]+f'#if {GUARD}\n        sm64ds_native_pause_begin({controller});\n#endif\n'+chunk[pos:]
        text=text[:start]+chunk+text[end:]
    for entry in ['8','0xa']:
        anchor=f'    case {entry}: {{';start,end,brace=block(text,anchor);body=text[brace+1:end-1]
        replacement=f'''{anchor}
#if {GUARD}
        sm64ds_native_pause_update();
        return;
#else{body}#endif
    }}'''
        text=text[:start]+replacement+text[end:]
    for target in [7,9]:
        anchor=f'            data_0209f1ec = {target};'
        count=text.count(anchor)
        if count!=2:raise ValueError(f'Expected two original transitions to {target}, found {count}')
        text=text.replace(anchor,f'#if {GUARD}\n            sm64ds_native_pause_parent(data_0209f248);\n#endif\n'+anchor)
    for entry in [0,2,4]:
        start,end,brace=block(text,f'    case {entry}: {{');chunk=text[start:end]
        chunk=chunk.replace('return;',f'\n#if {GUARD}\n        sm64ds_native_pause_restore_parent();\n#endif\n        return;')
        text=text[:start]+chunk+text[end:]
    contents[name]=text
    name=FILES[1];text=contents[name]
    text=once(text,'void Stage::PS_Init()\n{',f'''#if {GUARD}
extern "C" void sm64ds_native_pause_reset();
#endif
void Stage::PS_Init()
{{
#if {GUARD}
    sm64ds_native_pause_reset();
#endif''')
    contents[name]=text
    name=FILES[2];text=contents[name]
    text=once(text,'class Stage {',f'''#if {GUARD}
extern "C" int sm64ds_native_pause_render_cursor();
#endif
class Stage {{''')
    text=once(text,'    unsigned char f248b = data_0209f248;',f'''#if {GUARD}
    if (sm64ds_native_pause_render_cursor()) return;
#endif
    unsigned char f248b = data_0209f248;''')
    contents[name]=text
    for name,signature,y in [
        (FILES[3],'void Message::Display(unsigned int msg)',80),
        (FILES[4],'void Message::DisplayPauseText(unsigned short n, unsigned char b)',72),
    ]:
        text=contents[name]
        declaration=f'#if {GUARD}\nextern "C" void sm64ds_native_pause_controller_label(unsigned y);\n#endif\n'
        text=once(text,signature,declaration+signature)
        start,end,brace=block(text,signature)
        text=text[:end-1]+f'#if {GUARD}\n    sm64ds_native_pause_controller_label({y});\n#endif\n'+text[end-1:]
        contents[name]=text
    # Retail wrench element7 opens sound/backlight page8 in020b50e8.
    # Intercept its actual selection, not a coordinate-only global shortcut.
    wrench='src/func_ov007_020b63e4.c'
    text=contents[wrench]
    text='#ifdef __cplusplus\nextern "C" int sm64ds_front_options_begin(void);\n#else\nextern int sm64ds_front_options_begin(void);\n#endif\n'+text
    text=once(text,'    unsigned short v = *(unsigned short *)a[0];','    unsigned short v = *(unsigned short *)a[0];\n#if '+GUARD+'\n    if (v == 7 && sm64ds_front_options_begin()) { func_ov007_020c1d8c(0); return; }\n#endif')
    contents[wrench]=text
    pump='src/func_ov007_020b7090.c'
    text=contents[pump]
    text='#ifdef __cplusplus\nextern "C" int sm64ds_front_options_blocked(void);\n#else\nextern int sm64ds_front_options_blocked(void);\n#endif\n'+text
    signature='int func_ov007_020b7090(u16 a0, u16 a1, u16 a2, u16 a3, int arg4)\n{'
    # The intercepted wrench consumes the retail input latch without entering
    # the retail options page whose entry callback normally clears it. Release
    # it only after the modal AND its closing touch have ended; drain history
    # before accepting another title-screen gesture.
    resume='''
    static int front_was_blocked = 0;
    if (sm64ds_front_options_blocked()) { front_was_blocked = 1; return 0; }
    if (front_was_blocked) {
        extern void func_ov007_020c1d78(int);
        u16 released[4] = {255, 255, 0, 0};
        int keys = 0;
        front_was_blocked = 0;
        func_ov007_020c1d78(0);
        func_ov007_020c20b8(data_ov007_0210342c->f54, &keys);
        func_ov007_020c1db0((void *)data_ov007_0210342c->f50, released);
        func_ov007_020c1db0((void *)data_ov007_0210342c->f50, released);
        return 0;
    }
'''
    text=once(text,signature,signature+'\n#if '+GUARD+'\n'+resume+'#endif')
    # Opening happens inside this dispatch. Remember ownership immediately:
    # the modal can close before this pump is called again.
    text=once(text,'    func_ov007_020b2e64(buf);',
        '    func_ov007_020b2e64(buf);\n#if '+GUARD+'\n'
        '    if (sm64ds_front_options_blocked()) { front_was_blocked = 1; return 0; }\n#endif')
    contents[pump]=text
    # Validate every unit before mutating the generated tree.
    for name,text in contents.items():tree.write_text(name,text)
    tree.commit()
    return list(contents)
