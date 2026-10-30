#pragma once
#include <stdint.h>
#include <string.h>

// Original synthetic MBC1+RAM+battery cartridge, freely redistributable.
// Enables RAM, writes a marker, turns on LCD and loops in the cartridge.
static void make_test_rom(uint8_t *rom, int color)
{
    memset(rom, 0, 0x8000);
    rom[0x100] = 0xc3; rom[0x101] = 0x50; rom[0x102] = 0x01;
    rom[0x143] = color ? 0x80 : 0;
    rom[0x147] = 3;
    rom[0x149] = 2;
    const uint8_t code[] = {0x3e, 0x0a, 0xea, 0, 0, 0x3e, 0x42, 0xea, 0, 0xa0,
                            0x3e, 0x91, 0xe0, 0x40, 0x18, 0xfe};
    memcpy(rom + 0x150, code, sizeof(code));
    uint8_t checksum = 0;
    for (unsigned i = 0x134; i <= 0x14c; i++) checksum = checksum - rom[i] - 1;
    rom[0x14d] = checksum;
}
