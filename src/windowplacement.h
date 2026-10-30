#pragma once
#include <QWidget>
#include <QGuiApplication>
#include <QScreen>
#include <limits>

// Prefer nearby free space; when none fits, keep the window onscreen and minimize overlap.
inline QPoint toolWindowPosition(QSize size,const QRect&owner,const QList<QRect>&screens){
 QPoint best=owner.topLeft();qint64 bestScore=std::numeric_limits<qint64>::max();
 for(int screenIndex=0;screenIndex<screens.size();++screenIndex){const auto area=screens[screenIndex];
  const int maxX=qMax(area.left(),area.right()-size.width()+1),maxY=qMax(area.top(),area.bottom()-size.height()+1);
  const QPoint candidates[]={QPoint(owner.right()+13,owner.center().y()-size.height()/2),QPoint(owner.left()-size.width()-12,owner.center().y()-size.height()/2),QPoint(owner.center().x()-size.width()/2,owner.bottom()+13),QPoint(owner.center().x()-size.width()/2,owner.top()-size.height()-12)};
  for(auto point:candidates){point.setX(qBound(area.left(),point.x(),maxX));point.setY(qBound(area.top(),point.y(),maxY));const QRect rect(point,size);const auto intersection=rect.intersected(owner);
   const qint64 overlap=qint64(intersection.width())*intersection.height();
   const auto distance=rect.center()-owner.center();const auto visible=rect.intersected(area);const qint64 outside=qint64(size.width())*size.height()-qint64(visible.width())*visible.height();const qint64 score=outside*1000000000+overlap*1000000+screenIndex*100000+distance.manhattanLength();
   if(score<bestScore){bestScore=score;best=point;}
  }
 }
 return best;
}
inline void placeNewWindow(QWidget*window,QWidget*owner){
 if(!window||!owner)return;QList<QRect>areas;auto*preferred=owner->screen();if(preferred)areas.append(preferred->availableGeometry());
 for(auto*screen:QGuiApplication::screens())if(screen!=preferred)areas.append(screen->availableGeometry());
 window->move(toolWindowPosition(window->frameGeometry().size(),owner->frameGeometry(),areas));
}
