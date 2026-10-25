// Adapted from tangosdev/sm64ds-decomp port/ntr/ppu_sub.cpp at
// 6230b2996636e3fd0e03cb178161c94cb4b73cc9. MIT, copyright (c) 2026 Tango;
// full license: source/vendor/ntr/LICENSE. Original layer/sprite/window/blend
// algorithms retained; all memory reads now go through owned native_video.
#include "sm64ds/native_2d.hpp"
#include "sm64ds/native_2d_cache.hpp"
#include "sm64ds/native_video.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace sm64ds {
namespace {

uint32_t kRegBase = 0x04001000u;   // engine B
uint32_t kVramBase = 0x06200000u;  // engine B BG VRAM
uint32_t kPlttBase = 0x05000400u;  // engine B BG palette
uint32_t kObjVram = 0x06600000u;   // engine B OBJ VRAM
uint32_t kObjPltt = 0x05000600u;   // engine B OBJ palette
uint32_t kOamBase = 0x07000400u;   // engine B OAM
/* see the note over oam_b in the sub OBJ raster */
uint32_t g_oam_src_b;

// GXS::LoadBGExtPltt's own destination base; slots are 0x2000 apart (its
// caller passes 0x6000 for BG3).
uint32_t kBgExtPltt = 0x06898000u;
// GXS::LoadOBJExtPltt's own destination base, out of the ROM rather than a
// doc: src/_ZN3GXS14LoadOBJExtPlttEPKvjj.cpp computes its destination as
// `destSlotAddr + 0x068a0000`, and src/_ZN2GX23SetBankForSubOBJExtPlttEt.cpp is
// what puts a bank there (VRAMCNT_I = 0x83) and sets DISPCNT_B bit 31 in the
// same two lines. One OBJ extended palette is sixteen slots of 256 colours,
// so the slot stride is 0x200 and the whole store is 0x2000.
uint32_t kObjExtPltt = 0x068A0000u;

// The game and VBlank handler cannot change bank assignments while this
// synchronous scanout runs. Resolve a VRAM page on its first use, then sample
// its owned bytes directly instead of searching all nine banks for each pixel.
// A full table costs 9 KiB on ARM11 and avoids collisions between BG/OBJ banks.
constexpr uint32_t kVramCacheBase=0x06000000u;
constexpr uint32_t kVramCacheEnd=0x06900000u;
constexpr uint32_t kVramPageSize=0x1000u;
const uint8_t* g_vram_pages[(kVramCacheEnd-kVramCacheBase)/kVramPageSize]{};

template<unsigned Bytes> inline uint32_t scan_read(uint32_t address) {
    static_assert(Bytes==1 || Bytes==2 || Bytes==4);
    const uint32_t offset=address&(kVramPageSize-1);
    // Keep register side effects and ordinary palette/OAM reads on the original
    // path. The same path handles unaligned and cross-page accesses, including
    // its rejection of reads crossing noncontiguous or unmapped bank windows.
    if(address<kVramCacheBase || address>=kVramCacheEnd ||
       (address&(Bytes-1)) || offset>kVramPageSize-Bytes)
        return uint32_t(native_video_read(address,Bytes));
    auto*& page=g_vram_pages[(address-kVramCacheBase)/kVramPageSize];
    if(!page) {
        // Validate the actual requested address first, preserving its failure
        // diagnostic. All current VRAM bank windows have 4-KiB-aligned bounds;
        // still validate the entire cached span instead of assuming that fact.
        if(!native_video_pointer(address,Bytes))return 0;
        page=static_cast<const uint8_t*>(native_video_pointer(address-offset,kVramPageSize));
        if(!page)return 0;
    }
    uint32_t value=0;
    for(unsigned byte=0;byte<Bytes;++byte)value|=uint32_t(page[offset+byte])<<(8*byte);
    return value;
}
inline uint16_t rd16(uint32_t a) { return uint16_t(scan_read<2>(a)); }
inline uint32_t rd32(uint32_t a) { return scan_read<4>(a); }
inline uint8_t rd8(uint32_t a) { return uint8_t(scan_read<1>(a)); }

inline uint32_t bgr555(uint16_t c) {
    const uint32_t r = (c >> 0) & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8)
           | (b << 3 | b >> 2);
}

// Standard BG/OBJ palettes are owned RAM and cannot change during synchronous
// scanout. Convert each used color once per frame. Keep extended palettes on
// the validated VRAM path and leave every register read on native_video_read.
uint32_t g_palette_colors[0x400];
uint8_t g_palette_valid[0x400];
inline uint32_t palette_color(uint32_t address) {
    if(address>=0x05000000 && address<=0x050007fe && !(address&1)) {
        const unsigned slot=(address-0x05000000)>>1;
        if(!g_palette_valid[slot]) {
            g_palette_colors[slot]=bgr555(rd16(address));
            g_palette_valid[slot]=1;
        }
        return g_palette_colors[slot];
    }
    return bgr555(rd16(address));
}

// ---- backgrounds ------------------------------------------------------------

/* BG_BITMAP_* are the two shapes an EXTENDED AFFINE BG takes when BGxCNT
   bit 7 is set. GBATEK: in an extended-affine slot bit 7 chooses between
   16-bit bgmap entries (bit 7 = 0, the minimap's mode) and a BITMAP
   (bit 7 = 1), and inside the bitmap arm bit 2 chooses 256-colour (8 bits
   per pixel through the standard palette) from DIRECT COLOUR (16 bits per
   pixel, BGR555, bit 15 = opaque). Both go through the same affine matrix
   the map-based extended BG does; the only thing that changes is how a
   sampled (px, py) turns into a colour. */
enum BgKind { BG_OFF, BG_TEXT, BG_AFFINE, BG_EXT_AFFINE, BG_BITMAP_256,
              BG_BITMAP_DIRECT };

struct BgLayer {
    BgKind kind;
    int prio;
    uint32_t screen;   // map address
    uint32_t chars;    // tile data address
    bool bpp8;
    int map_w, map_h;  // text: tiles; affine: pixels
    bool wrap;         // affine display-area overflow
    int hofs, vofs;    // text scroll
    int pa, pb, pc, pd;
    int refx, refy;    // 8.8 fixed
    uint32_t ext;      // extended palette slot base, 0 = not in use
};

