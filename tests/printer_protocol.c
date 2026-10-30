#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GB_INTERNAL
#include <Core/gb.h>
// Exercise upstream packet decoding through its serial callbacks, from C so the core's private C headers stay out of Qt/C++.
static void byte(GB_gameboy_t*gb,uint8_t value){for(int b=7;b>=0;--b){gb->serial_transfer_bit_start_callback(gb,(value>>b)&1);gb->serial_transfer_bit_end_callback(gb);}}
static void packet(GB_gameboy_t*gb,uint8_t command,const uint8_t*data,unsigned size){
    byte(gb,0x88);byte(gb,0x33);byte(gb,command);byte(gb,0);byte(gb,size&255);byte(gb,size>>8);
    unsigned sum=command+(size&255)+(size>>8);
    for(unsigned i=0;i<size;++i){byte(gb,data[i]);sum+=data[i];}byte(gb,sum&255);byte(gb,sum>>8);byte(gb,0);byte(gb,0);
}
bool test_printer_packet(GB_gameboy_t*gb){
    packet(gb,1,0,0);uint8_t data[640];for(int i=0;i<640;i+=2){data[i]=0xaa;data[i+1]=0xcc;}packet(gb,4,data,sizeof(data));
    const uint8_t start[]={1,0x23,0xe4,0x40};packet(gb,2,start,sizeof(start));
    gb->printer.time_remaining=1;for(int i=0;i<10000&&gb->printer.time_remaining;++i)GB_run(gb);
    return gb->printer.time_remaining==0;
}
bool test_printer_clean(GB_gameboy_t*gb){return gb->printer.image_offset==0&&GB_get_built_in_accessory(gb)==GB_ACCESSORY_PRINTER;}
