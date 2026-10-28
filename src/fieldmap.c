#include "global.h"
#include "battle_pyramid.h"
#include "bg.h"
#include "fieldmap.h"
#include "fldeff.h"
#include "fldeff_misc.h"
#include "frontier_util.h"
#include "menu.h"
#include "malloc.h"
#include "mirage_tower.h"
#include "overworld.h"
#include "palette.h"
#include "pokenav.h"
#include "script.h"
#include "secret_base.h"
#include "trainer_hill.h"
#include "tv.h"
#include "constants/rgb.h"
#include "constants/metatile_behaviors.h"

#ifdef PORT_BRIDGE
#include "port_log.h"
#include "port_platform.h"
#endif
#ifdef PORT_BRIDGE
#define PORT_PROF(label) Port_ProfileMark(label)
#else
#define PORT_PROF(label) ((void)0)
#endif

struct ConnectionFlags
{
    u8 south:1;
    u8 north:1;
    u8 west:1;
    u8 east:1;
};

EWRAM_DATA static u16 ALIGNED(4) sBackupMapData[MAX_MAP_DATA_SIZE] = {0};
EWRAM_DATA struct MapHeader gMapHeader = {0};
EWRAM_DATA struct Camera gCamera = {0};
EWRAM_DATA static struct ConnectionFlags sMapConnectionFlags = {0};
EWRAM_DATA static u32 UNUSED sFiller = 0; // without this, the next file won't align properly

COMMON_DATA struct BackupMapLayout gBackupMapLayout = {0};

static const struct ConnectionFlags sDummyConnectionFlags = {0};

static void InitMapLayoutData(struct MapHeader *mapHeader);
static void InitBackupMapLayoutData(const u16 *map, u16 width, u16 height);
static void FillSouthConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset);
static void FillNorthConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset);
static void FillWestConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset);
static void FillEastConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset);
static void InitBackupMapLayoutConnections(struct MapHeader *mapHeader);
static void LoadSavedMapView(void);
static bool8 SkipCopyingMetatileFromSavedMap(u16 *mapBlock, u16 mapWidth, u8 yMode);
static const struct MapConnection *GetIncomingConnection(u8 direction, int x, int y);
static bool8 IsPosInIncomingConnectingMap(u8 direction, int x, int y, const struct MapConnection *connection);
static bool8 IsCoordInIncomingConnectingMap(int coord, int srcMax, int destMax, int offset);

static inline u16 GetBorderBlockAt(int x, int y)
{
    int i;
    const u16 *border = gMapHeader.mapLayout->border;

#ifdef PORT_BRIDGE
    border = Port_ResolveAssetPointer(border);
#endif

    i = (x + 1) & 1;
    i += ((y + 1) & 1) * 2;

    return border[i] | MAPGRID_IMPASSABLE;
}

#define AreCoordsWithinMapGridBounds(x, y) (x >= 0 && x < gBackupMapLayout.width && y >= 0 && y < gBackupMapLayout.height)

#define GetMapGridBlockAt(x, y) (AreCoordsWithinMapGridBounds(x, y) ? gBackupMapLayout.map[x + gBackupMapLayout.width * y] : GetBorderBlockAt(x, y))

const struct MapHeader *const GetMapHeaderFromConnection(const struct MapConnection *connection)
{
    return Overworld_GetMapHeaderByGroupAndId(connection->mapGroup, connection->mapNum);
}

void InitMap(void)
{
    InitMapLayoutData(&gMapHeader);
    SetOccupiedSecretBaseEntranceMetatiles(gMapHeader.events);
    RunOnLoadMapScript();
}

void InitMapFromSavedGame(void)
{
    InitMapLayoutData(&gMapHeader);
    InitSecretBaseAppearance(FALSE);
    SetOccupiedSecretBaseEntranceMetatiles(gMapHeader.events);
    LoadSavedMapView();
    RunOnLoadMapScript();
    UpdateTVScreensOnMap(gBackupMapLayout.width, gBackupMapLayout.height);
}

void InitBattlePyramidMap(bool8 setPlayerPosition)
{
    CpuFastFill16(MAPGRID_UNDEFINED, sBackupMapData, sizeof(sBackupMapData));
    GenerateBattlePyramidFloorLayout(sBackupMapData, setPlayerPosition);
}

void InitTrainerHillMap(void)
{
    CpuFastFill16(MAPGRID_UNDEFINED, sBackupMapData, sizeof(sBackupMapData));
    GenerateTrainerHillFloorLayout(sBackupMapData);
}

static void InitMapLayoutData(struct MapHeader *mapHeader)
{
    struct MapLayout const *mapLayout;
    int width;
    int height;
    mapLayout = mapHeader->mapLayout;
    CpuFastFill16(MAPGRID_UNDEFINED, sBackupMapData, sizeof(sBackupMapData));
    gBackupMapLayout.map = sBackupMapData;
    width = mapLayout->width + MAP_OFFSET_W;
    gBackupMapLayout.width = width;
    height = mapLayout->height + MAP_OFFSET_H;
    gBackupMapLayout.height = height;
    if (width * height <= MAX_MAP_DATA_SIZE)
    {
        InitBackupMapLayoutData(mapLayout->map, mapLayout->width, mapLayout->height);
        InitBackupMapLayoutConnections(mapHeader);
    }
}

static void InitBackupMapLayoutData(const u16 *map, u16 width, u16 height)
{
    u16 *dest;
    int y;

#ifdef PORT_BRIDGE
    map = Port_ResolveAssetPointer(map);
#endif

    dest = gBackupMapLayout.map;
    dest += gBackupMapLayout.width * 7 + MAP_OFFSET;
    for (y = 0; y < height; y++)
    {
        CpuCopy16(map, dest, width * 2);
        dest += width + MAP_OFFSET_W;
        map += width;
    }
}