// The DS BG-mode table, minus the 3D column engine B does not have. Each entry
// is the kind of BG0..BG3 in that mode. Mode 6 (large bitmap) is engine A only
// and is left off.
const unsigned char kModeKinds[6][4] = {
    /* 0 */ {BG_TEXT, BG_TEXT, BG_TEXT, BG_TEXT},
    /* 1 */ {BG_TEXT, BG_TEXT, BG_TEXT, BG_AFFINE},
    /* 2 */ {BG_TEXT, BG_TEXT, BG_AFFINE, BG_AFFINE},
    /* 3 */ {BG_TEXT, BG_TEXT, BG_TEXT, BG_EXT_AFFINE},
    /* 4 */ {BG_TEXT, BG_TEXT, BG_AFFINE, BG_EXT_AFFINE},
    /* 5 */ {BG_TEXT, BG_TEXT, BG_EXT_AFFINE, BG_EXT_AFFINE},
};

void read_bg(BgLayer &c, int bg, uint32_t dispcnt) {
    c.kind = BG_OFF;
    if (!((dispcnt >> (8 + bg)) & 1))
        return;
    const unsigned mode = dispcnt & 7;
    if (mode > 5)
        return;
    if(kRegBase==0x04000000u && bg==0 && (dispcnt&8))return;
    BgKind kind = (BgKind)kModeKinds[mode][bg];

    const uint16_t cnt = rd16(kRegBase + 0x08 + bg * 2);
    c.prio = cnt & 3;
    c.bpp8 = (cnt >> 7) & 1;

    // FOUR bits of character base, 16K units -- G2S::GetBG0CharPtr's own
    // (v & 0x3c) >> 2 << 14. Engine B has no DISPCNT base offset.
    c.chars = kVramBase + (((cnt & 0x3c) >> 2) << 14) + (kRegBase==0x04000000u ? ((dispcnt>>24)&7)*0x10000u : 0);
    c.screen = kVramBase + (((cnt & 0x1f00) >> 8) << 11) + (kRegBase==0x04000000u ? ((dispcnt>>27)&7)*0x10000u : 0);

    const int sz = (cnt >> 14) & 3;
    /* THE BITMAP ARM. `c.bpp8` is BGxCNT bit 7, which in an extended-affine
       slot does not mean "256 colours" at all -- it means BITMAP -- and this
       used to refuse the whole arm ("selects a bitmap, which is not hosted").
       It is hosted now, because it is how the display capture unit's frame
       reaches the screen: the dScMgD3DBase_c family points BG2-B at the block
       it captured into last frame, with BG2CNT_B 0x4284 (bit 7 bitmap, bit 2
       direct colour, screen base 2, size 1).

       THE BASE IS THE SCREEN BASE IN 16K UNITS, not the 2K units a map-based
       BG uses. The arithmetic is what makes the whole reading of this path
       check out: 2 x 0x4000 is 0x8000, and DISPCAPCNT 0x80360010's destination
       is bank C offset 1, which is 0x8000 bytes into bank C -- the same bytes,
       once bank C is mapped to engine B's BG. Two registers written by two
       different functions agreeing to the byte is not a coincidence a reader
       gets to ignore.

       The sizes are the bitmap set, not the map set: 128x128, 256x256, 512x256
       and 512x512 pixels. */
    if (kind == BG_EXT_AFFINE && c.bpp8) {
        static const short kBmpW[4] = {128, 256, 512, 512};
        static const short kBmpH[4] = {128, 256, 256, 512};
        c.kind = (cnt & 0x04) ? BG_BITMAP_DIRECT : BG_BITMAP_256;
        c.screen = kVramBase + (((cnt & 0x1f00) >> 8) << 14);
        c.map_w = kBmpW[sz];
        c.map_h = kBmpH[sz];
        c.wrap = (cnt >> 13) & 1;
        const uint32_t ab = kRegBase + 0x20 + (bg - 2) * 0x10;
        c.pa = (int16_t)rd16(ab + 0);
        c.pb = (int16_t)rd16(ab + 2);
        c.pc = (int16_t)rd16(ab + 4);
        c.pd = (int16_t)rd16(ab + 6);
        c.refx = (int)(rd32(ab + 8) << 4) >> 4;
        c.refy = (int)(rd32(ab + 12) << 4) >> 4;
        /* NO EXTENDED PALETTE. DISPCNT bit 30 arms them for 256-colour
           TILE BGs; a bitmap has no per-tile palette field to select a slot
           with, and the direct-colour arm has no palette at all. */
        c.ext = 0;
        return;
    }
    if (kind == BG_TEXT) {
        c.map_w = (sz & 1) ? 64 : 32;
        c.map_h = (sz & 2) ? 64 : 32;
        c.hofs = rd16(kRegBase + 0x10 + bg * 4) & 0x1FF;
        c.vofs = rd16(kRegBase + 0x12 + bg * 4) & 0x1FF;
    } else {
        c.map_w = c.map_h = 128 << sz;   // 128 / 256 / 512 / 1024 pixels
        c.wrap = (cnt >> 13) & 1;
        // Affine registers: BG2 at 0x20, BG3 at 0x30 (engine-relative).
        const uint32_t ab = kRegBase + 0x20 + (bg - 2) * 0x10;
        c.pa = (int16_t)rd16(ab + 0);
        c.pb = (int16_t)rd16(ab + 2);
        c.pc = (int16_t)rd16(ab + 4);
        c.pd = (int16_t)rd16(ab + 6);
        // BGxX/BGxY are 28-bit signed 20.8 values.
        c.refx = (int)(rd32(ab + 8) << 4) >> 4;
        c.refy = (int)(rd32(ab + 12) << 4) >> 4;
        // Extended affine with 16-bit entries uses 8bpp tiles whatever bit 7
        // said; bit 7 selected bitmap-vs-map, and we rejected bitmap above.
        if (kind == BG_EXT_AFFINE)
            c.bpp8 = true;
    }

    // Extended palettes. DISPCNT bit 30 arms them; they apply to 256-colour
    // BGs only. BG0/BG1 pick slot 0/1 or 2/3 through BGxCNT bit 13; BG2/BG3
    // are hardwired to slots 2/3.
    c.ext = 0;
    if (((dispcnt >> 30) & 1) && c.bpp8) {
        const int slot = bg < 2 ? (bg + (((cnt >> 13) & 1) ? 2 : 0)) : bg;
        c.ext = kBgExtPltt + (uint32_t)slot * 0x2000u;
    }
    c.kind = kind;
}

inline uint32_t map_entry_addr(const BgLayer &c, int tx, int ty) {
    const int bx = tx >> 5, by = ty >> 5;
    const int blocks_w = c.map_w >> 5;
    return c.screen + (by * blocks_w + bx) * 0x800 + (((ty & 31) * 32 + (tx & 31)) * 2);
}

