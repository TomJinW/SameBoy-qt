#include "locallink.h"
#include "debuggerbridge.h"
#include <algorithm>
#include <chrono>
void LocalLink::attachLocked(int side,GB_gameboy_t*core,bool*ready,uint64_t*cycles,std::atomic<bool>*paused,std::atomic<bool>*debugStopped,std::atomic<bool>*debugPending){ports_[side]={core,ready,cycles,paused,true,false,1,debugStopped,debugPending};offset_=0;}
void LocalLink::detachLocked(int side){ports_[side]={};offset_=0;condition_.notify_all();auto&other=ports_[1-side];if(other.core)GB_set_infrared_input(other.core,false);}
bool LocalLink::endBitLocked(int side){auto&other=ports_[1-side];if(!other.core)return true;bool bit=GB_serial_get_data_bit(other.core);GB_serial_set_data_bit(other.core,ports_[side].bit);return bit;}
void LocalLink::infraredLocked(int side,bool on){ports_[side].infrared=on;if(ports_[1-side].core)GB_set_infrared_input(ports_[1-side].core,on);}
bool LocalLink::runFrame(int side){
    auto guard=lock();auto&mine=ports_[side];if(!mine.core)return false;
    for(auto&p:ports_)if(p.core&&p.paused->load())return false;
    const auto frequency0=ports_[0].core?GB_get_clock_rate(ports_[0].core):1;
    const auto frequency1=ports_[1].core?GB_get_clock_rate(ports_[1].core):1;
    while(!*mine.ready){
        int next=offset_<=0?0:1;
        for(int i=0;i<2;++i)if(ports_[i].core&&ports_[i].debugStopped&&ports_[i].debugStopped->load()){auto&p=ports_[i];if(p.debugStopped&&p.debugStopped->load()&&(!p.debugPending||!p.debugPending->load()))return false;next=i;break;}
        if(!ports_[next].core)next=1-next;
        // Do not run one console ahead while the other has an unconsumed frame.
        if(ports_[next].core&&*ports_[next].ready){
            condition_.wait_for(guard,std::chrono::milliseconds(10),[&]{return !ports_[next].core||!*ports_[next].ready||mine.paused->load();});
            if(mine.paused->load()||(ports_[1-side].core&&ports_[1-side].paused->load()))return false;
            if(ports_[next].core&&*ports_[next].ready)return false;
            continue;
        }
        auto&p=ports_[next];if(!p.core||*p.ready)break;
        const auto cycles=sameboy_debug_run(p.core);if(!cycles&&GB_debugger_is_stopped(p.core)){if(p.debugStopped)*p.debugStopped=true;return false;}if(p.debugStopped&&p.debugStopped->load())*p.debugStopped=GB_debugger_is_stopped(p.core);*p.cycles+=cycles;
        offset_+=next==0?int64_t(cycles)*frequency1:-int64_t(cycles)*frequency0;
    }
    return *mine.ready;
}

double LocalLink::speedLocked()const{
    double speed=1;
    for(const auto&p:ports_)if(p.core){if(p.speed==0)return 0;speed=std::max(speed,p.speed);}
    return speed;
}
