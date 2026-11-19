#include "debugviews.h"
#include <QAbstractTableModel>
#include <cstring>
#include <array>
#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QFontMetrics>
#include <QPainterPath>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QRegularExpressionValidator>
namespace {
QString hex(unsigned v,int digits=4){return QString::number(v,16).rightJustified(digits,'0').toUpper();}
class HexModel:public QAbstractTableModel{
public:MemorySnapshot snapshot;int space=0,bank=0;std::function<bool(unsigned,uint8_t)>write;
 int length()const{return snapshot.totalSize?snapshot.totalSize:snapshot.bytes.size();}
 bool cached(int offset)const{return offset>=snapshot.offset&&offset<snapshot.offset+snapshot.bytes.size();}
 Qt::ItemFlags flags(const QModelIndex&i)const override{auto result=QAbstractTableModel::flags(i);if(i.isValid()&&i.column()<16&&cached(i.row()*16+i.column()))result|=Qt::ItemIsEditable;return result;}
 bool setData(const QModelIndex&i,const QVariant&value,int role)override{if(role!=Qt::EditRole||!(flags(i)&Qt::ItemIsEditable)||!write)return false;auto text=value.toString().trimmed();if(text.startsWith("$"))text.remove(0,1);if(text.startsWith("0x",Qt::CaseInsensitive))text.remove(0,2);bool ok=false;auto byte=text.toUInt(&ok,16);return ok&&!text.isEmpty()&&text.size()<=2&&byte<=255&&write(i.row()*16+i.column(),uint8_t(byte));}
 int rowCount(const QModelIndex&p={})const override{return p.isValid()?0:(length()+15)/16;}
 int columnCount(const QModelIndex&p={})const override{return p.isValid()?0:17;}
 QVariant data(const QModelIndex&i,int role)const override{if(!i.isValid()||(role!=Qt::DisplayRole&&role!=Qt::EditRole))return {};const int offset=i.row()*16;
  if(i.column()==16){char text[16];for(int n=0;n<16;++n){const auto c=cached(offset+n)?uint8_t(snapshot.bytes[offset+n-snapshot.offset]):uint8_t(0);text[n]=c>=32&&c<127?char(c):'.';}return QString::fromLatin1(text,qMin(16,length()-offset));}
  const int position=offset+i.column();if(!cached(position))return {};
  static const auto values=[](){std::array<QString,256> result;for(unsigned i=0;i<256;++i)result[i]=hex(i,2);return result;}();return values[uint8_t(snapshot.bytes[position-snapshot.offset])];
 }
 QVariant headerData(int section,Qt::Orientation orientation,int role)const override{if(role!=Qt::DisplayRole)return {};if(orientation==Qt::Vertical)return hex(snapshot.base+section*16);return section==16?QString("ASCII"):hex(section,1);}
 void set(MemorySnapshot value){const int total=value.totalSize?value.totalSize:value.bytes.size();const bool reset=total!=length()||value.base!=snapshot.base;
  if(!reset&&value.offset==snapshot.offset&&value.bytes==snapshot.bytes){snapshot.banks=value.banks;return;}
  if(reset)beginResetModel();snapshot=std::move(value);setProperty("snapshotBytes",snapshot.bytes.size());if(reset)endResetModel();
  else if(!snapshot.bytes.isEmpty())emit dataChanged(index(snapshot.offset/16,0),index((snapshot.offset+snapshot.bytes.size()-1)/16,16),{Qt::DisplayRole});
 }
};
class HexTable:public QTableView{
public:std::function<void()>resized;
protected:void resizeEvent(QResizeEvent*e)override{QTableView::resizeEvent(e);if(resized)resized();}
};
class ByteDelegate:public QStyledItemDelegate{
public:using QStyledItemDelegate::QStyledItemDelegate;
 QWidget*createEditor(QWidget*parent,const QStyleOptionViewItem&,const QModelIndex&)const override{auto*edit=new QLineEdit(parent);edit->setMaxLength(2);edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9A-Fa-f]{0,2}"),edit));return edit;}
};
class DebugScrollArea:public QScrollArea{
protected:void paintEvent(QPaintEvent*)override{QPainter painter(viewport());painter.fillRect(viewport()->rect(),palette().brush(QPalette::Mid));}
};
class DebugImage:public QWidget{
public:QImage image;bool grid=false,scrolling=false,tileset=false;QRect scrollRect;std::function<void(QPoint)>hover;
 DebugImage(){setMouseTracking(true);}
 void setImage(QImage value){if(image==value)return;const bool resized=image.size()!=value.size();image=std::move(value);if(resized)setFixedSize(image.size()*2);update();}
protected:void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),Qt::white);p.drawImage(rect(),image);
 if(grid){p.setPen(QColor(100,100,100,160));for(int x=0;x<=width();x+=16)p.drawLine(x,0,x,height());for(int y=0;y<=height();y+=16)p.drawLine(0,y,width(),y);if(tileset){p.setPen(QPen(QColor(240,240,240,220),4));p.drawLine(256,0,256,height());for(int y:{128,256})p.drawLine(0,y,width(),y);p.setPen(QPen(QColor(35,35,35,240),2));p.drawLine(256,0,256,height());for(int y:{128,256})p.drawLine(0,y,width(),y);}}
 if(scrolling&&!scrollRect.isEmpty()){
  QPainterPath mask;mask.addRect(rect());QPainterPath visible;
  for(int y=0;y<2;++y)for(int x=0;x<2;++x){auto r=scrollRect.translated(-256*x,-256*y);visible.addRect(QRectF(r.x()*2,r.y()*2,r.width()*2,r.height()*2));}
  mask=mask.subtracted(visible);p.fillPath(mask,QColor(51,51,51,128));p.save();p.setClipPath(mask);p.setPen(QPen(QColor(0,0,0,153),4));p.drawPath(visible);p.restore();
 }
 }

 void mouseMoveEvent(QMouseEvent*e)override{if(hover)hover(e->position().toPoint()/2);}
};
class DebugObjects:public QWidget{
 QVector<DebugObject> objects_;bool color_=false;
 int columns()const{return 4;}
 void updateHeight(){setMinimumHeight(qMax(1,(int(objects_.size())+columns()-1)/columns())*84);}
public:
 DebugObjects(const QFont&font){setFont(font);setMinimumWidth(512);setProperty("objectColumns",4);setObjectName("vramObjects");setToolTip(tr("P: priority · Y/X: flip · bank/palette · !: line limit exceeded"));}
 void setObjects(QVector<DebugObject> value,bool color){bool changed=color_!=color||value.size()!=objects_.size();
  if(!changed)for(int i=0;i<value.size();++i){const auto&a=value[i];const auto&b=objects_[i];if(a.image!=b.image||a.address!=b.address||a.x!=b.x||a.y!=b.y||a.tile!=b.tile||a.flags!=b.flags||a.limited!=b.limited){changed=true;break;}}
  if(!changed)return;objects_=std::move(value);color_=color;setProperty("objectCount",objects_.size());updateHeight();update();
 }
protected:
 void resizeEvent(QResizeEvent*)override{updateHeight();}
 void paintEvent(QPaintEvent*)override{QPainter p(this);const int cols=columns(),cell=width()/cols;QFont cardFont=font();while(cardFont.pointSizeF()>6&&QFontMetrics(cardFont).horizontalAdvance("(255,255)")>cell-48)cardFont.setPointSizeF(cardFont.pointSizeF()-0.5);p.setFont(cardFont);
  for(int row=0;row*84<height();++row)p.fillRect(0,row*84,width(),84,palette().brush(row&1?QPalette::AlternateBase:QPalette::Base));
  for(int i=0;i<objects_.size();++i){const auto&o=objects_[i];const int x=(i%cols)*cell,y=(i/cols)*84;p.save();p.translate(x,y);p.setPen(palette().color(QPalette::Text));
   // Like Cocoa GBObjectView (scale 32/height): 8x8 fills 32x32, 8x16 is 16x32 and centred.
   const int previewWidth=o.image.height()>0?8*32/o.image.height():32;p.drawImage(QRect(6+(32-previewWidth)/2,6,previewWidth,32),o.image);auto bold=cardFont;bold.setBold(true);p.setFont(bold);p.drawText(44,18,"$"+hex(o.address));p.setFont(cardFont);
   p.drawText(44,36,QString("(%1,%2)").arg(o.x).arg(o.y));QString flags=QString("%1%2%3").arg(o.flags&0x80?'P':'-').arg(o.flags&0x40?'Y':'-').arg(o.flags&0x20?'X':'-');
   flags+=color_?QString("%1%2").arg((o.flags>>3)&1).arg(o.flags&7):QString::number((o.flags>>4)&1);p.drawText(44,54,flags);
   p.drawText(6,74,QString("$%1 $%2").arg(hex(0x8000+o.tile*16)).arg(hex(o.tile,2)));
   if(o.limited){p.setPen(QColor(230,140,20));p.drawText(cell-18,18,"!");}
   if(i%cols!=cols-1){p.setPen(palette().color(QPalette::Mid));p.drawLine(cell-1,4,cell-1,80);}p.restore();
  }
 }
};
void setCell(QTableWidget*t,int row,int col,const QString&text){auto*item=t->item(row,col);if(!item){item=new QTableWidgetItem;item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);t->setItem(row,col,item);}if(item->text()!=text)item->setText(text);}
}
MemorySnapshot debugMemory(GB_gameboy_t*g,int space,int bank,int offset,int count){
 MemorySnapshot out;if(space<0||space>4)return out;
 const unsigned bases[]={0,0,0x8000,0xa000,0xc000};out.base=bases[space];out.totalSize=space==0?65536:space==1?32768:8192;
 if(space){const GB_direct_access_t kinds[]={GB_DIRECT_ACCESS_ROM,GB_DIRECT_ACCESS_VRAM,GB_DIRECT_ACCESS_CART_RAM,GB_DIRECT_ACCESS_RAM};const size_t units[]={0x4000,0x2000,0x2000,0x1000};size_t size=0;GB_get_direct_access(g,kinds[space-1],&size,nullptr);out.banks=qMax(1,int((size+units[space-1]-1)/units[space-1]));}
 bank=qBound(0,bank,out.banks-1);out.offset=qBound(0,offset,out.totalSize);count=count<0?out.totalSize-out.offset:qBound(0,count,out.totalSize-out.offset);out.bytes=QByteArray(count,char(0xff));
 // Like Cocoa's GBMemoryByteArray, copy only the requested visible range in page-bounded chunks.
 for(int done=0;done<count;){const unsigned address=out.base+out.offset+done;const int length=qMin(count-done,int(0x1000-(address&0xfff)));auto*destination=out.bytes.data()+done;
  GB_direct_access_t kind=GB_DIRECT_ACCESS_RAM;size_t unit=0x1000;bool slow=false;int selected=-1;
  switch(address>>12){
   case 0:case 1:case 2:case 3:kind=GB_DIRECT_ACCESS_ROM0;unit=0x4000;break;
   case 4:case 5:case 6:case 7:kind=GB_DIRECT_ACCESS_ROM;unit=0x4000;if(space)selected=bank;break;
   case 8:case 9:kind=GB_DIRECT_ACCESS_VRAM;unit=0x2000;if(space)selected=bank;break;
   case 10:case 11:kind=GB_DIRECT_ACCESS_CART_RAM;unit=0x2000;slow=space==0;if(space)selected=bank;break;
   case 12:case 14:selected=0;break;
   case 13:if(space)selected=bank;break;
   default:slow=true;break;
  }
  if(!slow){size_t size=0;uint16_t mapped=0;auto*data=static_cast<const char*>(GB_get_direct_access(g,kind,&size,&mapped));const size_t location=size_t(selected<0?mapped:selected)*unit+(address&(unit-1));
   if(kind==GB_DIRECT_ACCESS_CART_RAM&&size&&location+length>size)slow=true;
   else if(data&&location<size)std::memcpy(destination,data+location,qMin<size_t>(length,size-location));
  }
  if(slow)for(int i=0;i<length;++i)destination[i]=char(GB_safe_read_memory(g,address+i));done+=length;
 }
 return out;
}

