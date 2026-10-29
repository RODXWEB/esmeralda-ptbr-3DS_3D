#include "global.h"
#include "berry.h"
#include "bike.h"
#include "field_camera.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "gpu_regs.h"
#include "menu.h"
#include "overworld.h"
#include "rotating_gate.h"
#include "sprite.h"
#include "text.h"

#ifdef PORT_BRIDGE
#include "port_log.h"
#include "port_platform.h"

void PortDoor_Forget(void); // field_door.c
#endif
#if defined(PLATFORM_3DS)
#include "constants/maps.h"
#include "3ds_field_view.h"
#include "3ds_video.h"
#include "3ds_platform.h"
#endif

EWRAM_DATA bool8 gUnusedBikeCameraAheadPanback = FALSE;

struct FieldCameraOffset
{
#ifdef PORT_BRIDGE
    u16 xPixelOffset;
#else
    u8 xPixelOffset;
#endif
    u8 yPixelOffset;
    u8 xTileOffset;
    u8 yTileOffset;
    bool8 copyBGToVRAM;
};

#if !defined(PLATFORM_3DS)
static void RedrawMapSliceNorth(struct FieldCameraOffset *, const struct MapLayout *);
static void RedrawMapSliceSouth(struct FieldCameraOffset *, const struct MapLayout *);
static void RedrawMapSliceEast(struct FieldCameraOffset *, const struct MapLayout *);
static void RedrawMapSliceWest(struct FieldCameraOffset *, const struct MapLayout *);
#endif
static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *, s32, s32);
static void DrawWholeMapViewInternal(int, int, const struct MapLayout *);
static void DrawMetatileAt(const struct MapLayout *, u16, int, int);
static void DrawMetatileAtTilemapPos(const struct MapLayout *, int, int, int, int);
static void DrawMetatile(s32, const u16 *, u16);
static void CameraPanningCB_PanAhead(void);

#if defined(PLATFORM_3DS)
// The view is shifted left by FIELD_VIEW_OFFSET_X to centre the player, so the
// columns run from -(offset) to the right edge of the viewport, plus two
// columns at each end: one for the half-metatile and one so the column a step
// brings in is drawn before it can be seen, instead of flush with the edge,
// which showed as a black line down the right side while walking. 58 columns
// still fit in the 64-column ring buffer.
#define FIELD_CAMERA_VIEW_LEFT (-4 - FIELD_VIEW_OFFSET_X / 8)
#define FIELD_CAMERA_VIEW_RIGHT (4 + (DISPLAY_WIDTH - FIELD_VIEW_OFFSET_X) / 8)
#define FIELD_CAMERA_TILEMAP_WIDTH 64
#else
#define FIELD_CAMERA_REDRAW_EDGE_X 14
#define FIELD_CAMERA_REDRAW_EDGE_Y 14
#define FIELD_CAMERA_TILEMAP_WIDTH 32
#define FIELD_CAMERA_VIEW_LEFT 0
#define FIELD_CAMERA_VIEW_RIGHT 32
#endif

static struct FieldCameraOffset sFieldCameraOffset;
static s16 sHorizontalCameraPan;
#if defined(PLATFORM_3DS)
// The edge of the map (3ds_field_view.h). Where no other map joins the
// current one, the view stops at its edge: the camera goes on following the
// player, as the game expects of it, and the picture - backgrounds and
// sprites alike - is held back by this many pixels, east and south.
static s16 sEdgeShiftX, sEdgeShiftY;
// The metatiles the tilemap holds: columns FIELD_CAMERA_VIEW_LEFT / 2 to
// FIELD_CAMERA_VIEW_RIGHT / 2 and 16 rows from this one. They follow the
// picture rather than the camera, which the shift parts it from.
static s16 sMapWindowX, sMapWindowY;
// The bank (3ds_tile_banks.h) of each entry of the three tilemaps, 1 + bank
// or 0, handed over with them (FieldCamera_RegisterTileBanks), and the bank
// of the metatile DrawMetatile is drawing.
static u8 sTileBanks[3][BG_SCREEN_SIZE];
static u8 sDrawBank;
#endif
static s16 sVerticalCameraPan;
static bool8 sBikeCameraPanFlag;
static void (*sFieldCameraPanningCallback)(void);

COMMON_DATA struct CameraObject gFieldCamera = {0};
COMMON_DATA u16 gTotalCameraPixelOffsetY = 0;
COMMON_DATA u16 gTotalCameraPixelOffsetX = 0;

static void ResetCameraOffset(struct FieldCameraOffset *cameraOffset)
{
    cameraOffset->xTileOffset = 0;
    cameraOffset->yTileOffset = 0;
    cameraOffset->xPixelOffset = 0;
    cameraOffset->yPixelOffset = 0;
    cameraOffset->copyBGToVRAM = TRUE;
}

static void AddCameraTileOffset(struct FieldCameraOffset *cameraOffset, u32 xOffset, u32 yOffset)
{
    cameraOffset->xTileOffset += xOffset;
    cameraOffset->xTileOffset %= FIELD_CAMERA_TILEMAP_WIDTH;
    cameraOffset->yTileOffset += yOffset;
    cameraOffset->yTileOffset %= 32;
}