static void InitBackupMapLayoutConnections(struct MapHeader *mapHeader)
{
    int count;
    const struct MapConnection *connection;
    int i;

    if (mapHeader->connections)
    {
        count = mapHeader->connections->count;
        connection = mapHeader->connections->connections;
        sMapConnectionFlags = sDummyConnectionFlags;
        for (i = 0; i < count; i++, connection++)
        {
            struct MapHeader const *cMap = GetMapHeaderFromConnection(connection);
            s32 offset = connection->offset;
            switch (connection->direction)
            {
            case CONNECTION_SOUTH:
                FillSouthConnection(mapHeader, cMap, offset);
                sMapConnectionFlags.south = TRUE;
                break;
            case CONNECTION_NORTH:
                FillNorthConnection(mapHeader, cMap, offset);
                sMapConnectionFlags.north = TRUE;
                break;
            case CONNECTION_WEST:
                FillWestConnection(mapHeader, cMap, offset);
                sMapConnectionFlags.west = TRUE;
                break;
            case CONNECTION_EAST:
                FillEastConnection(mapHeader, cMap, offset);
                sMapConnectionFlags.east = TRUE;
                break;
            }
        }
    }
}

static void FillConnection(int x, int y, struct MapHeader const *connectedMapHeader, int x2, int y2, int width, int height)
{
    int i;
    const u16 *src;
    u16 *dest;
    int mapWidth;

    mapWidth = connectedMapHeader->mapLayout->width;
    src = connectedMapHeader->mapLayout->map;
#ifdef PORT_BRIDGE
    src = Port_ResolveAssetPointer(src);
#endif
    src += mapWidth * y2 + x2;
    dest = &gBackupMapLayout.map[gBackupMapLayout.width * y + x];

    for (i = 0; i < height; i++)
    {
        CpuCopy16(src, dest, width * 2);
        dest += gBackupMapLayout.width;
        src += mapWidth;
    }
}

static void FillSouthConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset)
{
    int x, y;
    int x2;
    int width;
    int cWidth;

    if (connectedMapHeader)
    {
        cWidth = connectedMapHeader->mapLayout->width;
        x = offset + MAP_OFFSET;
        y = mapHeader->mapLayout->height + MAP_OFFSET;
        if (x < 0)
        {
            x2 = -x;
            x += cWidth;
            if (x < gBackupMapLayout.width)
                width = x;
            else
                width = gBackupMapLayout.width;
            x = 0;
        }
        else
        {
            x2 = 0;
            if (x + cWidth < gBackupMapLayout.width)
                width = cWidth;
            else
                width = gBackupMapLayout.width - x;
        }

        FillConnection(
            x, y,
            connectedMapHeader,
            x2, /*y2*/ 0,
            width, /*height*/ MAP_OFFSET);
    }
}

static void FillNorthConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset)
{
    int x;
    int x2, y2;
    int width;
    int cWidth, cHeight;

    if (connectedMapHeader)
    {
        cWidth = connectedMapHeader->mapLayout->width;
        cHeight = connectedMapHeader->mapLayout->height;
        x = offset + MAP_OFFSET;
        y2 = cHeight - MAP_OFFSET;
        if (x < 0)
        {
            x2 = -x;
            x += cWidth;
            if (x < gBackupMapLayout.width)
                width = x;
            else
                width = gBackupMapLayout.width;
            x = 0;
        }
        else
        {
            x2 = 0;
            if (x + cWidth < gBackupMapLayout.width)
                width = cWidth;
            else
                width = gBackupMapLayout.width - x;
        }

        FillConnection(
            x, /*y*/ 0,
            connectedMapHeader,
            x2, y2,
            width, /*height*/ MAP_OFFSET);

    }
}

static void FillWestConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset)
{
    int y;
    int x2, y2;
    int height;
    int cWidth, cHeight;
    if (connectedMapHeader)
    {
        cWidth = connectedMapHeader->mapLayout->width;
        cHeight = connectedMapHeader->mapLayout->height;
        y = offset + MAP_OFFSET;
        x2 = cWidth - MAP_OFFSET;
        if (y < 0)
        {
            y2 = -y;
            if (y + cHeight < gBackupMapLayout.height)
                height = y + cHeight;
            else
                height = gBackupMapLayout.height;
            y = 0;
        }
        else
        {
            y2 = 0;
            if (y + cHeight < gBackupMapLayout.height)
                height = cHeight;
            else
                height = gBackupMapLayout.height - y;
        }

        FillConnection(
            /*x*/ 0, y,
            connectedMapHeader,
            x2, y2,
            /*width*/ MAP_OFFSET, height);
    }
}

static void FillEastConnection(struct MapHeader const *mapHeader, struct MapHeader const *connectedMapHeader, s32 offset)
{
    int x, y;
    int y2;
    int height;
    int cHeight;
    if (connectedMapHeader)
    {
        cHeight = connectedMapHeader->mapLayout->height;
        x = mapHeader->mapLayout->width + MAP_OFFSET;
        y = offset + MAP_OFFSET;
        if (y < 0)
        {
            y2 = -y;
            if (y + cHeight < gBackupMapLayout.height)
                height = y + cHeight;
            else
                height = gBackupMapLayout.height;
            y = 0;
        }
        else
        {
            y2 = 0;
            if (y + cHeight < gBackupMapLayout.height)
                height = cHeight;
            else
                height = gBackupMapLayout.height - y;
        }

        FillConnection(
            x, y,
            connectedMapHeader,
            /*x2*/ 0, y2,
            /*width*/ MAP_OFFSET + 1, height);
    }
}

u8 MapGridGetElevationAt(int x, int y)
{
    u16 block = GetMapGridBlockAt(x, y);

    if (block == MAPGRID_UNDEFINED)
        return 0;

    return UNPACK_ELEVATION(block);
}

