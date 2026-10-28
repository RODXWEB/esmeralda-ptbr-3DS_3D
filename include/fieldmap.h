#ifndef GUARD_FIELDMAP_H
#define GUARD_FIELDMAP_H

#define NUM_TILES_IN_PRIMARY 512
#define NUM_TILES_TOTAL 1024
#define NUM_METATILES_IN_PRIMARY 512
#define NUM_METATILES_TOTAL 1024
#define NUM_PALS_IN_PRIMARY 6
#define NUM_PALS_TOTAL 13
#define MAX_MAP_DATA_SIZE 10240

#define NUM_TILES_PER_METATILE 8

// Map coordinates are offset by 7 when using the map
// buffer because it needs to load sufficient border
// metatiles to fill the player's view (the player has
// 7 metatiles of view horizontally in either direction).
#define MAP_OFFSET 7
#define MAP_OFFSET_W (MAP_OFFSET * 2 + 1)
#define MAP_OFFSET_H (MAP_OFFSET * 2)

// How far around the camera origin object events are spawned and kept alive,
// in metatiles. It has to reach as far as the viewport can see, or the people
// standing at its edges never come into existence; the two extra metatiles are
// the margin the original keeps so they appear before they are on screen. On
// 3DS the window follows what the top screen shows, the 2D field or the voxel
// camera, and the people nearest come first (3ds_field_view.h).
#if !defined(PLATFORM_3DS)
#define OBJECT_EVENT_VIEW_LEFT   2
#define OBJECT_EVENT_VIEW_RIGHT  (MAP_OFFSET_W + 2)
#define OBJECT_EVENT_VIEW_TOP    0
#define OBJECT_EVENT_VIEW_BOTTOM (MAP_OFFSET_H + 2)
#endif

#include "main.h"

extern struct BackupMapLayout gBackupMapLayout;

u32 MapGridGetMetatileIdAt(int x, int y);
// Port only: what the wide camera draws past the GBA's map padding.
u32 MapGridGetMetatileIdForCameraAt(int x, int y);
const u16 *MapGridGetCameraBorderAt(int x, int y, u8 *index);
u32 MapGridGetMetatileBehaviorAt(int x, int y);
void MapGridSetMetatileIdAt(int x, int y, u16 metatile);
void MapGridSetMetatileEntryAt(int x, int y, u16 metatile);
void GetCameraCoords(u16 *x, u16 *y);
u8 MapGridGetCollisionAt(int x, int y);
int GetMapBorderIdAt(int x, int y);
bool32 CanCameraMoveInDirection(int direction);
u16 GetMetatileAttributesById(u16 metatile);
void GetCameraFocusCoords(u16 *x, u16 *y);
u8 MapGridGetMetatileLayerTypeAt(int x, int y);
u8 MapGridGetElevationAt(int x, int y);
bool8 CameraMove(int x, int y);
void SaveMapView(void);
void SetCameraFocusCoords(u16 x, u16 y);
void InitMap(void);
void InitMapFromSavedGame(void);
void InitTrainerHillMap(void);
void InitBattlePyramidMap(bool8 setPlayerPosition);
void CopyMapTilesetsToVram(struct MapLayout const *mapLayout);
void LoadMapTilesetPalettes(struct MapLayout const *mapLayout);
void LoadSecondaryTilesetPalette(struct MapLayout const *mapLayout);
void CopySecondaryTilesetToVramUsingHeap(struct MapLayout const *mapLayout);
void CopyPrimaryTilesetToVram(struct MapLayout const *mapLayout);
void CopySecondaryTilesetToVram(struct MapLayout const *mapLayout);
const struct MapHeader *const GetMapHeaderFromConnection(const struct MapConnection *connection);
const struct MapConnection *GetMapConnectionAtPos(s16 x, s16 y);
void MapGridSetMetatileImpassabilityAt(int x, int y, bool32 impassable);

// field_region_map.c
void FieldInitRegionMap(MainCallback callback);

#endif //GUARD_FIELDMAP_H