static void AddCameraPixelOffset(struct FieldCameraOffset *cameraOffset, u32 xOffset, u32 yOffset)
{
#ifdef PORT_BRIDGE
    cameraOffset->xPixelOffset = (cameraOffset->xPixelOffset + xOffset) & 0x1FF;
#else
    cameraOffset->xPixelOffset += xOffset;
#endif
    cameraOffset->yPixelOffset += yOffset;
}

#if defined(PLATFORM_3DS)
static void UpdateEdgeShift(bool32 snap, int speedX, int speedY);
#endif

void ResetFieldCamera(void)
{
    ResetCameraOffset(&sFieldCameraOffset);
#if defined(PLATFORM_3DS)
    // A new map, behind a fade: the picture is where it belongs at once.
    UpdateEdgeShift(TRUE, 0, 0);
#endif
}

void FieldUpdateBgTilemapScroll(void)
{
    u32 r4, r5;
    // Scrolling less shows the map further right and down, which is how the
    // field view is centred on a viewport wider than a GBA screen.
    r5 = sFieldCameraOffset.xPixelOffset + sHorizontalCameraPan - FIELD_VIEW_OFFSET_X;
    r4 = sVerticalCameraPan + sFieldCameraOffset.yPixelOffset + 8 - FIELD_VIEW_OFFSET_Y;
#if defined(PLATFORM_3DS)
    r5 += sEdgeShiftX;
    r4 += sEdgeShiftY;
#endif

    SetGpuReg(REG_OFFSET_BG1HOFS, r5);
    SetGpuReg(REG_OFFSET_BG1VOFS, r4);
    SetGpuReg(REG_OFFSET_BG2HOFS, r5);
    SetGpuReg(REG_OFFSET_BG2VOFS, r4);
    SetGpuReg(REG_OFFSET_BG3HOFS, r5);
    SetGpuReg(REG_OFFSET_BG3VOFS, r4);
}

void GetCameraOffsetWithPan(s16 *x, s16 *y)
{
    // The battle transition reproduces the field scroll, so it follows the
    // same centring as FieldUpdateBgTilemapScroll.
    *x = sFieldCameraOffset.xPixelOffset + sHorizontalCameraPan - FIELD_VIEW_OFFSET_X;
    *y = sFieldCameraOffset.yPixelOffset + sVerticalCameraPan + 8 - FIELD_VIEW_OFFSET_Y;
#if defined(PLATFORM_3DS)
    *x += sEdgeShiftX;
    *y += sEdgeShiftY;
#endif
}

#if defined(PLATFORM_3DS)
static void GetMapWindow(s16 *x, s16 *y);
#endif

void DrawWholeMapView(void)
{
#ifdef PORT_BRIDGE
    PortDoor_Forget();
#endif
#if defined(PLATFORM_3DS)
    GetMapWindow(&sMapWindowX, &sMapWindowY);
    DrawWholeMapViewInternal(sMapWindowX, sMapWindowY, gMapHeader.mapLayout);
#else
    DrawWholeMapViewInternal(gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, gMapHeader.mapLayout);
#endif
    sFieldCameraOffset.copyBGToVRAM = TRUE;
}

static void DrawWholeMapViewInternal(int x, int y, const struct MapLayout *mapLayout)
{
    int i;
    int j;
#if defined(PLATFORM_3DS)
    // The window's place in the ring: the camera's metatile is at the
    // offsets, and the window is (x, y) from it.
    int tileX = sFieldCameraOffset.xTileOffset + (x - gSaveBlock1Ptr->pos.x) * 2;
    int tileY = sFieldCameraOffset.yTileOffset + (y - gSaveBlock1Ptr->pos.y) * 2;
#else
    int tileX = sFieldCameraOffset.xTileOffset;
    int tileY = sFieldCameraOffset.yTileOffset;
#endif

    for (i = 0; i < 32; i += 2)
    {
        for (j = FIELD_CAMERA_VIEW_LEFT; j < FIELD_CAMERA_VIEW_RIGHT; j += 2)
            DrawMetatileAtTilemapPos(mapLayout,
                                     tileX + j,
                                     tileY + i,
                                     x + j / 2,
                                     y + i / 2);
    }
}

#if defined(PLATFORM_3DS)
/*
 * The tilemap is a ring the picture moves over: 64 columns, 32 rows, the
 * camera's metatile at the offsets. It holds the metatiles of a window around
 * the picture, and as the picture moves, the window follows it a metatile at a
 * time, the one coming in drawn into the slot of the one going out.
 *
 * The rows decide when. The 16 rows of the ring are 256px and the picture 240,
 * so a row coming in shares its slot with one that is still on screen at the
 * opposite edge until the picture is aligned to the rows: that is where the
 * window moves. A camera at rest has the picture half a metatile into a row
 * (8px), so a step moves it at the half step. The columns have room to spare
 * (29 of 32): the window is the nearest to the picture, two metatiles past
 * each side of it.
 */

