#include "global.h"
#include "crt0.h"
#include "malloc.h"
#include "link.h"
#include "link_rfu.h"
#include "librfu.h"
#include "m4a.h"
#include "bg.h"
#include "rtc.h"
#include "scanline_effect.h"
#include "overworld.h"
#include "play_time.h"
#include "random.h"
#include "dma3.h"
#include "gba/flash_internal.h"
#include "load_save.h"
#include "gpu_regs.h"
#include "agb_flash.h"
#include "sound.h"
#include "battle.h"
#include "battle_controllers.h"
#include "text.h"
#include "intro.h"
#include "title_screen.h"
#include "main.h"
#include "trainer_hill.h"
#include "platform.h"
#include "constants/rgb.h"

#ifdef PORT_BRIDGE
#include "port_platform.h"
#include "port_log.h"
#ifdef PLATFORM_3DS
u16 Port_RngSeed(u16 fallback);
#endif
#endif

static void VBlankIntr(void);
static void HBlankIntr(void);
static void VCountIntr(void);
static void SerialIntr(void);
static void IntrDummy(void);

const u8 gGameVersion = GAME_VERSION;

const u8 gGameLanguage = GAME_LANGUAGE; // English

const char BuildDateTime[] = "2005 02 21 11:10";

const IntrFunc gIntrTableTemplate[] =
{
    VCountIntr, // V-count interrupt
    SerialIntr, // Serial interrupt
    Timer3Intr, // Timer 3 interrupt
    HBlankIntr, // H-blank interrupt
    VBlankIntr, // V-blank interrupt
    IntrDummy,  // Timer 0 interrupt
    IntrDummy,  // Timer 1 interrupt
    IntrDummy,  // Timer 2 interrupt
    IntrDummy,  // DMA 0 interrupt
    IntrDummy,  // DMA 1 interrupt
    IntrDummy,  // DMA 2 interrupt
    IntrDummy,  // DMA 3 interrupt
    IntrDummy,  // Key interrupt
    IntrDummy,  // Game Pak interrupt
};

#define INTR_COUNT ((int)(sizeof(gIntrTableTemplate)/sizeof(IntrFunc)))

static u16 sUnusedVar; // Never read
static bool8 sMainCallback2JustChanged;

#ifdef PORTABLE
u8 gHeap[HEAP_SIZE];
#endif
u16 gKeyRepeatStartDelay;
bool8 gLinkTransferringData;
struct Main gMain;
u16 gKeyRepeatContinueDelay;
bool8 gSoftResetDisabled;
IntrFunc gIntrTable[INTR_COUNT];
u8 gLinkVSyncDisabled;
u32 IntrMain_Buffer[0x200];
s8 gPcmDmaCounter;

static EWRAM_DATA u16 sTrainerId = 0;

//EWRAM_DATA void (**gFlashTimerIntrFunc)(void) = NULL;

static void UpdateLinkAndCallCallbacks(void);
static void InitMainCallbacks(void);
static void CallCallbacks(void);
#ifdef BUGFIX
static void SeedRngWithRtc(void);
#endif
static void ReadKeys(void);
void InitIntrHandlers(void);
static void WaitForVBlank(void);
void EnableVCountIntrAtLine150(void);

#define B_START_SELECT (B_BUTTON | START_BUTTON | SELECT_BUTTON)

#ifdef PORT_BRIDGE
#define PORT_BOOT_STEP(tag)                                \
    do                                                    \
    {                                                     \
        if ((tag) < 0xA020)                               \
            Port_SetBirchDebugEx((tag), gMain.state, 0, 0, 0); \
    } while (0)
#else
#define PORT_BOOT_STEP(tag) ((void)0)
#endif

