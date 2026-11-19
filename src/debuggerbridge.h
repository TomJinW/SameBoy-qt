#pragma once
#include <Core/gb.h>
extern "C" {
unsigned sameboy_cpu_speed_factor(GB_gameboy_t*);
void sameboy_debug_attach(GB_gameboy_t*);
void sameboy_force_cpu_speed(GB_gameboy_t*,int);
unsigned sameboy_debug_run(GB_gameboy_t*,int mode=0);
void sameboy_debug_async(GB_gameboy_t*);
void sameboy_debug_enable(GB_gameboy_t*,bool);
void sameboy_debug_load_symbols(GB_gameboy_t*,const char*,size_t);
}
