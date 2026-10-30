#include "../src/mousemotion.h"
#include <cmath>
#include <cstdio>
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int main(){MouseMotion m;QRectF view(100,50,640,576);m.move({420,338},view,false,100);CHECK(m.tilt.isNull());
 m.move({100,50},view,false,100);CHECK(m.tilt==QPointF(1,1));m.move({0,0},view,false,100);CHECK(m.tilt==QPointF(1,1));
 m.move({420,338},view,false,100,true);CHECK(m.tilt==QPointF(1,1));m.controllerActivity();m.move({420,338},view,false,100);CHECK(m.tilt==QPointF(1,1));
 m.click(true,true);CHECK(m.enabled&&m.leftButton);m.click(true,false);CHECK(!m.leftButton);m.move({420,338},view,false,100);CHECK(m.tilt.isNull());
 m.move({100,50},view,true,100);CHECK(std::abs(m.tilt.x()-1.6)<1e-6&&std::abs(m.tilt.y()-224/114.)<1e-6);
 m.move({100,50},view,false,50);CHECK(m.tilt==QPointF(.5,.5));std::puts("Cocoa mouse mode switching, view coordinates, border compensation, click and drag behavior passed.");}