extern "C" void sameboy_debug_mark_battery_dirty(GB_gameboy_t*);
QString writeDebugMemory(GB_gameboy_t*g,int space,int bank,unsigned offset,uint8_t value){
 if(space==0){
  if(offset>=65536)return QObject::tr("Invalid memory address.");
  if(offset<0xa000){
   GB_direct_access_t access=offset<0x4000?GB_DIRECT_ACCESS_ROM0:offset<0x8000?GB_DIRECT_ACCESS_ROM:GB_DIRECT_ACCESS_VRAM;
   size_t size=0;uint16_t mappedBank=0;auto*data=static_cast<uint8_t*>(GB_get_direct_access(g,access,&size,&mappedBank));
   const size_t unit=offset<0x8000?0x4000:0x2000;const size_t location=mappedBank*unit+(offset&(unit-1));
   if(!data||location>=size)return QObject::tr("Address is not backed by memory.");data[location]=value;return {};
  }
  GB_write_memory(g,uint16_t(offset),value);return {};
 }
 if(space<1||space>4||bank<0)return QObject::tr("Invalid address space.");
 const GB_direct_access_t kinds[]={GB_DIRECT_ACCESS_ROM,GB_DIRECT_ACCESS_VRAM,GB_DIRECT_ACCESS_CART_RAM,GB_DIRECT_ACCESS_RAM};
 const unsigned units[]={0x4000,0x2000,0x2000,0x1000};size_t size=0;uint16_t mapped=0;const unsigned unit=units[space-1];
 auto kind=space==1&&offset<0x4000?GB_DIRECT_ACCESS_ROM0:kinds[space-1];auto*data=static_cast<uint8_t*>(GB_get_direct_access(g,kind,&size,&mapped));
 if(offset>=unsigned(space==1?0x8000:0x2000)||bank>=qMax<size_t>(1,(size+unit-1)/unit))return QObject::tr("Address is outside the selected bank.");
 const unsigned selected=space==1&&offset<0x4000?mapped:space==4&&offset<0x1000?0:bank;const size_t location=selected*unit+(offset&(unit-1));
 if(!data||location>=size){if(space==3&&size){GB_write_memory(g,0xa000+offset,value);return {};}return QObject::tr("Address is not backed by memory.");}
 data[location]=value;if(space==3)sameboy_debug_mark_battery_dirty(g);
 return {};
}
VramSnapshot debugVram(GB_gameboy_t*g,int tab,int selection,int map,int tileset){
 VramSnapshot out;out.color=GB_is_cgb(g);if(tab==1){auto*io=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_IO,nullptr,nullptr));out.scrollRect=QRect(io[0x43],io[0x42],160,144);}GB_palette_type_t type=GB_PALETTE_NONE;int index=0;
 if(selection==1)type=GB_PALETTE_AUTO;else if(selection>=2){type=selection>=10?GB_PALETTE_OAM:GB_PALETTE_BACKGROUND;index=(selection-2)&7;}
 if(tab==0||tab==1){out.image=QImage(256,tab==0?192:256,QImage::Format_RGB32);
  if(tab==0)GB_draw_tileset(g,reinterpret_cast<uint32_t*>(out.image.bits()),type==GB_PALETTE_AUTO?GB_PALETTE_NONE:type,index);
  else{
   GB_draw_tilemap(g,reinterpret_cast<uint32_t*>(out.image.bits()),type,index,GB_map_type_t(map),GB_tileset_type_t(tileset));
   auto*io=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_IO,nullptr,nullptr));auto*vram=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_VRAM,nullptr,nullptr));const int base=map==GB_MAP_9C00||(map==GB_MAP_AUTO&&(io[0x40]&8))?0x1c00:0x1800;
   // Match GB_draw_tilemap exactly; its enum names are historical (value 1 selects unsigned $8000 tiles).
   const bool unsignedTiles=tileset==GB_TILESET_8800||(tileset==GB_TILESET_AUTO&&(io[0x40]&0x10));const bool cgb=GB_is_cgb_in_cgb_mode(g);out.tiles.reserve(1024);
   for(int i=0;i<1024;++i){const int tile=vram[base+i],attributes=cgb?vram[base+i+0x2000]:0;out.tiles.append({tile,(attributes>>3)&1,unsignedTiles?0x8000+tile*16:0x9000+int(int8_t(tile))*16,0x8000+base+i,attributes});}
  }
 }else if(tab==2){GB_oam_info_t objects[40]{};uint8_t height=0;auto count=GB_get_oam_info(g,objects,&height);for(int i=0;i<count;++i){auto&o=objects[i];QImage image(reinterpret_cast<const uchar*>(o.image),8,height,QImage::Format_ARGB32);out.objects.append({image.copy(),o.oam_addr,int(o.x)-8,int(o.y)-16,o.tile,o.flags,o.obscured_by_line_limit});}}
 else{
  if(GB_is_cgb(g)){for(int kind=0;kind<2;++kind){size_t size=0;auto*data=static_cast<const uint8_t*>(GB_get_direct_access(g,kind?GB_DIRECT_ACCESS_OBP:GB_DIRECT_ACCESS_BGP,&size,nullptr));for(int pal=0;pal<8;++pal){QVector<QColor>colors;QVector<uint16_t>rawColors;for(int c=0;c<4;++c){const int offset=pal*8+c*2;uint16_t raw=data&&offset+1<int(size)?data[offset]|data[offset+1]<<8:0;rawColors.append(raw);colors.append(QColor::fromRgb(GB_convert_rgb15(g,raw,false)));}out.rawPalettes.append(rawColors);out.palettes.append(colors);out.paletteNames.append(QString(kind?"OBJ %1":"BG %1").arg(pal));}}}
  else{auto*io=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_IO,nullptr,nullptr));const auto*pal=GB_get_palette(g);if(!pal)pal=&GB_PALETTE_DMG;for(int p=0;p<3;++p){QVector<QColor>colors;QVector<uint16_t>rawColors;for(int c=0;c<4;++c){int shade=(io[0x47+p]>>(c*2))&3;rawColors.append(shade);const auto&rgb=pal->colors[shade];colors.append(QColor(rgb.r,rgb.g,rgb.b));}out.rawPalettes.append(rawColors);out.palettes.append(colors);out.paletteNames.append(p?QString("OBJ %1").arg(p-1):"BG 0");}}
 }return out;
}
MemoryDialog::MemoryDialog(DebugExecutor execute,const QFont&font,QWidget*parent,DebugAsyncExecutor async,std::function<void()>romModified):QDialog(parent,Qt::Window){
 setWindowTitle(tr("Memory"));setAttribute(Qt::WA_DeleteOnClose);resize(1000,650);auto*layout=new QVBoxLayout(this);auto*row=new QHBoxLayout;
 auto*space=new QComboBox;space->setObjectName("memorySpace");space->addItems({tr("Entire Space"),"ROM",tr("Video RAM"),tr("Cartridge RAM"),"RAM"});auto*bank=new QSpinBox;bank->setObjectName("memoryBank");bank->setDisplayIntegerBase(16);bank->setPrefix("$");bank->setRange(0,65535);
 auto*go=new QLineEdit;go->setObjectName("memoryAddress");go->setPlaceholderText(tr("Go to hexadecimal address"));auto*refresh=new QPushButton(tr("Refresh"));refresh->setObjectName("memoryRefresh");auto*automatic=new QCheckBox(tr("Auto refresh"));automatic->setChecked(true);
 row->addWidget(space);row->addWidget(new QLabel(tr("Bank:")));row->addWidget(bank);row->addWidget(go,1);row->addWidget(refresh);row->addWidget(automatic);layout->addLayout(row);
 auto*table=new HexTable;table->setFont(font);table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);table->verticalHeader()->setFixedWidth(table->fontMetrics().horizontalAdvance("FFFF")+16);auto*model=new HexModel;model->setParent(table);table->setModel(model);table->setItemDelegate(new ByteDelegate(table));for(int c=0;c<16;++c)table->setColumnWidth(c,32);table->setColumnWidth(16,180);table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height()+6);layout->addWidget(table,1);auto*status=new QLabel;status->setObjectName("memoryStatus");layout->addWidget(status);
 auto pending=std::make_shared<bool>(false);auto revision=std::make_shared<unsigned>(0);
 auto reload=[=]{if(*pending)return;*pending=true;const unsigned generation=++*revision;const int selected=space->currentIndex(),selectedBank=bank->value();auto data=std::make_shared<MemorySnapshot>();
  const int first=qMax(0,table->rowAt(0)),rows=qMax(2,table->viewport()->height()/table->verticalHeader()->defaultSectionSize()+2);const int offset=first*16,count=rows*16;
  auto command=[data,selected,selectedBank,offset,count](GB_gameboy_t*g){*data=debugMemory(g,selected,selectedBank,offset,count);return QString();};
  auto done=[=](QString error){if(generation!=*revision)return;*pending=false;if(table->findChild<QLineEdit*>())return;if(space->currentIndex()!=selected||bank->value()!=selectedBank||qMax(0,table->rowAt(0))*16!=data->offset){refresh->click();return;}
   if(!error.isEmpty()){status->setText(error);return;}
   {QSignalBlocker block(bank);bank->setMaximum(qMax(0,data->banks-1));bank->setEnabled(data->banks>1);}model->space=selected;model->bank=bank->value();model->set(std::move(*data));status->setText(tr("Double-click a byte to edit · %1 bytes · Bank $%2").arg(model->length()).arg(hex(bank->value(),2)));};
  if(async)async(this,std::move(command),std::move(done));else done(execute(std::move(command)));
 };

 model->write=[=](unsigned offset,uint8_t value){++*revision;*pending=false;const int selected=model->space,selectedBank=model->bank;const int start=model->snapshot.offset,count=model->snapshot.bytes.size();MemorySnapshot updated;const auto error=execute([&updated,selected,selectedBank,offset,value,start,count](GB_gameboy_t*g){const auto error=writeDebugMemory(g,selected,selectedBank,offset,value);if(error.isEmpty())updated=debugMemory(g,selected,selectedBank,start,count);return error;});if(!error.isEmpty()){status->setText(error);return false;}model->set(std::move(updated));if(romModified&&(selected==1||(selected==0&&offset<0x8000)))romModified();status->setText(tr("Wrote $%1 at $%2").arg(hex(value,2)).arg(hex(model->snapshot.base+offset)));return true;};
 reload_=[=]{++*revision;*pending=false;model->set({});reload();};
 connect(refresh,&QPushButton::clicked,this,reload);connect(space,&QComboBox::currentIndexChanged,this,[=]{++*revision;*pending=false;{QSignalBlocker block(bank);bank->setValue(0);}model->set({});reload();});connect(bank,&QSpinBox::valueChanged,this,[=]{reload();});
 connect(go,&QLineEdit::returnPressed,this,[=]{QString text=go->text().trimmed();text.remove('$');if(text.startsWith("0x",Qt::CaseInsensitive))text.remove(0,2);bool ok;unsigned address=text.toUInt(&ok,16);if(!ok||address<model->snapshot.base||address>=model->snapshot.base+unsigned(model->length())){status->setText(tr("Address is outside the selected space/bank."));return;}int offset=address-model->snapshot.base;auto index=model->index(offset/16,offset%16);table->setCurrentIndex(index);table->scrollTo(index,QAbstractItemView::PositionAtTop);});
 auto*scrollRefresh=new QTimer(this);scrollRefresh->setSingleShot(true);scrollRefresh->setInterval(0);connect(scrollRefresh,&QTimer::timeout,this,reload);connect(table->verticalScrollBar(),&QScrollBar::valueChanged,this,[=]{scrollRefresh->start();});table->resized=[=]{scrollRefresh->start();};
 auto*timer=new QTimer(this);timer->setTimerType(Qt::CoarseTimer);connect(timer,&QTimer::timeout,this,[=]{if(isVisible()&&!isMinimized()&&automatic->isChecked()&&!table->findChild<QLineEdit*>())reload();});timer->start(250);reload();
}
VramDialog::VramDialog(DebugExecutor execute,const QFont&font,QWidget*parent,DebugAsyncExecutor async):QDialog(parent,Qt::Window){
 setWindowTitle(tr("VRAM Viewer"));setAttribute(Qt::WA_DeleteOnClose);resize(550,620);auto*layout=new QVBoxLayout(this);layout->setContentsMargins(4,4,4,4);layout->setSpacing(4);auto*tabs=new QTabWidget;tabs->setObjectName("vramTabs");layout->addWidget(tabs,1);auto*status=new QLabel;status->setObjectName("vramStatus");status->setWordWrap(true);status->setMinimumWidth(0);status->setFont(font);layout->addWidget(status);
 DebugImage*images[2];QComboBox*palettes[2];QCheckBox*grids[2];auto*scrolling=new QCheckBox(tr("Scrolling"));scrolling->setObjectName("vramScrolling");QComboBox*map=new QComboBox,*tileset=new QComboBox;map->setMinimumContentsLength(0);map->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);tileset->setMinimumContentsLength(0);tileset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);map->addItems({tr("Automatic map"),"$9800","$9C00"});tileset->addItems({tr("Automatic tileset"),"$8000","$8800"});
 for(int page=0;page<2;++page){auto*body=new QWidget;auto*l=new QVBoxLayout(body);l->setContentsMargins(0,0,0,0);l->setSpacing(3);auto*options=new QHBoxLayout;palettes[page]=new QComboBox;palettes[page]->setMinimumContentsLength(0);palettes[page]->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);palettes[page]->addItem(tr("None"),0);if(page)palettes[page]->addItem(tr("Automatic palette"),1);for(int i=0;i<16;++i)palettes[page]->addItem(QString(i<8?"BG %1":"OBJ %1").arg(i&7),i+2);options->addWidget(palettes[page]);if(page){options->addWidget(map);options->addWidget(tileset);options->addWidget(scrolling);}grids[page]=new QCheckBox(tr("Grid"));options->addWidget(grids[page]);l->addLayout(options);auto*scroll=new DebugScrollArea;scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);scroll->viewport()->setBackgroundRole(QPalette::Mid);scroll->viewport()->setAutoFillBackground(true);scroll->viewport()->setObjectName(page?"vramTilemapViewport":"vramTilesetViewport");grids[page]->setObjectName(page?"vramTilemapGrid":"vramTilesetGrid");images[page]=new DebugImage;images[page]->tileset=page==0;scroll->setWidget(images[page]);scroll->viewport()->setBackgroundRole(QPalette::Mid);scroll->viewport()->setAutoFillBackground(true);l->addWidget(scroll,1);tabs->addTab(body,page?tr("Tilemap"):tr("Tileset"));}
 auto*objects=new DebugObjects(font);auto*objectScroll=new QScrollArea;objectScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);objectScroll->setWidgetResizable(true);objectScroll->setWidget(objects);tabs->addTab(objectScroll,tr("Objects"));

 auto*palettePage=new QWidget;auto*paletteLayout=new QVBoxLayout(palettePage);paletteLayout->setContentsMargins(0,0,0,0);auto*colorFormat=new QComboBox;colorFormat->setObjectName("vramColorFormat");colorFormat->addItems({tr("Game Boy (15-bit)"),tr("RGB (hex)")});paletteLayout->addWidget(colorFormat);
 auto*colors=new QTableWidget(0,4);colors->setObjectName("vramPaletteTable");auto headerFont=colors->verticalHeader()->font();headerFont.setPointSizeF(qMax(8.0,headerFont.pointSizeF()-1.5));colors->verticalHeader()->setFont(headerFont);colors->verticalHeader()->setMinimumWidth(QFontMetrics(headerFont).horizontalAdvance("OBJ 7")+20);colors->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);colors->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);colors->setSelectionMode(QAbstractItemView::NoSelection);colors->setFocusPolicy(Qt::NoFocus);colors->setHorizontalHeaderLabels({"0","1","2","3"});colors->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);paletteLayout->addWidget(colors,1);tabs->addTab(palettePage,tr("Palettes"));
 auto mapTiles=std::make_shared<QVector<DebugTile>>();auto hoveredMapPoint=std::make_shared<QPoint>(-1,-1);
 auto*refreshSoon=new QTimer(this);refreshSoon->setSingleShot(true);refreshSoon->setInterval(0);
 auto lastPalette=std::make_shared<VramSnapshot>();auto lastFormat=std::make_shared<int>(-1);
 auto pending=std::make_shared<bool>(false);auto revision=std::make_shared<unsigned>(0);
 auto reload=[=]{if(*pending)return;*pending=true;const unsigned generation=++*revision;const int tab=tabs->currentIndex(),selection=tab<2?palettes[tab]->currentData().toInt():0,m=map->currentIndex(),s=tileset->currentIndex();auto data=std::make_shared<VramSnapshot>();
  auto command=[data,tab,selection,m,s](GB_gameboy_t*g){*data=debugVram(g,tab,selection,m,s);return QString();};
  auto done=[=](QString error){if(generation!=*revision)return;*pending=false;if(tabs->currentIndex()!=tab||(tab<2&&palettes[tab]->currentData().toInt()!=selection)||map->currentIndex()!=m||tileset->currentIndex()!=s){refreshSoon->start();return;}
   if(!error.isEmpty()){status->setText(error);return;}
  if(tab==1){*mapTiles=std::move(data->tiles);if(hoveredMapPoint->x()>=0&&images[1]->hover)images[1]->hover(*hoveredMapPoint);}
  if(tab<2){if(images[tab]->scrollRect!=data->scrollRect){images[tab]->scrollRect=data->scrollRect;if(images[tab]->scrolling)images[tab]->update();}images[tab]->setImage(std::move(data->image));}
  if(tab==2){const int count=data->objects.size();objects->setObjects(std::move(data->objects),data->color);status->setText(tr("%1 objects intersect the screen").arg(count));}
  if(tab==3&&(*lastFormat!=colorFormat->currentIndex()||lastPalette->palettes!=data->palettes||lastPalette->rawPalettes!=data->rawPalettes||lastPalette->paletteNames!=data->paletteNames)){
   QSignalBlocker blocked(colors);colors->setUpdatesEnabled(false);if(colors->rowCount()!=data->palettes.size()){colors->setRowCount(data->palettes.size());colors->setVerticalHeaderLabels(data->paletteNames);}
   for(int row=0;row<data->palettes.size();++row)for(int col=0;col<4;++col){const auto color=data->palettes[row][col];const auto raw=data->rawPalettes[row][col];setCell(colors,row,col,colorFormat->currentIndex()==1?color.name().toUpper():data->color?"$"+hex(raw):tr("Shade %1").arg(raw));auto*item=colors->item(row,col);
    item->setToolTip(data->color?tr("Raw Game Boy color: $%1 · RGB: %2").arg(hex(raw)).arg(color.name().toUpper()):tr("Monochrome shade %1 · RGB: %2").arg(raw).arg(color.name().toUpper()));if(item->background().style()==Qt::NoBrush||item->background().color()!=color)item->setBackground(color);const int r=data->color?raw&31:color.red()>>3,g=data->color?(raw>>5)&31:color.green()>>3,b=data->color?(raw>>10)&31:color.blue()>>3;const QColor foreground=r*3+g*4+b*2>120?Qt::black:Qt::white;if(item->foreground().style()==Qt::NoBrush||item->foreground().color()!=foreground)item->setForeground(foreground);
   }
   *lastPalette=*data;*lastFormat=colorFormat->currentIndex();colors->setUpdatesEnabled(true);status->setText(tr("Current hardware palettes"));
  }

  };
  if(async)async(this,std::move(command),std::move(done));else done(execute(std::move(command)));
 };

 refresh_=reload;connect(refreshSoon,&QTimer::timeout,this,reload);
 reload_=[=]{++*revision;*pending=false;*lastPalette={};*lastFormat=-1;for(auto*image:images)image->setImage({});objects->setObjects({},false);colors->setRowCount(0);reload();};
 for(int page=0;page<2;++page){connect(palettes[page],&QComboBox::currentIndexChanged,this,[=]{refreshSoon->start();});connect(grids[page],&QCheckBox::toggled,this,[=](bool on){images[page]->grid=on;images[page]->update();});}
 images[0]->hover=[=](QPoint point){const int tile=point.y()/8*16+(point.x()%128)/8;status->setText(tr("Tile $%1 at %2:$%3").arg(hex(tile&255,2)).arg(point.x()/128).arg(hex(0x8000+tile*16)));};
 images[1]->hover=[=](QPoint point){*hoveredMapPoint=point;const int index=point.y()/8*32+point.x()/8;if(index<0||index>=mapTiles->size())return;const auto&tile=(*mapTiles)[index];const QString attributes=QString("%1%2%3%4%5").arg(tile.attributes&0x80?'P':'-').arg(tile.attributes&0x40?'V':'-').arg(tile.attributes&0x20?'H':'-').arg(tile.bank).arg(tile.attributes&7);const auto text=tr("Tile $%1 (%2:$%3) · Map $%4 · Attributes: %5").arg(hex(tile.tile,2)).arg(tile.bank).arg(hex(tile.address)).arg(hex(tile.mapAddress)).arg(attributes);status->setText(text);status->setToolTip(text);};

 connect(colorFormat,&QComboBox::currentIndexChanged,this,[=]{reload();});
 connect(scrolling,&QCheckBox::toggled,this,[=](bool on){images[1]->scrolling=on;images[1]->update();});
 connect(map,&QComboBox::currentIndexChanged,this,[=]{refreshSoon->start();});connect(tileset,&QComboBox::currentIndexChanged,this,[=]{refreshSoon->start();});connect(tabs,&QTabWidget::currentChanged,this,[=]{reload();});auto*timer=new QTimer(this);timer->setTimerType(Qt::PreciseTimer);connect(timer,&QTimer::timeout,this,[=]{if(isVisible()&&!isMinimized())reload();});timer->start(16);reload();
}

bool VramDialog::event(QEvent*event){
 const auto type=event->type();const bool result=QDialog::event(event);
 if(type==QEvent::Show&&!initialSizeApplied_){initialSizeApplied_=true;layout()->activate();if(auto*viewport=findChild<QWidget*>("vramTilesetViewport")){resize(width()+512-viewport->width(),height());layout()->activate();}}
 if(type==QEvent::WindowActivate||type==QEvent::Show)QTimer::singleShot(0,this,[this]{requestRefresh();});return result;
}