// One BG pixel. Returns false where the layer is transparent here.
bool sample_bg(const BgLayer &c, int x, int y, uint32_t &out) {
    int px, py;
    uint16_t se;
    int tile, fx, fy;

    /* ---- THE BITMAP ARMS, and they leave before any tile arithmetic ------
     *
     * A bitmap BG has no tiles, no map and no flip bits: the affine transform
     * lands directly on a pixel. Same matrix, same 8.8 fixed point, same
     * display-area-overflow rule as the map-based affine BGs above, so the
     * coordinate half is spelled once here and only the fetch differs. */
    if (c.kind == BG_BITMAP_256 || c.kind == BG_BITMAP_DIRECT) {
        px = (c.refx + c.pa * x + c.pb * y) >> 8;
        py = (c.refy + c.pc * x + c.pd * y) >> 8;
        if (c.wrap) {
            px &= c.map_w - 1;
            py &= c.map_h - 1;
        } else if (px < 0 || px >= c.map_w || py < 0 || py >= c.map_h) {
            return false;
        }
        const uint32_t at = (uint32_t)py * (uint32_t)c.map_w + (uint32_t)px;
        if (c.kind == BG_BITMAP_DIRECT) {
            /* BIT 15 IS THE ALPHA and it is a HARD transparency, not a blend
               factor: a direct-colour bitmap pixel with bit 15 clear is not
               drawn at all and whatever is behind it shows. The display
               capture unit sets the bit on every pixel it writes, so a
               captured frame is opaque everywhere -- which is what makes a
               captured screen look like a screen and not like a stencil. */
            const uint16_t v = rd16(c.screen + at * 2);
            if (!(v & 0x8000)) return false;
            out = bgr555(v);
            return true;
        }
        /* 256-colour bitmap: index 0 is transparent, the same rule every other
           paletted layer in this file follows, and there is no per-tile
           palette to offset it by. */
        const uint8_t i8 = rd8(c.screen + at);
        if (!i8) return false;
        out = palette_color(kPlttBase + (uint32_t)i8 * 2);
        return true;
    }

    if (c.kind == BG_TEXT) {
        px = (x + c.hofs) & (c.map_w * 8 - 1);
        py = (y + c.vofs) & (c.map_h * 8 - 1);
        se = rd16(map_entry_addr(c, px >> 3, py >> 3));
        tile = se & 0x3FF;
        fx = px & 7;
        fy = py & 7;
        if (se & 0x400) fx = 7 - fx;
        if (se & 0x800) fy = 7 - fy;
    } else {
        // Affine: texture coordinate = ref + x*P + y*P, in 8.8.
        px = (c.refx + c.pa * x + c.pb * y) >> 8;
        py = (c.refy + c.pc * x + c.pd * y) >> 8;
        if (c.wrap) {
            px &= c.map_w - 1;
            py &= c.map_h - 1;
        } else if (px < 0 || px >= c.map_w || py < 0 || py >= c.map_h) {
            return false;
        }
        const int tw = c.map_w >> 3;
        if (c.kind == BG_AFFINE) {
            // 8-bit map entries: a bare tile number, no flip, no palette.
            tile = rd8(c.screen + (py >> 3) * tw + (px >> 3));
            se = 0;
            fx = px & 7;
            fy = py & 7;
        } else {
            // Extended affine, 16-bit entries: laid out as one flat tw*th
            // array rather than the text BG's 32x32 blocks.
            se = rd16(c.screen + ((py >> 3) * tw + (px >> 3)) * 2);
            tile = se & 0x3FF;
            fx = px & 7;
            fy = py & 7;
            if (se & 0x400) fx = 7 - fx;
            if (se & 0x800) fy = 7 - fy;
        }
    }

    uint32_t index;
    if (c.bpp8) {
        index = rd8(c.chars + (uint32_t)tile * 64 + fy * 8 + fx);
        if (index == 0) return false;
        if (c.ext) {
            out = palette_color(c.ext + (((se >> 12) & 0xF) * 256 + index) * 2);
            return true;
        }
    } else {
        const uint8_t pair = rd8(c.chars + (uint32_t)tile * 32 + fy * 4 + (fx >> 1));
        index = (fx & 1) ? (pair >> 4) : (pair & 0xF);
        if (index == 0) return false;
        index += ((se >> 12) & 0xF) * 16;
    }
    out = palette_color(kPlttBase + index * 2);
    return true;
}

// ---- sprites ----------------------------------------------------------------
//
// Rasterised into a whole-screen buffer first, because priority has to be
// resolved against the backgrounds pixel by pixel and a sprite's priority is
// its own, not its layer's.
//
// SPRITE-VS-SPRITE ORDER IS BY PRIORITY, NOT BY OAM INDEX. When two OBJ pixels
// overlap the DS keeps the one with the lowest priority NUMBER (attr2 bits
// 10-11, 0 = nearest); OAM index only breaks a tie, low index winning. This is
// melonDS's DrawSpritePixel exactly: it walks OAM 0->127 and overwrites iff the
// new pixel is opaque AND (the slot is empty OR new prio < old prio). This walk
// runs 127->0 so a lower-index sprite is processed LATER, so the equivalent
// overwrite test is `new prio <= old prio`: `<=` keeps the last (lowest-index)
// writer on a tie, and a strictly-better priority wins outright whatever the
// index. The header note used to say order was "by OAM index regardless of
// priority" and the write below was unconditional, which was pure lowest-index.
// That put a low-index low-priority sprite in front of a high-index
// high-priority one -- in curling, a prio-1 shell body and its prio-2 semi
// shadow (OAM 0,1) covered the prio-0 cursor bar and the prio-0 snow (OAM 10+),
// so the blue shadow tint drew over the bumper and the snow took the shell's
// texture where it crossed one. Both are the same defect, fixed at the write.

struct ObjPixel {
    uint32_t color;
    uint8_t prio;
    uint8_t hit;
    uint8_t semi;   // OBJ mode 1: always a 1st target, always alpha-blended
};

ObjPixel g_obj[192][256];

// The OBJ window's mask. A mode-2 sprite is not drawn: every one of its
// non-transparent texels instead marks that pixel as "inside the OBJ window",
// and WINOUT's upper half then says which layers show there. Kept separate
// from g_obj because a mask pixel contributes no colour and has no priority.
uint8_t g_objwin[192][256];

