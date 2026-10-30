#include <Core/gb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "test_rom.h"

static unsigned frames, samples;
static uint32_t pixels[256 * 224];
static uint32_t rgb(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{ (void)gb; return 0xff000000u | (r << 16) | (g << 8) | b; }
static void vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{ (void)gb; (void)type; frames++; }
static void audio(GB_gameboy_t *gb, GB_sample_t *s)
{ (void)gb; (void)s; samples++; }

int main(int argc, char **argv)
{
    const char *kind = argc > 1 ? argv[1] : "dmg";
    GB_model_t model = GB_MODEL_DMG_B;
    const char *boot = "dmg_boot.bin";
    if (!strcmp(kind, "cgb")) { model = GB_MODEL_CGB_E; boot = "cgb_boot.bin"; }
    if (!strcmp(kind, "cgb0")) { model = GB_MODEL_CGB_0; boot = "cgb0_boot.bin"; }
    if (!strcmp(kind, "mgb")) { model = GB_MODEL_MGB; boot = "mgb_boot.bin"; }
    if (!strcmp(kind, "agb")) { model = GB_MODEL_AGB_A; boot = "agb_boot.bin"; }
    if (!strcmp(kind, "gbp")) { model = GB_MODEL_GBP_A; boot = "agb_boot.bin"; }
    if (!strcmp(kind, "sgb")) { model = GB_MODEL_SGB; boot = "sgb_boot.bin"; }
    if (!strcmp(kind, "sgb-pal")) { model = GB_MODEL_SGB_PAL; boot = "sgb_boot.bin"; }
    if (!strcmp(kind, "sgb2")) { model = GB_MODEL_SGB2; boot = "sgb2_boot.bin"; }
    const bool color = (model & GB_MODEL_FAMILY_MASK) == GB_MODEL_CGB_FAMILY;
    uint8_t rom[0x8000] = {0};
    make_test_rom(rom, color);
    GB_gameboy_t *gb = GB_init(GB_alloc(), model);
    char path[1024]; snprintf(path, sizeof(path), "%s/%s", BOOT_ROM_DIR, boot);
    if (GB_load_boot_rom(gb, path)) return 1;
    GB_load_rom_from_buffer(gb, rom, sizeof(rom));
    GB_set_rgb_encode_callback(gb, rgb);
    GB_set_pixels_output(gb, pixels);
    GB_set_vblank_callback(gb, vblank);
    GB_set_sample_rate(gb, 48000);
    GB_apu_set_sample_callback(gb, audio);
    GB_set_key_state(gb, GB_KEY_A, true);
    GB_set_rewind_length(gb, 2);
    for (unsigned i = 0; i < 600; i++) GB_run_frame(gb);
    const GB_registers_t *registers = GB_get_registers(gb);
    const bool reached_rom = registers->pc >= 0x15e && registers->pc <= 0x160;
    const bool ram_written = GB_read_memory(gb, 0xa000) == 0x42;
    const int size = GB_save_battery_size(gb);
    uint8_t *save = malloc(size);
    const bool saved = size > 0 && GB_save_battery_to_buffer(gb, save, size) == 0;
    GB_write_memory(gb, 0xa000, 0x17);
    GB_load_battery_from_buffer(gb, save, size);
    const bool restored = GB_read_memory(gb, 0xa000) == 0x42;
    free(save);
    for (unsigned i = 0; i < 10; ++i) { GB_write_memory(gb, 0xc000, i); GB_run_frame(gb); }
    const bool rewound = GB_rewind_pop(gb) && GB_rewind_pop(gb) && GB_read_memory(gb, 0xc000) < 9;
    printf("frames=%u samples=%u pc=%04x\n", frames, samples, registers->pc);
    GB_free(gb);
    GB_dealloc(gb);
    return frames >= 240 && samples > 100000 && reached_rom && ram_written && saved && restored && rewound ? 0 : 1;
}
