#ifndef GUARD_GPU_REGS_H
#define GUARD_GPU_REGS_H

// Exported type declarations

// Exported RAM declarations

// Exported ROM declarations
void InitGpuRegManager(void);
void CopyBufferedValuesToGpuRegs(void);
#ifdef PLATFORM_3DS
// A window's horizontal edges can pass 255 on the 400-pixel screen: WIN_RANGE
// (gba/io_reg.h) carries the rest above bit 15, and SetGpuReg keeps it.
void SetGpuReg(u8 regOffset, u32 value);
#else
void SetGpuReg(u8 regOffset, u16 value);
#endif
void SetGpuReg_ForcedBlank(u8 regOffset, u16 value);
u16 GetGpuReg(u8 regOffset);
void SetGpuRegBits(u8 regOffset, u16 mask);
void ClearGpuRegBits(u8 regOffset, u16 mask);
void EnableInterrupts(u16 mask);
void DisableInterrupts(u16 mask);

#endif //GUARD_GPU_REGS_H