void raster_obj(uint32_t dispcnt) {
    static const int kSizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
    };
    if (!((dispcnt >> 12) & 1))
        return;
    std::memset(g_obj, 0, sizeof g_obj);
    std::memset(g_objwin, 0, sizeof g_objwin);
    const uint32_t boundary = 32u << ((dispcnt >> 20) & 3);
    // DISPCNT bit 4: 1 = one-dimensional mapping (a sprite's tiles are
    // consecutive), 0 = two-dimensional (OBJ VRAM is a 32-tile-wide matrix and
    // the next row of a sprite is 32 tiles on). Stage::InitResources clears it
    // for the sub engine -- `*p1 &= 0xFFCFFFEF` -- so the bottom screen is 2D
    // and a 1D-only reader smears every HUD sprite.
    const bool map1d = (dispcnt >> 4) & 1;
    // DISPCNT bit 31: OBJ EXTENDED PALETTES. They apply to 256-colour sprites
    // ONLY -- a 16-colour sprite keeps its 16-entry bank in the standard OBJ
    // palette whatever this bit says -- and when they are armed the attr2
    // palette field stops naming a 16-colour bank and names one of sixteen
    // 256-colour SLOTS instead.
    //
    // WHY THIS IS HERE. The star select's character portrait is the sprite
    // that needs it and nothing else on that screen does. dScStarSel_c::
    // InitResources loads the shared sub OBJ tiles plus a full 0x200-byte
    // standard palette through GXS::LoadOBJPltt, and then loads the SELECTED
    // CHARACTER separately: data/2D_cad/d_2d_player_select_mario_8bit_ncg.bin
    // decompressed to OBJ VRAM + 0x4000, and its _ncl companion pushed through
    // GXS::Begin/Load/EndLoadOBJExtPltt. Measured on scene 4: four 64x64
    // sprites in colour mode 256 at palette 0, tiles 512/516/576/580, and
    // 512 * 32 bytes IS that 0x4000 -- against nine 16-colour sprites for the
    // star and the text strip. Reading a 256-colour portrait through the
    // 16-colour sprites' standard palette gives every one of its pixels the
    // wrong colour while leaving its shape and its position exactly right.
    const uint32_t objext = ((dispcnt >> 31) & 1) ? kObjExtPltt : 0;

    /* THE SEAM'S FRAME ALIGNMENT, engine B's half. In the minigames' dual-OAM
       mode OAM::Load uploads ONLY the main bank and returns, so the sub OAM
       at 0x07000400 is filled on a different beat with a different age: the
       owner measured a straddling bob-omb's two halves stepping on ALTERNATE
       frames -- the top ticking while the bottom held and vice versa. When
       the mod is engaged, hal points this at engine B's own working shadow
       (data_0209ea74), the same cure ppu.cpp's engine A raster got, so both
       halves draw one frame. Zero everywhere else. */
    const uint32_t oam_b = g_oam_src_b ? g_oam_src_b : kOamBase;

    for (int i = 127; i >= 0; --i) {
        const uint16_t a0 = rd16(oam_b + i * 8u);
        const uint16_t a1 = rd16(oam_b + i * 8u + 2);
        const uint16_t a2 = rd16(oam_b + i * 8u + 4);
        // All original sprites belong to the DS engine. The optional PC
        // seam-snow overlay is not part of this native display backend.
        /* SM64DS_OAMAGE_TRACE: engine B's half of the probe in ppu.cpp. */
        {
            static int bget = -1;
            if (bget < 0) {
                const char *e = std::getenv("SM64DS_OAMAGE_TRACE");
                bget = e && *e && *e != '0';
            }
            static unsigned bf;
            if (bget && i == 127) ++bf;
            if (bget && a2 == 0x1010)
                std::fprintf(stderr, "[oamage] B f%u src=%08x slot%d y=%d\n",
                             bf, (unsigned)oam_b, i, (int)(a0 & 0xff));
        }
        const bool affine = a0 & 0x100;
        if (!affine && (a0 & 0x200)) continue;          // disabled
        // OBJ MODE, attribute 0 bits 10-11: 0 normal, 1 semi-transparent,
        // 2 OBJ window, 3 prohibited (bitmap on the DS). This test used to be
        // `== 3` with the comment "OBJ window", which skipped the wrong mode
        // and drew a window sprite -- an invisible mask on hardware -- as an
        // ordinary opaque sprite. See the note over ppu.cpp's OBJ layer for
        // where the field position is pinned from the decomp's own OAM::Render.
        const unsigned objmode = (a0 >> 10) & 3;
        /* MODE 3 IS THE BITMAP SPRITE, and it used to be skipped as "not
           hosted". It is how the display capture unit's frame reaches the
           screen ENGINE B IS DRIVING ON THE OTHER HALF OF THE FLIP: the
           dScMgD3DBase_c family's sel==1 arm maps the bank it captured into
           last frame to engine B's OBJ VRAM, turns BG2 off and OBJ on, and
           the frame comes back as TWELVE 64x64 direct-colour sprites --
           4 x 3 of them, which is 256x192, which is 0x18000 bytes, which is
           the capture's own size to the byte. Measured on scene 384 with the
           block seated: 98299 of 131072 bytes nonzero in engine B's OBJ VRAM
           and exactly 12 placed entries in its OAM.
           Without this the upper screen is the white backdrop on every
           second frame, which is a 30 Hz flicker rather than a missing
           feature. */
        const bool is_bmp = objmode == 3;
        const bool is_win = objmode == 2;
        const int shape = (a0 >> 14) & 3;
        if (shape == 3) continue;
        const int size = (a1 >> 14) & 3;
        const int w = kSizes[shape][size][0], h = kSizes[shape][size][1];
        const bool dbl = affine && (a0 & 0x200);
        const int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
        int x = a1 & 0x1FF, y = a0 & 0xFF;
        if (x >= 256) x -= 512;
        if (y >= 192 && y >= 256 - bh) y -= 256;
        const bool c256 = a0 & 0x2000;
        const bool hflip = !affine && (a1 & 0x1000);
        const bool vflip = !affine && (a1 & 0x2000);
        const uint32_t tile = a2 & 0x3FF;
        const uint32_t pal = (a2 >> 12) & 0xF;
        const uint8_t prio = (a2 >> 10) & 3;

        int pa = 256, pb = 0, pc = 0, pd = 256;
        if (affine) {
            const int grp = (a1 >> 9) & 0x1F;
            pa = (int16_t)rd16(oam_b + (grp * 4 + 0) * 8u + 6);
            pb = (int16_t)rd16(oam_b + (grp * 4 + 1) * 8u + 6);
            pc = (int16_t)rd16(oam_b + (grp * 4 + 2) * 8u + 6);
            pd = (int16_t)rd16(oam_b + (grp * 4 + 3) * 8u + 6);
        }

        for (int sy = 0; sy < bh; ++sy) {
            const int py = y + sy;
            if (py < 0 || py >= 192) continue;
            for (int sx = 0; sx < bw; ++sx) {
                const int px = x + sx;
                if (px < 0 || px >= 256) continue;
                int tx, ty;
                if (affine) {
                    const int cx = sx - bw / 2, cy = sy - bh / 2;
                    tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                    ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                    if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
                } else {
                    tx = hflip ? w - 1 - sx : sx;
                    ty = vflip ? h - 1 - sy : sy;
                }
                const int tcol = tx >> 3, trow = ty >> 3;
                const int fx = tx & 7, fy = ty & 7;
                // Where this 8x8 cell lives, in 32-byte tile slots from the
                // sprite's base. 1D: consecutive. 2D: a 32-slot-wide matrix,
                // and a 256-colour cell is two slots wide.
                /* ---- THE BITMAP SPRITE, and it leaves before the tile
                   arithmetic below because it has no tiles at all -- the
                   sprite IS a rectangle of 16-bit pixels.

                   GBATEK. The rows are addressed by DISPCNT bit 5, the
                   BITMAP OBJ mapping bit, which is a different bit from the
                   tile mapping bit 4 this file reads into map1d:

                     1D (bit 5 set): the sprite's pixels are consecutive and
                        attr2's tile number steps in units of the boundary
                        DISPCNT bit 22 selects, 128 bytes or 256.
                     2D (bit 5 clear): OBJ VRAM is one wide bitmap and the
                        sprite is a window onto it. Bit 6 chooses the width,
                        128 dots or 256, and the tile number splits into a
                        column part and a row part accordingly.

                   Engine B reads DISPCNT_B 0x00011025 on the frames that
                   use this: bit 5 SET and bit 6 CLEAR, which is 2D mapping
                   at a 256-dot width. Both arms are implemented anyway -- a
                   reader that only does the arm the one scene uses is a
                   reader that lies the first time another scene does not --
                   but only the 2D arm is exercised and only it is proved.

                   ALPHA. attr2 bits 12-15 are the sprite's alpha, and ZERO
                   MEANS THE SPRITE IS NOT DISPLAYED -- the field is not a
                   palette bank here. Per pixel, bit 15 of the colour is the
                   opacity, the same rule the direct-colour bitmap BG uses.
                   The 1..15 blend levels are NOT applied: this file's alpha
                   path is BLDALPHA's EVA/EVB, a bitmap sprite's own alpha
                   is a third mechanism, and nothing in this game asks for a
                   value between -- the family submits its twelve at 15,
                   fully opaque. A sprite that asked for one would draw
                   opaque here, which is the honest failure: visible and in
                   the right place, rather than absent. */
                if (is_bmp) {
                    const unsigned alpha = (a2 >> 12) & 0xF;
                    if (!alpha) continue;
                    uint32_t at;
                    /* BIT 6 SELECTS THE MAPPING, BIT 5 THE 2D DIMENSION, and
                       an earlier version of this block had the two jobs the
                       other way round -- it chose the arm on bit 5 and took the
                       2D width from bit 6. GBATEK's NDS DISPCNT:

                         bit 5   Bitmap OBJ 2D-Dimension
                                 (0 = 128x512 dots, 1 = 256x256 dots)
                         bit 6   Bitmap OBJ Mapping  (0 = 2D, 1 = 1D)
                         bit 22  Bitmap OBJ 1D-Boundary (0 = 128, 1 = 256 bytes)

                       It is bit 4 that is the mapping bit for TILE OBJs, and
                       reading the bitmap pair as if it followed that layout is
                       how the two got swapped. */
                    if ((dispcnt >> 6) & 1) {
                        /* 1D: the sprite's pixels are consecutive and attr2's
                           number steps in boundary units. NOT the arm this game
                           uses -- see below -- and untested by anything in the
                           tree, so it is written from the doc and left labelled
                           as such rather than claimed. */
                        const uint32_t bnd = ((dispcnt >> 22) & 1) ? 256u : 128u;
                        at = tile * bnd + (uint32_t)(ty * w + tx) * 2u;
                    } else {
                        /* 2D: OBJ VRAM is ONE bitmap `wide` dots across and the
                           sprite is a window onto it, so attr2 is a (column,
                           row) pair rather than a linear offset -- low bits X in
                           8-dot units, upper bits Y -- and the within-sprite row
                           stride is the BITMAP's width, not the sprite's.

                           THE ROM SETTLES WHICH ARM THIS IS, in one statement.
                           src/actors/dScMgD3DBase_c.cpp, the family's own sprite
                           builder (matched, and in all four family slices),
                           opens with

                               *(u32 *)0x4001000 = (reg & 0xffbfff9f) | 0x20;

                           and 0xffbfff9f clears bits 5, 6 AND 22 before ORing
                           bit 5 alone back in: mapping 2D, dimension 256x256,
                           1D boundary deliberately zeroed because it is not in
                           use. The same function then writes

                               attr2 = (tx + (ty << 5)) | 0xf000

                           with tx stepping 0,8,16,24 across and ty 0,8,16 down,
                           which IS this encoding and is not a linear multiple of
                           any boundary. Under it the twelve 64x64 sprites tile
                           the 256x192 capture exactly: tile 8 -> byte 0x80 =
                           pixel (64,0), tile 256 -> 0x8000 = (0,64), tile 536 ->
                           0x10180 = (192,128). */
                        const uint32_t wide = ((dispcnt >> 5) & 1) ? 256u : 128u;
                        const uint32_t mask = (wide >> 3) - 1u;   /* 0x1F or 0x0F */
                        at = ((tile & mask) * 0x10u + (tile & ~mask) * 0x80u) +
                             ((uint32_t)ty * wide + (uint32_t)tx) * 2u;
                    }
                    const uint16_t v = rd16(kObjVram + at);
                    if (!(v & 0x8000)) continue;
                    if (g_obj[py][px].hit && prio > g_obj[py][px].prio)
                        continue;
                    g_obj[py][px].color = bgr555(v);
                    g_obj[py][px].prio = prio;
                    g_obj[py][px].hit = 1;
                    g_obj[py][px].semi = 0;
                    continue;
                }
                const uint32_t slot =
                    map1d ? (c256 ? (uint32_t)(trow * (w / 8) + tcol) * 2u
                                  : (uint32_t)(trow * (w / 8) + tcol))
                          : (uint32_t)(trow * 32 + (c256 ? tcol * 2 : tcol));
                const uint32_t cell = kObjVram + tile * boundary + slot * 32u;
                uint32_t color;
                if (c256) {
                    const uint32_t idx = rd8(cell + fy * 8u + fx);
                    if (!idx) continue;
                    // Extended: attr2's palette field picks a 256-colour slot
                    // in the OBJ extended store. Standard: one flat 256-entry
                    // palette and the field means nothing.
                    color = objext
                                ? palette_color(objext + (pal * 256u + idx) * 2u)
                                : palette_color(kObjPltt + idx * 2u);
                } else {
                    const uint8_t b = rd8(cell + fy * 4u + fx / 2);
                    const uint32_t idx = (fx & 1) ? (b >> 4) : (b & 0xF);
                    if (!idx) continue;
                    color = palette_color(kObjPltt + (pal * 16u + idx) * 2u);
                }
                // A window sprite contributes its SHAPE and nothing else: the
                // texel is opaque, so the pixel is inside the window, and the
                // colour it would have had is discarded.
                if (is_win) {
                    g_objwin[py][px] = 1;
                    continue;
                }
                // Priority resolves OBJ-vs-OBJ (see the header note): overwrite
                // only if this pixel is empty or this sprite's priority is at
                // least as good (lower or equal number). With the 127->0 walk
                // that keeps the lowest-index sprite on a tie and lets a
                // higher-priority sprite win regardless of its index.
                if (g_obj[py][px].hit && prio > g_obj[py][px].prio)
                    continue;
                g_obj[py][px].color = color;
                g_obj[py][px].prio = prio;
                g_obj[py][px].hit = 1;
                // OBJ mode 1 is semi-transparent: it alpha-blends with the
                // layer below it regardless of BLDCNT's first-target bits.
                g_obj[py][px].semi = (objmode == 1);
            }
        }
    }
}

