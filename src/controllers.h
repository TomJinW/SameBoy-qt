#pragma once
#include "settings.h"
#include <QElapsedTimer>
#include <QVector>
#include <memory>
#include <array>
#include <functional>
struct SDL_Gamepad;
enum class JoyConType { None, Left, Right, Pair };
struct PadSnapshot {
    QString id,name;
    quint32 buttons=0;
    float x=0,y=0,turbo=0,slow=0,motionX=0,motionY=0;
    bool sensor=false;
    JoyConType joyCon=JoyConType::None;
    float rightX=0,rightY=0,motionUp=0;
    QStringList sources; // Physical devices represented by a logical controller.
    bool horizontal=false;

};
struct PlayerInput { uint8_t keys=0,actions=0; float x=0,y=0; bool analog=false; };
struct ControllerInput {
    std::array<PlayerInput,4> players{};
    float motionX=0,motionY=0,speed=1;
    bool motion=false;
    quint8 hotkeys=0;
};
QString controllerIdentity(const QString&guid,const QString&serial,const QString&path,unsigned connection);
QString joyConPairId(const QString&left,const QString&right);
QVector<PadSnapshot> arrangeJoyCons(const QVector<PadSnapshot>&raw,const AppSettings&settings);
bool pairJoyCons(AppSettings&,const PadSnapshot&,const PadSnapshot&);
bool separateJoyCons(AppSettings&,const QString&pairId);
bool autoPairJoyCons(const QVector<PadSnapshot>&raw,AppSettings&);
// Commit automatic arrangements as a candidate; failed persistence must not
// alter live mapping or be retried on every input poll.
class JoyConAutoPairing {
public:
 bool update(const QVector<PadSnapshot>&,const AppSettings&,const std::function<bool(const AppSettings&)>&);
 void reset(){failedInput_.clear();}
private:QByteArray failedInput_;bool applying_=false;
};
// Pure mapping is also used by virtual-controller tests.
ControllerInput mapControllers(const QVector<PadSnapshot>& pads,const AppSettings& settings);
class Controllers {
public:
    Controllers();
    ~Controllers();
    void poll();
    QVector<PadSnapshot> logicalDevices(const AppSettings&s) const {return arrangeJoyCons(snapshots_,s);}
    const QVector<PadSnapshot>& devices() const { return snapshots_; }
    QString error() const {return error_;}
    void rumble(const AppSettings&,float amplitude);
    static QString buttonName(int button);
private:
    struct Device { unsigned id; SDL_Gamepad* handle; QString key; bool connectionOnly=false; };
    QVector<Device> devices_;
    QVector<PadSnapshot> snapshots_;
    QElapsedTimer discovery_,rumbleTimer_;
    QString error_;
    bool initialized_=false,discovered_=false;
    float lastRumble_=-1;
    QString lastRumbleDevice_;
};