u8 MapGridGetCollisionAt(int x, int y)
{
    u16 block = GetMapGridBlockAt(x, y);

    if (block == MAPGRID_UNDEFINED)
        return TRUE;

    return UNPACK_COLLISION(block);
}

#ifdef PORT_BRIDGE
// The centered port view reaches beyond the GBA collision/save-map padding,
// and the 3D view further still. What it shows there is visual only: keep
// MAP_OFFSET and the saved mapView layout unchanged.
//
// The maps that can be seen are the current map, its connections and theirs,
// placed as voxel_world.c places them. Where none of them reaches, the GBA
// tiles the current map's 2x2 border block for ever. Its 15 x 10 screen never
// looked far enough past a corner for that to matter; this one does, and the
// ground past a corner - outside both the map and its neighbour - changed
// drawing the moment the player crossed, because the border followed
// whichever map was current (Mauville's water against Route 117's trees). So
// each such cell takes the border of the nearest map instead: the one whose
// edge it lies against, whichever map the player stands on.
//
// Only a border the current tilesets can draw is a candidate. Every border of
// the overworld is in the shared primary tileset except a handful (Dewford,
// Fallarbor, Lavaridge and the routes that share their tilesets); next to a
// map with other tilesets those fall back to the next nearest map, ultimately
// the current one, as before.
#define CAMERA_MAPS_MAX 16

struct CameraMap
{
    const struct MapHeader *header;
    u16 id;          // mapGroup << 8 | mapNum: the stable tie-break
    s16 x, y;        // origin, in metatiles of the current map
    s16 phaseX, phaseY; // origin its 2x2 border is phased on
    bool8 drawn;     // its ground is drawn here
    bool8 border;    // its border is drawable with the current tilesets
};

static struct CameraMap sCameraMaps[CAMERA_MAPS_MAX];
static u8 sCameraMapCount;
static const struct MapLayout *sCameraMapsLayout;
static const struct MapConnections *sCameraMapsConnections;
static u16 sCameraMapsId;

static bool8 CameraTilesetsMatch(const struct MapLayout *layout)
{
    return layout->primaryTileset == gMapHeader.mapLayout->primaryTileset
        && layout->secondaryTileset == gMapHeader.mapLayout->secondaryTileset;
}

static bool8 CameraBorderDrawable(const struct MapLayout *layout)
{
    const u16 *border;
    int i;

    if (layout->border == NULL || layout->primaryTileset != gMapHeader.mapLayout->primaryTileset)
        return FALSE;
    if (layout->secondaryTileset == gMapHeader.mapLayout->secondaryTileset)
        return TRUE;
    border = Port_ResolveAssetPointer(layout->border);
    if (border == NULL)
        return FALSE;
    for (i = 0; i < 4; i++)
        if (UNPACK_METATILE(border[i]) >= NUM_METATILES_IN_PRIMARY)
            return FALSE;
    return TRUE;
}

static void PlaceCameraMap(const struct MapHeader *header, u16 id, int x, int y, bool8 near)
{
    struct CameraMap *map = &sCameraMaps[sCameraMapCount++];

    map->header = header;
    map->id = id;
    map->x = map->phaseX = x;
    map->y = map->phaseY = y;
    map->drawn = near || CameraTilesetsMatch(header->mapLayout);
    map->border = CameraBorderDrawable(header->mapLayout);
}

// The maps across the side connections of sCameraMaps[from]. The current map's
// own are taken as they are; one further away (far) only where it is not a
// map already placed and covers none of their ground, as in voxel_world.c.
static void AddCameraConnections(int from, bool8 far)
{
    const struct MapHeader *header = sCameraMaps[from].header;
    const struct MapConnection *connection;
    int i, k;

    if (header->connections == NULL)
        return;
    connection = header->connections->connections;
    for (i = 0; i < header->connections->count; i++, connection++)
    {
        const struct CameraMap *source = &sCameraMaps[from];
        const struct MapHeader *neighbour;
        const struct MapLayout *layout;
        u16 id = (connection->mapGroup << 8) | connection->mapNum;
        int x, y;
        bool8 clash = FALSE;

        if (sCameraMapCount >= CAMERA_MAPS_MAX)
            return;
        neighbour = GetMapHeaderFromConnection(connection);
        if (neighbour == NULL || neighbour->mapLayout == NULL)
            continue;
        layout = neighbour->mapLayout;
        switch (connection->direction)
        {
        case CONNECTION_NORTH:
            x = source->x + connection->offset;
            y = source->y - layout->height;
            break;
        case CONNECTION_SOUTH:
            x = source->x + connection->offset;
            y = source->y + header->mapLayout->height;
            break;
        case CONNECTION_WEST:
            x = source->x - layout->width;
            y = source->y + connection->offset;
            break;
        case CONNECTION_EAST:
            x = source->x + header->mapLayout->width;
            y = source->y + connection->offset;
            break;
        default:
            continue;
        }
        for (k = 0; far && k < sCameraMapCount && !clash; k++)
        {
            const struct CameraMap *placed = &sCameraMaps[k];
            const struct MapLayout *other = placed->header->mapLayout;

            clash = placed->id == id
                 || (x < placed->x + other->width && x + layout->width > placed->x
                  && y < placed->y + other->height && y + layout->height > placed->y);
        }
        if (!clash)
            PlaceCameraMap(neighbour, id, x, y, !far);
    }
}

