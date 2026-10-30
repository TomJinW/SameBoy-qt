#include "../src/controllers.h"
#include <SDL3/SDL.h>
#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <cmath>
#include <QProcess>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Failed line %d: %s (%s)\n",__LINE__,#x,SDL_GetError());return 1;}}while(0)
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--identity")){std::printf("%s",qPrintable(controllerIdentity("guid",{},{},42)));return 0;}
    QProcess identityProcess;identityProcess.start(app.applicationFilePath(),{"--identity"});CHECK(identityProcess.waitForFinished(2000)&&identityProcess.exitCode()==0);
    if(identityProcess.readAllStandardOutput()==controllerIdentity("guid",{},{},42).toUtf8()){std::fputs("Anonymous controller identity survived a process restart\n",stderr);return 1;}
    Controllers backend;CHECK(backend.error().isEmpty());
    CHECK(controllerIdentity("guid","serial","path",42)=="guid:serial");
    CHECK(controllerIdentity("guid",{},"path",42)=="guid:path");
    CHECK(controllerIdentity("guid",{},{},42)!=controllerIdentity("guid",{},{},43));
    Uint16 vibration=0;SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=26;desc.name="SameBoy virtual test pad";
    desc.button_mask=(1u<<26)-1;desc.axis_mask=63;desc.userdata=&vibration;
    desc.Rumble=[](void*u,Uint16 low,Uint16)->bool{*static_cast<Uint16*>(u)=low;return true;};
    SDL_VirtualJoystickSensorDesc sensor{SDL_SENSOR_ACCEL,60};desc.nsensors=1;desc.sensors=&sensor;
    desc.SetSensorsEnabled=[](void*,bool){return true;};
    auto id=SDL_AttachVirtualJoystick(&desc);CHECK(id);auto*j=SDL_OpenJoystick(id);CHECK(j);
    SDL_SetJoystickVirtualAxis(j,4,-32768);SDL_SetJoystickVirtualAxis(j,5,-32768);
    QThread::msleep(510);backend.poll();CHECK(backend.devices().size()==1);
    AppSettings s;SDL_SetJoystickVirtualButton(j,0,true);SDL_SetJoystickVirtualAxis(j,0,24000);backend.poll();
    auto input=mapControllers(backend.devices(),s);CHECK(input.players[0].keys&GB_KEY_A_MASK);CHECK(input.players[0].x>.5);
    CHECK(input.players[1].keys==0);
    s.controllers[0]="none";input=mapControllers(backend.devices(),s);CHECK(input.players[0].keys==0);CHECK(input.players[1].keys&GB_KEY_A_MASK);
    s.controllers[0]=backend.devices()[0].id;s.controllers[1]=s.controllers[0];input=mapControllers(backend.devices(),s);CHECK(input.players[1].keys==0);
    s.controllerButtons[0][4]=1;input=mapControllers(backend.devices(),s);CHECK(!(input.players[0].keys&GB_KEY_A_MASK));
    SDL_SetJoystickVirtualButton(j,1,true);SDL_SetJoystickVirtualButton(j,7,true);backend.poll();input=mapControllers(backend.devices(),s);CHECK(input.players[0].keys&GB_KEY_A_MASK);CHECK(input.hotkeys&1);
    SDL_SetJoystickVirtualAxis(j,0,1000);backend.poll();input=mapControllers(backend.devices(),s);CHECK(input.players[0].x==0);
    SDL_SetJoystickVirtualAxis(j,5,32767);backend.poll();s.analogControls=true;input=mapControllers(backend.devices(),s);CHECK(input.speed>2.9);
    float accel[]={SDL_STANDARD_GRAVITY/2,0,SDL_STANDARD_GRAVITY/4};
    CHECK(SDL_SendJoystickVirtualSensorData(j,SDL_SENSOR_ACCEL,SDL_GetTicksNS(),accel,3));backend.poll();
    input=mapControllers(backend.devices(),s);CHECK(input.motion);CHECK(std::abs(input.motionX-.5)<.01);
    s.preferJoystick=true;input=mapControllers(backend.devices(),s);CHECK(input.motionX==0);
    s.rumbleStrength=50;backend.rumble(s,.25);
    if(vibration<16380||vibration>16386){std::fputs("Rumble strength does not follow Cocoa response curve\n",stderr);return 1;}
    struct RumbleCase{int percent;float amplitude;int motor;};
    for(const auto&sample:{RumbleCase{25,.0625f,8191},RumbleCase{75,.0625f,6143},RumbleCase{100,.0625f,4095},RumbleCase{50,1.f,32767},RumbleCase{25,0,0},RumbleCase{0,1,0},RumbleCase{100,1,65535}}){
      s.rumbleStrength=sample.percent;backend.rumble(s,sample.amplitude);CHECK(std::abs(int(vibration)-sample.motor)<=3);
    }
    s.rumbleMode=0;backend.rumble(s,1);CHECK(vibration==0);s.rumbleMode=GB_RUMBLE_CARTRIDGE_ONLY;
    s.rumbleStrength=100;
    backend.rumble(s,.5);CHECK(vibration>30000&&vibration<34000);backend.rumble(s,0);CHECK(vibration==0);
    CHECK(SDL_DetachVirtualJoystick(id));SDL_CloseJoystick(j);backend.poll();input=mapControllers(backend.devices(),s);CHECK(input.players[0].keys==0);CHECK(input.motionX==0);
    // Identical pads with no serial or path must remain separately assignable.
    Uint16 firstRumble=0,secondRumble=0;desc.userdata=&firstRumble;auto first=SDL_AttachVirtualJoystick(&desc);CHECK(first);desc.userdata=&secondRumble;auto second=SDL_AttachVirtualJoystick(&desc);CHECK(second);
    auto*firstHandle=SDL_OpenJoystick(first);auto*secondHandle=SDL_OpenJoystick(second);CHECK(firstHandle&&secondHandle);
    SDL_SetJoystickVirtualButton(firstHandle,0,true);SDL_SetJoystickVirtualButton(secondHandle,1,true);
    QThread::msleep(510);backend.poll();CHECK(backend.devices().size()==2);
    if(backend.devices()[0].id==backend.devices()[1].id){std::fputs("Identical controllers share an identity\n",stderr);return 1;}
    const auto firstKey=backend.devices()[0].id,secondKey=backend.devices()[1].id;
    AppSettings identical;identical.controllers[0]=firstKey;identical.controllers[1]=secondKey;
    input=mapControllers(backend.devices(),identical);CHECK(input.players[0].keys==GB_KEY_A_MASK);CHECK(input.players[1].keys==GB_KEY_B_MASK);
    backend.rumble(identical,.5);CHECK(firstRumble>0&&secondRumble==0);
    {Controllers another;CHECK(another.devices().size()==2);CHECK(another.devices()[0].id==firstKey&&another.devices()[1].id==secondKey);}
    if(firstRumble==0){std::fputs("Destroying an unrelated controller backend stopped active rumble\n",stderr);return 1;}
    {
      Controllers idle;idle.rumble(identical,0);CHECK(firstRumble>0&&secondRumble==0);
      AppSettings otherPad=identical;otherPad.controllers[0]=secondKey;idle.rumble(otherPad,.75);CHECK(firstRumble>0&&secondRumble>48000);
      backend.rumble(identical,0);CHECK(firstRumble==0&&secondRumble>48000);
      backend.rumble(identical,.5);CHECK(firstRumble>30000&&secondRumble>48000);
      idle.rumble(identical,.25);CHECK(firstRumble>15000&&firstRumble<18000&&secondRumble==0);
      backend.rumble(identical,0);CHECK(firstRumble>15000); // A former owner cannot stop the new one.
      backend.rumble(identical,.5);CHECK(firstRumble>30000); // Reclaim without waiting for refresh interval.
    }
    CHECK(firstRumble>30000);backend.rumble(identical,0);CHECK(firstRumble==0);
    {Controllers owner;owner.rumble(identical,.5);CHECK(firstRumble>30000);}
    CHECK(firstRumble==0); // Destroying the actual owner still stops its motor.
    CHECK(SDL_DetachVirtualJoystick(first));SDL_CloseJoystick(firstHandle);QThread::msleep(510);backend.poll();
    CHECK(backend.devices().size()==1&&backend.devices()[0].id==secondKey);input=mapControllers(backend.devices(),identical);CHECK(input.players[0].keys==0&&input.players[1].keys==GB_KEY_B_MASK);
    desc.userdata=&firstRumble;auto replacement=SDL_AttachVirtualJoystick(&desc);CHECK(replacement);auto*replacementHandle=SDL_OpenJoystick(replacement);CHECK(replacementHandle);SDL_SetJoystickVirtualButton(replacementHandle,0,true);
    QThread::msleep(510);backend.poll();input=mapControllers(backend.devices(),identical);CHECK(input.players[0].keys==0&&input.players[1].keys==GB_KEY_B_MASK);
    CHECK(SDL_DetachVirtualJoystick(replacement));SDL_CloseJoystick(replacementHandle);CHECK(SDL_DetachVirtualJoystick(second));SDL_CloseJoystick(secondHandle);backend.poll();
    // Canonical vertical snapshots, like the SDL HIDAPI driver used by real Joy-Cons.
    PadSnapshot left,right;left.id="left:one";left.name="Joy-Con L";left.joyCon=JoyConType::Left;
    right.id="right:one";right.name="Joy-Con R";right.joyCon=JoyConType::Right;
    left.x=.8;left.y=-.4;left.buttons=1u<<13;left.sensor=true;left.motionX=.9;
    right.rightX=.6;right.rightY=.2;right.buttons=1u<<1;right.sensor=true;right.motionX=.3;right.motionY=-.2;
    AppSettings joy;joy.joyConAutoPair=false;
    auto logical=arrangeJoyCons({left,right},joy);CHECK(logical.size()==2);
    CHECK(logical[0].horizontal&&std::abs(logical[0].x+.4)<.001&&std::abs(logical[0].y+.8)<.001);
    CHECK(logical[0].buttons&1);CHECK(logical[1].buttons&1);CHECK(std::abs(logical[1].x+.2)<.001);
    joy.joyConGrips[left.id]=0;logical=arrangeJoyCons({left,right},joy);CHECK(!logical[0].horizontal);CHECK(logical[0].x==left.x);
    joy.controllers[0]=right.id;joy.controllers[1]=left.id;
    CHECK(pairJoyCons(joy,right,left));CHECK(!pairJoyCons(joy,left,right));
    logical=arrangeJoyCons({right,left},joy);CHECK(logical.size()==1);CHECK(logical[0].joyCon==JoyConType::Pair);
    CHECK(logical[0].x==left.x&&logical[0].motionX==right.motionX);CHECK(logical[0].sources.size()==2);
    CHECK(joy.controllers[0]==logical[0].id&&joy.controllers[1]=="none");
    auto pairedInput=mapControllers({left,right},joy);CHECK(pairedInput.players[1].keys==0);
    const auto pairId=logical[0].id;
    auto partial=arrangeJoyCons({left},joy);CHECK(partial.size()==1&&partial[0].id==pairId);CHECK(!partial[0].sensor&&partial[0].motionX==0);
    CHECK(!(partial[0].buttons&(1u<<1))); // Right-hand held buttons must release on disconnect.
    CHECK(arrangeJoyCons({},joy).isEmpty());
    CHECK(separateJoyCons(joy,pairId));CHECK(joy.controllers[0]==left.id);joy.joyConAutoPair=true;
    CHECK(!autoPairJoyCons({left,right},joy)); // Explicit split survives automatic pairing and reconnect.
    joy.joyConSingles.clear();CHECK(autoPairJoyCons({left,right},joy));CHECK(!autoPairJoyCons({left,right},joy));
    PadSnapshot left2=left;left2.id="left:two";CHECK(!pairJoyCons(joy,left,left2));
    JoyConAutoPairing automatic;AppSettings live;int attempts=0;
    bool candidateValid=true;auto reject=[&](const AppSettings&candidate){++attempts;candidateValid&=candidate.joyConPairs.size()==1&&live.joyConPairs.isEmpty();return false;};
    CHECK(!automatic.update({left,right},live,reject));CHECK(live.joyConPairs.isEmpty());
    CHECK(!automatic.update({right,left},live,reject));CHECK(attempts==1);
    auto moving=left;moving.buttons=123;CHECK(!automatic.update({moving,right},live,reject));CHECK(attempts==1);
    CHECK(!automatic.update({left},live,reject));CHECK(!automatic.update({left,right},live,reject));CHECK(attempts==2);
    automatic.reset();CHECK(automatic.update({left,right},live,[&](const AppSettings&candidate){
        candidateValid&=!automatic.update({left,right},live,reject);live=candidate;return true;
    }));CHECK(live.joyConPairs.size()==1&&attempts==2&&candidateValid);
    std::puts("Joy-Con: horizontal/vertical single grips, pair/split, stable identity, player assignment, right-hand sensor, reconnect and auto-pair passed.");
    std::puts("SDL virtual pad: hotplug, assignment, remapping, deadzone, triggers, accelerometer, rumble, disconnect passed.");
}
