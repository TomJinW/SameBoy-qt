#pragma once
#include "../src/controllers.h"
#include "../src/settingsdialog.h"
#include <SDL3/SDL.h>
#include <QSettings>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <optional>
inline bool autoPairIntegration(MainWindow&window){
 const auto path=QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").fileName();
 struct Cleanup{QString path;QFileDevice::Permissions permissions;QVector<SDL_JoystickID>ids;~Cleanup(){QFile::setPermissions(path,permissions);for(auto id:ids)SDL_DetachVirtualJoystick(id);}} cleanup{path,QFile::permissions(path),{}};
 if(!QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::ReadUser))return false;
 qApp->setProperty("expectedTestDialog",path);qApp->setProperty("expectedTestDialogSeen",false);qApp->setProperty("expectedTestDialogCount",0);
 SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=26;desc.button_mask=(1u<<26)-1;desc.axis_mask=63;desc.vendor_id=0x057e;
 for(int side=0;side<2;++side){desc.product_id=side?0x2007:0x2006;desc.name=side?"Audit Joy-Con R":"Audit Joy-Con L";auto id=SDL_AttachVirtualJoystick(&desc);if(!id)return false;cleanup.ids.append(id);}
 Controllers probe;QThread::msleep(510);probe.poll();int sides=0;for(const auto&pad:probe.devices())if(pad.joyCon==JoyConType::Left||pad.joyCon==JoyConType::Right)++sides;
 if(sides!=2){std::fputs("Virtual Joy-Con identity was not recognized\n",stderr);return false;}
 QElapsedTimer wait;wait.start();while(!qApp->property("expectedTestDialogSeen").toBool()&&wait.elapsed()<2000){qApp->processEvents();QThread::msleep(1);}
 if(!qApp->property("expectedTestDialogSeen").toBool())return false;
 QAction*preferences=nullptr;for(auto*action:window.findChildren<QAction*>())if(QString(action->text()).remove('&')=="Preferences…")preferences=action;if(!preferences)return false;
 auto inspectSettings=[&](bool accept){std::optional<AppSettings> actual;QTimer inspect;
  QObject::connect(&inspect,&QTimer::timeout,&window,[&]{for(auto*widget:qApp->topLevelWidgets())if(auto*dialog=dynamic_cast<SettingsDialog*>(widget);dialog&&dialog->isVisible()&&dialog->isModal()){inspect.stop();actual=dialog->settings();if(accept)dialog->accept();else dialog->reject();break;}});inspect.start(10);preferences->trigger();return actual;
 };
 const auto actual=inspectSettings(false);
 if(!actual||!actual->joyConPairs.isEmpty()){std::fputs("Failed automatic pairing mutated live preferences\n",stderr);return false;}
 {QEventLoop quiet;QTimer::singleShot(350,&quiet,&QEventLoop::quit);quiet.exec();}
 if(qApp->property("expectedTestDialogCount").toInt()!=1){std::fputs("Failed automatic pairing repeatedly attempted to save\n",stderr);return false;}
 qApp->setProperty("expectedTestDialog",QVariant());
 if(!QFile::setPermissions(path,cleanup.permissions)||!inspectSettings(true))return false;
 {QEventLoop recover;QTimer::singleShot(250,&recover,&QEventLoop::quit);recover.exec();}
 const auto recovered=inspectSettings(false);
 if(!recovered||recovered->joyConPairs.size()!=1||AppSettings::load().joyConPairs.size()!=1){std::fputs("Automatic pairing did not recover after preferences became writable\n",stderr);return false;}
 return true;
}
