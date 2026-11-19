#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <Core/gb.h>
/* Yield only from the core's input callback, before GB_run enters CPU execution.
   This keeps the native debugger command loop (step/next/finish/undo/backstep) intact,
   but returns control to our scheduler instead of blocking a core or link mutex.
   The C++ input callback has already returned before longjmp; no C++ frames are skipped. */
typedef struct DebugFrame {jmp_buf jump;struct DebugFrame *previous;GB_gameboy_t *core;} DebugFrame;
static _Thread_local DebugFrame *active_frame;
static char *debug_input(GB_gameboy_t *gb){
    char *input=gb->async_input_callback?gb->async_input_callback(gb):NULL;
    if(input)return input;
    if(active_frame&&active_frame->core==gb)longjmp(active_frame->jump,1);
    return NULL;
}
void sameboy_debug_attach(GB_gameboy_t *gb){GB_set_input_callback(gb,debug_input);}
void sameboy_force_cpu_speed(GB_gameboy_t *gb,int mode){
    if(mode){gb->cgb_double_speed=mode==2;gb->speed_switch_countdown=0;}
}
unsigned sameboy_debug_run(GB_gameboy_t *gb,int mode){
    sameboy_force_cpu_speed(gb,mode);
    if(!gb->debug_active){unsigned cycles=GB_run(gb);sameboy_force_cpu_speed(gb,mode);return cycles;}
    DebugFrame frame;frame.previous=active_frame;frame.core=gb;active_frame=&frame;
    unsigned cycles=0;if(setjmp(frame.jump)==0)cycles=GB_run(gb);
    active_frame=frame.previous;sameboy_force_cpu_speed(gb,mode);return cycles;
}
void sameboy_debug_async(GB_gameboy_t *gb){GB_debugger_handle_async_commands(gb);}
void sameboy_debug_enable(GB_gameboy_t *gb,bool enabled){
    if(!enabled){gb->debug_stopped=false;gb->debug_next_command=false;gb->debug_fin_command=false;}
    GB_debugger_set_disabled(gb,!enabled);
}
void sameboy_debug_load_symbols(GB_gameboy_t *gb,const char *bytes,size_t size){
    for(size_t start=0;start<size;){size_t end=start;while(end<size&&bytes[end]!='\n')end++;
        if(end-start<4096){char line[4096],symbol[1024];memcpy(line,bytes+start,end-start);line[end-start]=0;char *comment=strchr(line,';');if(comment)*comment=0;
            unsigned bank,address;if(sscanf(line,"%x:%x %1023s",&bank,&address,symbol)==3&&bank<=0x1ff&&address<=0xffff)GB_debugger_add_symbol(gb,bank,address,symbol);
        }start=end+1;
    }
}

unsigned sameboy_cpu_speed_factor(GB_gameboy_t *gb){return gb->cgb_double_speed?2:1;}
