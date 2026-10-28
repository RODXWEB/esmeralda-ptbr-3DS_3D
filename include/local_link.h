#ifndef GUARD_LOCAL_LINK_H
#define GUARD_LOCAL_LINK_H

// Local wireless play: the link through the emulated Wireless Adapter
// (3ds_port/src/3ds_rfu_adapter.c), its radio the 3DS's local wireless
// (3ds_port/src/3ds_local.c).

bool8 PlatformLocalLinkEnabled(void); // the emulated adapter is there
bool8 PlatformLocalLinkActive(void);  // linked to other players through it
// Battle messages go whole, straight to the other players (everyone gets
// their own back too, in order), instead of 12 bytes a frame.
void PlatformLinkBattleReset(void);
void PlatformLinkBattleSend(const void *data, u16 size, bool8 toSelf);
u16 PlatformLinkBattleRecv(void *out, u16 max); // next message, 0 if none
// Link blocks (SendBlock) the same way: the next block from the player with
// that index in the link layer (0 if none). The host works out the index of
// a child's blocks with PlatformRfuPlayerIndexForSlot (link_rfu_2.c).
void PlatformLinkBlockSend(const void *data, u16 size);
u16 PlatformLinkBlockTake(u8 playerIndex, void *out, u16 max);
u8 PlatformRfuPlayerIndexForSlot(u8 slot);

// The link counters' session (src/local_session.c): once a frame, leaves it
// when the player has walked away from the counter.
void LocalSession_Update(void);

#endif // GUARD_LOCAL_LINK_H