// ---- the window unit --------------------------------------------------------
//
// WIN0H/WIN1H hold X1<<8|X2 and WIN0V/WIN1V Y1<<8|Y2, all edges exclusive on
// the right/bottom. WININ's low six bits are what shows inside window 0 and
// its next six what shows inside window 1; WINOUT's low six are what shows
// outside every window and its NEXT six what shows inside the OBJ window.
// Bits 0..3 are BG0..BG3, bit 4 is OBJ, bit 5 the colour effect.
//
// THE OBJ WINDOW is the third window and it has no rectangle: its region is the
// union of the opaque texels of every mode-2 sprite, which raster_obj collects
// into g_objwin. DISPCNT bit 15 enables it, the same way bits 13 and 14 enable
// windows 0 and 1. Precedence is fixed in hardware: window 0 wins over window
// 1, window 1 over the OBJ window, and WINOUT covers what is in none of them.

struct Windows {
    bool any;
    int x1[2], x2[2], y1[2], y2[2];
    bool on[2];
    bool obj_on;
    unsigned in[2], obj_in, out;
};

void read_windows(uint32_t dispcnt, Windows &w) {
    w.on[0] = (dispcnt >> 13) & 1;
    w.on[1] = (dispcnt >> 14) & 1;
    // An enabled OBJ window still arms WINOUT when OBJ display is off, but
    // its raster is empty. Never sample a stale mask from the previous engine.
    w.obj_on = (dispcnt & 0x9000) == 0x9000;
    // Any window being on changes what the OTHER regions show, so the OBJ
    // window arms the whole unit exactly as the two rectangles do.
    w.any = (dispcnt & 0xe000) != 0;
    if (!w.any) return;
    w.obj_in = (rd16(kRegBase + 0x4A) >> 8) & 0x3F;
    for (int i = 0; i < 2; ++i) {
        const uint16_t hh = rd16(kRegBase + 0x40 + i * 2);
        const uint16_t vv = rd16(kRegBase + 0x44 + i * 2);
        w.x1[i] = hh >> 8;
        w.x2[i] = hh & 0xFF;
        w.y1[i] = vv >> 8;
        w.y2[i] = vv & 0xFF;
        // A right/bottom edge at or before the left/top one means the window
        // runs to the end of the line, which is how the hardware behaves when
        // the register holds 0 for a full-width window.
        if (w.x2[i] <= w.x1[i]) w.x2[i] = 256;
        if (w.y2[i] <= w.y1[i]) w.y2[i] = 192;
    }
    const uint16_t winin = rd16(kRegBase + 0x48);
    w.in[0] = winin & 0x3F;
    w.in[1] = (winin >> 8) & 0x3F;
    w.out = rd16(kRegBase + 0x4A) & 0x3F;
}

