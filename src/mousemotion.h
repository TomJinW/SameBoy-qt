#pragma once
#include <QPointF>
#include <QRectF>
struct MouseMotion {
    bool enabled=true,leftButton=false;
    QPointF tilt;
    void controllerActivity(){enabled=false;leftButton=false;}
    void click(bool left,bool pressed){if(left&&pressed)enabled=true;if(left)leftButton=pressed;}
    void move(QPointF point,QRectF view,bool border,int sensitivity,bool dragging=false){
        if(!enabled||dragging||view.isEmpty()||!view.contains(point))return;
        point-=view.topLeft();
        double x=-(2*point.x()/view.width()-1),y=-(2*point.y()/view.height()-1);
        // Match Cocoa, including its border-specific vertical compensation.
        if(border){x*=256/160.;y*=224/114.;}
        tilt=QPointF(x,y)*(sensitivity/100.);
    }
};