static void UpdateCameraMaps(void)
{
    u16 id = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;
    int near, i;

    if (sCameraMapCount != 0 && sCameraMapsLayout == gMapHeader.mapLayout
     && sCameraMapsConnections == gMapHeader.connections && sCameraMapsId == id)
        return;
    sCameraMapsLayout = gMapHeader.mapLayout;
    sCameraMapsConnections = gMapHeader.connections;
    sCameraMapsId = id;
    sCameraMapCount = 0;
    PlaceCameraMap(&gMapHeader, id, 0, 0, TRUE);
    AddCameraConnections(0, FALSE);
    near = sCameraMapCount;
    for (i = 1; i < near; i++)
        AddCameraConnections(i, TRUE);

    // Most of Hoenn shares one border block, and a map phases it on its own
    // origin: two neighbours an odd number of tiles apart would show the
    // pattern break where one's border gives way to the other's. Maps with
    // the same block take the phase of the lowest id among them instead.
    for (i = 0; i < sCameraMapCount; i++)
    {
        const u16 *border = Port_ResolveAssetPointer(sCameraMaps[i].header->mapLayout->border);
        u16 anchor = sCameraMaps[i].id;
        int k;

        if (border == NULL)
            continue;
        for (k = 0; k < sCameraMapCount; k++)
        {
            const u16 *other = Port_ResolveAssetPointer(sCameraMaps[k].header->mapLayout->border);

            if (sCameraMaps[k].id < anchor && other != NULL
             && memcmp(border, other, 4 * sizeof(u16)) == 0)
            {
                anchor = sCameraMaps[k].id;
                sCameraMaps[i].phaseX = sCameraMaps[k].x;
                sCameraMaps[i].phaseY = sCameraMaps[k].y;
            }
        }
    }
}

// The border at x, y (metatiles of the current map): that of the nearest map
// whose border can be drawn, as the layout's unresolved pointer, and which of
// its 2x2 blocks. Distance is to the map's rectangle; a tie goes to the lower
// map id, never to the current map, so the answer is the same from either side
// of a crossing. The 2x2 is phased on that map's own origin, as
// GetBorderBlockAt phases it when that map is the current one (or on the map
// that shares its block, above).
const u16 *MapGridGetCameraBorderAt(int x, int y, u8 *index)
{
    const struct CameraMap *best = NULL;
    u32 bestDistance = 0;
    int i;

    UpdateCameraMaps();
    for (i = 0; i < sCameraMapCount; i++)
    {
        const struct CameraMap *map = &sCameraMaps[i];
        const struct MapLayout *layout = map->header->mapLayout;
        int dx = 0, dy = 0;
        u32 distance;

        if (!map->border)
            continue;
        if (x < map->x)
            dx = map->x - x;
        else if (x >= map->x + layout->width)
            dx = x - (map->x + layout->width - 1);
        if (y < map->y)
            dy = map->y - y;
        else if (y >= map->y + layout->height)
            dy = y - (map->y + layout->height - 1);
        distance = dx * dx + dy * dy;
        if (best == NULL || distance < bestDistance
         || (distance == bestDistance && map->id < best->id))
        {
            best = map;
            bestDistance = distance;
        }
    }
    if (best == NULL)
        best = &sCameraMaps[0];
    x += MAP_OFFSET - best->phaseX;
    y += MAP_OFFSET - best->phaseY;
    *index = ((x + 1) & 1) + ((y + 1) & 1) * 2;
    return best->header->mapLayout->border;
}

static u16 GetNearestBorderBlockAt(int x, int y)
{
    u8 index;
    const u16 *border = Port_ResolveAssetPointer(MapGridGetCameraBorderAt(x, y, &index));

    return border[index] | MAPGRID_IMPASSABLE;
}

// x, y in backup-layout coordinates, outside the ground the backup holds.
static u16 GetCameraConnectionBlockAt(int x, int y)
{
    int i;

    x -= MAP_OFFSET;
    y -= MAP_OFFSET;
    UpdateCameraMaps();
    for (i = 1; i < sCameraMapCount; i++)
    {
        const struct CameraMap *map = &sCameraMaps[i];
        const struct MapLayout *layout = map->header->mapLayout;
        int mapX = x - map->x;
        int mapY = y - map->y;

        if (map->drawn && mapX >= 0 && mapX < layout->width && mapY >= 0 && mapY < layout->height)
        {
            const u16 *blocks = Port_ResolveAssetPointer(layout->map);
            return blocks[mapY * layout->width + mapX];
        }
    }
    return GetNearestBorderBlockAt(x, y);
}

// What the camera draws at x, y (backup-layout coordinates). Unlike
// MapGridGetMetatileIdAt, the cells the GBA leaves undefined in the backup
// (past a corner, within MAP_OFFSET) follow the rule above too; nothing but
// drawing reads this, so the game's own lookups are left as they were.
u32 MapGridGetMetatileIdForCameraAt(int x, int y)
{
    u16 block = AreCoordsWithinMapGridBounds(x, y)
        ? gBackupMapLayout.map[x + gBackupMapLayout.width * y]
        : MAPGRID_UNDEFINED;

    if (block == MAPGRID_UNDEFINED)
        block = GetCameraConnectionBlockAt(x, y);
    return UNPACK_METATILE(block);
}

#endif

u32 MapGridGetMetatileIdAt(int x, int y)
{
#ifdef PORT_BRIDGE
    u16 block = AreCoordsWithinMapGridBounds(x, y)
        ? gBackupMapLayout.map[x + gBackupMapLayout.width * y]
        : GetCameraConnectionBlockAt(x, y);
#else
    u16 block = GetMapGridBlockAt(x, y);
#endif

#ifdef PORT_BRIDGE
    Port_SetAssetDebugEx(0x0330,
                        x,
                        y,
                        gBackupMapLayout.width,
                        gBackupMapLayout.height);
    Port_SetAssetDebugEx(0x0331,
                        block,
                        (u16)((uintptr_t)gBackupMapLayout.map >> 16),
                        (u16)(uintptr_t)gBackupMapLayout.map,
                        0); /* was CheckHeap(): a walk of the whole heap on every lookup, for an argument the port drops */
#endif

    if (block == MAPGRID_UNDEFINED)
        return UNPACK_METATILE(GetBorderBlockAt(x, y));

    return UNPACK_METATILE(block);
}

