#pragma once
#include <Core/gb.h>
extern "C" {
void sameboy_debug_attach(GB_gameboy_t*);
unsigned sameboy_debug_run(GB_gameboy_t*);
void sameboy_debug_async(GB_gameboy_t*);
void sameboy_debug_enable(GB_gameboy_t*,bool);
void sameboy_debug_load_symbols(GB_gameboy_t*,const char*,size_t);
}