inline unsigned window_mask(const Windows &w, int x, int y) {
    if (!w.any) return 0x3F;
    for (int i = 0; i < 2; ++i)
        if (w.on[i] && x >= w.x1[i] && x < w.x2[i] && y >= w.y1[i] && y < w.y2[i])
            return w.in[i];
    if (w.obj_on && g_objwin[y][x]) return w.obj_in;
    return w.out;
}

// ---- master brightness ------------------------------------------------------

struct Bright { int mode, factor; };

inline uint32_t apply_bright(uint32_t c, const Bright &b) {
    if (!b.factor || b.mode == 0 || b.mode == 3) return c;
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, bl = c & 0xFF;
    if (b.mode == 1) {
        r += (255 - r) * b.factor / 16;
        g += (255 - g) * b.factor / 16;
        bl += (255 - bl) * b.factor / 16;
    } else {
        r -= r * b.factor / 16;
        g -= g * b.factor / 16;
        bl -= bl * b.factor / 16;
    }
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

// ---- the colour special-effects unit (BLDCNT) -------------------------------
//
// ALPHA (mode 1), the always-on semi-transparent-OBJ alpha, and the BLDY
// brightness modes (2/3) are all applied here, see the header note. Register
// offsets are engine-relative: BLDCNT 0x50, BLDALPHA 0x52, BLDY 0x54 on this
// engine's kRegBase. Layer ids match the window-mask bits: 0..3 BG0..BG3, 4
// OBJ, 5 the backdrop (BD).

// SM64DS_BLEND_OFF=1 keeps the old opaque path, so a before/after is one binary
// at one .dsstate base -- the only comparison notes/port-selftest-bmp-gate.md
// permits -- and a regression can be bisected against the pre-blend image.
inline bool blend_off_env() {
    static int v = -1;
    if (v < 0) {
        const char *e = std::getenv("SM64DS_BLEND_OFF");
        v = (e && *e && *e != '0') ? 1 : 0;
    }
    return v != 0;
}

struct Blend {
    bool off;
    int mode;          // BLDCNT bits 6-7: 0 off, 1 alpha, 2 bright-up, 3 down
    unsigned first;    // bits 0-5: 1st-target layers
    unsigned second;   // bits 8-13: 2nd-target layers
    int eva, evb;      // BLDALPHA: 1st/2nd coefficients, 0..16 in 1/16 steps
    int evy;           // BLDY: brightness up/down coefficient, 0..16 in 1/16 steps
};

inline Blend read_blend() {
    Blend b;
    b.off = blend_off_env();
    const uint16_t cnt = rd16(kRegBase + 0x50);
    const uint16_t alpha = rd16(kRegBase + 0x52);
    b.mode = (cnt >> 6) & 3;
    b.first = cnt & 0x3F;
    b.second = (cnt >> 8) & 0x3F;
    b.eva = alpha & 0x1F; if (b.eva > 16) b.eva = 16;
    b.evb = (alpha >> 8) & 0x1F; if (b.evb > 16) b.evb = 16;
    b.evy = rd16(kRegBase + 0x54) & 0x1F; if (b.evy > 16) b.evy = 16;
    return b;
}

// top*eva/16 + below*evb/16, per channel, in 5-bit and clamped to 31 -- the
// DS's own arithmetic. The framebuffer holds bgr555-expanded 8-bit channels and
// (v<<3|v>>2)>>3 recovers the original 5-bit value exactly, so the round trip
// is lossless and the blend is what the hardware produces.
inline uint32_t blend_alpha(uint32_t top, uint32_t below, int eva, int evb) {
    const int r1 = ((top >> 16) & 0xFF) >> 3, g1 = ((top >> 8) & 0xFF) >> 3,
              b1 = (top & 0xFF) >> 3;
    const int r2 = ((below >> 16) & 0xFF) >> 3, g2 = ((below >> 8) & 0xFF) >> 3,
              b2 = (below & 0xFF) >> 3;
    int r = (r1 * eva + r2 * evb) >> 4; if (r > 31) r = 31;
    int g = (g1 * eva + g2 * evb) >> 4; if (g > 31) g = 31;
    int b = (b1 * eva + b2 * evb) >> 4; if (b > 31) b = 31;
    return 0xFF000000u | ((uint32_t)(r << 3 | r >> 2) << 16)
                       | ((uint32_t)(g << 3 | g >> 2) << 8)
                       | (uint32_t)(b << 3 | b >> 2);
}

// The effect for one pixel, given the top layer and the one directly below it.
// `below` (col[1]/id[1]) is always valid: a pixel with nothing under the top is
// resolved against the backdrop, id 5, which BLDCNT can name as a 2nd target.
inline uint32_t blend_apply(const Blend &bl, unsigned mask, uint32_t top,
                            int top_id, bool top_semi, uint32_t below,
                            int below_id) {
    if (bl.off) return top;
    // Window bit 5 disables colour special effects inside this region.
    if (!(mask & 0x20)) return top;
    const bool below_second = (bl.second & (1u << below_id)) != 0;
    if (top_semi) {
        // Always 1st target, always alpha, whatever the mode says. With no 2nd
        // target beneath it a semi-transparent OBJ draws opaque (hardware).
        if (below_second) return blend_alpha(top, below, bl.eva, bl.evb);
        return top;
    }
    if (bl.mode == 1 && (bl.first & (1u << top_id)) && below_second)
        return blend_alpha(top, below, bl.eva, bl.evb);
    if ((bl.mode == 2 || bl.mode == 3) && (bl.first & (1u << top_id))) {
        // Brightness increase/decrease, gated on the first-target mask: only
        // the layers BLDCNT bits 0-5 name are affected, one layer at a time,
        // same as the DS. In 5-bit space, the DS's own arithmetic, the same
        // round trip blend_alpha uses.
        int r = ((top >> 16) & 0xFF) >> 3, g = ((top >> 8) & 0xFF) >> 3,
            b = (top & 0xFF) >> 3;
        if (bl.mode == 2) {
            r += ((31 - r) * bl.evy) >> 4;
            g += ((31 - g) * bl.evy) >> 4;
            b += ((31 - b) * bl.evy) >> 4;
        } else {
            r -= (r * bl.evy) >> 4;
            g -= (g * bl.evy) >> 4;
            b -= (b * bl.evy) >> 4;
        }
        if (r > 31) r = 31;
        if (g > 31) g = 31;
        if (b > 31) b = 31;
        return 0xFF000000u | ((uint32_t)(r << 3 | r >> 2) << 16)
                            | ((uint32_t)(g << 3 | g >> 2) << 8)
                            | (uint32_t)(b << 3 | b >> 2);
    }
    return top;
}

}  // namespace

