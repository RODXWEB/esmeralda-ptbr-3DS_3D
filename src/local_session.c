// Local wireless play: the link counters' session
// (CableClub_EventScript_LocalSession in data/scripts/cable_club.inc). At a
// counter the game joins the session of that counter a 3DS nearby is
// hosting, or hosts one; players in the same session meet through the
// wireless features. The wireless itself is 3ds_port/src/3ds_local.c.

#ifdef PLATFORM_3DS

#include "global.h"
#include "event_data.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "task.h"
#include "constants/map_groups.h"
#include "constants/local_link.h"
#include "3ds_local.h"
#include "local_link.h"

#define WAIT_FRAMES (60 * 10) // looking takes 2-3 seconds; give up after this
#define LEAVE_FRAMES 30       // free to walk around this long: left the session

// The counter the script is at (VAR_0x800A, LOCAL_KIND_*).
static u8 CounterKind(void)
{
    return gSpecialVar_0x800A;
}

u16 LocalSession_IsEnabled(void)
{
    return LocalLink_GetSession() != LOCAL_SESSION_OFF;
}

// Already in this counter's session: TRUE, nothing to do. Otherwise starts
// looking for one (LocalSession_Wait waits for it) and returns FALSE.
u16 LocalSession_Start(void)
{
    if (LocalLink_GetSession() == LOCAL_SESSION_IN && LocalLink_GetSessionKind() == CounterKind())
        return TRUE;
    LocalLink_StartSession(CounterKind());
    return FALSE;
}

// Whether the attendant lets the player in: with local wireless set up, only
// while in a session for this counter (without it, the missing Wireless
// Adapter keeps them out as before).
u16 LocalSession_CanEnter(void)
{
    u8 state = LocalLink_GetSession();
    u8 kind = LocalLink_GetSessionKind();

    if (state == LOCAL_SESSION_OFF)
        return TRUE;
    return state == LOCAL_SESSION_IN && (kind == LOCAL_KIND_ANY || kind == CounterKind());
}

void LocalSession_Leave(void)
{
    LocalLink_LeaveSession();
}

#define tFrames data[0]

static void Task_WaitForSession(u8 taskId)
{
    u8 state = LocalLink_GetSession();

    if (state == LOCAL_SESSION_SEARCHING && ++gTasks[taskId].tFrames < WAIT_FRAMES)
        return;
    if (state == LOCAL_SESSION_SEARCHING)
    {
        LocalLink_LeaveSession();
        state = LOCAL_SESSION_FAILED;
    }
    gSpecialVar_Result = state;
    DestroyTask(taskId);
    ScriptContext_Enable();
}

// Waits until the session is joined or hosted (or couldn't be);
// VAR_RESULT: LOCAL_SESSION_*.
void LocalSession_Wait(void)
{
    CreateTask(Task_WaitForSession, 80);
}

#undef tFrames

static bool8 InLinkRoom(void)
{
    static const u16 sLinkRooms[] = {
        MAP_UNION_ROOM, MAP_TRADE_CENTER, MAP_RECORD_CORNER,
        MAP_BATTLE_COLOSSEUM_2P, MAP_BATTLE_COLOSSEUM_4P,
    };
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sLinkRooms); i++)
        if (gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(sLinkRooms[i])
         && gSaveBlock1Ptr->location.mapNum == MAP_NUM(sLinkRooms[i]))
            return TRUE;
    return FALSE;
}

// Once a frame (from the emulated adapter): a session is only for meeting
// people through the counter it was started at, so once the player is
// walking around outside the link rooms again (left the Union Room or Trade
// Center, or didn't go in after all), leave it.
void LocalSession_Update(void)
{
    static u16 sFreeFrames;

    if (LocalLink_GetSession() != LOCAL_SESSION_IN)
    {
        sFreeFrames = 0;
        return;
    }
    if (gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked()
     && !ScriptContext_IsEnabled() && !InLinkRoom())
    {
        if (++sFreeFrames >= LEAVE_FRAMES)
        {
            sFreeFrames = 0;
            LocalLink_LeaveSession();
        }
    }
    else
    {
        sFreeFrames = 0;
    }
}

#endif // PLATFORM_3DS
