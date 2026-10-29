#ifndef GUARD_FOLLOWER_MON_H
#define GUARD_FOLLOWER_MON_H

#ifdef PLATFORM_3DS
/*
 * Port: the party's lead Pokemon walking behind the player (OPTIONS >
 * ENHANCEMENTS > FOLLOWER), drawn with HeartGold/SoulSilver's overworld
 * sprites that the builder puts in the data pack (followers/mon/KEY.bin,
 * builder/emerald3ds_builder/followers.py). It is an object event of its own
 * graphics id and local id, never saved: SaveObjectEvents leaves it out and
 * the field brings it back.
 */
#define OBJ_EVENT_GFX_FOLLOWER NUM_OBJ_EVENT_GFX   /* the one id no object uses */
#define LOCALID_FOLLOWER       254

struct ObjectEvent;
struct ObjectEventGraphicsInfo;
struct Sprite;

void FollowerMon_Update(void);
const struct ObjectEventGraphicsInfo *FollowerMon_GraphicsInfo(void);
void FollowerMon_BindSprite(struct Sprite *sprite);
bool8 FollowerMon_IsFollower(const struct ObjectEvent *objectEvent);
const u8 *FollowerMon_TalkScript(void);
void FollowerMon_CameraShift(s16 dx, s16 dy);
/* A door warp (field_screen_effect.c): it walks in behind the player. */
void FollowerMon_DoorBegin(void);
bool8 FollowerMon_DoorDone(void);
/* The visible wild Pokemon (wild_encounter.c) on the same sprites, a slot each:
 * MAX_SPRITES when there is none for it (they keep their icons then). */
u8 FollowerMon_WildSprite(u32 slot, u16 species, u32 personality, bool8 shiny);
/* A species of the map's tables: its sprite files read ahead, off the frame. */
void FollowerMon_WildPrefetch(u16 species);
void FollowerMon_WildFace(struct Sprite *sprite, s8 dx, s8 dy, bool8 walking);
void FollowerMon_WildFree(u32 slot, struct Sprite *sprite);
/* No palette fade running: an ext palette loaded now keeps its colours. */
bool8 FollowerMon_PalettesSettled(void);
/* The soft reset (3ds_game_full.c): every file it holds freed, before the
 * game's variables go back to their boot values. */
void FollowerMon_ReleaseAll(void);
#endif

#endif // GUARD_FOLLOWER_MON_H