void AgbMain(void)
{
    PORT_BOOT_STEP(0xA000);
    // Modern compilers are liberal with the stack on entry to this function,
    // so RegisterRamReset may crash if it resets IWRAM.
#if !MODERN
    RegisterRamReset(RESET_ALL);
#endif //MODERN
    PORT_BOOT_STEP(0xA001);
    *(vu16 *)BG_PLTT = RGB_WHITE; // Set the backdrop to white on startup
    InitGpuRegManager();
    PORT_BOOT_STEP(0xA002);
    REG_WAITCNT = WAITCNT_PREFETCH_ENABLE | WAITCNT_WS0_S_1 | WAITCNT_WS0_N_3;
    InitKeys();
    PORT_BOOT_STEP(0xA003);
    InitIntrHandlers();
    PORT_BOOT_STEP(0xA004);
    m4aSoundInit();
    PORT_BOOT_STEP(0xA005);
    EnableVCountIntrAtLine150();
    PORT_BOOT_STEP(0xA006);
#if !defined(PORTABLE) || defined(PLATFORM_3DS)
    InitRFU();
#endif
    RtcInit();
    PORT_BOOT_STEP(0xA007);
    CheckForFlashMemory();
    PORT_BOOT_STEP(0xA008);
    InitMainCallbacks();
    PORT_BOOT_STEP(0xA009);
    InitMapMusic();
    PORT_BOOT_STEP(0xA00A);
#if defined(PLATFORM_3DS) && defined(PORT_BRIDGE)
    SeedRng(Port_RngSeed(0));
#elif defined(BUGFIX)
    SeedRngWithRtc(); // see comment at SeedRngWithRtc definition below
#endif
    PORT_BOOT_STEP(0xA00B);
    ClearDma3Requests();
    PORT_BOOT_STEP(0xA00C);
    ResetBgs();
    PORT_BOOT_STEP(0xA00D);
    SetDefaultFontsPointer();
    PORT_BOOT_STEP(0xA00E);
    InitHeap(gHeap, HEAP_SIZE);
    PORT_BOOT_STEP(0xA00F);

    gSoftResetDisabled = FALSE;

#ifndef PORTABLE
    if (gFlashMemoryPresent != TRUE)
        SetMainCallback2(NULL);
#endif

    gLinkTransferringData = FALSE;
    sUnusedVar = 0xFC0;
    PORT_BOOT_STEP(0xA010);

#ifndef NDEBUG
#if (LOG_HANDLER == LOG_HANDLER_MGBA_PRINT)
    (void) MgbaOpen();
#elif (LOG_HANDLER == LOG_HANDLER_AGB_PRINT)
    AGBPrintInit();
#endif
#endif
    for (;;)
    {
#ifdef PORT_BRIDGE
        Port_RecordMainLoopIteration();
#endif
        PORT_BOOT_STEP(0xA020);
        ReadKeys();
        sMainCallback2JustChanged = FALSE;

#ifdef PLATFORM_3DS
        // L+R+START+SELECT, as in the 3DS games (port_platform.h).
        if (gSoftResetDisabled == FALSE
         && JOY_HELD_RAW(CTR_SOFT_RESET_KEYS) == CTR_SOFT_RESET_KEYS)
#else
        if (gSoftResetDisabled == FALSE
         && JOY_HELD_RAW(A_BUTTON)
         && JOY_HELD_RAW(B_START_SELECT) == B_START_SELECT)
#endif
        {
#if !defined(PORTABLE) || defined(PLATFORM_3DS)
            rfu_REQ_stopMode();
            rfu_waitREQComplete();
#endif
            DoSoftReset();
        }

        if (Overworld_SendKeysToLinkIsRunning() == TRUE)
        {
            gLinkTransferringData = TRUE;
            UpdateLinkAndCallCallbacks();
            gLinkTransferringData = FALSE;
        }
        else
        {
            gLinkTransferringData = FALSE;
            PORT_BOOT_STEP(0xA021);
            UpdateLinkAndCallCallbacks();
            PORT_BOOT_STEP(0xA022);

            if (Overworld_RecvKeysFromLinkIsRunning() == TRUE)
            {
                gMain.newKeys = 0;
                ClearSpriteCopyRequests();
                gLinkTransferringData = TRUE;
                UpdateLinkAndCallCallbacks();
                gLinkTransferringData = FALSE;
            }
        }

        if (sMainCallback2JustChanged)
        {
#ifdef PORT_BRIDGE
            Port_SetBirchDebugEx(0x0190,
                                (u16)((uintptr_t)gMain.callback2 >> 16),
                                (u16)(uintptr_t)gMain.callback2,
                                gMain.state,
                                0);
#endif
            continue;
        }

        PlayTimeCounter_Update();
        MapMusicMain();
        PORT_BOOT_STEP(0xA023);
        WaitForVBlank();
        PORT_BOOT_STEP(0xA024);
    }
}

static void UpdateLinkAndCallCallbacks(void)
{
    if (!HandleLinkConnection())
        CallCallbacks();
}

static void InitMainCallbacks(void)
{
    gMain.vblankCounter1 = 0;
    gTrainerHillVBlankCounter = NULL;
    gMain.vblankCounter2 = 0;
    gMain.callback1 = NULL;
    SetMainCallback2(CB2_InitCopyrightScreenAfterBootup);
    gSaveBlock2Ptr = &gSaveblock2.block;
    gPokemonStoragePtr = &gPokemonStorage.block;
}

static void CallCallbacks(void)
{
    if (gMain.callback1)
        gMain.callback1();

    if (gMain.callback2)
        gMain.callback2();
}

