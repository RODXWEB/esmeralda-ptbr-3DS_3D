#ifndef GUARD_PALETTE_H
#define GUARD_PALETTE_H

#define gPaletteFade_selectedPalettes (gPaletteFade.multipurpose1) // normal and fast fade
#define gPaletteFade_blendCnt         (gPaletteFade.multipurpose1) // hardware fade
#define gPaletteFade_delay            (gPaletteFade.multipurpose2) // normal and hardware fade
#define gPaletteFade_submode          (gPaletteFade.multipurpose2) // fast fade

#define PLTT_BUFFER_SIZE (PLTT_SIZE / sizeof(u16))

#define PALETTE_FADE_STATUS_DELAY 2
#define PALETTE_FADE_STATUS_ACTIVE 1
#define PALETTE_FADE_STATUS_DONE 0
#define PALETTE_FADE_STATUS_LOADING 0xFF

#define PALETTES_BG      0x0000FFFF
#define PALETTES_OBJECTS 0xFFFF0000
#define PALETTES_ALL     (PALETTES_BG | PALETTES_OBJECTS)

#define PLTT_ID(n) ((n) * 16)
#define BG_PLTT_OFFSET 0x000
#define OBJ_PLTT_OFFSET 0x100
#define BG_PLTT_ID(n) (BG_PLTT_OFFSET + PLTT_ID(n))
#define OBJ_PLTT_ID(n) (OBJ_PLTT_OFFSET + PLTT_ID(n))
#define OBJ_PLTT_ID2(n) (PLTT_ID((n) + 16))

#ifdef PLATFORM_3DS
/*
 * Port: OBJ palettes past the GBA's sixteen, for the field's sprites that
 * need one of their own (a follower, the visible wild mons), where object
 * events hold OBJ palettes 0-11. They sit after the GBA's 512 colours in both
 * buffers, so PLTT_EXT_ID(n) is an ordinary palette buffer offset; only the
 * 3DS renderers show them (TransferPlttBuffer hands them over with PLTT).
 *
 * They take every fade, blend and weather colour map an NPC's palette takes:
 * a selection mask selects them all when it selects OBJ palette
 * PLTT_EXT_MODEL (PALSLOT_NPC_1), and the weather treats them as that
 * palette. One is in use while its bit of gPlttExtInUse is set (sprite.c,
 * LoadSpritePaletteExt); with none in use nothing here costs a thing.
 */
#define PLTT_EXT_COUNT 16
#define PLTT_EXT_OFFSET PLTT_BUFFER_SIZE
#define PLTT_EXT_ID(n) (PLTT_EXT_OFFSET + PLTT_ID(n))
#define PLTT_EXT_SIZE PLTT_SIZEOF(PLTT_ID(PLTT_EXT_COUNT))
#define PLTT_EXT_MODEL 2
#define PLTT_EXT_SELECTED(selectedPalettes) ((((selectedPalettes) >> (16 + PLTT_EXT_MODEL)) & 1) ? gPlttExtInUse : 0)
#define PLTT_BUFFER_ALLOC (PLTT_BUFFER_SIZE + PLTT_ID(PLTT_EXT_COUNT))
// The bytes of the buffers that a copy or fill of all palettes covers.
#define PLTT_BUFFER_BYTES (PLTT_SIZE + (gPlttExtInUse ? PLTT_EXT_SIZE : 0))
extern u16 gPlttExtInUse;
#else
#define PLTT_BUFFER_ALLOC PLTT_BUFFER_SIZE
#define PLTT_BUFFER_BYTES PLTT_SIZE
#endif
// The bytes of a copy of the buffers kept to be put back later (in
// gPaletteDecompressionBuffer, which holds them): every ext palette, in use or
// not, so that one loaded or freed meanwhile does not change what goes back.
#define PLTT_BUFFER_ALLOC_BYTES PLTT_SIZEOF(PLTT_BUFFER_ALLOC)

enum
{
    FAST_FADE_IN_FROM_WHITE,
    FAST_FADE_OUT_TO_WHITE,
    FAST_FADE_IN_FROM_BLACK,
    FAST_FADE_OUT_TO_BLACK,
};

