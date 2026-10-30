#include "controllers.h"
#include <SDL3/SDL.h>
#include <QSet>
#include <QHash>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace {
// All controller backends run on the GUI thread. SDL shares physical handles
// across windows, so only the backend that last drove a motor may stop it.
QHash<unsigned,const Controllers*> rumbleOwners;
void releaseRumble(unsigned id,SDL_Gamepad*handle,const Controllers*owner){
    if(rumbleOwners.value(id)!=owner)return;
    SDL_RumbleGamepad(handle,0,0,0);rumbleOwners.remove(id);
}
float axis(float value,int deadzone) {
    const float d=deadzone/100.f;
    return std::abs(value)<=d?0:std::copysign((std::abs(value)-d)/(1-d),value);
}
QVector<int> assignments(const QVector<PadSnapshot>&pads,const AppSettings&s) {
    QVector<int> result(4,-1);QSet<int> used;
    for(int p=0;p<4;++p)if(!s.controllers[p].isEmpty()&&s.controllers[p]!="none")
        for(int i=0;i<pads.size();++i)if((pads[i].id==s.controllers[p]||pads[i].sources.contains(s.controllers[p]))&&!used.contains(i)){result[p]=i;used.insert(i);break;}
    for(int p=0;p<4;++p)if(s.controllers[p].isEmpty())
        for(int i=0;i<pads.size();++i)if(!used.contains(i)){result[p]=i;used.insert(i);break;}
    return result;
}
}
ControllerInput mapControllers(const QVector<PadSnapshot>&raw,const AppSettings&s) {
    const auto pads=arrangeJoyCons(raw,s);
    ControllerInput out;auto selected=assignments(pads,s);
    for(int p=0;p<4;++p){
        if(selected[p]<0)continue;
        const auto&pad=pads[selected[p]];auto&input=out.players[p];
        for(int a=0;a<15;++a){int b=s.controllerButtons[p][a];if(b<0||b>=32||!(pad.buttons&(1u<<b)))continue;
            if(a<8)input.keys|=1u<<a;else if(a<13)input.actions|=1u<<(a-8);else if(p==0)out.hotkeys|=1u<<(a-13);
        }
        input.x=axis(pad.x,s.deadzone);input.y=axis(pad.y,s.deadzone);
        bool motion=p==0&&(pad.sensor||std::abs(input.x)>0||std::abs(input.y)>0);
        if(p==0){
            out.motion=motion;
            out.motionX=(pad.sensor&&!s.preferJoystick?pad.motionX:-input.x)*s.motionSensitivity/100.f;
            out.motionY=(pad.sensor&&!s.preferJoystick?pad.motionY:-input.y)*s.motionSensitivity/100.f;
            if(s.analogControls){if(pad.turbo>.05f)out.speed=1+pad.turbo*2;else if(pad.slow>.05f)out.speed=std::max(1.f/3,1-pad.slow);}
        }
        // Motion cartridges consume the stick for tilt; ordinary games use it as a D-pad.
        input.analog=s.fauxAnalog;
    }
    return out;
}
QString controllerIdentity(const QString&guid,const QString&serial,const QString&path,unsigned connection){
    if(!serial.isEmpty())return guid+":"+serial;
    if(!path.isEmpty())return guid+":"+path;
    // SDL connection IDs are unique across backend instances for this process.
    // Without physical metadata, do not guess which identical pad reconnected.
    static const auto session=QUuid::createUuid().toString(QUuid::WithoutBraces);
    return "connection:"+session+":"+guid+":"+QString::number(connection);
}
Controllers::Controllers(){
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    // Keep physical halves stable; our arrangement layer combines and rotates them.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS,"0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS,"1");
    initialized_=SDL_InitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_SENSOR);
    if(!initialized_)error_=QString::fromUtf8(SDL_GetError());
    discovery_.start();rumbleTimer_.start();poll();
}
Controllers::~Controllers(){for(auto&d:devices_){releaseRumble(d.id,d.handle,this);SDL_CloseGamepad(d.handle);}if(initialized_)SDL_QuitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_SENSOR);}
void Controllers::poll(){
    if(!initialized_)return;
    SDL_PumpEvents();
    // SDL owns device discovery, but enumerate/open at most twice per second.
    if(!discovered_||discovery_.elapsed()>=500){
        discovered_=true;
        discovery_.restart();int count=0;auto ids=SDL_GetGamepads(&count);
        for(int i=devices_.size()-1;i>=0;--i)if(!SDL_GamepadConnected(devices_[i].handle)){rumbleOwners.remove(devices_[i].id);SDL_CloseGamepad(devices_[i].handle);devices_.removeAt(i);}
        for(int i=0;i<count;++i){bool exists=false;for(auto&d:devices_)if(d.id==ids[i])exists=true;if(exists)continue;
            auto*g=SDL_OpenGamepad(ids[i]);if(!g)continue;
            char guid[33];SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]),guid,sizeof(guid));
            const auto serial=QString::fromUtf8(SDL_GetGamepadSerial(g)),path=QString::fromUtf8(SDL_GetGamepadPath(g));
            bool connectionOnly=serial.isEmpty()&&path.isEmpty();
            auto base=controllerIdentity(QString::fromLatin1(guid),serial,path,ids[i]);
            if(std::any_of(devices_.cbegin(),devices_.cend(),[&](const Device&device){return device.key==base;})){
                // Some drivers/devices report duplicated serials or paths too.
                base=controllerIdentity(QString::fromLatin1(guid),{},{},ids[i]);connectionOnly=true;
            }
            if(SDL_GamepadHasSensor(g,SDL_SENSOR_ACCEL))SDL_SetGamepadSensorEnabled(g,SDL_SENSOR_ACCEL,true);
            devices_.append({ids[i],g,base,connectionOnly});
        }SDL_free(ids);
    }
    snapshots_.clear(); // SDL_PumpEvents already updates joystick/gamepad state.
    for(auto&d:devices_){if(!SDL_GamepadConnected(d.handle))continue;PadSnapshot p;p.id=d.key;p.name=QString::fromUtf8(SDL_GetGamepadName(d.handle));if(d.connectionOnly)p.name=QObject::tr("%1 (connection %2)").arg(p.name).arg(d.id);
        for(int b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)if(SDL_GetGamepadButton(d.handle,SDL_GamepadButton(b)))p.buttons|=1u<<b;
        switch(SDL_GetGamepadType(d.handle)){
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:p.joyCon=JoyConType::Left;break;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:p.joyCon=JoyConType::Right;break;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:p.joyCon=JoyConType::Pair;break;
            default:break;
        }
        p.rightX=SDL_GetGamepadAxis(d.handle,SDL_GAMEPAD_AXIS_RIGHTX)/32768.f;
        p.rightY=SDL_GetGamepadAxis(d.handle,SDL_GAMEPAD_AXIS_RIGHTY)/32768.f;
        p.x=SDL_GetGamepadAxis(d.handle,SDL_GAMEPAD_AXIS_LEFTX)/32768.f;p.y=SDL_GetGamepadAxis(d.handle,SDL_GAMEPAD_AXIS_LEFTY)/32768.f;
        p.turbo=std::max(0.f,SDL_GetGamepadAxis(d.handle,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)/32767.f);
        p.slow=std::max(0.f,SDL_GetGamepadAxis(d.handle,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)/32767.f);
        // Extra virtual button slots allow triggers to be assigned in the same table.
        if(p.slow>.5f)p.buttons|=1u<<30;if(p.turbo>.5f)p.buttons|=1u<<31;
        float accel[3];if(SDL_GetGamepadSensorData(d.handle,SDL_SENSOR_ACCEL,accel,3)){p.sensor=true;p.motionX=accel[0]/SDL_STANDARD_GRAVITY;p.motionY=accel[2]/SDL_STANDARD_GRAVITY;p.motionUp=accel[1]/SDL_STANDARD_GRAVITY;}
        snapshots_.append(p);
    }
    // We consume snapshots, so discard SDL input events instead of letting them accumulate.
    SDL_FlushEvents(SDL_EVENT_JOYSTICK_AXIS_MOTION,SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED);
    SDL_FlushEvents(SDL_EVENT_SENSOR_UPDATE,SDL_EVENT_SENSOR_UPDATE);
}
void Controllers::rumble(const AppSettings&s,float amplitude){
    const float strength=std::clamp(s.rumbleStrength,0,100)/100.f;
    amplitude=std::isfinite(amplitude)?std::clamp(amplitude,0.f,1.f):0;
    // Cocoa GBView.setRumble preserves weak pulses with this response curve.
    if(!s.rumbleMode||strength==0)amplitude=0;
    else if(strength!=1)amplitude=std::pow(amplitude,strength)*strength;
    const auto logical=arrangeJoyCons(snapshots_,s);auto selected=assignments(logical,s);
    QString key=selected[0]<0?QString():logical[selected[0]].id;
    QStringList sources=selected[0]<0?QStringList():logical[selected[0]].sources;
    bool lostOwnership=false;
    if(amplitude>0)for(const auto&d:devices_)if((d.key==key||sources.contains(d.key))&&rumbleOwners.value(d.id)!=this)lostOwnership=true;
    if(!lostOwnership&&amplitude==lastRumble_&&key==lastRumbleDevice_&&(amplitude==0||rumbleTimer_.elapsed()<100))return;
    lastRumble_=amplitude;lastRumbleDevice_=key;rumbleTimer_.restart();
    for(auto&d:devices_){
        const auto strength=(d.key==key||sources.contains(d.key))?Uint16(amplitude*65535):0;
        if(strength){if(SDL_RumbleGamepad(d.handle,strength,strength,150))rumbleOwners.insert(d.id,this);}
        else releaseRumble(d.id,d.handle,this);
    }
}
QString Controllers::buttonName(int b){
    if(b<0)return QObject::tr("Unassigned");if(b==30)return QObject::tr("Left trigger");if(b==31)return QObject::tr("Right trigger");
    static const char*names[]={"A / Cross (South)","B / Circle (East)","X / Square (West)","Y / Triangle (North)",
        "Back / Select","Guide","Start","Left stick click","Right stick click","Left shoulder","Right shoulder",
        "D-pad Up","D-pad Down","D-pad Left","D-pad Right","Misc 1","Right paddle 1","Left paddle 1","Right paddle 2","Left paddle 2","Touchpad","Misc 2","Misc 3","Misc 4","Misc 5","Misc 6"};
    return b<26?QObject::tr(names[b]):QString::number(b);
}