// The picture's place, in pixels from the camera's metatile (pos): the scroll
// runs ahead of the metatile offsets by the part of the step taken.
static int CameraStepX(void)
{
    int step = (sFieldCameraOffset.xPixelOffset - sFieldCameraOffset.xTileOffset * 8) & 0x1FF;

    return step >= 0x100 ? step - 0x200 : step;
}

static int CameraStepY(void)
{
    int step = (sFieldCameraOffset.yPixelOffset - sFieldCameraOffset.yTileOffset * 8) & 0xFF;

    return step >= 0x80 ? step - 0x100 : step;
}

static int FloorDiv16(int value)
{
    return value >= 0 ? value / 16 : -((15 - value) / 16);
}

static void GetMapWindow(s16 *x, s16 *y)
{
    // The picture's left edge, from the window's when it is the camera's,
    // and its top edge from the first row (FieldUpdateBgTilemapScroll).
    int left = CameraStepX() + sHorizontalCameraPan + sEdgeShiftX;
    int top = CameraStepY() + sVerticalCameraPan + 8 - FIELD_VIEW_OFFSET_Y + sEdgeShiftY;

    *x = gSaveBlock1Ptr->pos.x + FloorDiv16(left + 8);
    *y = gSaveBlock1Ptr->pos.y + FloorDiv16(top);
}

static void DrawMapWindowColumn(int x)
{
    int tileX = sFieldCameraOffset.xTileOffset + (x - gSaveBlock1Ptr->pos.x) * 2;
    int tileY = sFieldCameraOffset.yTileOffset + (sMapWindowY - gSaveBlock1Ptr->pos.y) * 2;
    int i;

    for (i = 0; i < 32; i += 2)
        DrawMetatileAtTilemapPos(gMapHeader.mapLayout, tileX, tileY + i, x, sMapWindowY + i / 2);
}

static void DrawMapWindowRow(int y)
{
    int tileX = sFieldCameraOffset.xTileOffset + (sMapWindowX - gSaveBlock1Ptr->pos.x) * 2;
    int tileY = sFieldCameraOffset.yTileOffset + (y - gSaveBlock1Ptr->pos.y) * 2;
    int j;

    for (j = FIELD_CAMERA_VIEW_LEFT; j < FIELD_CAMERA_VIEW_RIGHT; j += 2)
        DrawMetatileAtTilemapPos(gMapHeader.mapLayout, tileX + j, tileY, sMapWindowX + j / 2, y);
}

// Moves the window to the picture, drawing what comes into it.
static void UpdateMapWindow(void)
{
    s16 x, y;
    s16 oldX = sMapWindowX, oldY = sMapWindowY;

    GetMapWindow(&x, &y);
    if (x == oldX && y == oldY)
        return;
    if (x - oldX > 1 || oldX - x > 1 || y - oldY > 1 || oldY - y > 1)
    {
        DrawWholeMapView();
        return;
    }
    sMapWindowX = x;
    sMapWindowY = y;
    if (x > oldX)
        DrawMapWindowColumn(x + FIELD_CAMERA_VIEW_RIGHT / 2 - 1);
    else if (x < oldX)
        DrawMapWindowColumn(x + FIELD_CAMERA_VIEW_LEFT / 2);
    if (y > oldY)
        DrawMapWindowRow(y + 15);
    else if (y < oldY)
        DrawMapWindowRow(y);
    sFieldCameraOffset.copyBGToVRAM = TRUE;
}

// The maps whose camera stops at some of their edges, and which edges: chosen
// map by map, as the border past an edge is not always worth hiding. Any map
// not listed keeps the camera on the player at every edge.
#define EDGE_W CTR_FIELD_EDGE_WEST
#define EDGE_E CTR_FIELD_EDGE_EAST
#define EDGE_N CTR_FIELD_EDGE_NORTH
#define EDGE_S CTR_FIELD_EDGE_SOUTH

static const struct
{
    u16 map; // MAP_*
    u8 edges;
} sCameraEdgeMaps[] =
{
    {MAP_ROUTE103,      EDGE_N},
    {MAP_ROUTE104,      EDGE_W | EDGE_E},
    {MAP_ROUTE105,      EDGE_W | EDGE_E},
    {MAP_ROUTE106,      EDGE_W | EDGE_E | EDGE_N},
    {MAP_ROUTE107,      EDGE_N},
    {MAP_RUSTBORO_CITY, EDGE_W | EDGE_N},
    {MAP_LAVARIDGE_TOWN, EDGE_N | EDGE_S},
    {MAP_ROUTE111,      EDGE_N | EDGE_E},
    {MAP_ROUTE112,      EDGE_S | EDGE_N},
    {MAP_ROUTE113,      EDGE_N | EDGE_S},
    {MAP_FALLARBOR_TOWN, EDGE_N | EDGE_S},
    {MAP_ROUTE114,      EDGE_E | EDGE_N | EDGE_W},
    {MAP_ROUTE116,      EDGE_N},
    {MAP_ROUTE117,      EDGE_N},
    {MAP_ROUTE119,      EDGE_W},
    {MAP_ROUTE120,      EDGE_W},
    {MAP_ROUTE121,      EDGE_N},
    {MAP_LILYCOVE_CITY, EDGE_N},
    {MAP_MAUVILLE_CITY, EDGE_N},
    {MAP_ROUTE118,      EDGE_N},
    {MAP_SOOTOPOLIS_CITY, EDGE_W | EDGE_E | EDGE_N | EDGE_S},
    {MAP_VERDANTURF_TOWN, EDGE_W | EDGE_N},
    {MAP_JAGGED_PASS,   EDGE_W | EDGE_E | EDGE_N | EDGE_S},
    {MAP_MT_CHIMNEY,    EDGE_W | EDGE_E | EDGE_N | EDGE_S},
    {MAP_ROUTE110,      EDGE_W},
};