struct PaletteFadeControl
{
    u32 multipurpose1;
    u8 delayCounter:6;
    u16 y:5; // blend coefficient
    u16 targetY:5; // target blend coefficient
    u16 blendColor:15;
    bool16 active:1;
    u16 multipurpose2:6;
    bool16 yDec:1; // whether blend coefficient is decreasing
    bool16 bufferTransferDisabled:1;
    u16 mode:2;
    bool16 shouldResetBlendRegisters:1;
    bool16 hardwareFadeFinishing:1;
    u16 softwareFadeFinishingCounter:5;
    bool16 softwareFadeFinishing:1;
    bool16 objPaletteToggle:1;
    u8 deltaY:4; // rate of change of blend coefficient
};

extern struct PaletteFadeControl gPaletteFade;
extern u32 gPlttBufferTransferPending;
extern u8 ALIGNED(4) gPaletteDecompressionBuffer[];
extern u16 ALIGNED(4) gPlttBufferUnfaded[PLTT_BUFFER_ALLOC];
extern u16 ALIGNED(4) gPlttBufferFaded[PLTT_BUFFER_ALLOC];

void LoadCompressedPalette(const u32 *src, u16 offset, u16 size);
// Port: a 2-byte load from an exact asset base means sizeof an INCBIN_U16 stub.
// Use LoadPaletteWithOffset for explicit one-color reads from asset bases.
void LoadPalette(const void *src, u16 offset, u16 size);
// byteOffset and size are in bytes; dest is an index into the palette buffers.
// size is always an explicit copy length, including when byteOffset is zero.
void LoadPaletteWithOffset(const void *base, u32 byteOffset, u16 dest, u16 size);
// For palette animations that must leave the unfaded reference unchanged.
void LoadPaletteFadedWithOffset(const void *base, u32 byteOffset, u16 dest, u16 size);
void FillPalette(u16 value, u16 offset, u16 size);
void TransferPlttBuffer(void);
u8 UpdatePaletteFade(void);
void ResetPaletteFade(void);
bool8 BeginNormalPaletteFade(u32 selectedPalettes, s8 delay, u8 startY, u8 targetY, u16 blendColor);
void PaletteStruct_ResetById(u16 id);
void ResetPaletteFadeControl(void);
void InvertPlttBuffer(u32 selectedPalettes);
void TintPlttBuffer(u32 selectedPalettes, s8 r, s8 g, s8 b);
void UnfadePlttBuffer(u32 selectedPalettes);
void BeginFastPaletteFade(u8 submode);
void BeginHardwarePaletteFade(u8 blendCnt, u8 delay, u8 y, u8 targetY, u8 shouldResetBlendRegisters);
void BlendPalettes(u32 selectedPalettes, u8 coeff, u16 color);
void BlendPalettesUnfaded(u32 selectedPalettes, u8 coeff, u16 color);
void BlendPalettesGradually(u32 selectedPalettes, s8 delay, u8 coeff, u8 coeffTarget, u16 color, u8 priority, u8 id);
void TintPalette_GrayScale(u16 *palette, u16 count);
void TintPalette_GrayScale2(u16 *palette, u16 count);
void TintPalette_SepiaTone(u16 *palette, u16 count);
void TintPalette_CustomTone(u16 *palette, u16 count, u16 rTone, u16 gTone, u16 bTone);
#ifdef PORT_BRIDGE
// Port: TRUE while a normal fade must wait, as on one of its delay frames,
// for a picture the port has not shown yet (the 3DS voxel field's warm-up:
// 3ds_port/src/3ds_game_full.c). A weak FALSE in palette.c otherwise.
bool8 Port_HoldPaletteFade(void);
#endif

static inline void SetBackdropFromColor(u16 color)
{
  FillPalette(color, 0, PLTT_SIZEOF(1));
}

static inline void SetBackdropFromPalette(const u16 *palette)
{
  LoadPaletteWithOffset(palette, 0, 0, PLTT_SIZEOF(1));
}

#endif // GUARD_PALETTE_H
