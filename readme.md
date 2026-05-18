NOTE: I have repurposed the repository at
https://github.com/BrianKarcher/Blue-NES-Emulator

Into this new repo. The old repository is being retired. The reason being I am expanding this emulator to support more systems than just the NES and the old name didn't fit that purpose. The next target is the SNES.

This repo will be refactored to support multiple systems.

For NES emulation, among other things, it is compatible with:
- 6502 processor
- NesPpu
- NesApu
- Bank switching (MMC1, AxROM, CNROM, DxROM, MMC2, MMC3, NROM, and UxROM are supported)
- Game saves
- Savestates

My reference guide on how NES internals work:
https://www.nesdev.org/wiki/NES_reference_guide

When designing and creating an emulator for NES games, there are several considerations we have to take into account.

The NES contains an 8-bit processor that can access a 16-bit memory address.
The CPU can access a memory range of 65 KB. Of which 2 KB is dedicated to system RAM. ($0000 to $07FF). This then gets mirrored from $0800-$1FFF. $2000 to $2007 are for the NesPpu registers. $2008 to $3FFF are mirrors of the NesPpu registers. $4000 to $4017 are NesApu and I/O registers. $4018 to $401F is NesApu and I/O functionality that is normally disabled.

$6000-$7FFF is cartridge RAM, when present (This is where your saves are stored. It is also a work area as it is vastly larger than the built-in RAM).
$8000-$FFFF is the cartridge ROM and mapper registers.

Very important interrupt vectors:
$FFFA-$FFFB: NMI vector.
$FFFC-$FFFD: Reset vector.
$FFFE-$FFFF: IRQ vector.

The NES is a parallel processing system.

***
High-level architecture
***

The NES does this during the screen drawing:
1. (Starting screen draw). The NesPpu draws the current frame while the CPU prepares the next frame. Each only accesses their own memory.
..................
..................
..................
..................
2. (Screen draw complete, the NesPpu trips the NMI vector on the CPU while the ray repositions to the top-left corner of the screen in preparation of the next frame)
2.1 The CPU sends the NesPpu data for the next frame
2.2 When the television's positioning of the raygun is complete, go back to step 1.

If the CPU is not complete for the next frame before step 2 above, the program must ignore the NMI vector and the NesPpu redraws the current frame. This is what causes slowdown on the NES.

If the CPU is not done sending data to the NesPpu by step 2.2 above... this is a very bad case as the CPU must not send data to the NesPpu while the screen is drawing. The frame gets garbled.

Blue NES is accurate at the cycle level, not just the instruction level. This means that if an instruction takes 2 cycles to execute, the emulator will take 2 cycles to execute it. This is important for timing-sensitive games like Super Mario Bros.

***
Save states
***

We support save states. This is a feature that allows you to save the exact state of the emulator at any point in time and load it later. This is useful for debugging and for saving your progress in games that do not have a built-in save feature.