#pragma once
#include <QString>
#include <atomic>
#include <utility>
// A request may finish once started, but an ended session cannot start new work.
class RequestSession {
public:
    void close(){active_.store(false);}
    bool active()const{return active_.load();}
    template<class Operation> QString run(Operation&&operation)const{if(!active())return "Debug session closed before execution";return std::forward<Operation>(operation)();}
    int pending=0; // GUI thread only; each service session owns its own counter.
private:std::atomic<bool>active_{true};
};
