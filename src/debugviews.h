#pragma once
#include <Core/gb.h>
#include <QDialog>
#include <QImage>
#include <QFont>
#include <QColor>
#include <functional>
using DebugExecutor=std::function<QString(std::function<QString(GB_gameboy_t*)>)>;
using DebugAsyncExecutor=std::function<void(QObject*,std::function<QString(GB_gameboy_t*)>,std::function<void(QString)>)>;
struct MemorySnapshot{QByteArray bytes;unsigned base=0;int banks=1,totalSize=0,offset=0;};
MemorySnapshot debugMemory(GB_gameboy_t*,int space,int bank,int offset=0,int count=-1);
QString writeDebugMemory(GB_gameboy_t*,int space,int bank,unsigned offset,uint8_t value);
struct DebugObject{QImage image;int address,x,y,tile,flags;bool limited;};
struct DebugTile{int tile=0,bank=0,address=0,mapAddress=0,attributes=0;};
struct VramSnapshot{QImage image;QRect scrollRect;bool color=false;QVector<DebugObject>objects;QVector<DebugTile>tiles;QVector<QVector<QColor>>palettes;QVector<QVector<uint16_t>>rawPalettes;QStringList paletteNames;};
VramSnapshot debugVram(GB_gameboy_t*,int tab,int palette,int map,int tileset);
class MemoryDialog final:public QDialog{public:MemoryDialog(DebugExecutor,const QFont&,QWidget*parent=nullptr,DebugAsyncExecutor async={},std::function<void()>romModified={});void cartridgeChanged(){if(reload_)reload_();}private:std::function<void()>reload_;};
class VramDialog final:public QDialog{
public:VramDialog(DebugExecutor,const QFont&,QWidget*parent=nullptr,DebugAsyncExecutor async={});
 void cartridgeChanged(){if(reload_)reload_();}
 void requestRefresh(){if(refresh_)refresh_();}
protected:bool event(QEvent*)override;
private:std::function<void()>reload_,refresh_;bool initialSizeApplied_=false;
};
