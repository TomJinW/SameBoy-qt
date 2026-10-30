#pragma once
#include <array>
#include <chrono>
#include <cstdint>
using InputClock=std::chrono::steady_clock;
struct InputLease {
    std::array<uint8_t,4> keys{};
    std::array<InputClock::time_point,4> deadlines{};
    std::array<uint8_t,4> active(InputClock::time_point now=InputClock::now())const{
        auto result=keys;
        for(unsigned player=0;player<result.size();++player)if(deadlines[player]==InputClock::time_point{}||deadlines[player]<=now)result[player]=0;
        return result;
    }
};