template<bool CaptureLayers=false>
static void scan_engine(Native2DLayer& fb, Native2DLayer* overlay)
{

    const uint32_t dispcnt = rd32(kRegBase);
    const unsigned disp_mode = (dispcnt >> 16) & 3;
    const bool geometry=overlay && (dispcnt&0x108)==0x108;
    const unsigned geometry_priority=rd16(kRegBase+8)&3;
    if(overlay)std::memset(overlay->pixels,0,sizeof overlay->pixels);
    const bool forced_blank = (dispcnt >> 7) & 1;

    Bright br{0,0};
    if constexpr(!CaptureLayers) {
        const uint16_t mb = rd16(kRegBase + 0x6C);
        br.factor = mb & 0x1F;
        if (br.factor > 16) br.factor = 16;
        br.mode = (mb >> 14) & 3;
    }

    if (!CaptureLayers && (disp_mode == 0 || forced_blank)) {
        // Display off is white on a DS panel, not black.
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x) fb.pixels[y*256+x] = 0xFFFFFFFFu;
        return;
    }
    if(!CaptureLayers && kRegBase==0x04000000u && disp_mode==2) {
        const uint32_t base=0x06800000u+((dispcnt>>18)&3)*0x20000u;
        for(int y=0;y<192;++y)for(int x=0;x<256;++x)
            fb.pixels[y*256+x]=apply_bright(bgr555(rd16(base+2*(y*256+x))),br);
        return;
    }

    BgLayer bgs[4];
    for (int i = 0; i < 4; ++i) read_bg(bgs[i], i, dispcnt);
    // Priority and BG-number ties cannot change during this scan. Sort just
    // the enabled layers once instead of visiting four BGs at four priorities
    // for every pixel. Stable insertion keeps the original lower-BG tie break.
    unsigned order[4],bg_count=0;
    for(unsigned bg=0;bg<4;++bg)if(bgs[bg].kind!=BG_OFF) {
        unsigned at=bg_count;
        while(at && bgs[order[at-1]].prio>bgs[bg].prio) {
            order[at]=order[at-1];--at;
        }
        order[at]=bg;++bg_count;
    }

    Windows win;
    read_windows(dispcnt, win);

    raster_obj(dispcnt);
    const bool objects_enabled=(dispcnt&0x1000)!=0;

    const uint32_t backdrop = palette_color(kPlttBase);
    const Blend bld = read_blend();

    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 256; ++x) {
            const unsigned mask = window_mask(win, x, y);
            // Resolve the TOP visible layer and the one directly BELOW it, which
            // is all the colour-effect unit needs. Priority 0 is nearest; at
            // equal priority a sprite is above a background and among
            // backgrounds the lower number wins. Layer ids: 0..3 BG, 4 OBJ.
            uint32_t col[2] = {backdrop, backdrop};
            int priority[2]={4,4};
            int id[2] = {5, 5};        // 5 = backdrop (BD), the implicit bottom
            bool semi = false;
            int found = 0;
            const ObjPixel& o=g_obj[y][x];
            bool object_pending=objects_enabled && o.hit && (mask&0x10);
            unsigned next_bg=0;
            while(found<2 && (object_pending || next_bg<bg_count)) {
                // At a priority tie OBJ precedes BG. Only one winning OBJ
                // pixel exists after raster_obj, so insert it once in the
                // preordered background stream instead of testing it four times.
                if(object_pending && (next_bg==bg_count || o.prio<=bgs[order[next_bg]].prio)) {
                    if (!found) semi = o.semi;
                    col[found]=o.color;id[found]=4;priority[found]=o.prio;++found;
                    object_pending=false;
                } else {
                    const unsigned bg=order[next_bg++];
                    if (!(mask & (1u << bg))) continue;
                    uint32_t s;
                    if (sample_bg(bgs[bg], x, y, s)) {
                        col[found]=s;id[found]=bg;priority[found]=bgs[bg].prio;++found;
                    }
                }
            }
            // Nothing drawn: the pixel is pure backdrop, no top layer to blend.
            const uint32_t c = found ? blend_apply(bld, mask, col[0], id[0], semi,
                                                   col[1], id[1])
                                     : backdrop;
            fb.pixels[y*256+x] = apply_bright(c, br);
            if(geometry) {
                // Keep original 2D content behind the native 3D plane; only
                // foreground layers are uploaded as its transparent overlay.
                const bool foreground=priority[0]<int(geometry_priority) ||
                    (priority[0]==int(geometry_priority) && id[0]==4);
                if(!(mask&1) || foreground)overlay->pixels[y*256+x]=fb.pixels[y*256+x];
            }
        }
    }
}


