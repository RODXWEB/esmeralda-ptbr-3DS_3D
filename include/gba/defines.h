#ifndef GUARD_GBA_DEFINES_H
#define GUARD_GBA_DEFINES_H

#include <stddef.h>

#define TRUE  1
#define FALSE 0

#ifdef PORTABLE
#define IWRAM_DATA
#define EWRAM_DATA
#define COMMON_DATA
#else
#define IWRAM_DATA __attribute__((section("iwram_data")))
#define EWRAM_DATA __attribute__((section("ewram_data")))
#define COMMON_DATA __attribute__((section("common_data")))
#endif
#define UNUSED __attribute__((unused))

#if MODERN
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

#define ALIGNED(n) __attribute__((aligned(n)))

#define BG_PLTT_SIZE  0x200
#define OBJ_PLTT      (PLTT + BG_PLTT_SIZE)
#define OBJ_PLTT_SIZE 0x200
#define PLTT_SIZE     (BG_PLTT_SIZE + OBJ_PLTT_SIZE)

#ifndef PORTABLE
#define SOUND_INFO_PTR (*(struct SoundInfo **)0x3007FF0)
#define INTR_CHECK     (*(u16 *)0x3007FF8)
#define INTR_VECTOR    (*(void **)0x3007FFC)

#define EWRAM_START 0x02000000
#define EWRAM_END   (EWRAM_START + 0x40000)
#define IWRAM_START 0x03000000
#define IWRAM_END   (IWRAM_START + 0x8000)

#define PLTT      0x5000000
#else
extern struct SoundInfo * SOUND_INFO_PTR;
extern unsigned short INTR_CHECK;
extern void * INTR_VECTOR;

extern unsigned char PLTT[PLTT_SIZE] __attribute__ ((aligned (4)));
#endif

#define BG_PLTT       PLTT

#define VRAM_SIZE 0x18000
#ifndef PORTABLE
#define VRAM      0x6000000
#else
extern unsigned char VRAM_[VRAM_SIZE] __attribute__ ((aligned (4)));
#define VRAM (u32)VRAM_
#endif

#define BG_VRAM           VRAM
#define BG_VRAM_SIZE      0x10000
#define BG_CHAR_SIZE      0x4000
#define BG_SCREEN_SIZE    0x800
#define BG_CHAR_ADDR(n)   (BG_VRAM + (BG_CHAR_SIZE * (n)))
#define BG_SCREEN_ADDR(n) (BG_VRAM + (BG_SCREEN_SIZE * (n)))

#define BG_TILE_H_FLIP(n) (0x400 + (n))
#define BG_TILE_V_FLIP(n) (0x800 + (n))

#define NUM_BACKGROUNDS 4

// text-mode BG
#define OBJ_VRAM0      (VRAM + 0x10000)
#define OBJ_VRAM0_SIZE 0x8000

// bitmap-mode BG
#define OBJ_VRAM1      (VRAM + 0x14000)
#define OBJ_VRAM1_SIZE 0x4000

#define OAM_SIZE 0x400
#ifndef PORTABLE
#define OAM      0x7000000
#else
extern unsigned char OAM[OAM_SIZE] __attribute__ ((aligned (4)));
#endif

#define ROM_HEADER_SIZE   0xC0

// Dimensions of a tile in pixels
#define TILE_WIDTH  8
#define TILE_HEIGHT 8

// Native 3DS viewport: the game lays out menus, windows and text over the whole
// top screen, so nothing is letterboxed or clipped. Resources authored for the
// GBA keep their own size and leave the remaining area as backdrop.
//
// CTR_GBA_STAGE marks the translation units staged as one whole GBA picture
// instead (the intro, the title screen and the credits): their art is a single
// 240x160 composition, so they keep the GBA geometry exactly and the 3DS
// compositor scales that stage to fill the screen (see docs/ARCHITECTURE.md).
#if defined(PLATFORM_3DS) && !defined(CTR_GBA_STAGE)
#define DISPLAY_WIDTH  400
#define DISPLAY_HEIGHT 240
#else
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 160
#endif

// Overworld field of view, and how far it is shifted to keep the player in the
// middle of it. The camera puts the player at the centre of a GBA screen, so
// on a wider viewport the whole field view moves by half the extra size.
//
// Horizontally that is the full half difference. Vertically it is bounded by
// the field tilemap, which is 32 rows (256px) for a 240px viewport: the
// overworld installs the pan-ahead camera, whose resting value lifts the view
// 32px, and that ran the view off the end of the tilemap. Cancelling exactly
// that much leaves the GBA's own 8px half-metatile margin above and 8px below,
// and both are needed: the row a camera step brings in reaches VRAM one frame
// later, so an edge flush with the viewport shows the stale row until then.
// The player therefore sits 16px above the centre; closing that would need a
// 64-row tilemap.
#define FIELD_VIEW_WIDTH  DISPLAY_WIDTH
#define FIELD_VIEW_HEIGHT DISPLAY_HEIGHT
#ifdef PLATFORM_3DS
#define FIELD_VIEW_OFFSET_X ((DISPLAY_WIDTH - 240) / 2)
#define FIELD_VIEW_OFFSET_Y 32
#else
#define FIELD_VIEW_OFFSET_X 0
#define FIELD_VIEW_OFFSET_Y 0
#endif

// Dimensions of the GBA screen in tiles
#define DISPLAY_TILE_WIDTH  (DISPLAY_WIDTH / TILE_WIDTH)
#define DISPLAY_TILE_HEIGHT (DISPLAY_HEIGHT / TILE_HEIGHT)

// Size of different tile formats in bytes
#define TILE_SIZE(bpp) ((bpp) * TILE_WIDTH * TILE_HEIGHT / 8)
#define TILE_SIZE_1BPP TILE_SIZE(1) // 8
#define TILE_SIZE_4BPP TILE_SIZE(4) // 32
#define TILE_SIZE_8BPP TILE_SIZE(8) // 64

#define TILE_OFFSET_4BPP(n) ((n) * TILE_SIZE_4BPP)
#define TILE_OFFSET_8BPP(n) ((n) * TILE_SIZE_8BPP)

#define TOTAL_OBJ_TILE_COUNT 1024

#define PLTT_SIZEOF(n) ((n) * sizeof(u16))
#define PLTT_SIZE_4BPP PLTT_SIZEOF(16)
#define PLTT_SIZE_8BPP PLTT_SIZEOF(256)

#define PLTT_OFFSET_4BPP(n) ((n) * PLTT_SIZE_4BPP)

#endif // GUARD_GBA_DEFINES_H