#undef EDGE_W
#undef EDGE_E
#undef EDGE_N
#undef EDGE_S

// Whether the player stands in front of a map joined to this one on that
// side: along that edge, between the two ends of the map beyond it.
static bool32 FacesConnectedMap(u8 direction)
{
    int i;

    if (gMapHeader.connections == NULL)
        return FALSE;
    for (i = 0; i < gMapHeader.connections->count; i++)
    {
        const struct MapConnection *connection = &gMapHeader.connections->connections[i];
        const struct MapHeader *beyond;
        int at, length;

        if (connection->direction != direction)
            continue;
        beyond = GetMapHeaderFromConnection(connection);
        if (beyond == NULL || beyond->mapLayout == NULL)
            continue;
        if (direction == CONNECTION_NORTH || direction == CONNECTION_SOUTH)
            at = gSaveBlock1Ptr->pos.x, length = beyond->mapLayout->width;
        else
            at = gSaveBlock1Ptr->pos.y, length = beyond->mapLayout->height;
        if (at >= connection->offset && at < connection->offset + length)
            return TRUE;
    }
    return FALSE;
}

// The current map's edges where the camera stops (CTR_FIELD_EDGE_*): those
// sCameraEdgeMaps gives it, less any the player stands in front of another
// map joined there (an edge only partly joined, like Route 104's east, stops
// the camera only past the map beyond it). None in the dark, whose circle of
// light is drawn round the middle of the picture. The voxel camera stops at
// the same ones (voxel_world.c).
u32 GetMapClosedEdges(void)
{
    u16 map = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;
    u32 closed = 0;
    int i;

    if (gMapHeader.mapLayout == NULL || GetFlashLevel() != 0)
        return 0;
    for (i = 0; i < (int)ARRAY_COUNT(sCameraEdgeMaps); i++)
    {
        if (sCameraEdgeMaps[i].map == map)
        {
            closed = sCameraEdgeMaps[i].edges;
            break;
        }
    }
    if ((closed & CTR_FIELD_EDGE_WEST) && FacesConnectedMap(CONNECTION_WEST))
        closed &= ~CTR_FIELD_EDGE_WEST;
    if ((closed & CTR_FIELD_EDGE_EAST) && FacesConnectedMap(CONNECTION_EAST))
        closed &= ~CTR_FIELD_EDGE_EAST;
    if ((closed & CTR_FIELD_EDGE_NORTH) && FacesConnectedMap(CONNECTION_NORTH))
        closed &= ~CTR_FIELD_EDGE_NORTH;
    if ((closed & CTR_FIELD_EDGE_SOUTH) && FacesConnectedMap(CONNECTION_SOUTH))
        closed &= ~CTR_FIELD_EDGE_SOUTH;
    return closed;
}

// How fast the picture closes in on where it belongs after that changed under
// it (a map joined by a crossing, the 2D/voxel option), in pixels a frame more than the
// camera moves: on a step at an edge, where it stays still, it keeps up.
#define EDGE_SHIFT_EASE 2

static int EdgeTarget(int at, int length, int low, int high, unsigned closed)
{
    return (int)CtrFieldView_Limit((float)at, 0.0f, (float)length, (float)low, (float)high, closed) - at;
}

static int EdgeStep(int shift, int target, int speed)
{
    int most = (speed < 0 ? -speed : speed) + EDGE_SHIFT_EASE;

    if (target > shift + most)
        return shift + most;
    if (target < shift - most)
        return shift - most;
    return target;
}

/*
 * Where the picture belongs: the map's own pixels (its grid starts MAP_OFFSET
 * metatiles in) and the picture's place without the shift, the GBA's view
 * widened round the player. Not in the voxel option, whose world has its own
 * camera (voxel_camera.c) and lays the sprites out round the player.
 */