u32 MapGridGetMetatileBehaviorAt(int x, int y)
{
    u16 metatile = MapGridGetMetatileIdAt(x, y);
#ifdef PORT_BRIDGE
    Port_SetAssetDebugEx(0x0332, metatile, x, y, 0);
#endif
    return UNPACK_BEHAVIOR(GetMetatileAttributesById(metatile));
}

u8 MapGridGetMetatileLayerTypeAt(int x, int y)
{
    u16 metatile = MapGridGetMetatileIdAt(x, y);
    return UNPACK_LAYER_TYPE(GetMetatileAttributesById(metatile));
}

void MapGridSetMetatileIdAt(int x, int y, u16 metatile)
{
    int i;
    if (AreCoordsWithinMapGridBounds(x, y))
    {
        i = x + y * gBackupMapLayout.width;

        // Elevation is ignored in the argument, but copy metatile ID and collision
        gBackupMapLayout.map[i] = (gBackupMapLayout.map[i] & MAPGRID_ELEVATION_MASK) | (metatile & ~MAPGRID_ELEVATION_MASK);
    }
}

void MapGridSetMetatileEntryAt(int x, int y, u16 metatile)
{
    int i;
    if (AreCoordsWithinMapGridBounds(x, y))
    {
        i = x + gBackupMapLayout.width * y;
        gBackupMapLayout.map[i] = metatile;
    }
}

u16 GetMetatileAttributesById(u16 metatile)
{
    const u16 *attributes;
    if (metatile < NUM_METATILES_IN_PRIMARY)
    {
        attributes = gMapHeader.mapLayout->primaryTileset->metatileAttributes;
#ifdef PORT_BRIDGE
        Port_SetAssetDebugEx(0x0333,
                            metatile,
                            0,
                            (u16)((uintptr_t)attributes >> 16),
                            (u16)(uintptr_t)attributes);
        attributes = Port_ResolveAssetPointer(attributes);
        Port_SetAssetDebugEx(0x0334,
                            metatile,
                            0,
                            (u16)((uintptr_t)attributes >> 16),
                            (u16)(uintptr_t)attributes);
#endif
#ifdef PORT_BRIDGE
        Port_SetAssetDebugEx(0x0335, metatile, attributes[metatile], 0, 0);
#endif
        return attributes[metatile];
    }
    else if (metatile < NUM_METATILES_TOTAL)
    {
        attributes = gMapHeader.mapLayout->secondaryTileset->metatileAttributes;
#ifdef PORT_BRIDGE
        Port_SetAssetDebugEx(0x0336,
                            metatile,
                            1,
                            (u16)((uintptr_t)attributes >> 16),
                            (u16)(uintptr_t)attributes);
        attributes = Port_ResolveAssetPointer(attributes);
        Port_SetAssetDebugEx(0x0337,
                            metatile,
                            1,
                            (u16)((uintptr_t)attributes >> 16),
                            (u16)(uintptr_t)attributes);
#endif
#ifdef PORT_BRIDGE
        Port_SetAssetDebugEx(0x0338,
                            metatile,
                            attributes[metatile - NUM_METATILES_IN_PRIMARY],
                            metatile - NUM_METATILES_IN_PRIMARY,
                            0);
#endif
        return attributes[metatile - NUM_METATILES_IN_PRIMARY];
    }
    else
    {
        return MB_INVALID;
    }
}

void SaveMapView(void)
{
    int i, j;
    int x, y;
    u16 *mapView;
    int width;
    mapView = gSaveBlock1Ptr->mapView;
    width = gBackupMapLayout.width;
    x = gSaveBlock1Ptr->pos.x;
    y = gSaveBlock1Ptr->pos.y;
    for (i = y; i < y + MAP_OFFSET_H; i++)
    {
        for (j = x; j < x + MAP_OFFSET_W; j++)
            *mapView++ = sBackupMapData[width * i + j];
    }
}

static bool32 SavedMapViewIsEmpty(void)
{
    u16 i;
    u32 marker = 0;

#ifndef UBFIX
    // BUG: This loop extends past the bounds of the mapView array. Its size is only 0x100.
    for (i = 0; i < 0x200; i++)
        marker |= gSaveBlock1Ptr->mapView[i];
#else
    // UBFIX: Only iterate over 0x100
    for (i = 0; i < ARRAY_COUNT(gSaveBlock1Ptr->mapView); i++)
        marker |= gSaveBlock1Ptr->mapView[i];
#endif


    if (marker == 0)
        return TRUE;
    else
        return FALSE;
}

static void ClearSavedMapView(void)
{
    CpuFill16(0, gSaveBlock1Ptr->mapView, sizeof(gSaveBlock1Ptr->mapView));
}

static void LoadSavedMapView(void)
{
    u8 yMode;
    int i, j;
    int x, y;
    u16 *mapView;
    int width;
    mapView = gSaveBlock1Ptr->mapView;
    if (!SavedMapViewIsEmpty())
    {
        width = gBackupMapLayout.width;
        x = gSaveBlock1Ptr->pos.x;
        y = gSaveBlock1Ptr->pos.y;
        for (i = y; i < y + MAP_OFFSET_H; i++)
        {
            if (i == y && i != 0)
                yMode = 0;
            else if (i == y + MAP_OFFSET_H - 1 && i != gMapHeader.mapLayout->height - 1)
                yMode = 1;
            else
                yMode = 0xFF;

            for (j = x; j < x + MAP_OFFSET_W; j++)
            {
                if (!SkipCopyingMetatileFromSavedMap(&sBackupMapData[j + width * i], width, yMode))
                    sBackupMapData[j + width * i] = *mapView;
                mapView++;
            }
        }
        for (j = x; j < x + MAP_OFFSET_W; j++)
        {
            if (y != 0)
                FixLongGrassMetatilesWindowTop(j, y - 1);
            if (i < gMapHeader.mapLayout->height - 1)
                FixLongGrassMetatilesWindowBottom(j, y + MAP_OFFSET_H - 1);
        }
        ClearSavedMapView();
    }
}