void SetMainCallback2(MainCallback callback)
{
#ifdef PORT_BRIDGE
    {
        uintptr_t cb = (uintptr_t)callback;

        if (cb != 0)
        {
            callback = (MainCallback)cb;
        }

        Port_SetBirchDebugEx(0x0182, (u16)(cb >> 16), (u16)cb, gMain.state, 0);
        Port_SetAssetDebugEx(0x0301, (u16)(cb >> 16), (u16)cb, gMain.state, 0);
    }
#endif
    sMainCallback2JustChanged = TRUE;
    gMain.callback2 = callback;
    gMain.state = 0;
}

void StartTimer1(void)
{
    REG_TM1CNT_H = 0x80;
}

void SeedRngAndSetTrainerId(void)
{
    u16 val = REG_TM1CNT_L;
    SeedRng(val);
    REG_TM1CNT_H = 0;
    sTrainerId = val;
}

u16 GetGeneratedTrainerIdLower(void)
{
    return sTrainerId;
}

void EnableVCountIntrAtLine150(void)
{
    u16 gpuReg = (GetGpuReg(REG_OFFSET_DISPSTAT) & 0xFF) | (150 << 8);
    SetGpuReg(REG_OFFSET_DISPSTAT, gpuReg | DISPSTAT_VCOUNT_INTR);
    EnableInterrupts(INTR_FLAG_VCOUNT);
}

// FRLG commented this out to remove RTC, however Emerald didn't undo this!
#ifdef BUGFIX
static void SeedRngWithRtc(void)
{
    u32 seed = RtcGetMinuteCount();
    seed = (seed >> 16) ^ (seed & 0xFFFF);
    SeedRng(seed);
}
#endif

void InitKeys(void)
{
    gKeyRepeatContinueDelay = 5;
    gKeyRepeatStartDelay = 40;

    gMain.heldKeys = 0;
    gMain.newKeys = 0;
    gMain.newAndRepeatedKeys = 0;
    gMain.heldKeysRaw = 0;
    gMain.newKeysRaw = 0;
}

static void ReadKeys(void)
{
#ifndef PORTABLE
    u16 keyInput = REG_KEYINPUT ^ KEYS_MASK;
#else
    u16 keyInput = Platform_GetKeyInput();
#endif
    gMain.newKeysRaw = keyInput & ~gMain.heldKeysRaw;
    gMain.newKeys = gMain.newKeysRaw;
    gMain.newAndRepeatedKeys = gMain.newKeysRaw;

    // BUG: Key repeat won't work when pressing L using L=A button mode
    // because it compares the raw key input with the remapped held keys.
    // Note that newAndRepeatedKeys is never remapped either.

    if (keyInput != 0 && gMain.heldKeys == keyInput)
    {
        gMain.keyRepeatCounter--;

        if (gMain.keyRepeatCounter == 0)
        {
            gMain.newAndRepeatedKeys = keyInput;
            gMain.keyRepeatCounter = gKeyRepeatContinueDelay;
        }
    }
    else
    {
        // If there is no input or the input has changed, reset the counter.
        gMain.keyRepeatCounter = gKeyRepeatStartDelay;
    }

    gMain.heldKeysRaw = keyInput;
    gMain.heldKeys = gMain.heldKeysRaw;

    // Remap L to A if the L=A option is enabled.
    if (gSaveBlock2Ptr->optionsButtonMode == OPTIONS_BUTTON_MODE_L_EQUALS_A)
    {
        if (JOY_NEW(L_BUTTON))
            gMain.newKeys |= A_BUTTON;

        if (JOY_HELD(L_BUTTON))
            gMain.heldKeys |= A_BUTTON;
    }

    if (JOY_NEW(gMain.watchedKeysMask))
        gMain.watchedKeysPressed = TRUE;
}

void InitIntrHandlers(void)
{
    int i;

    for (i = 0; i < INTR_COUNT; i++)
        gIntrTable[i] = gIntrTableTemplate[i];

#ifndef PORT_BRIDGE
    DmaCopy32(3, IntrMain, IntrMain_Buffer, sizeof(IntrMain_Buffer));
    INTR_VECTOR = IntrMain_Buffer;
#endif

    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    SetSerialCallback(NULL);

    REG_IME = 1;

    EnableInterrupts(INTR_FLAG_VBLANK);
}

void SetVBlankCallback(IntrCallback callback)
{
    gMain.vblankCallback = callback;
}

void SetHBlankCallback(IntrCallback callback)
{
    gMain.hblankCallback = callback;
}

void SetVCountCallback(IntrCallback callback)
{
    gMain.vcountCallback = callback;
}

void RestoreSerialTimer3IntrHandlers(void)
{
    gIntrTable[1] = SerialIntr;
    gIntrTable[2] = Timer3Intr;
}

void SetSerialCallback(IntrCallback callback)
{
    gMain.serialCallback = callback;
}

