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

static void RedrawMapSliceNorth(struct FieldCameraOffset *, const struct MapLayout *);
static void RedrawMapSliceSouth(struct FieldCameraOffset *, const struct MapLayout *);
static void RedrawMapSliceEast(struct FieldCameraOffset *, const struct MapLayout *);
static void RedrawMapSliceWest(struct FieldCameraOffset *, const struct MapLayout *);
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
// The column a camera step brings in on each side. It has to be the last one
// the view covers, or the columns past it keep whatever the previous map view
// left there.
#define FIELD_CAMERA_REDRAW_EDGE_X (FIELD_CAMERA_VIEW_RIGHT / 2 - 1)
#define FIELD_CAMERA_REDRAW_EDGE_Y 15
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
/* Vertical slice waiting for the half step, and how far is left of it. */
static s8 sPendingVerticalSlice;
static s8 sPendingVerticalPixels;
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

void ResetFieldCamera(void)
{
    ResetCameraOffset(&sFieldCameraOffset);
}

void FieldUpdateBgTilemapScroll(void)
{
    u32 r4, r5;
    // Scrolling less shows the map further right and down, which is how the
    // field view is centred on a viewport wider than a GBA screen.
    r5 = sFieldCameraOffset.xPixelOffset + sHorizontalCameraPan - FIELD_VIEW_OFFSET_X;
    r4 = sVerticalCameraPan + sFieldCameraOffset.yPixelOffset + 8 - FIELD_VIEW_OFFSET_Y;

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
}

void DrawWholeMapView(void)
{
    DrawWholeMapViewInternal(gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, gMapHeader.mapLayout);
    sFieldCameraOffset.copyBGToVRAM = TRUE;
#if defined(PLATFORM_3DS)
    // Every row is current now, so a slice still waiting would redraw one of
    // them for a camera step that is already accounted for.
    sPendingVerticalSlice = 0;
#endif
}

static void DrawWholeMapViewInternal(int x, int y, const struct MapLayout *mapLayout)
{
    int i;
    int j;

    for (i = 0; i < 32; i += 2)
    {
        for (j = FIELD_CAMERA_VIEW_LEFT; j < FIELD_CAMERA_VIEW_RIGHT; j += 2)
            DrawMetatileAtTilemapPos(mapLayout,
                                     sFieldCameraOffset.xTileOffset + j,
                                     sFieldCameraOffset.yTileOffset + i,
                                     x + j / 2,
                                     y + i / 2);
    }
}

static void RedrawMapSlicesForCameraUpdate(struct FieldCameraOffset *cameraOffset, int x, int y)
{
    const struct MapLayout *mapLayout = gMapHeader.mapLayout;

    if (x > 0)
        RedrawMapSliceWest(cameraOffset, mapLayout);
    if (x < 0)
        RedrawMapSliceEast(cameraOffset, mapLayout);
#if defined(PLATFORM_3DS)
    // The row a vertical step brings in and the row it retires are the same
    // ring slot: the tilemap holds 16 metatile rows, 256px, and the viewport
    // takes 240 of them, so for the first half of the step that slot is still
    // on screen at the opposite edge. Drawing it now would put the incoming
    // row where the outgoing one still belongs, which is the bottom of the
    // map appearing in the top strip. Both constraints meet at the half step,
    // so the vertical slice waits for it. A 160px GBA screen has five spare
    // rows and no such conflict, which is why the original draws it at once.
    if (y != 0)
        sPendingVerticalSlice = y > 0 ? 1 : -1, sPendingVerticalPixels = 8;
#else
    if (y > 0)
        RedrawMapSliceNorth(cameraOffset, mapLayout);
    if (y < 0)
        RedrawMapSliceSouth(cameraOffset, mapLayout);
#endif
    cameraOffset->copyBGToVRAM = TRUE;
}

#if defined(PLATFORM_3DS)
// Draws the deferred vertical slice once the camera has travelled half a
// metatile into the step, or right away if it stops before getting there.
static void DrawPendingVerticalSlice(int movementSpeedY)
{
    if (!sPendingVerticalSlice)
        return;
    sPendingVerticalPixels -= movementSpeedY < 0 ? -movementSpeedY : movementSpeedY;
    if (sPendingVerticalPixels > 0 && movementSpeedY != 0)
        return;
    if (sPendingVerticalSlice > 0)
        RedrawMapSliceNorth(&sFieldCameraOffset, gMapHeader.mapLayout);
    else
        RedrawMapSliceSouth(&sFieldCameraOffset, gMapHeader.mapLayout);
    sFieldCameraOffset.copyBGToVRAM = TRUE;
    sPendingVerticalSlice = 0;
}
#endif

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

#ifdef PORT_BRIDGE
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
    ScheduleBgCopyTilemapToVram(1);
    ScheduleBgCopyTilemapToVram(2);
    ScheduleBgCopyTilemapToVram(3);
}

static s32 MapPosToBgTilemapOffset(struct FieldCameraOffset *cameraOffset, s32 x, s32 y)
{
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
        else
#endif
        RedrawMapSlicesForCameraUpdate(&sFieldCameraOffset, deltaX * 2, deltaY * 2);
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
    DrawPendingVerticalSlice(movementSpeedY);
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