static void MoveMapViewToBackup(u8 direction)
{
    int width;
    u16 *mapView;
    int x0, y0;
    int x2, y2;
    u16 *src, *dest;
    int srci, desti;
    int r9, r8;
    int x, y;
    int i, j;
    mapView = gSaveBlock1Ptr->mapView;
    width = gBackupMapLayout.width;
    r9 = 0;
    r8 = 0;
    x0 = gSaveBlock1Ptr->pos.x;
    y0 = gSaveBlock1Ptr->pos.y;
    x2 = MAP_OFFSET_W;
    y2 = MAP_OFFSET_H;
    switch (direction)
    {
    case CONNECTION_NORTH:
        y0 += 1;
        y2 = MAP_OFFSET_H - 1;
        break;
    case CONNECTION_SOUTH:
        r8 = 1;
        y2 = MAP_OFFSET_H - 1;
        break;
    case CONNECTION_WEST:
        x0 += 1;
        x2 = MAP_OFFSET_W - 1;
        break;
    case CONNECTION_EAST:
        r9 = 1;
        x2 = MAP_OFFSET_W - 1;
        break;
    }
    for (y = 0; y < y2; y++)
    {
        i = 0;
        j = 0;
        for (x = 0; x < x2; x++)
        {
            desti = width * (y + y0);
            srci = (y + r8) * MAP_OFFSET_W + r9;
            src = &mapView[srci + i];
            dest = &sBackupMapData[x0 + desti + j];
            *dest = *src;
            i++;
            j++;
        }
    }
    ClearSavedMapView();
}

int GetMapBorderIdAt(int x, int y)
{
    if (GetMapGridBlockAt(x, y) == MAPGRID_UNDEFINED)
        return CONNECTION_INVALID;

    if (x >= (gBackupMapLayout.width - (MAP_OFFSET + 1)))
    {
        if (!sMapConnectionFlags.east)
            return CONNECTION_INVALID;

        return CONNECTION_EAST;
    }
    else if (x < MAP_OFFSET)
    {
        if (!sMapConnectionFlags.west)
            return CONNECTION_INVALID;

        return CONNECTION_WEST;
    }
    else if (y >= (gBackupMapLayout.height - MAP_OFFSET))
    {
        if (!sMapConnectionFlags.south)
            return CONNECTION_INVALID;

        return CONNECTION_SOUTH;
    }
    else if (y < MAP_OFFSET)
    {
        if (!sMapConnectionFlags.north)
            return CONNECTION_INVALID;

        return CONNECTION_NORTH;
    }
    else
    {
        return CONNECTION_NONE;
    }
}

int GetPostCameraMoveMapBorderId(int x, int y)
{
    return GetMapBorderIdAt(gSaveBlock1Ptr->pos.x + MAP_OFFSET + x, gSaveBlock1Ptr->pos.y + MAP_OFFSET + y);
}

bool32 CanCameraMoveInDirection(int direction)
{
    int x, y;
    x = gSaveBlock1Ptr->pos.x + MAP_OFFSET + gDirectionToVectors[direction].x;
    y = gSaveBlock1Ptr->pos.y + MAP_OFFSET + gDirectionToVectors[direction].y;

    if (GetMapBorderIdAt(x, y) == CONNECTION_INVALID)
        return FALSE;

    return TRUE;
}

static void SetPositionFromConnection(const struct MapConnection *connection, int direction, int x, int y)
{
    struct MapHeader const *mapHeader;
    mapHeader = GetMapHeaderFromConnection(connection);
    switch (direction)
    {
    case CONNECTION_EAST:
        gSaveBlock1Ptr->pos.x = -x;
        gSaveBlock1Ptr->pos.y -= connection->offset;
        break;
    case CONNECTION_WEST:
        gSaveBlock1Ptr->pos.x = mapHeader->mapLayout->width;
        gSaveBlock1Ptr->pos.y -= connection->offset;
        break;
    case CONNECTION_SOUTH:
        gSaveBlock1Ptr->pos.x -= connection->offset;
        gSaveBlock1Ptr->pos.y = -y;
        break;
    case CONNECTION_NORTH:
        gSaveBlock1Ptr->pos.x -= connection->offset;
        gSaveBlock1Ptr->pos.y = mapHeader->mapLayout->height;
        break;
    }
}