void native_2d_scanout(Native2DFrame& frame) {
    // Bank routing may have changed since the previous simulation/VBlank.
    // Cache pointers only; pixel contents are always read from current memory.
    std::memset(g_vram_pages,0,sizeof g_vram_pages);
    std::memset(g_palette_valid,0,sizeof g_palette_valid);
    // Keep the original display swap. Gameplay's main engine normally owns
    // the upper screen; the original menu may move it between the panels.
    frame.main_on_top=(native_video_read(0x04000304,2)&0x8000)!=0;
    kRegBase=0x04000000;kVramBase=0x06000000;kPlttBase=0x05000000;
    kObjVram=0x06400000;kObjPltt=0x05000200;kOamBase=0x07000000;
    kBgExtPltt=0x06880000;kObjExtPltt=0x06890000;
    const auto control=uint32_t(native_video_read(kRegBase,4));
    frame.has_3d=(control&0x108)==0x108 && ((control>>16)&3)==1 && !(control&0x80);
    scan_engine(frame.underlay,&frame.overlay);
    kRegBase=0x04001000;kVramBase=0x06200000;kPlttBase=0x05000400;
    kObjVram=0x06600000;kObjPltt=0x05000600;kOamBase=0x07000400;
    kBgExtPltt=0x06898000;kObjExtPltt=0x068a0000;
    scan_engine(frame.bottom,nullptr);
}

bool native_2d_scanout_cached(Native2DFrame& frame,Native2DScanoutCache& cache) {
    if(!native_video_ok()) {cache.invalidate();return false;}
    const Native2DScanoutCache::Inputs inputs{
        static_cast<const uint8_t*>(native_video_pointer(0x04000000,0x2000)),
        static_cast<const uint8_t*>(native_video_pointer(0x06800000,0xa4000)),
        static_cast<const uint8_t*>(native_video_pointer(0x05000000,0x800)),
        static_cast<const uint8_t*>(native_video_pointer(0x07000000,0x800))};
    if(!native_video_ok() || !inputs.complete()) {cache.invalidate();return false;}
    if(cache.matches(inputs,frame))return true;
    native_2d_scanout(frame);
    if(!native_video_ok()) {cache.invalidate();return false;}
    return cache.commit(inputs,frame);
}

bool native_2d_capture_layers(Native2DFrame& frame, std::string& error) {
    if(!native_video_ok()) {
        error="Engine A capture has unavailable native video state";return false;
    }
    const auto control=uint32_t(native_video_read(0x04000000,4));
    // The published video-system diagrams put Source A before display-mode
    // selection and MASTER_BRIGHT. They do not settle forced-blank behavior.
    // Do not silently substitute the white presentation buffer for a capture.
    // https://blocksds.skylyrac.net/tutorial/advanced/video_capture/
    if(control&0x80) {
        error="Engine A capture under forced blank is not verified";return false;
    }
    if((control&7)>5) {
        error="Engine A capture uses an unsupported background mode";return false;
    }
    std::memset(g_vram_pages,0,sizeof g_vram_pages);
    std::memset(g_palette_valid,0,sizeof g_palette_valid);
    kRegBase=0x04000000;kVramBase=0x06000000;kPlttBase=0x05000000;
    kObjVram=0x06400000;kObjPltt=0x05000200;kOamBase=0x07000000;
    kBgExtPltt=0x06880000;kObjExtPltt=0x06890000;
    frame.main_on_top=(native_video_read(0x04000304,2)&0x8000)!=0;
    // BG0 enable and its 2D/3D switch belong to the graphics composition,
    // while DISPCNT.16-17 selects the later display output only.
    frame.has_3d=(control&0x108)==0x108;
    scan_engine<true>(frame.underlay,&frame.overlay);
    if(!native_video_ok()) {
        char message[128];
        std::snprintf(message,sizeof message,"Engine A capture cannot read native video address %08x",unsigned(native_video_bad_address()));
        error=message;return false;
    }
    error.clear();return true;
}
} // namespace sm64ds
