#pragma once
#include <QImage>
#include <QMutex>
#include <atomic>
QImage prepareCameraImage(const QImage&,bool mirror=false);
class CameraInput {
public:
    enum Mode{Noise,Device,Image};
    struct Snapshot{QImage image;quint64 epoch=0,version=0;int mode=Device;bool failed=false;};
    Snapshot snapshot()const;
    void configure(int mode);
    void publish(const QImage&prepared);
    void fail();
    void request(){requested_.store(true);}
    bool takeRequest(){return requested_.exchange(false);}
private:
    mutable QMutex mutex_;Snapshot state_;std::atomic<bool>requested_{false};
};