static void UpdateEdgeShift(bool32 snap, int speedX, int speedY)
{
    int targetX = 0, targetY = 0;
    u32 closed = CtrSettings_Voxel() ? 0 : GetMapClosedEdges();

    if (closed != 0)
    {
        int left = gSaveBlock1Ptr->pos.x * 16 + CameraStepX() - FIELD_VIEW_OFFSET_X;
        int top = gSaveBlock1Ptr->pos.y * 16 + CameraStepY() + 8 + 32 - FIELD_VIEW_OFFSET_Y;

        targetX = EdgeTarget(left, DISPLAY_WIDTH, MAP_OFFSET * 16,
                             (MAP_OFFSET + gMapHeader.mapLayout->width) * 16, closed & 3);
        targetY = EdgeTarget(top, DISPLAY_HEIGHT, MAP_OFFSET * 16,
                             (MAP_OFFSET + gMapHeader.mapLayout->height) * 16, (closed >> 2) & 3);
    }
    if (snap)
    {
        sEdgeShiftX = targetX;
        sEdgeShiftY = targetY;
    }
    else
    {
        sEdgeShiftX = EdgeStep(sEdgeShiftX, targetX, speedX);
        sEdgeShiftY = EdgeStep(sEdgeShiftY, targetY, speedY);
    }
    // The people the view reaches move with it (event_object_movement.c).
    CtrFieldView_SetFlatShift(FloorDiv16(sEdgeShiftX + 8), FloorDiv16(sEdgeShiftY + 8));
}
#else
static void RedrawMapSlicesForCameraUpdate(struct FieldCameraOffset *cameraOffset, int x, int y)
{
    const struct MapLayout *mapLayout = gMapHeader.mapLayout;

    if (x > 0)
        RedrawMapSliceWest(cameraOffset, mapLayout);
    if (x < 0)
        RedrawMapSliceEast(cameraOffset, mapLayout);
    if (y > 0)
        RedrawMapSliceNorth(cameraOffset, mapLayout);
    if (y < 0)
        RedrawMapSliceSouth(cameraOffset, mapLayout);
    cameraOffset->copyBGToVRAM = TRUE;
}

static void RedrawMapSliceNorth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    int i;

    for (i = FIELD_CAMERA_VIEW_LEFT; i < FIELD_CAMERA_VIEW_RIGHT; i += 2)
        DrawMetatileAtTilemapPos(mapLayout,
                                 cameraOffset->xTileOffset + i,
                                 cameraOffset->yTileOffset + FIELD_CAMERA_REDRAW_EDGE_Y * 2,
                                 gSaveBlock1Ptr->pos.x + i / 2,
                                 gSaveBlock1Ptr->pos.y + FIELD_CAMERA_REDRAW_EDGE_Y);
}

static void RedrawMapSliceSouth(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    int i;

    for (i = FIELD_CAMERA_VIEW_LEFT; i < FIELD_CAMERA_VIEW_RIGHT; i += 2)
        DrawMetatileAtTilemapPos(mapLayout,
                                 cameraOffset->xTileOffset + i,
                                 cameraOffset->yTileOffset,
                                 gSaveBlock1Ptr->pos.x + i / 2,
                                 gSaveBlock1Ptr->pos.y);
}

static void RedrawMapSliceEast(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    u8 i;
    int r6 = cameraOffset->xTileOffset + FIELD_CAMERA_VIEW_LEFT;

    for (i = 0; i < 32; i += 2)
        DrawMetatileAtTilemapPos(mapLayout,
                                 r6,
                                 cameraOffset->yTileOffset + i,
                                 gSaveBlock1Ptr->pos.x + FIELD_CAMERA_VIEW_LEFT / 2,
                                 gSaveBlock1Ptr->pos.y + i / 2);
}

static void RedrawMapSliceWest(struct FieldCameraOffset *cameraOffset, const struct MapLayout *mapLayout)
{
    u8 i;
    u8 r5 = cameraOffset->xTileOffset + FIELD_CAMERA_REDRAW_EDGE_X * 2;

    for (i = 0; i < 32; i += 2)
        DrawMetatileAtTilemapPos(mapLayout,
                                 r5,
                                 cameraOffset->yTileOffset + i,
                                 gSaveBlock1Ptr->pos.x + FIELD_CAMERA_REDRAW_EDGE_X,
                                 gSaveBlock1Ptr->pos.y + i / 2);
}
#endif

void CurrentMapDrawMetatileAt(int x, int y)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatileAt(gMapHeader.mapLayout, offset, x, y);
        sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

void DrawDoorMetatileAt(int x, int y, u16 *tiles)
{
    int offset = MapPosToBgTilemapOffset(&sFieldCameraOffset, x, y);

    if (offset >= 0)
    {
        DrawMetatile(METATILE_LAYER_TYPE_COVERED, tiles, offset);
        sFieldCameraOffset.copyBGToVRAM = TRUE;
    }
}

