#include "../src/mainwindow.h"
#include "../src/nativemodalpump.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "test_rom.h"
#include "../src/uistyle.h"
#include "../src/storagepaths.h"
#include "../src/cartridgeinstance.h"
#include "../src/printerwindow.h"
#include "../src/debugviews.h"
#include "../src/windowplacement.h"
#include "../src/language.h"
#include "../src/screenwidget.h"
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QStatusBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QMessageBox>
#include <QTimer>
#include "../src/cheatsdialog.h"
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QMediaFormat>
#include <QMenuBar>
#include <QMenu>
#include <QLabel>
#include <QComboBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QSet>
#include <QDir>
#include <cstdio>
#include "ai_integration.h"
#include "autopair_integration.h"
#include "core_version.h"

int main(int argc, char **argv)
{
    QApplication app(argc,argv);
    const QRect desktop(0,0,1920,1080),owner(500,200,640,640);const QSize toolSize(500,650);
    auto placed=toolWindowPosition(toolSize,owner,{desktop});if(!desktop.contains(QRect(placed,toolSize))||owner.intersects(QRect(placed,toolSize))){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    placed=toolWindowPosition(QSize(1800,1000),owner,{desktop});if(!desktop.contains(QRect(placed,QSize(1800,1000)))){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    placed=toolWindowPosition(toolSize,QRect(0,0,1920,1080),{desktop,QRect(1920,0,1920,1080)});if(placed.x()<1920){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}

    app.setStyleSheet(compactMenuStyle());
    bool unexpectedDialog=false;QTimer dialogWatch;QObject::connect(&dialogWatch,&QTimer::timeout,&app,[&]{for(auto*widget:app.topLevelWidgets())if(auto*box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()){const auto expected=app.property("expectedTestDialog").toString();const bool isExpected=!expected.isEmpty()&&(box->text().contains(expected)||(expected=="The battery save failed."&&box->text().contains(QString::fromUtf8("电池存档保存失败"))));if(isExpected){app.setProperty("expectedTestDialogSeen",true);app.setProperty("expectedTestDialogCount",app.property("expectedTestDialogCount").toInt()+1);}else if(box->objectName()=="unsavedRomModifications"){box->button(QMessageBox::Discard)->click();continue;}else{unexpectedDialog=true;std::fprintf(stderr,"Unexpected dialog: %s\n",box->text().toUtf8().constData());}if(isExpected&&app.property("expectedDiscardDialogs").toInt()>0){app.setProperty("expectedDiscardDialogs",app.property("expectedDiscardDialogs").toInt()-1);box->button(QMessageBox::Discard)->click();}else box->reject();}});dialogWatch.start(200);

    QTemporaryDir folder(QDir::currentPath()+"/menu-smoke-XXXXXX");
    if (!folder.isValid()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,folder.path());
    MainWindow window; window.setAttribute(Qt::WA_DontShowOnScreen); window.show(); app.processEvents();
    window.resize(240,220);app.processEvents();if(window.width()>260||window.height()>250){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}window.resize(640,640);
    QStringList titles;
    for (auto *action : window.menuBar()->actions()) titles << QString(action->text()).remove('&');
    if (titles != QStringList{"File","Emulation","Cheats","Connect","Window","Debug","Help"}) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    auto menu = [&](const QString &title) -> QMenu * {
        for (auto *item : window.findChildren<QMenu *>()) if (QString(item->title()).remove('&') == title) return item;
        return nullptr;
    };
    auto action = [&](const QString &title) -> QAction * {
        for (auto *item : window.findChildren<QAction *>()) if (QString(item->text()).remove('&') == title) return item;
        return nullptr;
    };
    if(!action("Show Status Bar")->isChecked()||!action("Show Menu Bar")->isChecked()||menu("Window")->actions().contains(action("Show Menu Bar")))return 1;
    const auto viewport=window.centralWidget()->size();action("Show Status Bar")->trigger();app.processEvents();if(!window.statusBar()->isHidden()||window.centralWidget()->size()!=viewport)return 1;action("Show Status Bar")->trigger();app.processEvents();if(window.centralWidget()->size()!=viewport)return 1;
    action("Show Menu Bar")->trigger();app.processEvents();if(!window.menuBar()->isHidden()||window.centralWidget()->size()!=viewport)return 1;
    QApplication::setActiveWindow(&window);QKeyEvent altDown(QEvent::KeyPress,Qt::Key_Alt,Qt::AltModifier),altUp(QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier);
    QApplication::sendEvent(&window,&altDown);QApplication::sendEvent(&window,&altUp);if(!window.menuBar()->isHidden()||QApplication::activePopupWidget())return 1;action("Show Menu Bar")->trigger();app.processEvents();
    ScreenWidget*gameScreen=nullptr;for(auto*widget:window.findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))gameScreen=screen;if(!gameScreen)return 1;
    if(menu("Audio Channels")->actions().first()!=action("Mute Sound"))return 1;
    auto settingsPeer=std::make_unique<MainWindow>();settingsPeer->setAttribute(Qt::WA_DontShowOnScreen);
    auto*cpuFrequency=menu("CPU Frequency");if(!cpuFrequency||cpuFrequency->actions().size()!=3)return 1;
    for(int i=0;i<3;++i)if(cpuFrequency->actions()[i]->data().toInt()!=i||cpuFrequency->actions()[i]->isChecked()!=(i==0))return 1;
    cpuFrequency->actions()[2]->trigger();if(AppSettings::load().cpuFrequency!=2||!cpuFrequency->actions()[2]->isChecked())return 1;
    auto*peerCpuFrequency=settingsPeer->findChild<QMenu*>("cpuFrequencyMenu");if(!peerCpuFrequency||!peerCpuFrequency->actions()[2]->isChecked())return 1;
    cpuFrequency->actions()[0]->trigger();if(AppSettings::load().cpuFrequency!=0||!cpuFrequency->actions()[0]->isChecked())return 1;
    bool contextVerified=false;QTimer::singleShot(0,&window,[&]{auto*popup=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(popup&&popup->objectName()=="gameContextMenu"&&popup->actions().first()==action("Open…")&&popup->actions().value(1)==action("Show Menu Bar")){auto*filters=popup->findChild<QMenu*>("gameFilters");auto*correction=popup->findChild<QMenu*>("gameColorCorrection");auto*palettes=popup->findChild<QMenu*>("gamePalettes");auto*video=popup->findChild<QMenu*>("gameVideo");auto*audio=popup->findChild<QMenu*>("gameAudio");auto*view=popup->findChild<QMenu*>("gameWindow");auto*blending=popup->findChild<QMenu*>("gameBlending");auto*border=popup->findChild<QMenu*>("gameBorder");auto*volume=popup->findChild<QMenu*>("gameVolume");
        if(filters&&filters->actions().size()==16&&correction&&correction->actions().size()==7&&palettes&&video&&audio&&view&&blending&&blending->actions().size()==3&&border&&border->actions().size()==3&&volume&&volume->actions().size()==4&&popup->actions().contains(action("Pause"))&&popup->actions().contains(action("Reset"))&&popup->actions().contains(menu("Emulated Model")->menuAction())&&popup->actions().contains(menu("Save State")->menuAction())&&popup->actions().contains(menu("Load State")->menuAction())&&popup->actions().contains(action("Save Screenshot"))&&popup->actions().contains(action("Preferences…"))&&popup->actions().contains(menu("Cheats")->menuAction())&&popup->actions().contains(menu("Connect")->menuAction())&&popup->actions().contains(menu("Debug")->menuAction())&&audio->actions().contains(action("Mute Sound"))&&view->actions().contains(action("Show Status Bar"))){contextVerified=true;
        if(action("Pause")->isEnabled()||menu("Save State")->menuAction()->isEnabled())contextVerified=false;
        for(auto*choice:volume->actions())if(choice->data().toInt()==50)choice->trigger();for(auto*choice:blending->actions())if(choice->data().toInt()==1)choice->trigger();
        auto*aspect=popup->findChild<QAction*>("gameAspect");auto*integer=popup->findChild<QAction*>("gameIntegerScale");if(!aspect||!integer)contextVerified=false;else{aspect->trigger();if(AppSettings::load().aspect)contextVerified=false;aspect->trigger();}
        for(auto*choice:filters->actions())if(choice->data().toInt()==1)choice->trigger();for(auto*choice:palettes->actions())if(!choice->isSeparator()&&choice->data().toInt()==2)choice->trigger();}action("Show Menu Bar")->trigger();}if(popup)popup->close();});
    QContextMenuEvent context(QContextMenuEvent::Mouse,QPoint(10,10),gameScreen->mapToGlobal(QPoint(10,10)));QApplication::sendEvent(gameScreen,&context);if(!contextVerified||!window.menuBar()->isHidden()||AppSettings::load().scaling!=1||AppSettings::load().palette!=2||AppSettings::load().volume!=50||AppSettings::load().blending!=1||!AppSettings::load().aspect)return 1;action("Show Menu Bar")->trigger();
    bool synchronizedVolume=false;QTimer::singleShot(0,settingsPeer.get(),[&]{if(auto*popup=qobject_cast<QMenu*>(QApplication::activePopupWidget())){if(auto*volume=popup->findChild<QMenu*>("gameVolume"))for(auto*choice:volume->actions())if(choice->isChecked()&&choice->data().toInt()==50)synchronizedVolume=true;popup->close();}});
    ScreenWidget*peerScreen=nullptr;for(auto*widget:settingsPeer->findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))peerScreen=screen;
    if(!peerScreen)return 1;QApplication::sendEvent(peerScreen,&context);settingsPeer.reset();
    if(!synchronizedVolume){std::fputs("Existing window retained stale global volume settings\n",stderr);return 1;}
    const auto fileActions=menu("File")->actions();if(fileActions.indexOf(action("Camera Settings…"))!=fileActions.indexOf(action("Preferences…"))+1||menu("Window")->actions().contains(action("Camera Settings…"))){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if(!window.findChild<QLabel*>("statusFps")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    bool ampersand=false;for(auto *m:window.findChildren<QMenu*>())if(m->title()=="Game Link Cable && Infrared")ampersand=true;
    if(!ampersand){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if (!action("Pause") || action("Pause")->shortcut() != QKeySequence("Ctrl+P") || action("Pause")->isEnabled()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if (!menu("File")->actions().contains(action("Preferences…")) || !menu("Help")->actions().contains(action("About SameBoy Qt")) || !menu("Help")->actions().contains(action("Sponsor Original SameBoy"))) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    for(const auto &removed:{"Hide SameBoy","Services","Hide Others","Show All"})if(action(removed)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if (action("Save Screenshot")->shortcut() != QKeySequence("Ctrl+S")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if (action("Save Screenshot As…")->shortcut() != QKeySequence("Ctrl+Alt+S")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if (action("Mute Sound")->shortcut() != QKeySequence("Ctrl+M")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QByteArray bytes(0x8000,'\0'); make_test_rom(reinterpret_cast<uint8_t *>(bytes.data()),false);
    // Poll the direction buttons into WRAM so GUI input can be checked at the emulated hardware boundary.
    const uint8_t pollKeys[]={0x3e,0x20,0xe0,0x00,0xf0,0x00,0xea,0x00,0xc0,0x18,0xf9};std::memcpy(bytes.data()+0x15e,pollKeys,sizeof(pollKeys));
    const auto path = folder.filePath(QString::fromUtf8("测试菜单.gb"));
    QFile rom(path); if (!rom.open(QIODevice::WriteOnly) || rom.write(bytes) != bytes.size()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;} rom.close();
    {
        const auto firstPath=folder.filePath("first-drop.gb");QFile firstRom(firstPath);if(!firstRom.open(QIODevice::WriteOnly)||firstRom.write(bytes)!=bytes.size())return 1;firstRom.close();
        MainWindow initial;initial.setAttribute(Qt::WA_DontShowOnScreen);initial.show();app.processEvents();
        ScreenWidget*display=nullptr;for(auto*widget:initial.findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))display=screen;if(!display)return 1;
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(firstPath)});QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&enter);
        QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&drop);if(!drop.isAccepted())return 1;
        QElapsedTimer first;first.start();while(display->screenshot().isNull()&&first.elapsed()<1000){app.processEvents();QThread::msleep(5);}app.processEvents();
        std::fprintf(stderr,"First drop: viewport=%dx%d, image=%dx%d\n",display->width(),display->height(),display->gameRect().width(),display->gameRect().height());
        if(display->gameRect().size()!=QSize(480,432))return 1;
    }
    {
        const auto guardedPath=folder.filePath("guarded.gb");if(!QFile::copy(path,guardedPath))return 1;
        MainWindow guarded;guarded.setAttribute(Qt::WA_DontShowOnScreen);guarded.show();if(!guarded.openRom(guardedPath))return 1;
        const auto save=batterySavePath(guardedPath);QFile::remove(save);if(!QDir().mkdir(save))return 1;
        AiDebugServer closingServer("sameboy-closing-test-"+QString::number(QCoreApplication::applicationPid()));if(!closingServer.setEnabled(true)){std::fprintf(stderr,"Failed to enable closing test RPC server: %s\n",closingServer.error().toUtf8().constData());return 1;}
        QLocalSocket closingClient;closingClient.connectToServer(closingServer.endpoint());if(!closingClient.waitForConnected(1000)){std::fputs("Failed to connect closing test RPC client\n",stderr);return 1;}
        bool canceled=false,rpcBlocked=false;dialogWatch.stop();QTimer::singleShot(0,&guarded,[&]{if(auto*box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
            canceled=!guarded.openRom(path);
            closingClient.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"emulation.resume\"}\n");closingClient.flush();
            QElapsedTimer wait;wait.start();while(!closingClient.canReadLine()&&wait.elapsed()<1000){app.processEvents();QThread::msleep(1);}
            rpcBlocked=QJsonDocument::fromJson(closingClient.readLine()).object().value("error").toObject().value("code")==-32005;
            box->done(QMessageBox::Cancel);
        }});
        const bool closed=guarded.close();dialogWatch.start(200);
        if(closed||!canceled||!rpcBlocked||guarded.currentRomPath()!=guardedPath){std::fprintf(stderr,"Failed save close safety: closed=%d canceled=%d rpcBlocked=%d sameRom=%d unexpectedDialog=%d\n",closed,canceled,rpcBlocked,guarded.currentRomPath()==guardedPath,unexpectedDialog);return 1;}
        if(!QDir().rmdir(save)){std::fputs("Failed to remove guarded save directory\n",stderr);return 1;}if(!guarded.close()){std::fputs("Failed to close cartridge after removing save guard\n",stderr);return 1;}if(QFileInfo(save).size()<8192){std::fputs("Battery save did not contain expected data\n",stderr);return 1;}
    }
    QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").remove("recentROMs");
    if(batterySavePath(folder.filePath("game.v1.gbc"))!=folder.filePath("game.v1.sav")||batterySavePath(folder.filePath("game.gb"))!=folder.filePath("game.sav")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QFile legacy(path+".sav");if(!legacy.open(QIODevice::WriteOnly)||legacy.write(QByteArray(8192,'\x17'))!=8192){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}legacy.close();
    if (!window.openRom(path)) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    {MainWindow duplicate;duplicate.setAttribute(Qt::WA_DontShowOnScreen);
     const auto alias=folder.filePath(QString::fromUtf8("测试菜单.gbc"));if(!QFile::copy(path,alias))return 1;
     const auto independent=folder.filePath("independent.gb");if(!QFile::copy(path,independent)||!duplicate.openRom(independent))return 1;
     if(duplicate.openRom(alias)||duplicate.currentRomPath()!=independent){
      std::fputs("Two windows can write the same battery/state/cheat files\n",stderr);return 1;
     }
    }
    auto*pathLabel=window.findChild<QLabel*>("statusRomPath");if(!pathLabel||pathLabel->property("romPath").toString()!=path){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QThread::msleep(100); app.processEvents();
    if (!action("Pause")->isEnabled()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QSet<QString> shortcuts;
    for (auto *item : window.findChildren<QAction *>()) {
        if (!item->isEnabled()) continue;
        for (const auto &shortcut : item->shortcuts()) {
            const auto key = shortcut.toString(QKeySequence::PortableText);
            if (key.isEmpty()) continue;
            if (shortcuts.contains(key)) { std::fprintf(stderr,"Duplicate shortcut: %s\n",qPrintable(key)); {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;} }
            shortcuts.insert(key);
        }
    }
    action("Game Boy Printer")->trigger();if(!action("Game Boy Printer")->isChecked()||action("None")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    for(auto*dialog:window.findChildren<QDialog*>())if(dynamic_cast<PrinterWindow*>(dialog)&&dialog->isVisible()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    action("None")->trigger();if(!action("None")->isChecked()||action("Game Boy Printer")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    action("Pause")->trigger(); if (!action("Pause")->isChecked()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    menu("Save State")->actions().first()->trigger();
    const auto statePath=stateSavePath(path,1);
    if (!QFile::exists(statePath)||QFile::exists(path+".s1")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if(stateSavePath(folder.filePath("game.v1.gbc"),10)!=folder.filePath("game.v1.s10")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    menu("Load State")->actions().first()->trigger();
    // Legacy state names remain readable; subsequent saves use the new name.
    if(!QFile::rename(statePath,path+".s1")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    menu("Load State")->actions().first()->trigger();
    menu("Save State")->actions().first()->trigger();
    if(!QFile::exists(statePath)||!QFile::exists(path+".s1")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    action("Quick Reset")->trigger(); action("Reset")->trigger();
    if (menu("Open Recent")->actions().size() != 3) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    action("Mute Sound")->trigger(); if (!action("Mute Sound")->isChecked()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    for(const auto&name:{"Show Background and Window","Show Objects","Channel 1 — Pulse 1","Channel 2 — Pulse 2","Channel 3 — Wave","Channel 4 — Noise"}){
        auto*toggle=action(QString::fromUtf8(name));if(!toggle||!toggle->isCheckable()||!toggle->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}toggle->trigger();if(toggle->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}toggle->trigger();}
    {QMediaFormat format(QMediaFormat::MPEG4);format.setVideoCodec(QMediaFormat::VideoCodec::H264);format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
     if(format.isSupported(QMediaFormat::Encode)){
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);const auto movie=folder.filePath("gameplay.mp4");QTimer respond;QObject::connect(&respond,&QTimer::timeout,&window,[&]{auto*modal=QApplication::activeModalWidget();if(auto*file=qobject_cast<QFileDialog*>(modal)){file->selectFile(movie);QMetaObject::invokeMethod(file,"accept");}else if(modal&&modal->objectName()=="videoRecordingOptions"){if(auto*scale=modal->findChild<QComboBox*>())scale->setCurrentIndex(1);QMetaObject::invokeMethod(modal,"accept");}});respond.start(20);
      auto*recordVideo=action("Start Video Recording…");if(!recordVideo)return 1;recordVideo->trigger();respond.stop();if(recordVideo->text()!="Stop Video Recording")return 1;action("Pause")->setChecked(false);QElapsedTimer recording;recording.start();while(recording.elapsed()<250){app.processEvents();QThread::msleep(1);}recordVideo->trigger();action("Pause")->setChecked(true);
      QFile movieFile(movie);if(!movieFile.open(QIODevice::ReadOnly)){std::fprintf(stderr,"No gameplay recording\n");return 1;}auto data=movieFile.readAll();if(!data.contains("avc1")||!data.contains("mp4a"))return 1;
     }
    }
#ifdef Q_OS_WIN
    {int nativeTicks=0;NativeModalPump pump([&]{++nativeTicks;window.refreshDuringNativeMove();});MSG enter{};enter.hwnd=reinterpret_cast<HWND>(window.winId());enter.message=WM_ENTERSIZEMOVE;qintptr result=0;pump.nativeEventFilter("windows_generic_MSG",&enter,&result);QElapsedTimer wait;wait.start();while(nativeTicks<2&&wait.elapsed()<1000){app.processEvents();QThread::msleep(1);}enter.message=WM_EXITSIZEMOVE;pump.nativeEventFilter("windows_generic_MSG",&enter,&result);if(nativeTicks<2)return 1;}
#endif
    action("Show Memory")->trigger();action("Show VRAM Viewer")->trigger();action("Show Cheats")->trigger();action("Search Cheats")->trigger();
    QList<QPointer<QDialog>> tools;for(auto*dialog:window.findChildren<QDialog*>())if(dynamic_cast<MemoryDialog*>(dialog)||dynamic_cast<VramDialog*>(dialog)||dynamic_cast<CheatsDialog*>(dialog)||dynamic_cast<CheatSearchDialog*>(dialog))tools.append(dialog);
    if(tools.size()!=4){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QList<QRect> toolGeometry;for(auto tool:tools){tool->move(110,120);toolGeometry.append(tool->geometry());}

    auto*inspector=tools.front().data();QApplication::setActiveWindow(inspector);inspector->setFocus();QApplication::setActiveWindow(&window);
    window.menuBar()->setFocus();QMouseEvent click(QEvent::MouseButtonPress,QPointF(20,20),QPointF(gameScreen->mapToGlobal(QPoint(20,20))),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(gameScreen,&click);app.processEvents();if(QApplication::focusWidget()!=gameScreen)return 1;
    MemoryDialog*memory=nullptr;for(auto tool:tools)if(auto*view=dynamic_cast<MemoryDialog*>(tool.data()))memory=view;if(!memory)return 1;
    auto*address=memory->findChild<QLineEdit*>("memoryAddress");auto*refresh=memory->findChild<QPushButton*>("memoryRefresh");auto*hexTable=memory->findChild<QTableView*>();if(!address||!refresh||!hexTable)return 1;
    address->setText("C000");QMetaObject::invokeMethod(address,"returnPressed",Qt::DirectConnection);action("Pause")->setChecked(false);
    auto waitJoypad=[&](bool pressed){QElapsedTimer wait;wait.start();while(wait.elapsed()<3000){refresh->click();app.processEvents();bool ok=false;const auto value=hexTable->model()->data(hexTable->model()->index(0xc00,0)).toString().toUInt(&ok,16);if(ok&&value>=0xc0&&bool((value&1)==0)==pressed)return true;QThread::msleep(10);}return false;};
    QKeyEvent rightDown(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier),rightUp(QEvent::KeyRelease,Qt::Key_Right,Qt::NoModifier);
    QApplication::sendEvent(gameScreen,&rightDown);if(!waitJoypad(true)){std::fprintf(stderr,"Input did not reach emulated JOYP after inspector focus\n");return 1;}QApplication::sendEvent(gameScreen,&rightUp);if(!waitJoypad(false)){std::fprintf(stderr,"Key release did not reach emulated JOYP\n");return 1;}for(auto tool:tools)if(dynamic_cast<VramDialog*>(tool.data())){
        QApplication::setActiveWindow(tool);tool->setFocus();QApplication::setActiveWindow(&window);window.menuBar()->setFocus();QApplication::sendEvent(gameScreen,&click);app.processEvents();
        QApplication::sendEvent(gameScreen,&rightDown);if(!waitJoypad(true))return 1;QApplication::sendEvent(gameScreen,&rightUp);if(!waitJoypad(false))return 1;
    }
    action("Pause")->setChecked(true);
    const auto instance = folder.filePath("instance.gbcart"); QDir().mkpath(instance);
    QFile link(instance+"/rom.gbl");
    if (!link.open(QIODevice::WriteOnly)) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    link.write((QDir(instance).relativeFilePath(path)+'\n'+path+'\n').toUtf8()); link.close();
    if (!window.openRom(instance)) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    const auto compatSource=folder.filePath(QString::fromUtf8("候选 ROM #100% 空格.gbc"));
    if(!QFile::copy(path,compatSource))return 1;
    const auto compatInstance=folder.filePath("compat.gbcart");if(!QDir().mkpath(compatInstance))return 1;
    const auto compatUrl=QUrl::fromLocalFile(compatSource).toString(QUrl::FullyEncoded).toUtf8();
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly)||descriptor.write("missing-first.gb\nmissing-second.gb\n"+compatUrl+"\n")<0)return 1;}
    if(!window.openRom(compatInstance)||window.currentRomPath()!=compatInstance)return 1;
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::ReadOnly))return 1;const auto actual=descriptor.readAll(),expected=cartridgeInstanceDescriptor(compatInstance,compatSource);if(actual!=expected){std::fprintf(stderr,"Cocoa file-URL fallback repair mismatch\nactual: %s\nexpected: %s\n",actual.constData(),expected.constData());return 1;}}
    const auto encodedRelative=QUrl::toPercentEncoding(QDir(compatInstance).relativeFilePath(compatSource),QByteArray("/"),QByteArray("%"));
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly|QIODevice::Truncate)||descriptor.write(encodedRelative+'\n')<0)return 1;}
    if(!window.openRom(compatInstance))return 1;
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly|QIODevice::Truncate)||descriptor.write("missing-first.gb\n"+compatSource.toUtf8()+"\n")<0)return 1;}
    if(!window.openRom(compatInstance))return 1;
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::ReadOnly)||descriptor.readAll()!=cartridgeInstanceDescriptor(compatInstance,compatSource))return 1;}
    if(!window.openRom(instance))return 1;
    const auto oversizedInstance=folder.filePath("oversized.gbcart");if(!QDir().mkpath(oversizedInstance))return 1;
    {QFile descriptor(oversizedInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly)||descriptor.write((path+'\n').toUtf8())<=0||!descriptor.resize(1024*1024+1))return 1;}
    app.setProperty("expectedTestDialog","Cannot read cartridge instance.");app.setProperty("expectedTestDialogSeen",false);
    const bool oversizedOpened=window.openRom(oversizedInstance);const bool descriptorError=app.property("expectedTestDialogSeen").toBool();app.setProperty("expectedTestDialog",QVariant());
    if(oversizedOpened||!descriptorError||window.currentRomPath()!=instance){std::fputs("Oversized cartridge link was accepted or replaced the active ROM\n",stderr);return 1;}
    for(int i=0;i<tools.size();++i)if(!tools[i]||!tools[i]->isVisible()||tools[i]->geometry()!=toolGeometry[i]||!tools[i]->windowTitle().contains("instance.gbcart")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}

    action("Pause")->setChecked(true);
    menu("Save State")->actions().first()->trigger();
    if (!QFile::exists(instance+"/cartridge.s1")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    if (argc > 1) {
        for (int i = 0; i < window.menuBar()->actions().size(); ++i) {
            auto *popup = window.menuBar()->actions()[i]->menu();
            popup->setAttribute(Qt::WA_DontShowOnScreen); popup->popup(QPoint(0,0)); app.processEvents();
            if (!popup->grab().save(QString::fromLocal8Bit(argv[1])+QString("/menu-%1.png").arg(i))) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
            popup->hide();
        }
    }
    {
        MainWindow peer;peer.setAttribute(Qt::WA_DontShowOnScreen);peer.show();
        const auto peerPath=folder.filePath("peer.gb");QFile peerRom(peerPath);if(!peerRom.open(QIODevice::WriteOnly)||peerRom.write(bytes)!=bytes.size()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}peerRom.close();if(!peer.openRom(peerPath)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
        QMenu*links=nullptr;for(auto*m:window.findChildren<QMenu*>())if(m->title().startsWith("Game Link Cable"))links=m;
        if(!links||!QMetaObject::invokeMethod(links,"aboutToShow",Qt::DirectConnection)||links->actions().size()!=1){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
        links->actions().first()->trigger();if(action("None")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
        action("Pause")->setChecked(true);app.processEvents();

        QAction*peerPause=nullptr;for(auto*a:peer.findChildren<QAction*>())if(a->text()=="Pause")peerPause=a;
        if(!peerPause||!peerPause->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
        action("None")->trigger();if(!action("None")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    }

    action("Close")->trigger(); if (action("Pause")->isEnabled()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    QFile migrated(batterySavePath(path));if(!migrated.open(QIODevice::ReadOnly)||migrated.size()!=8192||migrated.read(1)!=QByteArray(1,'\x42')||migrated.readAll()!=QByteArray(8191,'\x17')){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    migrated.close();
    if(!legacy.open(QIODevice::ReadOnly)||legacy.readAll()!=QByteArray(8192,'\x17')){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    legacy.close();
    if(!QFile::exists(instance+"/cartridge.sav")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}

    QByteArray music(176,0);music.replace(0,4,QByteArray("GBS\1",4));music[4]=2;music[5]=1;
    std::memcpy(music.data()+16,"Pokemon Gold",12);std::memcpy(music.data()+48,"Junichi Masuda, Go Ichinose",26);std::memcpy(music.data()+80,"1999 Nintendo",13);
    music[7]=4;music[9]=4;music[10]=0x30;music[11]=4;music[12]=char(0xfe);music[13]=char(0xff);music[112]=music[160]=char(0xc9);
    const auto musicPath=folder.filePath("music.gbs");QFile musicFile(musicPath);if(!musicFile.open(QIODevice::WriteOnly)||musicFile.write(music)!=music.size()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}musicFile.close();
    const auto gameWindowSize=window.size();
    if(!window.openRom(musicPath)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}auto*tracks=window.findChild<QComboBox*>("gbsTracks");if(!tracks||tracks->count()!=2){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    app.processEvents();if(argc>1&&!window.grab().save(QString::fromLocal8Bit(argv[1])+"/gbs-compact.png"))return 1;
    for(int theme:{2,1}){applyInterfaceTheme(theme);app.processEvents();
     if((window.palette().color(QPalette::WindowText).lightness()>128)!=(theme==2))return 1;
     if(argc>1&&!window.grab().save(QString::fromLocal8Bit(argv[1])+QString("/gbs-theme-%1.png").arg(theme)))return 1;
    }applyInterfaceTheme(0);
    if(action("Save ROM Modifications")->isEnabled()||action("Save ROM Modifications As…")->isEnabled())return 1;
    action("Save ROM Modifications")->trigger();QFile preservedMusic(musicPath);
    if(!preservedMusic.open(QIODevice::ReadOnly)||preservedMusic.readAll()!=music){std::fputs("Save ROM Modifications overwrote the GBS source with generated ROM data\n",stderr);return 1;}preservedMusic.close();
    tracks->setCurrentIndex(1);action("Reset")->trigger();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QTimer chooseSwap;
    QObject::connect(&chooseSwap,&QTimer::timeout,&window,[&]{for(auto*widget:app.topLevelWidgets())if(auto*dialog=qobject_cast<QFileDialog*>(widget);dialog&&dialog->isVisible()){chooseSwap.stop();dialog->selectFile(path);QMetaObject::invokeMethod(dialog,"accept");break;}});chooseSwap.start(10);
    action("Hot Swap Cartridge…")->trigger();app.processEvents();
    if(window.currentRomPath()!=path||tracks->isVisible()||action("Save ROM Modifications")->isEnabled()||action("Save ROM Modifications As…")->isEnabled()||window.size()!=gameWindowSize){std::fputs("GBS hot swap retained music mode or marked a clean ROM modified\n",stderr);return 1;}

    if(argc>1&&!window.grab().save(QString::fromLocal8Bit(argv[1])+"/gbs-return-game.png"))return 1;
    if(!window.openRom(path)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
    action("Show Memory")->trigger();memory=nullptr;for(auto*dialog:window.findChildren<QDialog*>())if(auto*view=dynamic_cast<MemoryDialog*>(dialog))memory=view;if(!memory){std::fputs("Memory viewer was not reopened after cartridge close\n",stderr);return 1;}
    {auto*space=memory->findChild<QComboBox*>("memorySpace");auto*address=memory->findChild<QLineEdit*>("memoryAddress");auto*table=memory->findChild<QTableView*>();auto*save=action("Save ROM Modifications");if(!space||!address||!table||!save||save->isEnabled()){std::fputs("ROM edit controls were not initialized\n",stderr);return 1;}
     QFile romFile(path);if(!romFile.open(QIODevice::ReadOnly)){std::fputs("Could not reopen test ROM\n",stderr);return 1;}romFile.seek(0x150);const auto original=romFile.read(1);if(original.size()!=1){std::fputs("Could not read test ROM byte\n",stderr);return 1;}romFile.close();
     space->setCurrentIndex(1);QElapsedTimer initialLoad;initialLoad.start();while(initialLoad.elapsed()<2000&&table->model()->rowCount()<2048){app.processEvents();QThread::msleep(2);}if(table->model()->rowCount()<2048){std::fputs("ROM memory space did not load\n",stderr);return 1;}address->setText("0150");QMetaObject::invokeMethod(address,"returnPressed",Qt::DirectConnection);QElapsedTimer wait;wait.start();const auto index=table->model()->index(0x150/16,0);while(wait.elapsed()<2000&&!(table->model()->flags(index)&Qt::ItemIsEditable)){app.processEvents();QThread::msleep(2);}
     if(!(table->model()->flags(index)&Qt::ItemIsEditable)||!table->model()->setData(index,QString("C9"))||!save->isEnabled()||!window.isWindowModified()){auto*status=memory->findChild<QLabel*>("memoryStatus");std::fprintf(stderr,"ROM byte was not editable or did not mark document dirty (flags=%d, save=%d, modified=%d, status=%s)\n",int(table->model()->flags(index)),save->isEnabled(),window.isWindowModified(),status?status->text().toUtf8().constData():"none");return 1;}save->trigger();if(save->isEnabled()||window.isWindowModified()){std::fputs("Saving edited ROM did not clear dirty state\n",stderr);return 1;}
     QFile saved(path);if(!saved.open(QIODevice::ReadOnly)||!saved.seek(0x150)||saved.read(1)!=QByteArray(1,char(0xc9))){std::fputs("Edited ROM byte was not written to disk\n",stderr);return 1;}saved.close();
     if(!table->model()->setData(index,QString::number(uint8_t(original[0]),16).rightJustified(2,'0').toUpper())||!save->isEnabled()){std::fputs("Restoring edited ROM byte did not mark document dirty\n",stderr);return 1;}save->trigger();QFile restored(path);if(!restored.open(QIODevice::ReadOnly)||!restored.seek(0x150)||restored.read(1)!=original||save->isEnabled()){std::fputs("Restored ROM byte was not saved cleanly\n",stderr);return 1;}
    }
    window.resize(240,220);app.processEvents();if(window.width()>260||window.height()>250){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}action("Close")->trigger();
    {AppTranslator translator;if(!translator.load(":/translations/zh_CN.qm")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}app.installTranslator(&translator);MainWindow chinese;if(chinese.menuBar()->actions().first()->text()!=QString::fromUtf8("文件(&F)")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}app.removeTranslator(&translator);}
    bool versionChecked=false;QTimer::singleShot(0,&window,[&]{if(auto*box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){versionChecked=box->text().contains(QStringLiteral(SAMEBOY_CORE_COMMIT))&&box->text().contains("https://github.com/LIJI32/SameBoy/commit/");box->accept();}});action("About SameBoy Qt")->trigger();if(!versionChecked)return 1;
    if(!aiIntegration(window,path))return 1;
    if(!autoPairIntegration(window))return 1;
    std::puts("Cocoa menu structure, shortcuts, no conflicts, recent ROMs, pause, save/load states, resets and close passed.");
    return unexpectedDialog?1:0;
}
