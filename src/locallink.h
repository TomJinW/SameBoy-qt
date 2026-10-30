#pragma once
#include <Core/gb.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
// Every access to either connected core is serialized by this scheduler.
// The caller holding the lock may run both cores, as in Cocoa's master/slave loop.
class LocalLink {
public:
    std::unique_lock<std::mutex> lock(){return std::unique_lock<std::mutex>(mutex_);}
    void attachLocked(int side,GB_gameboy_t*core,bool*ready,uint64_t*cycles,std::atomic<bool>*paused,std::atomic<bool>*debugStopped=nullptr,std::atomic<bool>*debugPending=nullptr);
    void detachLocked(int side);
    bool runFrame(int side);
    void consumeFrameLocked(int side){*ports_[side].ready=false;condition_.notify_all();}
    void setSpeedLocked(int side,double speed){ports_[side].speed=speed;}
    double speedLocked()const;
    void startBitLocked(int side,bool bit){ports_[side].bit=bit;}
    bool endBitLocked(int side);
    void infraredLocked(int side,bool on);
private:
    struct Port{GB_gameboy_t*core=nullptr;bool*ready=nullptr;uint64_t*cycles=nullptr;std::atomic<bool>*paused=nullptr;bool bit=true,infrared=false;double speed=1;std::atomic<bool>*debugStopped=nullptr,*debugPending=nullptr;};
    Port ports_[2];std::mutex mutex_;std::condition_variable condition_;int64_t offset_=0;
};