static void DrawMetatileAt(const struct MapLayout *mapLayout, u16 offset, int x, int y)
{
#ifdef PORT_BRIDGE
    u16 metatileId = MapGridGetMetatileIdForCameraAt(x, y);
#else
    u16 metatileId = MapGridGetMetatileIdAt(x, y);
#endif
    const u16 *metatiles;
#ifdef PORT_BRIDGE
    bool8 isPrimary;
    u16 rawMetatileId = metatileId;
#endif
#ifdef PLATFORM_3DS
    // A neighbour with a bank of its own is drawn with its own tilesets.
    const struct MapLayout *bankLayout = NULL;
    u8 bank = MapGridGetCameraBankAt(x, y, &bankLayout);

    if (bank != 0)
        mapLayout = bankLayout;
#endif

    if (metatileId > NUM_METATILES_TOTAL)
        metatileId = 0;
#ifdef PORT_BRIDGE
    isPrimary = metatileId < NUM_METATILES_IN_PRIMARY;
#endif
    if (metatileId < NUM_METATILES_IN_PRIMARY)
    {
        metatiles = mapLayout->primaryTileset->metatiles;
    }
    else
    {
        metatiles = mapLayout->secondaryTileset->metatiles;
        metatileId -= NUM_METATILES_IN_PRIMARY;
    }

#ifdef PORT_BRIDGE
    {
        static bool8 sLoggedPrimaryMetatile;
        const u16 *source = metatiles;

        metatiles = Port_ResolveAssetPointer(metatiles);
        if (!sLoggedPrimaryMetatile && isPrimary)
        {
            const u16 *tiles = metatiles + metatileId * NUM_TILES_PER_METATILE;

            Port_SetGfxMetatileDebug(metatileId, metatiles != source,
                                    tiles[0], tiles[1]);
            PORT_LOG("Metatile P id=%03X src=%08lX dst=%08lX %04X/%04X\n",
                    metatileId, (unsigned long)source, (unsigned long)metatiles,
                    tiles[0], tiles[1]);
            sLoggedPrimaryMetatile = TRUE;
        }
    }
#endif

#if defined(PLATFORM_3DS)
    sDrawBank = bank;
    DrawMetatile(UNPACK_LAYER_TYPE(bank != 0 ? GetMetatileAttributesFromLayout(bankLayout, rawMetatileId)
                                             : GetMetatileAttributesById(rawMetatileId)),
                 metatiles + metatileId * NUM_TILES_PER_METATILE, offset);
    sDrawBank = 0;
#elif defined(PORT_BRIDGE)
    // The id is already in hand; MapGridGetMetatileLayerTypeAt would look the
    // same cell up again (twice, with its asset resolution) for every metatile.
    DrawMetatile(UNPACK_LAYER_TYPE(GetMetatileAttributesById(rawMetatileId)),
                 metatiles + metatileId * NUM_TILES_PER_METATILE, offset);
#else
    DrawMetatile(MapGridGetMetatileLayerTypeAt(x, y), metatiles + metatileId * NUM_TILES_PER_METATILE, offset);
#endif
}

static void DrawMetatileAtTilemapPos(const struct MapLayout *mapLayout, int tileX, int tileY, int mapX, int mapY)
{
    tileX &= FIELD_CAMERA_TILEMAP_WIDTH - 1;
    tileY &= 31;

#ifdef PORT_BRIDGE
    if (tileX >= 32)
        DrawMetatileAt(mapLayout, BG_SCREEN_SIZE / 2 + tileY * 32 + tileX - 32, mapX, mapY);
    else
#endif
        DrawMetatileAt(mapLayout, tileY * 32 + tileX, mapX, mapY);
}

static void DrawMetatile(s32 metatileLayerType, const u16 *tiles, u16 offset)
{
    switch (metatileLayerType)
    {
    case METATILE_LAYER_TYPE_SPLIT:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw transparent tiles to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = 0;
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = 0;

        // Draw metatile's top layer to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    case METATILE_LAYER_TYPE_COVERED:
        // Draw metatile's bottom layer to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg3[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[7];

        // Draw transparent tiles to the top background layer.
        gOverworldTilemapBuffer_Bg1[offset] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 1] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = 0;
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = 0;
        break;
    case METATILE_LAYER_TYPE_NORMAL:
        // Draw garbage to the bottom background layer.
        gOverworldTilemapBuffer_Bg3[offset] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 1] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x20] = 0x3014;
        gOverworldTilemapBuffer_Bg3[offset + 0x21] = 0x3014;

        // Draw metatile's bottom layer to the middle background layer.
        gOverworldTilemapBuffer_Bg2[offset] = tiles[0];
        gOverworldTilemapBuffer_Bg2[offset + 1] = tiles[1];
        gOverworldTilemapBuffer_Bg2[offset + 0x20] = tiles[2];
        gOverworldTilemapBuffer_Bg2[offset + 0x21] = tiles[3];

        // Draw metatile's top layer to the top background layer, which covers object event sprites.
        gOverworldTilemapBuffer_Bg1[offset] = tiles[4];
        gOverworldTilemapBuffer_Bg1[offset + 1] = tiles[5];
        gOverworldTilemapBuffer_Bg1[offset + 0x20] = tiles[6];
        gOverworldTilemapBuffer_Bg1[offset + 0x21] = tiles[7];
        break;
    }
