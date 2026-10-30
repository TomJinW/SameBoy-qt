#include "../src/uistyle.h"
#include "../src/gbsplayer.h"
#include "../src/printerwindow.h"
#include <QMenu>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QEventLoop>
#include <QApplication>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){
 QApplication app(argc,argv);applyInterfaceTheme(1);app.setStyleSheet(compactMenuStyle());
 GB_gbs_info_t info{};std::memcpy(info.title,"Pokemon Gold",13);std::memcpy(info.author,"Junichi Masuda, Go Ichinose",26);std::memcpy(info.copyright,"1999 Nintendo",14);info.track_count=2;
 GbsPlayer player;player.setInfo(info);player.resize(500,340);player.show();
 QDialog dialog;QVBoxLayout layout(&dialog);QLabel label("Settings",&dialog);QPushButton button("OK",&dialog);
 layout.addWidget(&label);layout.addWidget(&button);dialog.show();
 PrinterWindow printer;printer.show();
 QMenu menu;menu.addAction("Open ROM");menu.ensurePolished();
 const auto roles={QPalette::Window,QPalette::WindowText,QPalette::Base,QPalette::AlternateBase,QPalette::Text,QPalette::Button,
                   QPalette::ButtonText,QPalette::BrightText,QPalette::Light,QPalette::Midlight,QPalette::Mid,QPalette::Dark,
                   QPalette::Shadow,QPalette::Highlight,QPalette::HighlightedText,QPalette::Link,QPalette::LinkVisited,
                   QPalette::ToolTipBase,QPalette::ToolTipText,QPalette::PlaceholderText,QPalette::Accent};
 for(int theme:{2,1,2,1}){
  applyInterfaceTheme(theme);
  if(theme==2){
   // Native platform themes can publish their palette after setColorScheme().
   // Simulate that late stale light palette while the app has already selected dark.
   auto stale=QApplication::palette();const auto staleColor=QColor(250,250,250);
   for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled})for(auto role:roles)stale.setColor(group,role,staleColor);
   for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled})for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText,QPalette::ToolTipText,QPalette::PlaceholderText})stale.setColor(group,role,Qt::black);
   QApplication::setPalette(stale);
  }
  // Include delayed native palette/theme processing, rather than checking just the root.
  QEventLoop loop;QTimer::singleShot(150,&loop,&QEventLoop::quit);loop.exec();
  if(argc>1&&!player.grab().save(QString::fromLocal8Bit(argv[1])+QString("/theme-%1.png").arg(theme)))return 1;
  auto widgets=player.findChildren<QWidget*>();widgets<<printer.findChildren<QWidget*>()<<&printer<<&menu<<&player<<&dialog<<&label<<&button;
  const bool dark=theme==2;const auto appPalette=QApplication::palette();
  if(appPalette.color(QPalette::Window)!=(dark?QColor(30,30,30):QColor(245,245,245))||
     appPalette.color(QPalette::PlaceholderText)!=(dark?QColor(145,145,145):QColor(120,120,120))||
     appPalette.color(QPalette::Button)!=(dark?QColor(50,50,50):QColor(240,240,240))){
   std::fprintf(stderr,"Application palette did not fully switch to theme %d\n",theme);return 1;
  }
  for(auto*widget:widgets)for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled})for(auto role:roles){
   if(widget->palette().color(group,role)!=appPalette.color(group,role)){
    std::fprintf(stderr,"Stale palette: %s role %d theme %d\n",widget->metaObject()->className(),int(role),theme);return 1;
   }
  }
  const auto expected=QApplication::palette().color(QPalette::Window);
  if(player.grab().toImage().pixelColor(4,4)!=expected||printer.grab().toImage().pixelColor(4,4)!=expected){
   std::fprintf(stderr,"Existing top-level window was not repainted for theme %d\n",theme);return 1;
  }
 }
 applyInterfaceTheme(0);return 0;
}
