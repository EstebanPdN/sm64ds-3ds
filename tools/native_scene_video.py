#!/usr/bin/env python3
"""Compile reviewed original scene bodies with native video and ABI repairs.

Sources are generated copies. Pin, exact source hashes, replacement counts,
round-trip reconstruction and a remaining-address check prevent silently
rewriting scene logic or leaving an unadapted DS register access behind.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

import native_operators
import native_vtable

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream'
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
SOURCES = {
    'src/func_0205f650.c':
        ('b9b734d65471134f5450ca78c8614f7229af2b9a4de61abb40fdd3d15b331181', 0),
    'src/func_0205f68c.c':
        ('ac94f27ff1e35925181ece4ca65de2a4b7ac87b0c9095a66542a33baff33bbd8', 2),
    'src/FUN_02029a68.c':
        ('ccf4a5dcc47270dcb244fc4d09f276325b2c5201d8495207a14e641fc7d730b1', 1),
    'src/_ZN7Message12UpdateWindowEv.cpp':
        ('186ae7fbd085731681844de6adccd3b573cbaba87d5e73880a6310e8dde5bfdd', 20),
    'src/_ZN7Message13DisplaySavingEt.cpp':
        ('ffd178246f23ab2289a3be7ace3cf7df623c545990ce5b3d7b9d7b6b1397a472', 5),
    'src/func_0201f32c.c':
        ('d1853871d3699e706f583992b9c6dc6393b0285bf633c6bd8cb8c83110446234', 25),
    'src/func_020321fc.c':
        ('b8fc4770f72b1302966786f0b6000ef5210018d9e218dedf1aed69fefb80fc05', 20),
    'src/func_020345b0.c':
        ('5413c1f7611beb166521ac6ae5fcdc5f5516ba8fba2c8221b7a9268eaea33de5', 11),
    # RenderFog uploads this state even when fog is disabled.
    'src/_ZN3G3X6SetFogEbiii.cpp':
        ('b18a9a5d056566a901afad6e56ef53ca555a273c04a31359e6b4be8c102d1ef6', 5),
    'src/_ZN3G3X11SetFogTableEPv.cpp':
        ('b7dc4cb74c4e69e7549034b790d55ea44433aeb2805cf0c4748da82d9effd442', 1),
    'src/func_02029408.c':
        ('ab71f8d9f5312edc6ae7ff64e5579832f1e7298550154335ae5e80d5a8cba51a', 0),
    # InitResources dispatches the original slot 9 (Render) before returning.
    # Preserve that complete body and translate its viewport/blend accesses.
    'src/_ZN6Camera6RenderEv.cpp':
        ('2dc600690969533b7f900e39fd0d6a81b1ba65bc7165e8149f1cc435d39742a0', 2),
    'src/_ZN9BootScene13InitResourcesEv.cpp':
        ('b017e465593f927810fbda3595b9c5b4e3cd11174b88b24ded14d6ddd17f43e2', 41),
    'src/_ZN9BootScene8BehaviorEv.cpp':
        ('a54b5dad29c303765f6580818078c703a0f3e0e63ce28017440a82937142bbc0', 2),
    'src/_ZN10dScTitle_c13InitResourcesEv.cpp':
        ('8435dcaca9480391b5971ed2a807059f1f19e07b50bb1637b74070a5d5bab0fc', 9),
    'src/_ZN8dScene_c22ResetHardwareRegistersEv.cpp':
        ('1e2379bdbb35bb74e9645c447dae95472d65f94164d800d156d3ff0bba0b5b55', 75),
    'src/_ZN8dScene_c20Initialise3dGraphicsEv.cpp':
        ('f73f6515d9e409765b380c6fa3ad3b5c98f348ab99dc42111fc43169e583d03b', 4),
    'src/func_0205583c.c':
        ('e32e8bb257b4d468c9013392a1d78881d9415c08744691e764366293f62b0b89', 23),
    'src/func_020556d0.c':
        ('a52c45acb29ac20fc01db724039ff44c2d9083e33a7f4a9e6ec1ba0b05a0792d', 9),
    'src/func_02049ee8.c':
        ('7dffc32d8f118716205163fae2466611353f5ab6a830b9456f27a15cb18b33d6', 2),
    'src/func_0204af3c.c':
        ('2a1dfd1268abd224b77fd2997ed1e8e3dbe41b3f1679c9a6029e5fc5b9186514', 5),
    'src/func_0204b244.c':
        ('078cc3e9813b99e359791b08e76f63852556b140a44ab68d816a134b1cff8a21', 3),
    'src/func_0204b028.c':
        ('51f5113d422e626af2e190e5b11856a35262f1fa452ba77503c64da2030a16a1', 3),
    'src/func_0204b460.c':
        ('122c3933be3360369a3afc5b0da8182ba0a21bc378887f2fcd2cd9a9af844169', 4),
    'src/func_0204b81c.c':
        ('c86dd217fc965bac87d2b61aa27450ed8f520a4f31c0843f2efbec54d9b81998', 4),
    'src/func_0204bbd8.c':
        ('00ef144812504312c4b09b5d855cadb066b5b220ce99a62f3d61dc85bad14ea4', 4),
    'src/func_0204be40.c':
        ('ad534f13c26bfac940571a821753223e96e9dae4197407bc46de805305f0f4de', 4),
    'src/func_0204c194.c':
        ('92ed82832535d94ebf2a21beadbc18e4f218ecd176793b3b1e9b9d7ac8d0a6d5', 10),
    'src/func_0204c24c.c':
        ('8a83074a35e602aed6296ba788e1faf00b6a0b318ad1a46fffc14c2b2357344d', 10),
    'src/engine/fader/_ZN10FaderColor11AdvanceFadeEv.cpp':
        ('e6be613d48c83dd5948e70976e14b3aed85a802ce3e0a6b3f336dd93806f896b', 4),
    'src/engine/fader/_ZN9FaderWipe11AdvanceFadeEv.cpp':
        ('cccf155a47265b0da960af09e991f1592f7c01191cbaed3d9942f50024bc944c', 5),
    'src/_ZN7dWipe_c14SetForwardTimeEj.cpp':
        ('3cabb88ff54f817c67e04567f8256aced4a443a2533d89c280c6463bb8efd5d0', 12),
    'src/_ZN7dWipe_c15SetBackwardTimeEj.c':
        ('2296631715f750c51f92b3d0f998b74ec08f4baf2f0d9940c425600f83ae6f3f', 12),
    'src/func_0202f58c.c':
        ('0f1ec07b978aaae50196d7038dd472ace05c8cb3880984e11a459b9d005b3898', 19),
    'src/func_0202fb30.c':
        ('68dab32717c2484613602a6df0124a53afdb55fd9619da92191e68ed9d6c3c65', 6),
    'src/_ZN10dScEntry_c13InitResourcesEv.cpp':
        ('7e2f9f13e1abb2008b1b27b01c04c43d5f72f4a317394ef0113ae3fdc6475bfa', 12),
    'src/_ZN10dScEntry_c8BehaviorEv.cpp':
        ('9cd6be860d23f2681d3d1cb197cefb9b4cd9d75b6dc492dedd64d446892cc1ea', 2),
    'src/_ZN10dScEntry_c15graphCallback_c14GraphCallback2Ev.cpp':
        ('bf6f81ebdf3637fcf24e2e41690df0f5ed29efc6db8ad17276f108f6a9aa8b71', 1),
    'src/_ZN9dScDSMT_c13InitResourcesEv.cpp':
        ('01a03c4ac0ad454e8f1af2a209a886af873c69e3d3ed6fe5f53282f503f9ed22', 3),
    'src/func_ov007_020b413c.c':
        ('111168efaae4285cf3fbb7eefcbc862f7c3da20fd160f5d814ba97099a67d6e2', 2),
    'src/func_ov007_020b6c54.c':
        ('4df21718df8c8bd23f8fecca9be8a5775bf2f045b2f1be5f8c769abf5f96388e', 1),
    'src/func_ov007_020b6f4c.c':
        ('b8df8af97751397c2847bd104359c144414b18b1462172c9fc9cde0e86ab0c10', 6),
    'src/func_ov007_020b7138.c':
        ('31b2e429c393f82734a3f721523aea3e475f97fa9e9209f5dd35f471f754bfa5', 5),
    'src/func_ov007_020bd1c0.c':
        ('f3e26be1f6af464f9e9d421a5e035dc2f0b3da4b163344d97d4e2647652db11d', 0),
    'src/func_ov007_020bd500.cpp':
        ('2752bdd547d4730305cb03ea553957c71288a578e995b61f216f1c05f8d39073', 10),
    'src/func_ov007_020bd5a8.cpp':
        ('02f5df0594d69826c6374c9bcc1f748e4015d5f005317414003905ab87fd7876', 6),
    'src/func_ov007_020bd648.c':
        ('c1b4898385a34834eb442a0710d1530cbe508d223ccba1f8978404ec490ebbc1', 7),
    'src/func_ov007_020bd960.c':
        ('7d5ebb7fb6f4b70beaf9f56ed2f61652081aeffc332d88e52a98012205149179', 0),
    'src/func_ov007_020bf690.c':
        ('cd975c3eae303dc57d2366eb1330ada71ef063937762d2159d290ba622fa5551', 2),
    'src/func_ov007_020bf790.c':
        ('d4d5e0f2af6fdbc08ff0782bf5b82c2c823eb9b6961661ec45ee7cbce38b9177', 1),
    'src/func_ov007_020c0078.c':
        ('c5bcdd5dcd32e2e414181600ee5e1e51be88e0d2b34eccc60b93cf9830ddacf6', 2),
    'src/func_ov007_020c03b0.c':
        ('85742e0a140af239f467299e3d0ed226b59c75d07daa1aac3c5fcf42c4810726', 16),
    'src/func_ov007_020c076c.c':
        ('99fdbae9db06610253e156170f0f89f757db78c660231a63049ba60c6012b735', 8),
    'src/func_ov007_020c1404.c':
        ('01e0d1b4597ce5aca5582d6b82c0617aca2d17b28bcd64ee6f1e293f8c86afd8', 0),
    'src/func_ov007_020c1448.c':
        ('ba0dff0a318212f6fdfe39d02279c74d417ed6eaa61ae4d4d7e25e7800f0a839', 1),
    'src/func_ov007_020c19cc.c':
        ('01583152e5cd37c54920f18c5d66a11fad6b5262e39f72d8be5b5475acafb011', 20),
    'src/func_ov007_020c232c.c':
        ('6bc0b32804432496c3d409cbf73ee41c5ec646177d02003a89a555db72de4d28', 2),
    'src/func_ov007_020c49bc.c':
        ('c4682ae4bc318af7ca04119a2fd56ff9255e79843975c6629b7f5f85ec147983', 22),
    'src/func_ov007_020c4fcc.cpp':
        ('4d1fae4edee1425d8fbbcaeaf8516d6050c259eb54fbe523692908700a8e5ee5', 3),
    'src/func_ov007_020c86c4.c':
        ('11468fcb03c477e57822659b0fe743400963bd5c0af3aac1542d0e7314f122e8', 20),
    'src/func_ov007_020c897c.c':
        ('efa21dedbfa5d9f62793af8b47a57f8ce201a12cd7bffc3c3adf49aad57837e6', 10),
    'src/func_ov007_020c8bc8.cpp':
        ('af6de68b5f62dbd8d55270dd28d9c006bb73294cd29a133ffc091e88a7527fa7', 2),
    'src/func_ov007_020c8c88.c':
        ('78f512a26a9360ceb001189c4d8d6ebd94efdd86bda8df8176661513b8922cd0', 5),
    'src/func_ov007_020ca5f0.c':
        ('4af2c6b2b9449ac7d0428ef2e404fbfb60b230845c43f3f1e936c675bc6da818', 15),
    'src/func_ov007_020ca86c.c':
        ('56d94af9165762dc12894a18af25337b4326d6aa5273c7e07e6943f45c489bb4', 4),
    'src/func_ov007_020caeac.c':
        ('d9c4c997ffdb9c8d55030cc04aa8afff6eff996c8b1e5b8a4b6b824bddbf5cae', 5),
    'src/func_ov007_020c421c.c':
        ('b539aee80277142657eab2f99bd2264ea8ef9aef1e5e9de37607893efaff2f3d', 5),
    'src/func_ov007_020c42f8.c':
        ('425a72ae1854371fb0bad345acef6b26f5a7200ea6aa3f8ac574cd3558414ed9', 3),
    'src/func_ov007_020c4684.c':
        ('c47b285a83108116a1a3ba128866055f60ed797238a662cb56c6c6c6ab9dc308', 24),
    'src/func_ov007_020c8b34.c':
        ('943e8c43a931bf112fc4c4304d0713675c26203aaa086fe9e9d1767bc92f3ce7', 0),
    'src/func_ov007_020b6eb4.c':
        ('c2f748ddee430ebdc97e13fed69d0cd71dbeb07fe50ddd800937acf9b6e77a4d', 0),
    'src/func_ov007_020bd4e8.c':
        ('e81eb2749b872c7127271006c377be344b5e3204bf3ded93565773b02571200c', 1),
    'src/d_s_stage.cpp':
        ('ece43c4fad6b5f4b3d7a1f43284bc2d6b9a0f8fa4e7e7d6123fa5471021bfa7c', 0),
    'src/_ZN5Stage13InitResourcesEv.cpp':
        ('304c619dad003bfe9007c21c9229fc8bc69d786dbbad446089bb5d26e6aadf05', 4),
    'src/_ZN5Stage14LoadGraphics2DEbi.cpp':
        ('e1eec39da4f8164d6596dfaa80ba88afedecdbd8840e2823975c4d9557f08174', 5),
    'src/_ZN5Stage10PS_CleanupEv.cpp':
        ('9c28f23cfea92d6aede4ced1f955b905276f8c971b323f52cce92dead4c6aa0b', 2),
    'src/_ZN5Stage7PS_InitEv.cpp':
        ('acce9da351e1e32ca7db0b9dcb684ff6644b274c494d5426b666ef5e32b14ecf', 4),
    'src/_ZN5Stage7VE_InitEv.cpp':
        ('6a12d9256ae5315704f4f8a9549c7210a054bfdf6c2feaf31511760689d699f2', 6),
    'src/_ZN5Stage9LC_UpdateEv.cpp':
        ('4d3af323b98ddf0c9a5588d4cf56ab3f6606eb47677cb03755d2a4f8fc3deacc', 11),
    'src/_ZN5Stage9PS_UpdateEv.cpp':
        ('0e8d5eb34333841a587a2876fc2ae8175261d4b3303f9ea07a9b490681eb498e', 34),
    'src/_ZN5Stage9RenderFogEv.cpp':
        ('05f23eea21f47893d5ee2eb1b0ba414417dfce655cc83b6f3f411a64e1d4e616', 1),
    'src/_ZN5Stage9VE_UpdateEv.cpp':
        ('9d9887a3867ea62770d7fcebbf34730b8cb7c3bf1af0f9c2205eef0d546b01e9', 2),
    'src/_ZN3HUD13InitResourcesEv.cpp':
        ('c4b5afae25b32be4bb1c871369cfe292c9c7207388e3601e06565c00a3040b59', 0),
    'src/_ZN7Minimap13InitResourcesEv.cpp':
        ('cfc917bffc6a4a76d1968c77d92b07e5e2c24414284c3f74c0f45871c101e294', 9),
    'src/_ZN7Minimap8BehaviorEv.cpp':
        ('ec019e2e0aea05e88195f1c850794c57b8745087fd887104c026e1a8f735a95e', 14),
    # The intro CutsceneObject controllers own the sub-screen cloud fades,
    # scrolling windows and their setup/cleanup. C13 reached the raw DISPCNT
    # read in 020f1fcc; include the later state and paired controller sites
    # together, preserving the original counters and scene decisions.
    'src/func_ov002_020f1fcc.c':
        ('03aeca1de58e490a2319b7ff18d4ee8771d6ba3c9085a79ef189ada0fe1de361', 4),
    'src/func_ov002_020f23f0.c':
        ('7798d51c571958f24b66aa444999512ed2f159f4fa606b964333a9707e485dd2', 2),
    'src/func_ov002_020f2aec.c':
        ('b6a9fe847a39e5e7ab74f186165cb13ae97a7190230ba61f084c51ad976e2b55', 2),
    'src/func_ov002_020f2bf4.c':
        ('5289ccfda54ad2b54715d98b9213e86b469616d4079ab6e194d8a68a1054365d', 1),
    'src/func_ov002_020f30f8.c':
        ('828103a5811572725fe5a3be63cceedc6df1c99f66a24480bb8c4415dd06fcd1', 2),
    'src/func_ov002_020f335c.c':
        ('d3be63dad92e5472ac4b53238d9ce6fdce2e56b4ce158d895fd5f13115b0c32b', 2),
    'src/func_ov002_020f340c.c':
        ('937fcb061a8d14d5421e38eac70bb81078dcae855a77beccee4569b29ed42733', 2),
    'src/func_ov002_020f34bc.c':
        ('0654e95c2a12c504c9d2f26d1acaca8cc5ac09411ace7452c6474686dbe6a20d', 2),
    'src/func_ov002_020f35e4.c':
        ('b9b055c0b5827c95140df1b479e0b4e53091651b29199563563633a6856a7735', 8),
    'src/func_ov002_020f3828.c':
        ('e4e2d97a690e9571d40133d379d4aacc53ccbb366faa5d9f52a310a385308b18', 8),
    'src/func_ov002_020f5b24.c':
        ('42ec6904e22e4c4a1bb4f2e98f4af54f6a1d0a2def62ec74b4840e4f38bd0d5c', 1),
    'src/func_ov002_020f5fe4.c':
        ('7a4610c4d785d0171dbe7f475239c8f1941e1572eff6682f66936aefc467190a', 20),
}

# Exact original callers which pass DS display destinations to LZ16. Rename
# declarations and calls together, preserving their original GP-register ABI.
# The original decompressor and every other game caller remain unchanged.
VIDEO_LZ_CALLS = {
    'src/_ZN3HUD13InitResourcesEv.cpp': 21,
    'src/_ZN7Minimap13InitResourcesEv.cpp': 3,
    'src/func_ov002_020f5fe4.c': 6,
}
NONCODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
REGISTER_TYPES = {'u8': 1, 'u16': 2, 'u32': 4, 'unsigned int': 4, 'unsigned short': 2, 'unsigned': 4,
                  'int': 4, 'vu16': 2, 'vu32': 4, 'unsigned char': 1,
                  'short': 2, 's16': 2, 's32': 4}
REGISTER_KINDS = '|'.join(map(re.escape, sorted(REGISTER_TYPES, key=len, reverse=True)))
REGISTER = re.compile(r'\*\s*(?P<outer>\()?\s*\(\s*(?:volatile\s+)?(?P<kind>' + REGISTER_KINDS + r')\s*\*\s*\)\s*(?P<address>0[xX][0-9a-fA-F]+)\b(?(outer)\s*\))')
# Only literal invocations of the two reviewed register macros are adapted.
# Their variable definitions and all other expressions remain verbatim.
REGISTER_MACRO = re.compile(r'\bREG(?P<bits>16|32)\(\s*(?P<address>0[xX][0-9a-fA-F]+)\s*\)')
LITERAL = re.compile(r'\b0[xX][0-9a-fA-F]+\b')
WRAPPER = re.compile(r'sm64ds::native_register<(?:' + REGISTER_KINDS + r')>\(0x[0-9a-f]{8}\)')
SDK_POINTER = re.compile(r'\(\s*(volatile unsigned short|volatile u16|unsigned short|volatile void|void|u16)\s*\*\s*\)\s*(0[xX][0-9a-fA-F]+)\b')
POINTER_WRAPPER = re.compile(r'sm64ds::native_register_pointer<(?:volatile unsigned short|volatile u16|unsigned short|volatile void|void|u16)>\(0x[0-9a-f]{8}, (?:2|6|16|32)\)')
# Reviewed SDK arguments: four affine blocks and two master-brightness blocks.
SDK_BLOCKS = {0x04000020: ('volatile void', 16), 0x04000030: ('volatile void', 16),
              0x04001020: ('volatile void', 16), 0x04001030: ('volatile void', 16),
              0x0400006c: ('u16', 2), 0x0400106c: ('u16', 2),
              0x04000050: ('blend', 6), 0x04001050: ('blend', 6),
              0x0400100c: ('volatile unsigned short', 2),
              0x04000048: ('volatile u16', 2), 0x0400004a: ('volatile u16', 2),
              0x04000360: ('void', 32)}

# These two RHS values encode VTX_10 vertices, not bus addresses. Scope each
# exact assignment to its pinned source; every LHS must still be adapted.
DATA_LITERAL_SITES = {
    'src/func_0204c194.c': (
        'sm64ds::native_register<int>(0x04000490) = 0x40003c0;',
        'sm64ds::native_register<int>(0x04000490) = 0x4000040;'),
}

# Exact local-pointer and bulk-memory sites in the original menu. A geometry
# port cannot be replaced with a RAM pointer: dereferencing its proxy must
# still submit the command. Bulk fills, in contrast, get a bounded owned span.
POINTER_EDITS = {
    # The original PM interval uses the ARM7-maintained VBlank word, not the
    # monotonic timer. Use the actual split-loop count, preserving u32 deltas.
    'src/func_0205f68c.c': [
        ('*(int*)0x27ffc3c', 'sm64ds_native_vblank_count()')],
    'src/func_0205f650.c': [
        ('REG_POWCNT1', 'sm64ds::native_register<unsigned short>(0x04000304)')],
    'src/_ZN7Message13DisplaySavingEt.cpp': [
        ('volatile unsigned int *disp = (volatile unsigned int *)0x4000000;',
         'auto disp = sm64ds::native_register_address<unsigned int>(0x04000000);')],
    'src/func_ov007_020b6c54.c': [
        ('volatile u16 *reg = (volatile u16*)0x4000060',
         'auto reg = sm64ds::native_register_address<u16>(0x04000060)')],
    'src/func_ov007_020bd1c0.c': [
        ('volatile u16* reg = (volatile u16*)0x4000060',
         'auto reg = sm64ds::native_register_address<u16>(0x04000060)')],
    'src/func_ov007_020c1404.c': [
        ('unsigned int *reg = (unsigned int*)0x40004ac',
         'auto reg = sm64ds::native_register_address<unsigned int>(0x040004ac)')],
    'src/func_ov007_020bd960.c': [
        ('(int *)0x6800000', 'sm64ds::native_register_pointer<int>(0x06800000, 0xa4000)'),
        ('(int *)0x7000000', 'sm64ds::native_register_pointer<int>(0x07000000, 0x400)'),
        ('(int *)0x5000000', 'sm64ds::native_register_pointer<int>(0x05000000, 0x400)')],
    'src/func_ov007_020c4684.c': [
        ('volatile int *new_var;', 'sm64ds::NativeRegisterAddress<int> new_var{0};'),
        ('new_var = (volatile int *) 0x04000488;',
         'new_var = sm64ds::native_register_address<int>(0x04000488);')],
    'src/func_ov007_020c8b34.c': [
        ('new_var2 = 0x4000050;',
         'new_var2 = (int)sm64ds::native_register_pointer<S>(0x04000050, 6);'),
        ('(struct S *) 0x4001050',
         'sm64ds::native_register_pointer<S>(0x04001050, 6)')],
    'src/_ZN5Stage13InitResourcesEv.cpp': [
        ('volatile u32 *p0 = (volatile u32*)0x04000000',
         'auto p0 = sm64ds::native_register_address<u32>(0x04000000)'),
        ('volatile u32 *p1 = (volatile u32*)0x04001000',
         'auto p1 = sm64ds::native_register_address<u32>(0x04001000)'),
        ('volatile u16 *pVram = (volatile u16*)0x0400000A',
         'auto pVram = sm64ds::native_register_address<u16>(0x0400000a)')],
}
REVIEWED_POINTER_WRAPPER = re.compile(
    r'sm64ds::native_register_address<(?:u16|u32|unsigned int|int)>\(0x[0-9a-f]{8}\)|'
    r'sm64ds::native_register_pointer<(?:int|S)>\(0x[0-9a-f]{8}, (?:0x[0-9a-f]+|6)\)')

# ROM OV007 0x020b6f08 loads the pending mode from +0x1c into r0;
# 0x020b6f14 calls bd648(int). The recovered C lost that argument and the
# compiled caller consequently passes a stale r0. Keep this separate from
# MMIO replacement: it is a reviewed repair of the recovered call ABI.
ABI_EDITS = {
    # OV002 0x020bd828 is the compiled Player::CanPause body. The legacy
    # caller retained its address-based spelling; preserve its real argument
    # and return value while selecting that same original body.
    'src/func_02029408.c': [
        ('extern int func_020bd828(void *p);',
         'extern int _ZN6Player8CanPauseEv(void *p);'),
        ('return func_020bd828(data_0209f394[data_0209f250]);',
         'return _ZN6Player8CanPauseEv(data_0209f394[data_0209f250]);')],
    'src/func_ov007_020b6eb4.c': [
        ('extern void func_ov007_020bd648(void);',
         'extern void func_ov007_020bd648(int mode);'),
        ('func_ov007_020bd648();',
         'func_ov007_020bd648(*(int*)(data_ov007_0210342c + 0x1c));')],
    # The manual factory stores a vptr rather than invoking a GCC constructor.
    # GCC _ZTV symbols begin with offset-to-top and RTTI; PMFs expect the first
    # callable slot. Genuine compiler-emitted constructors already use +2.
    # Keep this local: original ROM data_* vtables have no such header.
}


def adapt_abi(name, source):
    result, edits = source, []
    for old, new in ABI_EDITS.get(name, []):
        if result.count(old) != 1:
            raise ValueError(f'Reviewed menu ABI site changed: {name}: {old}')
        result = result.replace(old, new)
        edits.append({'original': old, 'replacement': new})
    inverse = result
    for edit in reversed(edits):
        inverse = inverse.replace(edit['replacement'], edit['original'])
    if inverse != source:
        raise ValueError('Menu ABI transformation changed unrelated source')
    return result, edits


def adapt_pointers(name, source):
    result, edits = source, []
    for old, new in POINTER_EDITS.get(name, []):
        count = result.count(old)
        if count != (2 if name in ('src/func_ov007_020bd1c0.c', 'src/func_0205f68c.c') else 1):
            raise ValueError(f'Reviewed menu pointer site changed: {name}: {old}')
        result = result.replace(old, new)
        edits.append({'original': old, 'replacement': new, 'count': count})
    inverse = result
    for edit in reversed(edits):
        inverse = inverse.replace(edit['replacement'], edit['original'])
    if inverse != source:
        raise ValueError('Menu pointer transformation changed unrelated source')
    return result, edits


def digest(data):
    return hashlib.sha256(data).hexdigest()


def code_only(text):
    return NONCODE.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), text)


def adapt_video_lz(name, source):
    expected = VIDEO_LZ_CALLS.get(name, 0)
    if not expected:
        return source, []
    matches = list(re.finditer(r'\bDecompressLZ16\b', code_only(source)))
    if len(matches) != expected:
        raise ValueError('Reviewed display LZ16 caller count changed: ' + name)
    result = source
    edits = []
    replacement = 'sm64ds_decompress_lz16_video'
    for match in reversed(matches):
        result = result[:match.start()] + replacement + result[match.end():]
        edits.append(dict(original='DecompressLZ16', replacement=replacement,
                          original_offset=match.start(),
                          original_line=source.count('\n', 0, match.start()) + 1))
    inverse = result
    for edit in reversed(edits):
        start = edit['original_offset']
        if inverse[start:start + len(replacement)] != replacement:
            raise ValueError('Display LZ16 adaptation cannot be inverted')
        inverse = inverse[:start] + edit['original'] + inverse[start + len(replacement):]
    if inverse != source:
        raise ValueError('Display LZ16 adaptation changed unrelated source')
    return result, list(reversed(edits))


def adapt(source, expected_count, data_literal_sites=()):
    pieces, replacements, old_end, new_size = [], [], 0, 0
    code = code_only(source)
    matches = [(match, 'register') for match in REGISTER.finditer(code)]
    matches.extend((match, 'sdk_pointer') for match in SDK_POINTER.finditer(code)
                   if not any(start.start() <= match.start() < start.end() for start in REGISTER.finditer(code)))
    matches.extend((match, 'macro') for match in REGISTER_MACRO.finditer(code))
    for match, mode in sorted(matches, key=lambda item: item[0].start()):
        pointer = mode == 'sdk_pointer'
        kind = match[1] if pointer else ('u' + match['bits'] if mode == 'macro' else match['kind'])
        address = int(match[2] if pointer else match['address'], 16)
        if not 0x04000000 <= address < 0x05000000:
            continue
        if pointer:
            kind, width = SDK_BLOCKS.get(address, ('', 0))
            if kind == 'blend' and match[1] in ('volatile unsigned short','volatile u16','volatile void','void'):
                kind=match[1]
            if kind != match[1]:
                raise ValueError(f'Unreviewed SDK register pointer: {match[0]}')
            replacement = f'sm64ds::native_register_pointer<{kind}>({address:#010x}, {width})'
        else:
            width = REGISTER_TYPES[kind]
            replacement = f'sm64ds::native_register<{kind}>({address:#010x})'
        prefix = source[old_end:match.start()]
        pieces.extend([prefix, replacement])
        new_size += len(prefix)
        replacements.append({
            'line': source.count('\n', 0, match.start()) + 1,
            'address': f'{address:#010x}', 'bytes': width, 'sdk_pointer': pointer,
            'register_macro': mode == 'macro',
            'original': source[match.start():match.end()],
            'replacement': replacement, 'generated_offset': new_size,
        })
        new_size += len(replacement)
        old_end = match.end()
    pieces.append(source[old_end:])
    result = ''.join(pieces)
    if len(replacements) != expected_count:
        raise ValueError(f'Expected {expected_count} explicit register sites, found {len(replacements)}')
    reconstructed = result
    for row in reversed(replacements):
        at = row['generated_offset']
        if reconstructed[at:at + len(row['replacement'])] != row['replacement']:
            raise ValueError('Register transformation cannot be inverted')
        reconstructed = reconstructed[:at] + row['original'] + reconstructed[at + len(row['replacement']):]
    if reconstructed != source:
        raise ValueError('A source change outside the listed MMIO accesses was introduced')
    remaining = code_only(result)
    for site in data_literal_sites:
        if remaining.count(site) != 1:
            raise ValueError(f'Reviewed GX vertex data site changed: {site}')
        # Only exclude the RHS data, preserving the wrapped LHS in the check.
        left, _ = site.split('=', 1)
        remaining = remaining.replace(site, left + '= 0;')
    remaining = REVIEWED_POINTER_WRAPPER.sub('', POINTER_WRAPPER.sub('', WRAPPER.sub('', remaining)))
    for literal in LITERAL.finditer(remaining):
        if 0x04000000 <= int(literal[0], 16) < 0x05000000:
            raise ValueError(f'Unadapted DS register literal remains: {literal[0]}')
    return result, replacements


def verify_headers():
    generated = ROOT / 'build/native-core/generated/include'
    hashes = []
    for path in sorted((UPSTREAM / 'include').rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(UPSTREAM / 'include')
        expected = path.read_bytes()
        if relative.as_posix() == 'types.h':
            expected = expected.replace(b'#ifdef _MSC_VER\n#include <stddef.h>',
                                        b'#if defined(_MSC_VER) || defined(__3DS__)\n#include <stddef.h>')
            expected = expected.replace(b'#if defined(__cplusplus) && defined(_MSC_VER)',
                                        b'#if defined(__cplusplus) && (defined(_MSC_VER) || defined(__3DS__))')
        actual = (generated / relative).read_bytes()
        if actual != expected:
            raise ValueError(f'Generated native ABI header differs: {relative}')
        hashes.append(relative.as_posix() + ':' + digest(actual))
    return generated, digest('\n'.join(hashes).encode())


def run(command):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout or f'Command failed: {command[0]}')
    return result.stdout, result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-directory', type=Path, default=ROOT / 'build/native-runtime/scene-video')
    args = parser.parse_args()
    out = args.output_directory.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'status': 'failed', 'scope': 'original scene ARM11 compilation; no game execution',
              'upstream_commit': PIN, 'entries': []}
    try:
        commit, _ = run(['git', '-C', UPSTREAM, 'rev-parse', 'HEAD'])
        if commit.strip() != PIN:
            raise ValueError('Upstream revision differs from the reviewed scene sources')
        includes, headers_hash = verify_headers()
        report['generated_headers_sha256'] = headers_hash
        report['register_header_sha256'] = digest((ROOT / 'include/sm64ds/native_register.hpp').read_bytes())
        core = json.loads((ROOT / 'build/native-core/report.json').read_text())
        original_objects = {entry['source']: entry['object'] for entry in core['entries'] if entry['compiled']}
        known_symbols = set()
        for path in (UPSTREAM / 'config/arm9').rglob('symbols.txt'):
            known_symbols.update(re.findall(r'^(\S+) kind:', path.read_text(), re.M))
        flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
                 '-D__3DS__', '-Os', '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
                 '-Wno-builtin-declaration-mismatch', '-Wno-invalid-offsetof', '-Wa,--noexecstack',
                 '-Werror=trampolines', '-std=gnu++17', '-I', str(includes), '-I', str(ROOT / 'include')]
        report['compiler_flags'] = flags
        for name, (expected_hash, count) in SOURCES.items():
            raw = (UPSTREAM / name).read_bytes()
            if digest(raw) != expected_hash:
                raise ValueError(f'Reviewed original source changed: {name}')
            body, vptr_edits = native_vtable.adapt(name, raw.decode())
            body, abi_edits = adapt_abi(name, body)
            body, lz_edits = adapt_video_lz(name, body)
            body, pointer_edits = adapt_pointers(name, body)
            body, replacements = adapt(body, count, DATA_LITERAL_SITES.get(name, ()))
            generated = out / Path(name).name
            preamble = ('// Generated by tools/native_scene_video.py; original logic retained.\n'
                        '#include "sm64ds/native_register.hpp"\n')
            # C SDK originals retain their ABI when compiled with the C++
            # register proxy. Preinclude guarded types outside C linkage.
            if Path(name).suffix == '.c':
                preamble += '#include "types.h"\nextern "C" {\n'
                if name == 'src/func_0205f68c.c':
                    preamble += 'unsigned int sm64ds_native_vblank_count(void);\n'
                body += '\n}\n'
            generated.write_text(preamble + body)
            unisolated = generated.with_suffix('.unisolated.o')
            target = generated.with_suffix('.o')
            _, diagnostics = run([TOOL + 'g++', *flags, '-c', generated, '-o', unisolated])
            native_operators.copy_isolated(TOOL, unisolated, target, ROOT)
            # Match the main native bundle's audited repair of declarations
            # which wrap an already-mangled original function name a second time.
            undefined, _ = run([TOOL + 'nm', '-u', target])
            aliases = []
            for line in undefined.splitlines():
                symbol = line.split()[-1]
                match = re.match(r'_Z(\d+)', symbol)
                if match:
                    original = symbol[match.end():match.end() + int(match[1])]
                    if original in known_symbols:
                        aliases.extend(['--redefine-sym', symbol + '=' + original])
            if aliases:
                run([TOOL + 'objcopy', *aliases, target])
            native_operators.verify_isolated(TOOL, [target], ROOT)
            unisolated.unlink()
            definitions, _ = run([TOOL + 'nm', '-g', '--defined-only', target])
            report['entries'].append({
                'source': name, 'sha256_original': expected_hash,
                'generated_source': str(generated.relative_to(ROOT)),
                'sha256_generated': digest(generated.read_bytes()),
                'object': str(target.relative_to(ROOT)), 'sha256_object': digest(target.read_bytes()),
                'original_object': original_objects[name], 'compiled': True,
                'only_reviewed_video_and_abi_edits': True, 'replacement_count': len(replacements),
                'replacements': replacements, 'reviewed_pointer_edits': pointer_edits,
                'reviewed_abi_edits': abi_edits,
                'reviewed_vptr_edits': vptr_edits,
                'reviewed_video_lz_calls': lz_edits,
                'reviewed_vertex_data_sites': DATA_LITERAL_SITES.get(name, ()),
                'abi_symbol_aliases': aliases[1::2],
                'definitions': [line.split()[-1] for line in definitions.splitlines()],
                'diagnostics': diagnostics,
            })
        report['status'] = 'compiled'
        report['replacement_count'] = sum(row['replacement_count'] for row in report['entries'])
        objects = ['"' + str(ROOT / row['object']) + '"' for row in report['entries']]
        (out / 'objects.cmake').write_text('set(SM64DS_SCENE_VIDEO_OBJECTS\n  ' + '\n  '.join(objects) + '\n)\n')
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        report['error'] = str(error)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items()
                      if key in ('status', 'scope', 'replacement_count', 'error')}, indent=2))
    return 0 if report['status'] == 'compiled' else 1


if __name__ == '__main__':
    sys.exit(main())
