#include "global.h"
#include "gpu_regs.h"

#define GPU_REG_BUF_SIZE 0x60

#define GPU_REG_BUF(offset) (*(u16 *)(&sGpuRegBuffer[offset]))
#define GPU_REG(offset) (*(vu16 *)(REG_BASE + offset))

#define EMPTY_SLOT 0xFF

static ALIGNED(2) u8 sGpuRegBuffer[GPU_REG_BUF_SIZE]; // sGpuRegBuffer is read as u16 so it needs to be properly aligned
static u8 sGpuRegWaitingList[GPU_REG_BUF_SIZE];
static vbool8 sGpuRegBufferLocked;
static vbool8 sShouldSyncRegIE;
static vu16 sRegIE;

static void CopyBufferedValueToGpuReg(u8 regOffset);
static void SyncRegIE(void);
static void UpdateRegDispstatIntrBits(u16 regIE);

void InitGpuRegManager(void)
{
    s32 i;

    for (i = 0; i < GPU_REG_BUF_SIZE; i++)
    {
        sGpuRegBuffer[i] = 0;
        sGpuRegWaitingList[i] = EMPTY_SLOT;
    }

    sGpuRegBufferLocked = FALSE;
    sShouldSyncRegIE = FALSE;
    sRegIE = 0;
}

static void CopyBufferedValueToGpuReg(u8 regOffset)
{
    if (regOffset == REG_OFFSET_DISPSTAT)
    {
        REG_DISPSTAT &= ~(DISPSTAT_HBLANK_INTR | DISPSTAT_VBLANK_INTR);
        REG_DISPSTAT |= GPU_REG_BUF(REG_OFFSET_DISPSTAT);
    }
    else
    {
        GPU_REG(regOffset) = GPU_REG_BUF(regOffset);
    }
}

void CopyBufferedValuesToGpuRegs(void)
{
    if (!sGpuRegBufferLocked)
    {
        s32 i;

        for (i = 0; i < GPU_REG_BUF_SIZE; i++)
        {
            u8 regOffset = sGpuRegWaitingList[i];
            if (regOffset == EMPTY_SLOT)
                return;
            CopyBufferedValueToGpuReg(regOffset);
            sGpuRegWaitingList[i] = EMPTY_SLOT;
        }
    }
}

#ifdef PLATFORM_3DS
static void SetGpuReg16(u8 regOffset, u16 value)
#else
void SetGpuReg(u8 regOffset, u16 value)
#endif
{
    if (regOffset < GPU_REG_BUF_SIZE)
    {
        u16 vcount;

        GPU_REG_BUF(regOffset) = value;
        vcount = REG_VCOUNT & 0xFF;

        if ((vcount >= 161 && vcount <= 225) || (REG_DISPCNT & DISPCNT_FORCED_BLANK))
        {
            CopyBufferedValueToGpuReg(regOffset);
        }
        else
        {
            s32 i;

            sGpuRegBufferLocked = TRUE;

            for (i = 0; i < GPU_REG_BUF_SIZE && sGpuRegWaitingList[i] != EMPTY_SLOT; i++)
            {
                if (sGpuRegWaitingList[i] == regOffset)
                {
                    sGpuRegBufferLocked = FALSE;
                    return;
                }
            }

            sGpuRegWaitingList[i] = regOffset;
            sGpuRegBufferLocked = FALSE;
        }
    }
}

#ifdef PLATFORM_3DS
// WIN0H and WIN1H: what the edges pass 255 by goes beside the register,
// buffered and copied with it, tagged with the value it belongs to. A window
// kept in an s16 (a task's data) arrives sign-extended when its left edge is
// 128 or more: 0xFFFF above bit 15 is that, never edges past 255.
void SetGpuReg(u8 regOffset, u32 value)
{
    if (regOffset == REG_OFFSET_WIN0H || regOffset == REG_OFFSET_WIN1H)
    {
        u8 over = regOffset == REG_OFFSET_WIN0H ? REG_OFFSET_WIN0H_OVER : REG_OFFSET_WIN1H_OVER;
        u8 tag = regOffset == REG_OFFSET_WIN0H ? REG_OFFSET_WIN0H_TAG : REG_OFFSET_WIN1H_TAG;

        SetGpuReg16(over, (value >> 16) == 0xFFFF ? 0 : value >> 16);
        SetGpuReg16(tag, value);
    }
    SetGpuReg16(regOffset, value);
}

#endif
void SetGpuReg_ForcedBlank(u8 regOffset, u16 value)
{
    if (regOffset < GPU_REG_BUF_SIZE)
    {
        GPU_REG_BUF(regOffset) = value;

        if (REG_DISPCNT & DISPCNT_FORCED_BLANK)
        {
            CopyBufferedValueToGpuReg(regOffset);
        }
        else
        {
            s32 i;

            sGpuRegBufferLocked = TRUE;

            for (i = 0; i < GPU_REG_BUF_SIZE && sGpuRegWaitingList[i] != EMPTY_SLOT; i++)
            {
                if (sGpuRegWaitingList[i] == regOffset)
                {
                    sGpuRegBufferLocked = FALSE;
                    return;
                }
            }

            sGpuRegWaitingList[i] = regOffset;
            sGpuRegBufferLocked = FALSE;
        }
    }
}

u16 GetGpuReg(u8 regOffset)
{
    if (regOffset == REG_OFFSET_DISPSTAT)
        return REG_DISPSTAT;

    if (regOffset == REG_OFFSET_VCOUNT)
        return REG_VCOUNT;

    return GPU_REG_BUF(regOffset);
}

void SetGpuRegBits(u8 regOffset, u16 mask)
{
    u16 regValue = GPU_REG_BUF(regOffset);
    SetGpuReg(regOffset, regValue | mask);
}

void ClearGpuRegBits(u8 regOffset, u16 mask)
{
    u16 regValue = GPU_REG_BUF(regOffset);
    SetGpuReg(regOffset, regValue & ~mask);
}

static void SyncRegIE(void)
{
    if (sShouldSyncRegIE)
    {
        u16 temp = REG_IME;
        REG_IME = 0;
        REG_IE = sRegIE;
        REG_IME = temp;
        sShouldSyncRegIE = FALSE;
    }
}

void EnableInterrupts(u16 mask)
{
    sRegIE |= mask;
    sShouldSyncRegIE = TRUE;
    SyncRegIE();
    UpdateRegDispstatIntrBits(sRegIE);
}

void DisableInterrupts(u16 mask)
{
    sRegIE &= ~mask;
    sShouldSyncRegIE = TRUE;
    SyncRegIE();
    UpdateRegDispstatIntrBits(sRegIE);
}

static void UpdateRegDispstatIntrBits(u16 regIE)
{
    u16 oldValue = GetGpuReg(REG_OFFSET_DISPSTAT) & (DISPSTAT_HBLANK_INTR | DISPSTAT_VBLANK_INTR);
    u16 newValue = 0;

    if (regIE & INTR_FLAG_VBLANK)
        newValue |= DISPSTAT_VBLANK_INTR;

    if (regIE & INTR_FLAG_HBLANK)
        newValue |= DISPSTAT_HBLANK_INTR;

    if (oldValue != newValue)
        SetGpuReg(REG_OFFSET_DISPSTAT, newValue);
}