#if defined(PLATFORM_3DS)
    {
        // The empty and filler entries name no tile or palette of a bank, so
        // all three layers take the metatile's bank as they are.
        int bg;

        for (bg = 0; bg < 3; bg++)
        {
            sTileBanks[bg][offset] = sDrawBank;
            sTileBanks[bg][offset + 1] = sDrawBank;
            sTileBanks[bg][offset + 0x20] = sDrawBank;
            sTileBanks[bg][offset + 0x21] = sDrawBank;
        }
    }
#endif
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

#if defined(PLATFORM_3DS)
void FieldCamera_RegisterTileBanks(void)
{
    memset(sTileBanks, 0, sizeof(sTileBanks));
    CtrVideo_RegisterTilemapBanks(gOverworldTilemapBuffer_Bg1, sTileBanks[0], BG_SCREEN_SIZE);
    CtrVideo_RegisterTilemapBanks(gOverworldTilemapBuffer_Bg2, sTileBanks[1], BG_SCREEN_SIZE);
    CtrVideo_RegisterTilemapBanks(gOverworldTilemapBuffer_Bg3, sTileBanks[2], BG_SCREEN_SIZE);
}

void FieldCamera_UnregisterTileBanks(void)
{
    CtrVideo_UnregisterTilemapBanks(gOverworldTilemapBuffer_Bg1);
    CtrVideo_UnregisterTilemapBanks(gOverworldTilemapBuffer_Bg2);
    CtrVideo_UnregisterTilemapBanks(gOverworldTilemapBuffer_Bg3);
}
#endif

static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *cameraOffset, s32 x, s32 y)
{
#if defined(PLATFORM_3DS)
    // In the window, wherever the picture has taken it.
    if ((x - sMapWindowX) * 2 >= FIELD_CAMERA_VIEW_RIGHT || (x - sMapWindowX) * 2 < FIELD_CAMERA_VIEW_LEFT
     || y < sMapWindowY || y >= sMapWindowY + 16)
        return -1;
    x = ((x - gSaveBlock1Ptr->pos.x) * 2 + cameraOffset->xTileOffset) & (FIELD_CAMERA_TILEMAP_WIDTH - 1);
    y = ((y - gSaveBlock1Ptr->pos.y) * 2 + cameraOffset->yTileOffset) & 31;
#else
    x -= gSaveBlock1Ptr->pos.x;
    x *= 2;
    if (x >= FIELD_CAMERA_VIEW_RIGHT || x < FIELD_CAMERA_VIEW_LEFT)
        return -1;
    x = x + cameraOffset->xTileOffset;
    x &= FIELD_CAMERA_TILEMAP_WIDTH - 1;

    y = (y - gSaveBlock1Ptr->pos.y) * 2;
    if (y >= 32 || y < 0)
        return -1;
    y = y + cameraOffset->yTileOffset;
    if (y >= 32)
        y -= 32;
#endif

#ifdef PORT_BRIDGE
    if (x >= 32)
        return BG_SCREEN_SIZE / 2 + y * 32 + x - 32;
#endif
    return y * 32 + x;
}

static void CameraUpdateCallback(struct CameraObject *fieldCamera)
{
    if (fieldCamera->spriteId != 0)
    {
        fieldCamera->movementSpeedX = gSprites[fieldCamera->spriteId].sCamera_MoveX;
        fieldCamera->movementSpeedY = gSprites[fieldCamera->spriteId].sCamera_MoveY;
    }
}

void ResetCameraUpdateInfo(void)
{
    gFieldCamera.movementSpeedX = 0;
    gFieldCamera.movementSpeedY = 0;
    gFieldCamera.x = 0;
    gFieldCamera.y = 0;
    gFieldCamera.spriteId = 0;
    gFieldCamera.callback = NULL;
}

u32 InitCameraUpdateCallback(u8 trackedSpriteId)
{
    if (gFieldCamera.spriteId != 0)
        DestroySprite(&gSprites[gFieldCamera.spriteId]);
    gFieldCamera.spriteId = AddCameraObject(trackedSpriteId);
    gFieldCamera.callback = CameraUpdateCallback;
    return 0;
}