static void VBlankIntr(void)
{
#ifdef PORT_BRIDGE
    Port_ProfBegin();
#endif
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E00, gMain.vblankCounter1, gMain.state, 0, 0);
#endif
    if (gWirelessCommType != 0)
        RfuVSync();
    else if (gLinkVSyncDisabled == FALSE)
        LinkVSync();
#ifdef PORT_BRIDGE
    Port_ProfAcc("link");
#endif

#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E01, gMain.vblankCounter1, gMain.state, 0, 0);
#endif

    gMain.vblankCounter1++;

    if (gTrainerHillVBlankCounter && *gTrainerHillVBlankCounter < 0xFFFFFFFF)
        (*gTrainerHillVBlankCounter)++;

#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E02, gMain.vblankCounter1, gMain.state, 0, 0);
#endif
    if (gMain.vblankCallback)
        gMain.vblankCallback();
#ifdef PORT_BRIDGE
    Port_ProfAcc("callback");
#endif

#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E03, gMain.vblankCounter1, gMain.state, 0, 0);
#endif

    gMain.vblankCounter2++;

    CopyBufferedValuesToGpuRegs();
#ifdef PORT_BRIDGE
    Port_ProfAcc("gpuregs");
#endif
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E04, gMain.vblankCounter1, gMain.state, 0, 0);
#endif
    ProcessDma3Requests();
#ifdef PORT_BRIDGE
    Port_ProfAcc("dma3");
#endif
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E05, gMain.vblankCounter1, gMain.state, 0, 0);
#endif

#ifdef PORT_BRIDGE
    // The port drives the GBA VBlank handler from VBlankIntrWait(), but
    // does not currently synthesize the separate GBA VCount interrupt.
    m4aSoundVSync();
#endif
#ifdef PORT_BRIDGE
    Port_ProfAcc("m4aVSync");
#endif
    gPcmDmaCounter = gSoundInfo.pcmDmaCounter;
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E06, gSoundInfo.pcmDmaCounter, gMain.state, 0, 0);
#endif
    m4aSoundMain();
#ifdef PORT_BRIDGE
    Port_ProfAcc("m4aMain");
#endif
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E07, gSoundInfo.pcmDmaCounter, gMain.state, 0, 0);
#endif
    TryReceiveLinkBattleData();
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E08, gMain.vblankCounter1, gMain.state, 0, 0);
#endif

    if (!gMain.inBattle || !(gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_RECORDED)))
        Random();

    UpdateWirelessStatusIndicatorSprite();
#ifdef PORT_BRIDGE
    Port_ProfAcc("rest");
#endif
#ifdef PORT_BRIDGE
    Port_SetDmaDebugEx(0x0E09, gMain.vblankCounter1, gMain.state, 0, 0);
#endif

    INTR_CHECK |= INTR_FLAG_VBLANK;
    gMain.intrCheck |= INTR_FLAG_VBLANK;
}

void InitFlashTimer(void)
{
    SetFlashTimerIntr(2, gIntrTable + 0x7);
}

static void HBlankIntr(void)
{
    if (gMain.hblankCallback)
        gMain.hblankCallback();

    INTR_CHECK |= INTR_FLAG_HBLANK;
    gMain.intrCheck |= INTR_FLAG_HBLANK;
}

static void VCountIntr(void)
{
    if (gMain.vcountCallback)
        gMain.vcountCallback();

    m4aSoundVSync();
    INTR_CHECK |= INTR_FLAG_VCOUNT;
    gMain.intrCheck |= INTR_FLAG_VCOUNT;
}

static void SerialIntr(void)
{
    if (gMain.serialCallback)
        gMain.serialCallback();

    INTR_CHECK |= INTR_FLAG_SERIAL;
    gMain.intrCheck |= INTR_FLAG_SERIAL;
}

static void IntrDummy(void)
{}

static void WaitForVBlank(void)
{
#ifdef PORTABLE
    VBlankIntrWait();
#else
    gMain.intrCheck &= ~INTR_FLAG_VBLANK;

    while (!(gMain.intrCheck & INTR_FLAG_VBLANK))
        ;
#endif
}

void SetTrainerHillVBlankCounter(u32 *counter)
{
    gTrainerHillVBlankCounter = counter;
}

void ClearTrainerHillVBlankCounter(void)
{
    gTrainerHillVBlankCounter = NULL;
}

void DoSoftReset(void)
{
    REG_IME = 0;
    m4aSoundVSyncOff();
    ScanlineEffect_Stop();
    DmaStop(1);
    DmaStop(2);
    DmaStop(3);
    SiiRtcProtect();
    SoftReset(RESET_ALL);
}

void ClearPokemonCrySongs(void)
{
    CpuFill16(0, gPokemonCrySongs, MAX_POKEMON_CRIES * sizeof(struct PokemonCrySong));
}