bool8 CameraMove(int x, int y)
{
    int direction;
    const struct MapConnection *connection;
    int old_x, old_y;
    gCamera.active = FALSE;
    direction = GetPostCameraMoveMapBorderId(x, y);
    if (direction == CONNECTION_NONE || direction == CONNECTION_INVALID)
    {
        gSaveBlock1Ptr->pos.x += x;
        gSaveBlock1Ptr->pos.y += y;
    }
    else
    {
        PORT_PROF("camera-begin");
        SaveMapView();
        PORT_PROF("SaveMapView");
        ClearMirageTowerPulseBlendEffect();
        old_x = gSaveBlock1Ptr->pos.x;
        old_y = gSaveBlock1Ptr->pos.y;
        connection = GetIncomingConnection(direction, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
        SetPositionFromConnection(connection, direction, x, y);
        LoadMapFromCameraTransition(connection->mapGroup, connection->mapNum);
        PORT_PROF("LoadMapFromCameraTransition");
        gCamera.active = TRUE;
        gCamera.x = old_x - gSaveBlock1Ptr->pos.x;
        gCamera.y = old_y - gSaveBlock1Ptr->pos.y;
        gSaveBlock1Ptr->pos.x += x;
        gSaveBlock1Ptr->pos.y += y;
        MoveMapViewToBackup(direction);
        PORT_PROF("MoveMapViewToBackup");
    }
    return gCamera.active;
}

static const struct MapConnection *GetIncomingConnection(u8 direction, int x, int y)
{
    int count;
    int i;
    const struct MapConnection *connection;
    const struct MapConnections *connections = gMapHeader.connections;

#ifdef UBFIX // UB: Multiple possible null dereferences
    if (connections == NULL || connections->connections == NULL)
        return NULL;
#endif
    count = connections->count;
    connection = connections->connections;
    for (i = 0; i < count; i++, connection++)
    {
        if (connection->direction == direction && IsPosInIncomingConnectingMap(direction, x, y, connection) == TRUE)
            return connection;
    }
    return NULL;
}

static bool8 IsPosInIncomingConnectingMap(u8 direction, int x, int y, const struct MapConnection *connection)
{
    struct MapHeader const *mapHeader;
    mapHeader = GetMapHeaderFromConnection(connection);
    switch (direction)
    {
    case CONNECTION_SOUTH:
    case CONNECTION_NORTH:
        return IsCoordInIncomingConnectingMap(x, gMapHeader.mapLayout->width, mapHeader->mapLayout->width, connection->offset);
    case CONNECTION_WEST:
    case CONNECTION_EAST:
        return IsCoordInIncomingConnectingMap(y, gMapHeader.mapLayout->height, mapHeader->mapLayout->height, connection->offset);
    }
    return FALSE;
}

static bool8 IsCoordInIncomingConnectingMap(int coord, int srcMax, int destMax, int offset)
{
    int offset2;
    offset2 = offset;

    if (offset2 < 0)
        offset2 = 0;

    if (destMax + offset < srcMax)
        srcMax = destMax + offset;

    if (offset2 <= coord && coord <= srcMax)
        return TRUE;

    return FALSE;
}

static int IsCoordInConnectingMap(int coord, int max)
{
    if (coord >= 0 && coord < max)
        return TRUE;

    return FALSE;
}

static int IsPosInConnectingMap(const struct MapConnection *connection, int x, int y)
{
    struct MapHeader const *mapHeader;
    mapHeader = GetMapHeaderFromConnection(connection);
    switch (connection->direction)
    {
    case CONNECTION_SOUTH:
    case CONNECTION_NORTH:
        return IsCoordInConnectingMap(x - connection->offset, mapHeader->mapLayout->width);
    case CONNECTION_WEST:
    case CONNECTION_EAST:
        return IsCoordInConnectingMap(y - connection->offset, mapHeader->mapLayout->height);
    }
    return FALSE;
}

const struct MapConnection *GetMapConnectionAtPos(s16 x, s16 y)
{
    int count;
    const struct MapConnection *connection;
    int i;
    u8 direction;
    if (!gMapHeader.connections)
    {
        return NULL;
    }
    else
    {
        count = gMapHeader.connections->count;
        connection = gMapHeader.connections->connections;
        for (i = 0; i < count; i++, connection++)
        {
            direction = connection->direction;
            if ((direction == CONNECTION_DIVE || direction == CONNECTION_EMERGE)
             || (direction == CONNECTION_NORTH && y > MAP_OFFSET - 1)
             || (direction == CONNECTION_SOUTH && y < gMapHeader.mapLayout->height + MAP_OFFSET)
             || (direction == CONNECTION_WEST && x > MAP_OFFSET - 1)
             || (direction == CONNECTION_EAST && x < gMapHeader.mapLayout->width + MAP_OFFSET))
            {
                continue;
            }
            if (IsPosInConnectingMap(connection, x - MAP_OFFSET, y - MAP_OFFSET) == TRUE)
            {
                return connection;
            }
        }
    }
    return NULL;
}

void SetCameraFocusCoords(u16 x, u16 y)
{
    gSaveBlock1Ptr->pos.x = x - MAP_OFFSET;
    gSaveBlock1Ptr->pos.y = y - MAP_OFFSET;
}

void GetCameraFocusCoords(u16 *x, u16 *y)
{
    *x = gSaveBlock1Ptr->pos.x + MAP_OFFSET;
    *y = gSaveBlock1Ptr->pos.y + MAP_OFFSET;
}

static void UNUSED SetCameraCoords(u16 x, u16 y)
{
    gSaveBlock1Ptr->pos.x = x;
    gSaveBlock1Ptr->pos.y = y;
}

void GetCameraCoords(u16 *x, u16 *y)
{
    *x = gSaveBlock1Ptr->pos.x;
    *y = gSaveBlock1Ptr->pos.y;
}

void MapGridSetMetatileImpassabilityAt(int x, int y, bool32 impassable)
{
    if (AreCoordsWithinMapGridBounds(x, y))
    {
        if (impassable)
            gBackupMapLayout.map[x + gBackupMapLayout.width * y] |= MAPGRID_COLLISION_MASK;
        else
            gBackupMapLayout.map[x + gBackupMapLayout.width * y] &= ~MAPGRID_COLLISION_MASK;
    }
}

static bool8 SkipCopyingMetatileFromSavedMap(u16 *mapBlock, u16 mapWidth, u8 yMode)
{
    if (yMode == 0xFF)
        return FALSE;

    if (yMode == 0)
        mapBlock -= mapWidth;
    else
        mapBlock += mapWidth;

    if (IsLargeBreakableDecoration(UNPACK_METATILE(*mapBlock), yMode) == TRUE)
        return TRUE;
    return FALSE;
}

static void CopyTilesetToVram(struct Tileset const *tileset, u16 numTiles, u16 offset)
{
    if (tileset)
    {
        if (!tileset->isCompressed)
            LoadBgTiles(2, tileset->tiles, numTiles * 32, offset);
        else
            DecompressAndCopyTileDataToVram(2, tileset->tiles, numTiles * 32, offset, 0);
    }
}

static void CopyTilesetToVramUsingHeap(struct Tileset const *tileset, u16 numTiles, u16 offset)
{
    if (tileset)
    {
#ifdef PORT_BRIDGE
        const void *tiles = tileset->tiles;

        if (!tileset->isCompressed)
            tiles = Port_ResolveAssetPointer(tiles);

        if (!tileset->isCompressed)
            LoadBgTiles(2, tiles, numTiles * 32, offset);
        else
            DecompressAndLoadBgGfxUsingHeap(2, tiles, numTiles * 32, offset, 0);
#else
        if (!tileset->isCompressed)
            LoadBgTiles(2, tileset->tiles, numTiles * 32, offset);
        else
            DecompressAndLoadBgGfxUsingHeap(2, tileset->tiles, numTiles * 32, offset, 0);
#endif
    }
}

// Below two are dummied functions from FRLG, used to tint the overworld palettes for the Quest Log
static void ApplyGlobalTintToPaletteEntries(u16 offset, u16 size)
{

}

static void UNUSED ApplyGlobalTintToPaletteSlot(u8 slot, u8 count)
{

}

static void LoadTilesetPalette(struct Tileset const *tileset, u16 destOffset, u16 size)
{
    u16 black = RGB_BLACK;

    if (tileset)
    {
#ifdef PORT_BRIDGE
        u16 paletteData[NUM_PALS_TOTAL][16];
        const u16 (*palettes)[16] = tileset->palettes;
        static u8 sLoggedPalettes;
        u8 logMask = tileset->isSecondary ? 2 : 1;

        if (tileset->isSecondary <= TRUE && Port_GetAssetSizeExact(palettes) != 0)
        {
            u32 sourceOffset = tileset->isSecondary ? NUM_PALS_IN_PRIMARY * PLTT_SIZE_4BPP : 0;
            if (!Port_LoadAssetRangeToBuffer(palettes, sourceOffset,
                                           (u8 *)paletteData + sourceOffset, size))
                return;
            palettes = paletteData;
        }

        if (!(sLoggedPalettes & logMask))
        {
            u8 nonzero = 0;

            for (u32 i = 0; i < size; i++)
                nonzero |= ((const u8 *)palettes)[i];
            Port_SetGfxPaletteDebug(tileset->isSecondary, nonzero,
                                   palettes != tileset->palettes);
            PORT_LOG("Palette %c src=%08lX dst=%08lX nz=%02X\n",
                    tileset->isSecondary ? 'S' : 'P',
                    (unsigned long)tileset->palettes,
                    (unsigned long)palettes, nonzero);
            sLoggedPalettes |= logMask;
        }
#else
        const u16 (*palettes)[16] = tileset->palettes;
#endif

        if (tileset->isSecondary == FALSE)
        {
            LoadPalette(&black, destOffset, PLTT_SIZEOF(1));
            LoadPalette(palettes[0] + 1, destOffset + 1, size - PLTT_SIZEOF(1));
            ApplyGlobalTintToPaletteEntries(destOffset + 1, (size - PLTT_SIZEOF(1)) >> 1);
        }
        else if (tileset->isSecondary == TRUE)
        {
            LoadPalette(palettes[NUM_PALS_IN_PRIMARY], destOffset, size);
            ApplyGlobalTintToPaletteEntries(destOffset, size >> 1);
        }
        else
        {
            LoadCompressedPalette((const u32 *)palettes, destOffset, size);
            ApplyGlobalTintToPaletteEntries(destOffset, size >> 1);
        }
    }
}

void CopyPrimaryTilesetToVram(struct MapLayout const *mapLayout)
{
    CopyTilesetToVram(mapLayout->primaryTileset, NUM_TILES_IN_PRIMARY, 0);
}

void CopySecondaryTilesetToVram(struct MapLayout const *mapLayout)
{
    CopyTilesetToVram(mapLayout->secondaryTileset, NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY, NUM_TILES_IN_PRIMARY);
}

void CopySecondaryTilesetToVramUsingHeap(struct MapLayout const *mapLayout)
{
    CopyTilesetToVramUsingHeap(mapLayout->secondaryTileset, NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY, NUM_TILES_IN_PRIMARY);
}

static void LoadPrimaryTilesetPalette(struct MapLayout const *mapLayout)
{
    LoadTilesetPalette(mapLayout->primaryTileset, BG_PLTT_ID(0), NUM_PALS_IN_PRIMARY * PLTT_SIZE_4BPP);
}

void LoadSecondaryTilesetPalette(struct MapLayout const *mapLayout)
{
    LoadTilesetPalette(mapLayout->secondaryTileset, BG_PLTT_ID(NUM_PALS_IN_PRIMARY), (NUM_PALS_TOTAL - NUM_PALS_IN_PRIMARY) * PLTT_SIZE_4BPP);
}

void CopyMapTilesetsToVram(struct MapLayout const *mapLayout)
{
    if (mapLayout)
    {
#ifdef PORT_BRIDGE
        // Returning from a UI or battle must use the same buffer ownership as
        // the initial map load. Publish both tilesets before drawing the map.
        CopyPrimaryTilesetToVram(mapLayout);
        // Release the primary temporary before allocating the secondary one.
        while (FreeTempTileDataBuffersIfPossible())
            ;
        CopySecondaryTilesetToVram(mapLayout);
        while (FreeTempTileDataBuffersIfPossible())
            ;
#else
        CopyTilesetToVramUsingHeap(mapLayout->primaryTileset, NUM_TILES_IN_PRIMARY, 0);
        CopyTilesetToVramUsingHeap(mapLayout->secondaryTileset, NUM_TILES_TOTAL - NUM_TILES_IN_PRIMARY, NUM_TILES_IN_PRIMARY);
#endif
    }
}

void LoadMapTilesetPalettes(struct MapLayout const *mapLayout)
{
    if (mapLayout)
    {
        LoadPrimaryTilesetPalette(mapLayout);
        LoadSecondaryTilesetPalette(mapLayout);
    }
}