void CameraUpdate(void)
{
    int deltaX;
    int deltaY;
    int curMovementOffsetY;
    int curMovementOffsetX;
    int movementSpeedX;
    int movementSpeedY;

    if (gFieldCamera.callback != NULL)
        gFieldCamera.callback(&gFieldCamera);
    movementSpeedX = gFieldCamera.movementSpeedX;
    movementSpeedY = gFieldCamera.movementSpeedY;
    deltaX = 0;
    deltaY = 0;
    curMovementOffsetX = gFieldCamera.x;
    curMovementOffsetY = gFieldCamera.y;


    if (curMovementOffsetX == 0 && movementSpeedX != 0)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY == 0 && movementSpeedY != 0)
    {
        if (movementSpeedY > 0)
            deltaY = 1;
        else
            deltaY = -1;
    }
    if (curMovementOffsetX != 0 && curMovementOffsetX == -movementSpeedX)
    {
        if (movementSpeedX > 0)
            deltaX = 1;
        else
            deltaX = -1;
    }
    if (curMovementOffsetY != 0 && curMovementOffsetY == -movementSpeedY)
    {
        if (movementSpeedY > 0)
            deltaY = 1;
        else
            deltaY = -1;
    }

    gFieldCamera.x += movementSpeedX;
    gFieldCamera.x %= 16;
    gFieldCamera.y += movementSpeedY;
    gFieldCamera.y %= 16;

    if (deltaX != 0 || deltaY != 0)
    {
        CameraMove(deltaX, deltaY);
        UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
        RotatingGatePuzzleCameraUpdate(deltaX, deltaY);
        SetBerryTreesSeen();
        AddCameraTileOffset(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
#ifdef PORT_BRIDGE
        // The saved GBA map view is smaller than the port viewport. A map
        // connection can replace borders/tilesets anywhere in that viewport,
        // so its old ring-buffer contents cannot be retained across maps.
        if (gCamera.active)
        {
            Port_ProfileMark("camera-pre-draw");
            DrawWholeMapView();
            Port_ProfileMark("DrawWholeMapView");
        }
#endif
        // On the 3DS the window follows the picture instead (UpdateMapWindow).
#if !defined(PLATFORM_3DS)
#ifdef PORT_BRIDGE
        else
#endif
        RedrawMapSlicesForCameraUpdate(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
#endif
    }
#if defined(PLATFORM_3DS)
    // At rest, a change of view (the 2D/voxel option, the voxel camera's
    // pitch or zoom) brings in or lets go the people it reaches, as a step
    // does.
    else if (gFieldCamera.x == 0 && gFieldCamera.y == 0 && movementSpeedX == 0 && movementSpeedY == 0)
    {
        UpdateObjectEventsForViewChange();
    }
#endif

    AddCameraPixelOffset(&sFieldCameraOffset, movementSpeedX, movementSpeedY);
#if defined(PLATFORM_3DS)
    UpdateEdgeShift(FALSE, movementSpeedX, movementSpeedY);
    UpdateMapWindow();
#endif
    gTotalCameraPixelOffsetX -= movementSpeedX;
    gTotalCameraPixelOffsetY -= movementSpeedY;
}

void MoveCameraAndRedrawMap(int deltaX, int deltaY) //unused
{
    CameraMove(deltaX, deltaY);
    UpdateObjectEventsForCameraUpdate(deltaX, deltaY);
    DrawWholeMapView();
    gTotalCameraPixelOffsetX -= deltaX * 16;
    gTotalCameraPixelOffsetY -= deltaY * 16;
}

void SetCameraPanningCallback(void (*callback)(void))
{
    sFieldCameraPanningCallback = callback;
}

void SetCameraPanning(s16 horizontal, s16 vertical)
{
    sHorizontalCameraPan = horizontal;
    sVerticalCameraPan = vertical + 32;
}

void InstallCameraPanAheadCallback(void)
{
    sFieldCameraPanningCallback = CameraPanningCB_PanAhead;
    sBikeCameraPanFlag = FALSE;
    sHorizontalCameraPan = 0;
    sVerticalCameraPan = 32;
}

void UpdateCameraPanning(void)
{
    if (sFieldCameraPanningCallback != NULL)
        sFieldCameraPanningCallback();
    //Update sprite offset of overworld objects
    // Overworld sprites live in the same space as the map layers, so they take
    // the same centring shift; otherwise the player would part company with
    // the metatile it stands on.
    gSpriteCoordOffsetX = gTotalCameraPixelOffsetX - sHorizontalCameraPan + FIELD_VIEW_OFFSET_X;
    gSpriteCoordOffsetY = gTotalCameraPixelOffsetY - sVerticalCameraPan - 8 + FIELD_VIEW_OFFSET_Y;
#if defined(PLATFORM_3DS)
    gSpriteCoordOffsetX -= sEdgeShiftX;
    gSpriteCoordOffsetY -= sEdgeShiftY;
#endif
}

static void CameraPanningCB_PanAhead(void)
{
    u8 var;

    if (gUnusedBikeCameraAheadPanback == FALSE)
    {
        InstallCameraPanAheadCallback();
    }
    else
    {
        // this code is never reached
        if (gPlayerAvatar.tileTransitionState == T_TILE_TRANSITION)
        {
            sBikeCameraPanFlag ^= 1;
            if (sBikeCameraPanFlag == FALSE)
                return;
        }
        else
        {
            sBikeCameraPanFlag = FALSE;
        }

        var = GetPlayerMovementDirection();
        if (var == 2)
        {
            if (sVerticalCameraPan > -8)
                sVerticalCameraPan -= 2;
        }
        else if (var == 1)
        {
            if (sVerticalCameraPan < 72)
                sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan < 32)
        {
            sVerticalCameraPan += 2;
        }
        else if (sVerticalCameraPan > 32)
        {
            sVerticalCameraPan -= 2;
        }
    }
}
