#include "../src/mainwindow.h"
#include "../src/audiooutput.h"
#include "../src/audiobuffer.h"
#include "../src/videorecorder.h"
#include "../src/nativemodalpump.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "test_rom.h"
#include "stack_dump.h"
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
#include <QSessionManager>
#include <QRegularExpression>
#include "../src/cheatsdialog.h"
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QMediaFormat>
#include <QMediaDevices>
#include <QCryptographicHash>
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
#include <atomic>
#include <chrono>
#include <thread>
#include "ai_integration.h"
#include "autopair_integration.h"
#include "core_version.h"

static int menuFailure(int line){std::fprintf(stderr,"menu-smoke failure at line %d\n",line);return 1;}
// Diagnostics only: if a run stalls, report the last stage reached before the CTest timeout.
static std::atomic<int> menuStage{0};
#define MENU_STAGE() menuStage.store(__LINE__)

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    // Exercise QWidget menu actions deterministically instead of native menu tracking.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuWindows);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication app(argc,argv);
    std::thread([]{std::this_thread::sleep_for(std::chrono::seconds(50));std::fprintf(stderr,"menu-smoke still running after 50 s; last stage reached at line %d\n",menuStage.load());dumpAllThreadStacks();}).detach();
    // Qt 6.10 WASAPI: resetting a sink suspended for longer than its 2 s wait timeout never joined its worker,
    // freezing the GUI when a ROM that had been paused for a while was closed. AudioOutput::stop resumes first.
    if(const auto device=QMediaDevices::defaultAudioOutput();!device.isNull()){MENU_STAGE();QAudioFormat format;format.setSampleRate(96000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Int16);
     auto buffer=std::make_shared<AudioBuffer>(96000);QElapsedTimer stopping;{AudioOutput output(device,format);output.setBufferSize(96000*4/25);output.start(buffer.get());buffer->append(QByteArray(96000*4/50,0));
      QThread::msleep(30);output.suspend();QThread::msleep(2300);stopping.start();output.stop();}
     if(stopping.elapsed()>3000){std::fprintf(stderr,"Stopping a long-suspended audio sink took %lld ms\n",(long long)stopping.elapsed());return menuFailure(__LINE__);}}
#ifdef Q_OS_MACOS
 const QString closeRomTitle="Close ROM";
 const QString preferencesTitle=QString::fromUtf8("Settings…");
#else
 const QString closeRomTitle="Close";
 const QString preferencesTitle=QString::fromUtf8("Preferences…");
#endif
    const QRect desktop(0,0,1920,1080),owner(500,200,640,640);const QSize toolSize(500,650);
    auto placed=toolWindowPosition(toolSize,owner,{desktop});if(!desktop.contains(QRect(placed,toolSize))||owner.intersects(QRect(placed,toolSize))){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    placed=toolWindowPosition(QSize(1800,1000),owner,{desktop});if(!desktop.contains(QRect(placed,QSize(1800,1000)))){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    placed=toolWindowPosition(toolSize,QRect(0,0,1920,1080),{desktop,QRect(1920,0,1920,1080)});if(placed.x()<1920){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}

    app.setStyleSheet(compactMenuStyle());
    bool unexpectedDialog=false;QTimer dialogWatch;QObject::connect(&dialogWatch,&QTimer::timeout,&app,[&]{for(auto*widget:app.topLevelWidgets())if(auto*box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()){const auto expected=app.property("expectedTestDialog").toString();const bool isExpected=!expected.isEmpty()&&(box->text().contains(expected)||(expected=="The battery save failed."&&box->text().contains(QString::fromUtf8("电池存档保存失败"))));if(isExpected){app.setProperty("expectedTestDialogSeen",true);app.setProperty("expectedTestDialogCount",app.property("expectedTestDialogCount").toInt()+1);}else if(box->objectName()=="unsavedRomModifications"){box->button(QMessageBox::Discard)->click();continue;}else{unexpectedDialog=true;std::fprintf(stderr,"Unexpected dialog: %s\n",box->text().toUtf8().constData());}if(isExpected&&app.property("expectedDiscardDialogs").toInt()>0){app.setProperty("expectedDiscardDialogs",app.property("expectedDiscardDialogs").toInt()-1);box->button(QMessageBox::Discard)->click();}else box->reject();}});dialogWatch.start(200);

    QTemporaryDir folder(QDir::currentPath()+"/menu-smoke-XXXXXX");
    if (!folder.isValid()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,folder.path());
#ifdef Q_OS_MACOS
    {MainWindow emptyWindow;emptyWindow.show();QAction*close=nullptr;
     for(auto*a:emptyWindow.findChildren<QAction*>())if(a->text()=="Close")close=a;
     if(!close||!close->isEnabled()||close->shortcut()!=QKeySequence(QKeySequence::Close))return menuFailure(__LINE__);
     close->trigger();if(emptyWindow.isVisible())return menuFailure(__LINE__);}
#endif
    MainWindow window;
    // Cocoa popups need a window registered with the window server.
#ifndef Q_OS_MACOS
    window.setAttribute(Qt::WA_DontShowOnScreen);
#endif
    window.show(); app.processEvents();
    window.resize(240,220);app.processEvents();if(window.width()>260||window.height()>250){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}window.resize(640,640);
    QStringList titles;
    for (auto *action : window.menuBar()->actions()) titles << QString(action->text()).remove('&');
    if (titles != QStringList{"File","Emulation","Cheats","Connect","Window","Debug","Help"}) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    auto menu = [&](const QString &title) -> QMenu * {
        for (auto *item : window.findChildren<QMenu *>()) if (QString(item->title()).remove('&') == title) return item;
        return nullptr;
    };
    auto action = [&](const QString &title) -> QAction * {
        for (auto *item : window.findChildren<QAction *>()) if (QString(item->text()).remove('&') == title) return item;
        return nullptr;
    };
    if(!action("Show Status Bar")->isChecked()||!action("Show Menu Bar")->isChecked()||menu("Window")->actions().contains(action("Show Menu Bar")))return menuFailure(__LINE__);
    const auto viewport=window.centralWidget()->size();action("Show Status Bar")->trigger();app.processEvents();if(!window.statusBar()->isHidden()||window.centralWidget()->size()!=viewport)return menuFailure(__LINE__);action("Show Status Bar")->trigger();app.processEvents();if(window.centralWidget()->size()!=viewport)return menuFailure(__LINE__);
    action("Show Menu Bar")->trigger();app.processEvents();if(!window.menuBar()->isHidden()||window.centralWidget()->size()!=viewport)return menuFailure(__LINE__);
    QApplication::setActiveWindow(&window);QKeyEvent altDown(QEvent::KeyPress,Qt::Key_Alt,Qt::AltModifier),altUp(QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier);
    QApplication::sendEvent(&window,&altDown);QApplication::sendEvent(&window,&altUp);if(!window.menuBar()->isHidden()||QApplication::activePopupWidget())return menuFailure(__LINE__);action("Show Menu Bar")->trigger();app.processEvents();
    ScreenWidget*gameScreen=nullptr;for(auto*widget:window.findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))gameScreen=screen;if(!gameScreen)return menuFailure(__LINE__);
    if(menu("Audio Channels")->actions().first()!=action("Mute Sound"))return menuFailure(__LINE__);
    auto settingsPeer=std::make_unique<MainWindow>();settingsPeer->setAttribute(Qt::WA_DontShowOnScreen);
    auto*cpuFrequency=menu("Force CPU Frequency");if(!cpuFrequency||cpuFrequency->actions().size()!=5)return menuFailure(__LINE__);
    if(cpuFrequency->actions()[0]->isEnabled()||cpuFrequency->actions()[0]->isCheckable()||!cpuFrequency->actions()[1]->isSeparator())return menuFailure(__LINE__);
    for(int i=0;i<3;++i)if(cpuFrequency->actions()[i+2]->data().toInt()!=i||cpuFrequency->actions()[i+2]->isChecked()!=(i==0))return menuFailure(__LINE__);
    cpuFrequency->actions()[4]->trigger();if(AppSettings::load().cpuFrequency!=2||!cpuFrequency->actions()[4]->isChecked())return menuFailure(__LINE__);
    auto*peerCpuFrequency=settingsPeer->findChild<QMenu*>("cpuFrequencyMenu");if(!peerCpuFrequency||!peerCpuFrequency->actions()[4]->isChecked())return menuFailure(__LINE__);
    cpuFrequency->actions()[2]->trigger();if(AppSettings::load().cpuFrequency!=0||!cpuFrequency->actions()[2]->isChecked())return menuFailure(__LINE__);
#ifdef Q_OS_MACOS
    constexpr bool contextMenuToggle=false;
#else
    constexpr bool contextMenuToggle=true;
#endif
    bool contextVerified=false;QTimer::singleShot(30,&window,[&]{auto*popup=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(popup&&popup->objectName()=="gameContextMenu"&&popup->actions().first()==action("Open…")&&popup->actions().contains(action("Show Menu Bar"))==contextMenuToggle){auto*filters=popup->findChild<QMenu*>("gameFilters");auto*correction=popup->findChild<QMenu*>("gameColorCorrection");auto*palettes=popup->findChild<QMenu*>("gamePalettes");auto*video=popup->findChild<QMenu*>("gameVideo");auto*audio=popup->findChild<QMenu*>("gameAudio");auto*view=popup->findChild<QMenu*>("gameWindow");auto*blending=popup->findChild<QMenu*>("gameBlending");auto*border=popup->findChild<QMenu*>("gameBorder");auto*volume=popup->findChild<QMenu*>("gameVolume");
        if(filters&&filters->actions().size()==16&&correction&&correction->actions().size()==7&&palettes&&video&&audio&&view&&blending&&blending->actions().size()==3&&border&&border->actions().size()==3&&volume&&volume->actions().size()==5&&popup->actions().contains(action("Pause"))&&popup->actions().contains(action("Reset"))&&popup->actions().contains(menu("Emulated Model")->menuAction())&&popup->actions().contains(menu("Save State")->menuAction())&&popup->actions().contains(menu("Load State")->menuAction())&&popup->actions().contains(action("Save Screenshot"))&&popup->actions().contains(action(preferencesTitle))&&popup->actions().contains(menu("Cheats")->menuAction())&&popup->actions().contains(menu("Connect")->menuAction())&&popup->actions().contains(menu("Debug")->menuAction())&&audio->actions().contains(action("Mute Sound"))&&view->actions().contains(action("Show Status Bar"))){contextVerified=true;
        if(action("Pause")->isEnabled()||menu("Save State")->menuAction()->isEnabled())contextVerified=false;
        for(auto*choice:volume->actions())if(choice->data().toInt()==50)choice->trigger();for(auto*choice:blending->actions())if(choice->data().toInt()==1)choice->trigger();
        auto*aspect=popup->findChild<QAction*>("gameAspect");auto*integer=popup->findChild<QAction*>("gameIntegerScale");if(!aspect||!integer)contextVerified=false;else{aspect->trigger();if(AppSettings::load().aspect)contextVerified=false;aspect->trigger();}
        for(auto*choice:filters->actions())if(choice->data().toInt()==1)choice->trigger();for(auto*choice:palettes->actions())if(!choice->isSeparator()&&choice->data().toInt()==2)choice->trigger();}if(contextMenuToggle)action("Show Menu Bar")->trigger();}if(popup)popup->close();});
    QContextMenuEvent context(QContextMenuEvent::Mouse,QPoint(10,10),gameScreen->mapToGlobal(QPoint(10,10)));QApplication::sendEvent(gameScreen,&context);if(!contextVerified||window.menuBar()->isHidden()!=contextMenuToggle||AppSettings::load().scaling!=1||AppSettings::load().palette!=2||AppSettings::load().volume!=50||AppSettings::load().blending!=1||!AppSettings::load().aspect)return menuFailure(__LINE__);if(contextMenuToggle)action("Show Menu Bar")->trigger();
    bool synchronizedVolume=false;QTimer::singleShot(30,settingsPeer.get(),[&]{if(auto*popup=qobject_cast<QMenu*>(QApplication::activePopupWidget())){if(auto*volume=popup->findChild<QMenu*>("gameVolume"))for(auto*choice:volume->actions())if(choice->isChecked()&&choice->data().toInt()==50)synchronizedVolume=true;popup->close();}});
    ScreenWidget*peerScreen=nullptr;for(auto*widget:settingsPeer->findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))peerScreen=screen;
    if(!peerScreen)return menuFailure(__LINE__);QApplication::sendEvent(peerScreen,&context);settingsPeer.reset();
    if(!synchronizedVolume){std::fputs("Existing window retained stale global volume settings\n",stderr);return menuFailure(__LINE__);}
#ifdef Q_OS_MACOS
    if(action(preferencesTitle)->menuRole()!=QAction::PreferencesRole||action(preferencesTitle)->shortcut()!=QKeySequence(QKeySequence::Preferences))return menuFailure(__LINE__);
#endif
    const auto fileActions=menu("File")->actions();if(fileActions.indexOf(action("Camera Settings…"))!=fileActions.indexOf(action(preferencesTitle))+1||menu("Window")->actions().contains(action("Camera Settings…"))){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if(!window.findChild<QLabel*>("statusFps")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    bool ampersand=false;for(auto *m:window.findChildren<QMenu*>())if(m->title()=="Game Link Cable && Infrared")ampersand=true;
    if(!ampersand){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if (!action("Pause") || action("Pause")->shortcut() != QKeySequence("Ctrl+P") || action("Pause")->isEnabled()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if (!menu("File")->actions().contains(action(preferencesTitle)) || !menu("Help")->actions().contains(action("About SameBoy Qt")) || !menu("Help")->actions().contains(action("Sponsor Original SameBoy"))) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    for(const auto &removed:{"Hide SameBoy","Services","Hide Others","Show All"})if(action(removed)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if (action("Save Screenshot")->shortcut() != QKeySequence("Ctrl+S")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if (action("Save Screenshot As…")->shortcut() != QKeySequence("Ctrl+Alt+S")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if (action("Mute Sound")->shortcut() != QKeySequence("Ctrl+M")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    // MainMenu.xib: channel toggles are Option+1..4.
    {int channel=1;for(const auto&name:{"Channel 1 — Pulse 1","Channel 2 — Pulse 2","Channel 3 — Wave","Channel 4 — Noise"}){auto*toggle=action(QString::fromUtf8(name));if(!toggle||toggle->shortcut()!=QKeySequence(QString("Alt+%1").arg(channel++))){std::fprintf(stderr,"Channel toggle shortcut differs from Cocoa: %s\n",name);return menuFailure(__LINE__);}}}
    QByteArray bytes(0x8000,'\0'); make_test_rom(reinterpret_cast<uint8_t *>(bytes.data()),false);
    // Poll the direction buttons into WRAM so GUI input can be checked at the emulated hardware boundary.
    const uint8_t pollKeys[]={0x3e,0x20,0xe0,0x00,0xf0,0x00,0xea,0x00,0xc0,0x18,0xf9};std::memcpy(bytes.data()+0x15e,pollKeys,sizeof(pollKeys));
    const auto path = folder.filePath(QString::fromUtf8("测试菜单.gb"));
    QFile rom(path); if (!rom.open(QIODevice::WriteOnly) || rom.write(bytes) != bytes.size()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);} rom.close();
    {
        const auto firstPath=folder.filePath("first-drop.gb");QFile firstRom(firstPath);if(!firstRom.open(QIODevice::WriteOnly)||firstRom.write(bytes)!=bytes.size())return menuFailure(__LINE__);firstRom.close();
        MainWindow initial;initial.setAttribute(Qt::WA_DontShowOnScreen);initial.show();app.processEvents();
        ScreenWidget*display=nullptr;for(auto*widget:initial.findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))display=screen;if(!display)return menuFailure(__LINE__);
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(firstPath)});QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&enter);
        QDropEvent drop(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&drop);if(!drop.isAccepted())return menuFailure(__LINE__);
        QElapsedTimer first;first.start();while(display->screenshot().isNull()&&first.elapsed()<1000){app.processEvents();QThread::msleep(5);}app.processEvents();
        std::fprintf(stderr,"First drop: viewport=%dx%d, image=%dx%d\n",display->width(),display->height(),display->gameRect().width(),display->gameRect().height());
        if(display->gameRect().size()!=QSize(480,432))return menuFailure(__LINE__);
        // Dropping onto a running window replaces its game instead of routing to another window.
        auto mainWindows=[&]{int count=0;for(auto*widget:app.topLevelWidgets())if(dynamic_cast<MainWindow*>(widget))++count;return count;};const int windowsBefore=mainWindows();
        const auto secondPath=folder.filePath("second-drop.gb");if(!QFile::copy(firstPath,secondPath))return menuFailure(__LINE__);
        for(const auto&dropped:{secondPath,secondPath}){QMimeData next;next.setUrls({QUrl::fromLocalFile(dropped)});QDragEnterEvent reenter(QPoint(20,20),Qt::CopyAction,&next,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&reenter);QDropEvent again(QPointF(20,20),Qt::CopyAction,&next,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&again);
         if(!again.isAccepted()||initial.currentRomPath()!=dropped||mainWindows()!=windowsBefore){std::fprintf(stderr,"Dropped ROM did not open in the target window: accepted=%d samePath=%d windows=%d/%d\n",again.isAccepted(),initial.currentRomPath()==dropped,mainWindows(),windowsBefore);return menuFailure(__LINE__);}}
        // Cocoa GBView performDragOperation: a dropped save state (recognised by content) loads into the game.
        auto initialAction=[&](const QString&title)->QAction*{for(auto*a:initial.findChildren<QAction*>())if(QString(a->text()).remove('&')==title)return a;return nullptr;};
        auto dropFile=[&](const QString&file){QMimeData data;data.setUrls({QUrl::fromLocalFile(file)});QDragEnterEvent over(QPoint(20,20),Qt::CopyAction,&data,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&over);
            QDropEvent put(QPointF(20,20),Qt::CopyAction,&data,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&initial,&put);app.processEvents();return put.isAccepted();};
        {QMenu*saveMenu=nullptr;for(auto*m:initial.findChildren<QMenu*>())if(QString(m->title()).remove('&')=="Save State")saveMenu=m;if(!saveMenu)return menuFailure(__LINE__);saveMenu->actions().first()->trigger();}
        const auto droppedStatePath=folder.filePath("dropped-state.bin");if(!QFile::copy(stateSavePath(secondPath,1),droppedStatePath))return menuFailure(__LINE__);
        const auto notState=folder.filePath("not-a-state.bin");{QFile junk(notState);if(!junk.open(QIODevice::WriteOnly)||junk.write(QByteArray(4096,'\x5a'))!=4096)return menuFailure(__LINE__);}
        if(!dropFile(droppedStatePath)||initial.currentRomPath()!=secondPath||mainWindows()!=windowsBefore||unexpectedDialog){std::fputs("Dropped save state was not loaded into the running game\n",stderr);return menuFailure(__LINE__);}
        if(dropFile(notState)){std::fputs("A file that is neither a ROM nor a save state was accepted\n",stderr);return menuFailure(__LINE__);}
        // Cocoa hides the pointer over a running full-screen game and shows it again when paused.
        auto waitCursor=[&](bool hidden){QElapsedTimer wait;wait.start();while(wait.elapsed()<1000&&(display->cursor().shape()==Qt::BlankCursor)!=hidden){app.processEvents();QThread::msleep(5);}return (display->cursor().shape()==Qt::BlankCursor)==hidden;};
        initialAction("Pause")->setChecked(false);initial.setWindowState(Qt::WindowFullScreen);
        if(!waitCursor(true)){std::fputs("Pointer was not hidden over a running full-screen game\n",stderr);return menuFailure(__LINE__);}
        initialAction("Pause")->setChecked(true);if(!waitCursor(false)){std::fputs("Pointer stayed hidden while paused\n",stderr);return menuFailure(__LINE__);}
        initial.setWindowState(Qt::WindowNoState);
    }
    {
        const auto guardedPath=folder.filePath("guarded.gb");if(!QFile::copy(path,guardedPath))return menuFailure(__LINE__);
        MainWindow guarded;guarded.setAttribute(Qt::WA_DontShowOnScreen);guarded.show();if(!guarded.openRom(guardedPath))return menuFailure(__LINE__);
        const auto save=batterySavePath(guardedPath);QFile::remove(save);if(!QDir().mkdir(save))return menuFailure(__LINE__);
        AiDebugServer closingServer("sameboy-closing-test-"+QString::number(QCoreApplication::applicationPid()));if(!closingServer.setEnabled(true)){std::fprintf(stderr,"Failed to enable closing test RPC server: %s\n",closingServer.error().toUtf8().constData());return menuFailure(__LINE__);}
        QLocalSocket closingClient;closingClient.connectToServer(closingServer.endpoint());if(!closingClient.waitForConnected(1000)){std::fputs("Failed to connect closing test RPC client\n",stderr);return menuFailure(__LINE__);}
        bool canceled=false,rpcBlocked=false;dialogWatch.stop();QTimer::singleShot(0,&guarded,[&]{if(auto*box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
            canceled=!guarded.openRom(path);
            closingClient.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"emulation.resume\"}\n");closingClient.flush();
            QElapsedTimer wait;wait.start();while(!closingClient.canReadLine()&&wait.elapsed()<1000){app.processEvents();QThread::msleep(1);}
            rpcBlocked=QJsonDocument::fromJson(closingClient.readLine()).object().value("error").toObject().value("code")==-32005;
            box->done(QMessageBox::Cancel);
        }});
        const bool closed=guarded.close();dialogWatch.start(200);
        if(closed||!canceled||!rpcBlocked||guarded.currentRomPath()!=guardedPath){std::fprintf(stderr,"Failed save close safety: closed=%d canceled=%d rpcBlocked=%d sameRom=%d unexpectedDialog=%d\n",closed,canceled,rpcBlocked,guarded.currentRomPath()==guardedPath,unexpectedDialog);return menuFailure(__LINE__);}
        if(!QDir().rmdir(save)){std::fputs("Failed to remove guarded save directory\n",stderr);return menuFailure(__LINE__);}if(!guarded.close()){std::fputs("Failed to close cartridge after removing save guard\n",stderr);return menuFailure(__LINE__);}if(QFileInfo(save).size()<8192){std::fputs("Battery save did not contain expected data\n",stderr);return menuFailure(__LINE__);}
    }
    QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").remove("recentROMs");
    if(batterySavePath(folder.filePath("game.v1.gbc"))!=folder.filePath("game.v1.sav")||batterySavePath(folder.filePath("game.gb"))!=folder.filePath("game.sav")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
#ifdef Q_OS_WIN
    // The save-conflict guard compares storage identities: case and 8.3 aliases of one file must match.
    {const auto native=QDir::toNativeSeparators(path);std::wstring shortName(MAX_PATH,L'\0');const auto length=GetShortPathNameW(reinterpret_cast<const wchar_t*>(native.utf16()),shortName.data(),MAX_PATH);
     const auto shortPath=length>0&&length<MAX_PATH?QString::fromWCharArray(shortName.c_str(),int(length)):path;
     if(shortPath==path)std::fputs("No 8.3 alias on this volume; only the case alias is checked\n",stderr);
     if(storageIdentity(path.toUpper())!=storageIdentity(path)||storageIdentity(shortPath)!=storageIdentity(path)||romPathIdentity(shortPath)!=romPathIdentity(path)||storageIdentity(batterySavePath(shortPath))!=storageIdentity(batterySavePath(path))){std::fprintf(stderr,"Storage identity differs for aliases: %s / %s\n",qPrintable(storageIdentity(path.toUpper())),qPrintable(storageIdentity(shortPath)));return menuFailure(__LINE__);}}
#elif defined(Q_OS_MACOS)
    // APFS/HFS+ default to case-insensitive; one file reached through two spellings must keep one identity.
    if(caseInsensitiveVolume(path)&&(storageIdentity(path.toUpper())!=storageIdentity(path)||romPathIdentity(path.toUpper())!=romPathIdentity(path))){std::fputs("Storage identity differs for a case alias\n",stderr);return menuFailure(__LINE__);}
#endif
    QFile legacy(path+".sav");if(!legacy.open(QIODevice::WriteOnly)||legacy.write(QByteArray(8192,'\x17'))!=8192){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}legacy.close();
    if (!window.openRom(path)) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    {MainWindow duplicate;duplicate.setAttribute(Qt::WA_DontShowOnScreen);
     const auto alias=folder.filePath(QString::fromUtf8("测试菜单.gbc"));if(!QFile::copy(path,alias))return menuFailure(__LINE__);
     const auto independent=folder.filePath("independent.gb");if(!QFile::copy(path,independent)||!duplicate.openRom(independent))return menuFailure(__LINE__);
     if(duplicate.openRom(alias)||duplicate.currentRomPath()!=independent){
      std::fputs("Two windows can write the same battery/state/cheat files\n",stderr);return menuFailure(__LINE__);
     }
    }
    auto*pathLabel=window.findChild<QLabel*>("statusRomPath");if(!pathLabel||pathLabel->property("romPath").toString()!=path){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    QThread::msleep(100); app.processEvents();
    if (!action("Pause")->isEnabled()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    QSet<QString> shortcuts;
    for (auto *item : window.findChildren<QAction *>()) {
        if (!item->isEnabled()) continue;
        for (const auto &shortcut : item->shortcuts()) {
            const auto key = shortcut.toString(QKeySequence::PortableText);
            if (key.isEmpty()) continue;
            if (shortcuts.contains(key)) { std::fprintf(stderr,"Duplicate shortcut: %s\n",qPrintable(key)); {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);} }
            shortcuts.insert(key);
        }
    }
    action("Game Boy Printer")->trigger();if(!action("Game Boy Printer")->isChecked()||action("None")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    for(auto*dialog:window.findChildren<QDialog*>())if(dynamic_cast<PrinterWindow*>(dialog)&&dialog->isVisible()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    action("None")->trigger();if(!action("None")->isChecked()||action("Game Boy Printer")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    action("Pause")->trigger(); if (!action("Pause")->isChecked()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    menu("Save State")->actions().first()->trigger();
    const auto statePath=stateSavePath(path,1);
    if (!QFile::exists(statePath)||QFile::exists(path+".s1")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if(stateSavePath(folder.filePath("game.v1.gbc"),10)!=folder.filePath("game.v1.s10")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    menu("Load State")->actions().first()->trigger();
    // Legacy state names remain readable; subsequent saves use the new name.
    if(!QFile::rename(statePath,path+".s1")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    menu("Load State")->actions().first()->trigger();
    menu("Save State")->actions().first()->trigger();
    if(!QFile::exists(statePath)||!QFile::exists(path+".s1")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    action("Quick Reset")->trigger(); action("Reset")->trigger();
    if (menu("Open Recent")->actions().size() != 3) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    action("Mute Sound")->trigger(); if (!action("Mute Sound")->isChecked()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    for(const auto&name:{"Show Background and Window","Show Objects","Channel 1 — Pulse 1","Channel 2 — Pulse 2","Channel 3 — Wave","Channel 4 — Noise"}){
        auto*toggle=action(QString::fromUtf8(name));if(!toggle||!toggle->isCheckable()||!toggle->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}toggle->trigger();if(toggle->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}toggle->trigger();}
    {QMediaFormat format(QMediaFormat::MPEG4);format.setVideoCodec(QMediaFormat::VideoCodec::H264);format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
     if(VideoRecorder::availabilityError().isEmpty()&&format.isSupported(QMediaFormat::Encode)){
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);const auto movie=folder.filePath("gameplay.mp4");QTimer respond;QObject::connect(&respond,&QTimer::timeout,&window,[&]{auto*modal=QApplication::activeModalWidget();if(auto*file=qobject_cast<QFileDialog*>(modal)){file->setDirectory(folder.path());file->selectFile(movie);if(auto*edit=file->findChild<QLineEdit*>("fileNameEdit"))edit->setText(QFileInfo(movie).fileName());QMetaObject::invokeMethod(file,"accept");}else if(modal&&modal->objectName()=="videoRecordingOptions"){if(auto*scale=modal->findChild<QComboBox*>())scale->setCurrentIndex(1);QMetaObject::invokeMethod(modal,"accept");}});respond.start(20);
      auto*recordVideo=action("Start Video Recording…");if(!recordVideo)return menuFailure(__LINE__);recordVideo->trigger();respond.stop();if(recordVideo->text()!="Stop Video Recording")return menuFailure(__LINE__);action("Pause")->setChecked(false);QElapsedTimer recording;recording.start();while(recording.elapsed()<250){app.processEvents();QThread::msleep(1);}recordVideo->trigger();action("Pause")->setChecked(true);
      QFile movieFile(movie);if(!movieFile.open(QIODevice::ReadOnly)){std::fprintf(stderr,"No gameplay recording: %s\n",qPrintable(window.statusBar()->currentMessage()));return menuFailure(__LINE__);}auto data=movieFile.readAll();if(!data.contains("avc1")||!data.contains("mp4a"))return menuFailure(__LINE__);
     }else if(!VideoRecorder::availabilityError().isEmpty()){
      app.setProperty("expectedTestDialog",VideoRecorder::availabilityError());app.setProperty("expectedTestDialogSeen",false);
      MENU_STAGE();action("Start Video Recording…")->trigger();
      if(!app.property("expectedTestDialogSeen").toBool())return (std::fprintf(stderr,"Missing backend warning at %d\n",__LINE__),1);
      app.setProperty("expectedTestDialog",QString());
     }
    }
    // Cocoa's recording formats: WAV/AIFF/raw PCM written by the frontend; VGM is the core's register log.
    MENU_STAGE();{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
     auto record=[&](const QString&name){const auto file=folder.filePath(name);QTimer respond;
      QObject::connect(&respond,&QTimer::timeout,&window,[&]{if(auto*dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->setDirectory(folder.path());dialog->selectFile(file);if(auto*edit=dialog->findChild<QLineEdit*>("fileNameEdit"))edit->setText(QFileInfo(file).fileName());QMetaObject::invokeMethod(dialog,"accept");}});
      respond.start(20);action("Start Audio Recording…")->trigger();respond.stop();action("Pause")->setChecked(false);
      QElapsedTimer running;running.start();while(running.elapsed()<300){app.processEvents();QThread::msleep(2);}
      if(auto*stop=action("Stop Audio Recording"))stop->trigger();action("Pause")->setChecked(true);
      QFile recorded(file);return recorded.open(QIODevice::ReadOnly)?recorded.readAll():QByteArray();};
     const auto aiff=record("recording.aiff"),raw=record("recording.raw"),vgm=record("recording.vgm");
     if(!aiff.startsWith("FORM")||aiff.mid(8,8)!="AIFFCOMM"||aiff.size()<=54||(aiff.size()-54)%4){std::fprintf(stderr,"AIFF recording is malformed (%lld bytes)\n",(long long)aiff.size());return menuFailure(__LINE__);}
     if(raw.isEmpty()||raw.size()%4){std::fprintf(stderr,"Raw PCM recording is malformed (%lld bytes)\n",(long long)raw.size());return menuFailure(__LINE__);}
     if(!vgm.startsWith("Vgm ")){std::fprintf(stderr,"VGM recording is malformed (%lld bytes)\n",(long long)vgm.size());return menuFailure(__LINE__);}}
#ifdef Q_OS_WIN
    MENU_STAGE();{int nativeTicks=0;NativeModalPump pump([&]{++nativeTicks;window.refreshDuringNativeMove();});MSG enter{};enter.hwnd=reinterpret_cast<HWND>(window.winId());enter.message=WM_ENTERSIZEMOVE;qintptr result=0;pump.nativeEventFilter("windows_generic_MSG",&enter,&result);QElapsedTimer wait;wait.start();while(nativeTicks<2&&wait.elapsed()<1000){app.processEvents();QThread::msleep(1);}enter.message=WM_EXITSIZEMOVE;pump.nativeEventFilter("windows_generic_MSG",&enter,&result);if(nativeTicks<2)return menuFailure(__LINE__);}
#endif
    MENU_STAGE();action("Show Memory")->trigger();action("Show VRAM Viewer")->trigger();action("Show Cheats")->trigger();action("Search Cheats")->trigger();
    QList<QPointer<QDialog>> tools;for(auto*dialog:window.findChildren<QDialog*>())if(dynamic_cast<MemoryDialog*>(dialog)||dynamic_cast<VramDialog*>(dialog)||dynamic_cast<CheatsDialog*>(dialog)||dynamic_cast<CheatSearchDialog*>(dialog))tools.append(dialog);
    if(tools.size()!=4){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    QList<QRect> toolGeometry;for(auto tool:tools){tool->move(110,120);toolGeometry.append(tool->geometry());}

    auto*inspector=tools.front().data();QApplication::setActiveWindow(inspector);inspector->setFocus();QApplication::setActiveWindow(&window);
    window.menuBar()->setFocus();QMouseEvent click(QEvent::MouseButtonPress,QPointF(20,20),QPointF(gameScreen->mapToGlobal(QPoint(20,20))),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(gameScreen,&click);if(QApplication::focusWidget()!=gameScreen)return menuFailure(__LINE__);app.processEvents();
    MemoryDialog*memory=nullptr;for(auto tool:tools)if(auto*view=dynamic_cast<MemoryDialog*>(tool.data()))memory=view;if(!memory)return menuFailure(__LINE__);
    auto*address=memory->findChild<QLineEdit*>("memoryAddress");auto*refresh=memory->findChild<QPushButton*>("memoryRefresh");auto*hexTable=memory->findChild<QTableView*>();if(!address||!refresh||!hexTable)return menuFailure(__LINE__);
    address->setText("C000");QMetaObject::invokeMethod(address,"returnPressed",Qt::DirectConnection);action("Pause")->setChecked(false);
    window.raise();window.activateWindow();QElapsedTimer activation;activation.start();while(activation.elapsed()<150){app.processEvents();QThread::msleep(2);}
    auto waitJoypad=[&](bool pressed){QElapsedTimer wait;wait.start();while(wait.elapsed()<3000){
        // Keep synthetic input independent of OS activation: Cocoa/Xvfb notifications, and Windows
        // foreground-lock, which refuses activateWindow() while another application is in front.
        QApplication::setActiveWindow(&window);QKeyEvent held(pressed?QEvent::KeyPress:QEvent::KeyRelease,Qt::Key_Right,Qt::NoModifier);QApplication::sendEvent(gameScreen,&held);
        refresh->click();app.processEvents();bool ok=false;const auto value=hexTable->model()->data(hexTable->model()->index(0xc00,0)).toString().toUInt(&ok,16);if(ok&&value>=0xc0&&bool((value&1)==0)==pressed)return true;QThread::msleep(10);}return false;};
    QKeyEvent rightDown(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier),rightUp(QEvent::KeyRelease,Qt::Key_Right,Qt::NoModifier);
    QApplication::sendEvent(gameScreen,&rightDown);if(!waitJoypad(true)){std::fprintf(stderr,"Input did not reach emulated JOYP after inspector focus\n");return menuFailure(__LINE__);}QApplication::sendEvent(gameScreen,&rightUp);if(!waitJoypad(false)){std::fprintf(stderr,"Key release did not reach emulated JOYP\n");return menuFailure(__LINE__);}for(auto tool:tools)if(dynamic_cast<VramDialog*>(tool.data())){
        QApplication::setActiveWindow(tool);tool->setFocus();QApplication::setActiveWindow(&window);window.menuBar()->setFocus();QApplication::sendEvent(gameScreen,&click);app.processEvents();
        QApplication::sendEvent(gameScreen,&rightDown);if(!waitJoypad(true))return menuFailure(__LINE__);QApplication::sendEvent(gameScreen,&rightUp);if(!waitJoypad(false))return menuFailure(__LINE__);
    }
    action("Pause")->setChecked(true);
    MENU_STAGE();const auto instance = folder.filePath("instance.gbcart"); QDir().mkpath(instance);
    QFile link(instance+"/rom.gbl");
    if (!link.open(QIODevice::WriteOnly)) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    link.write((QDir(instance).relativeFilePath(path)+'\n'+path+'\n').toUtf8()); link.close();
    if (!window.openRom(instance)) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    const auto compatSource=folder.filePath(QString::fromUtf8("候选 ROM #100% 空格.gbc"));
    if(!QFile::copy(path,compatSource))return menuFailure(__LINE__);
    const auto compatInstance=folder.filePath("compat.gbcart");if(!QDir().mkpath(compatInstance))return menuFailure(__LINE__);
    const auto compatUrl=QUrl::fromLocalFile(compatSource).toString(QUrl::FullyEncoded).toUtf8();
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly)||descriptor.write("missing-first.gb\nmissing-second.gb\n"+compatUrl+"\n")<0)return menuFailure(__LINE__);}
    if(!window.openRom(compatInstance)||window.currentRomPath()!=compatInstance)return menuFailure(__LINE__);
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::ReadOnly))return menuFailure(__LINE__);const auto actual=descriptor.readAll(),expected=cartridgeInstanceDescriptor(compatInstance,compatSource);if(actual!=expected){std::fprintf(stderr,"Cocoa file-URL fallback repair mismatch\nactual: %s\nexpected: %s\n",actual.constData(),expected.constData());return menuFailure(__LINE__);}}
    const auto encodedRelative=QUrl::toPercentEncoding(QDir(compatInstance).relativeFilePath(compatSource),QByteArray("/"),QByteArray("%"));
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly|QIODevice::Truncate)||descriptor.write(encodedRelative+'\n')<0)return menuFailure(__LINE__);}
    if(!window.openRom(compatInstance))return menuFailure(__LINE__);
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly|QIODevice::Truncate)||descriptor.write("missing-first.gb\n"+compatSource.toUtf8()+"\n")<0)return menuFailure(__LINE__);}
    if(!window.openRom(compatInstance))return menuFailure(__LINE__);
    {QFile descriptor(compatInstance+"/rom.gbl");if(!descriptor.open(QIODevice::ReadOnly)||descriptor.readAll()!=cartridgeInstanceDescriptor(compatInstance,compatSource))return menuFailure(__LINE__);}
    if(!window.openRom(instance))return menuFailure(__LINE__);
    const auto oversizedInstance=folder.filePath("oversized.gbcart");if(!QDir().mkpath(oversizedInstance))return menuFailure(__LINE__);
    {QFile descriptor(oversizedInstance+"/rom.gbl");if(!descriptor.open(QIODevice::WriteOnly)||descriptor.write((path+'\n').toUtf8())<=0||!descriptor.resize(1024*1024+1))return menuFailure(__LINE__);}
    app.setProperty("expectedTestDialog","Cannot read cartridge instance.");app.setProperty("expectedTestDialogSeen",false);
    const bool oversizedOpened=window.openRom(oversizedInstance);const bool descriptorError=app.property("expectedTestDialogSeen").toBool();app.setProperty("expectedTestDialog",QVariant());
    if(oversizedOpened||!descriptorError||window.currentRomPath()!=instance){std::fputs("Oversized cartridge link was accepted or replaced the active ROM\n",stderr);return menuFailure(__LINE__);}
    for(int i=0;i<tools.size();++i)if(!tools[i]||!tools[i]->isVisible()||tools[i]->geometry()!=toolGeometry[i]||!tools[i]->windowTitle().contains("instance.gbcart")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}

    action("Pause")->setChecked(true);
    menu("Save State")->actions().first()->trigger();
    if (!QFile::exists(instance+"/cartridge.s1")) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    if (argc > 1) {
        for (int i = 0; i < window.menuBar()->actions().size(); ++i) {
            auto *popup = window.menuBar()->actions()[i]->menu();
            popup->setAttribute(Qt::WA_DontShowOnScreen); popup->popup(QPoint(0,0)); app.processEvents();
            if (!popup->grab().save(QString::fromLocal8Bit(argv[1])+QString("/menu-%1.png").arg(i))) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
            popup->hide();
        }
    }
    {
        MENU_STAGE();MainWindow peer;peer.setAttribute(Qt::WA_DontShowOnScreen);peer.show();
        MENU_STAGE();const auto peerPath=folder.filePath("peer.gb");QFile peerRom(peerPath);if(!peerRom.open(QIODevice::WriteOnly)||peerRom.write(bytes)!=bytes.size()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}peerRom.close();if(!peer.openRom(peerPath)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
        MENU_STAGE();QMenu*links=nullptr;for(auto*m:window.findChildren<QMenu*>())if(m->title().startsWith("Game Link Cable"))links=m;
        if(!links||!QMetaObject::invokeMethod(links,"aboutToShow",Qt::DirectConnection)||links->actions().size()!=1){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
        MENU_STAGE();links->actions().first()->trigger();if(action("None")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
        MENU_STAGE();action("Pause")->setChecked(true);app.processEvents();

        MENU_STAGE();QAction*peerPause=nullptr;for(auto*a:peer.findChildren<QAction*>())if(a->text()=="Pause")peerPause=a;
        if(!peerPause||!peerPause->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
        MENU_STAGE();action("None")->trigger();if(!action("None")->isChecked()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
        MENU_STAGE(); // Before the linked peer window is destroyed.
    }

    MENU_STAGE();action(closeRomTitle)->trigger(); if (action("Pause")->isEnabled()) {std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    MENU_STAGE();QFile migrated(batterySavePath(path));if(!migrated.open(QIODevice::ReadOnly)||migrated.size()!=8192||migrated.read(1)!=QByteArray(1,'\x42')||migrated.readAll()!=QByteArray(8191,'\x17')){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    migrated.close();
    if(!legacy.open(QIODevice::ReadOnly)||legacy.readAll()!=QByteArray(8192,'\x17')){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    legacy.close();
    if(!QFile::exists(instance+"/cartridge.sav")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}

    MENU_STAGE();QByteArray music(176,0);music.replace(0,4,QByteArray("GBS\1",4));music[4]=2;music[5]=1;
    std::memcpy(music.data()+16,"Pokemon Gold",12);std::memcpy(music.data()+48,"Junichi Masuda, Go Ichinose",26);std::memcpy(music.data()+80,"1999 Nintendo",13);
    music[7]=4;music[9]=4;music[10]=0x30;music[11]=4;music[12]=char(0xfe);music[13]=char(0xff);music[112]=music[160]=char(0xc9);
    const auto musicPath=folder.filePath("music.gbs");QFile musicFile(musicPath);if(!musicFile.open(QIODevice::WriteOnly)||musicFile.write(music)!=music.size()){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}musicFile.close();
    const auto gameWindowSize=window.size();
    if(!window.openRom(musicPath)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}auto*tracks=window.findChild<QComboBox*>("gbsTracks");if(!tracks||tracks->count()!=2){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    app.processEvents();if(argc>1&&!window.grab().save(QString::fromLocal8Bit(argv[1])+"/gbs-compact.png"))return menuFailure(__LINE__);
    for(int theme:{2,1}){applyInterfaceTheme(theme);app.processEvents();
     if((window.palette().color(QPalette::WindowText).lightness()>128)!=(theme==2))return menuFailure(__LINE__);
     if(argc>1&&!window.grab().save(QString::fromLocal8Bit(argv[1])+QString("/gbs-theme-%1.png").arg(theme)))return menuFailure(__LINE__);
    }applyInterfaceTheme(0);
    if(action("Save ROM Modifications")->isEnabled()||action("Save ROM Modifications As…")->isEnabled())return menuFailure(__LINE__);
    action("Save ROM Modifications")->trigger();QFile preservedMusic(musicPath);
    if(!preservedMusic.open(QIODevice::ReadOnly)||preservedMusic.readAll()!=music){std::fputs("Save ROM Modifications overwrote the GBS source with generated ROM data\n",stderr);return menuFailure(__LINE__);}preservedMusic.close();
    tracks->setCurrentIndex(1);action("Reset")->trigger();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QTimer chooseSwap;
    QObject::connect(&chooseSwap,&QTimer::timeout,&window,[&]{for(auto*widget:app.topLevelWidgets())if(auto*dialog=qobject_cast<QFileDialog*>(widget);dialog&&dialog->isVisible()){chooseSwap.stop();dialog->setDirectory(QFileInfo(path).absolutePath());dialog->selectFile(path);if(auto*edit=dialog->findChild<QLineEdit*>("fileNameEdit"))edit->setText(QFileInfo(path).fileName());QMetaObject::invokeMethod(dialog,"accept");break;}});chooseSwap.start(10);
    MENU_STAGE();action("Hot Swap Cartridge…")->trigger();app.processEvents();
    if(window.currentRomPath()!=path||tracks->isVisible()||action("Save ROM Modifications")->isEnabled()||action("Save ROM Modifications As…")->isEnabled()||window.size()!=gameWindowSize){std::fprintf(stderr,"GBS hot swap: pathMatch=%d tracksVisible=%d modified=%d/%d size=%dx%d expected=%dx%d\n",window.currentRomPath()==path,tracks->isVisible(),action("Save ROM Modifications")->isEnabled(),action("Save ROM Modifications As…")->isEnabled(),window.width(),window.height(),gameWindowSize.width(),gameWindowSize.height());return menuFailure(__LINE__);}

    if(argc>1&&!window.grab().save(QString::fromLocal8Bit(argv[1])+"/gbs-return-game.png"))return menuFailure(__LINE__);
    if(!window.openRom(path)){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}
    action("Show Memory")->trigger();memory=nullptr;for(auto*dialog:window.findChildren<QDialog*>())if(auto*view=dynamic_cast<MemoryDialog*>(dialog))memory=view;if(!memory){std::fputs("Memory viewer was not reopened after cartridge close\n",stderr);return menuFailure(__LINE__);}
    {auto*space=memory->findChild<QComboBox*>("memorySpace");auto*address=memory->findChild<QLineEdit*>("memoryAddress");auto*table=memory->findChild<QTableView*>();auto*save=action("Save ROM Modifications");if(!space||!address||!table||!save||save->isEnabled()){std::fputs("ROM edit controls were not initialized\n",stderr);return menuFailure(__LINE__);}
     QFile romFile(path);if(!romFile.open(QIODevice::ReadOnly)){std::fputs("Could not reopen test ROM\n",stderr);return menuFailure(__LINE__);}romFile.seek(0x150);const auto original=romFile.read(1);if(original.size()!=1){std::fputs("Could not read test ROM byte\n",stderr);return menuFailure(__LINE__);}romFile.close();
     space->setCurrentIndex(1);QElapsedTimer initialLoad;initialLoad.start();while(initialLoad.elapsed()<2000&&table->model()->rowCount()<2048){app.processEvents();QThread::msleep(2);}if(table->model()->rowCount()<2048){std::fputs("ROM memory space did not load\n",stderr);return menuFailure(__LINE__);}address->setText("0150");QMetaObject::invokeMethod(address,"returnPressed",Qt::DirectConnection);QElapsedTimer wait;wait.start();const auto index=table->model()->index(0x150/16,0);while(wait.elapsed()<2000&&!(table->model()->flags(index)&Qt::ItemIsEditable)){app.processEvents();QThread::msleep(2);}
     if(!(table->model()->flags(index)&Qt::ItemIsEditable)||!table->model()->setData(index,QString("C9"))||!save->isEnabled()||!window.isWindowModified()){auto*status=memory->findChild<QLabel*>("memoryStatus");std::fprintf(stderr,"ROM byte was not editable or did not mark document dirty (flags=%d, save=%d, modified=%d, status=%s)\n",int(table->model()->flags(index)),save->isEnabled(),window.isWindowModified(),status?status->text().toUtf8().constData():"none");return menuFailure(__LINE__);}save->trigger();if(save->isEnabled()||window.isWindowModified()){std::fputs("Saving edited ROM did not clear dirty state\n",stderr);return menuFailure(__LINE__);}
     QFile saved(path);if(!saved.open(QIODevice::ReadOnly)||!saved.seek(0x150)||saved.read(1)!=QByteArray(1,char(0xc9))){std::fputs("Edited ROM byte was not written to disk\n",stderr);return menuFailure(__LINE__);}saved.close();
     if(!table->model()->setData(index,QString::number(uint8_t(original[0]),16).rightJustified(2,'0').toUpper())||!save->isEnabled()){std::fputs("Restoring edited ROM byte did not mark document dirty\n",stderr);return menuFailure(__LINE__);}save->trigger();QFile restored(path);if(!restored.open(QIODevice::ReadOnly)||!restored.seek(0x150)||restored.read(1)!=original||save->isEnabled()){std::fputs("Restored ROM byte was not saved cleanly\n",stderr);return menuFailure(__LINE__);}
#ifdef Q_OS_WIN
     // Logoff/shutdown: Qt emits commitDataRequest for WM_QUERYENDSESSION and may exit without
     // closing windows, so the battery must reach disk synchronously (main.cpp makes this connection).
     {const auto connection=QObject::connect(&app,&QGuiApplication::commitDataRequest,&app,[](QSessionManager&){MainWindow::flushAllBatteries();},Qt::DirectConnection);
      space->setCurrentIndex(3);QElapsedTimer ramLoad;ramLoad.start();while(ramLoad.elapsed()<2000&&table->model()->rowCount()<2){app.processEvents();QThread::msleep(2);}
      const auto cell=table->model()->index(1,0);QElapsedTimer editable;editable.start();while(editable.elapsed()<2000&&!(table->model()->flags(cell)&Qt::ItemIsEditable)){app.processEvents();QThread::msleep(2);}
      if(!table->model()->setData(cell,QString("5A"))){std::fputs("Cartridge RAM byte was not editable\n",stderr);return menuFailure(__LINE__);}
      MENU_STAGE();SendMessageW(reinterpret_cast<HWND>(window.winId()),WM_QUERYENDSESSION,0,ENDSESSION_LOGOFF);QObject::disconnect(connection);
      QFile battery(batterySavePath(path));if(!battery.open(QIODevice::ReadOnly)||!battery.seek(0x10)||battery.read(1)!=QByteArray(1,'\x5a')){std::fputs("Battery was not written when the session asked to commit data\n",stderr);return menuFailure(__LINE__);}}
#endif
    }
    window.resize(240,220);app.processEvents();if(window.width()>260||window.height()>250){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}action(closeRomTitle)->trigger();
    {AppTranslator translator;if(!translator.load(":/translations/zh_CN.qm")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}app.installTranslator(&translator);MainWindow chinese;if(chinese.menuBar()->actions().first()->text()!=QString::fromUtf8("文件(&F)")){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return menuFailure(__LINE__);}app.removeTranslator(&translator);}
    // Cocoa opens ISX files (GB_load_isx) and loads "<name>.ram" before "<name>.sav".
    MENU_STAGE();{QByteArray isx(1,'\x11');const quint32 zero=0,length=quint32(bytes.size());isx.append(reinterpret_cast<const char*>(&zero),4).append(reinterpret_cast<const char*>(&length),4).append(bytes);
     const auto isxPath=folder.filePath(QString::fromUtf8("调试 ROM.isx"));{QFile out(isxPath);if(!out.open(QIODevice::WriteOnly)||out.write(isx)!=isx.size())return menuFailure(__LINE__);}
     {QFile ram(folder.filePath(QString::fromUtf8("调试 ROM.ram")));if(!ram.open(QIODevice::WriteOnly)||ram.write(QByteArray(8192,'\x5a'))!=8192)return menuFailure(__LINE__);}
     {MainWindow isxWindow;isxWindow.setAttribute(Qt::WA_DontShowOnScreen);isxWindow.show();
      if(!isxWindow.openRom(isxPath)||isxWindow.currentRomPath()!=QFileInfo(isxPath).absoluteFilePath()){std::fputs("ISX file was not opened as a ROM\n",stderr);return menuFailure(__LINE__);}
      QElapsedTimer booted;booted.start();while(booted.elapsed()<300){app.processEvents();QThread::msleep(2);}}
     QFile saved(batterySavePath(isxPath));if(!saved.open(QIODevice::ReadOnly)||saved.size()!=8192||!saved.seek(1)||saved.read(1)!=QByteArray(1,'\x5a')){std::fputs("ISX did not load its .ram battery as Cocoa does\n",stderr);return menuFailure(__LINE__);}}
    // The status bar separates frames the core emulated from frames this window drew.
    {auto withOsd=AppSettings::load();withOsd.osd=true;if(!withOsd.save().isEmpty())return menuFailure(__LINE__);
     MainWindow counted;counted.setAttribute(Qt::WA_DontShowOnScreen);counted.show();const auto copy=folder.filePath("fps.gb");if(!QFile::copy(path,copy)||!counted.openRom(copy))return menuFailure(__LINE__);
     // Cocoa GBOSDView: loading shows the title and CRC32; saving a state shows "State saved".
     ScreenWidget*osdScreen=nullptr;for(auto*widget:counted.findChildren<QWidget*>())if(auto*screen=dynamic_cast<ScreenWidget*>(widget))osdScreen=screen;
     app.processEvents();if(!osdScreen||osdScreen->osdText().count('\n')!=2||!QRegularExpression("\\n[0-9A-F]{8}$").match(osdScreen->osdText()).hasMatch()){std::fprintf(stderr,"OSD did not show the loaded ROM: %s\n",osdScreen?qPrintable(osdScreen->osdText()):"no screen");return menuFailure(__LINE__);}
     {QMenu*saveMenu=nullptr;for(auto*m:counted.findChildren<QMenu*>())if(QString(m->title()).remove('&')=="Save State")saveMenu=m;if(!saveMenu)return menuFailure(__LINE__);saveMenu->actions().first()->trigger();}
     if(osdScreen->osdText()!="State saved"){std::fprintf(stderr,"OSD did not report the saved state: %s\n",qPrintable(osdScreen->osdText()));return menuFailure(__LINE__);}
     withOsd.osd=false;if(!withOsd.save().isEmpty())return menuFailure(__LINE__);
     QElapsedTimer run;run.start();while(run.elapsed()<1300){app.processEvents();QThread::msleep(1);}
     const auto text=counted.findChild<QLabel*>("statusFps")->text();const auto match=QRegularExpression("^(\\d+\\.\\d) FPS · (\\d+\\.\\d) drawn$").match(text);
     if(!match.hasMatch()||match.captured(1).toDouble()<=0||match.captured(2).toDouble()<=0){std::fprintf(stderr,"Frame rate label does not separate emulated and drawn frames: %s\n",qPrintable(text));return menuFailure(__LINE__);}}
    MENU_STAGE();bool versionChecked=false;QTimer::singleShot(0,&window,[&]{if(auto*box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){versionChecked=box->text().contains(QStringLiteral(SAMEBOY_CORE_COMMIT))&&box->text().contains("https://github.com/LIJI32/SameBoy/commit/");box->accept();}});action("About SameBoy Qt")->trigger();if(!versionChecked)return menuFailure(__LINE__);
    // External opens use path + content identity, not just a shared file hash.
    if(!window.openRom(path))return menuFailure(__LINE__);const auto firstHash=window.currentRomHash();action("Pause")->setChecked(true);
    if(MainWindow::openRequestedRom(path,&window)!=&window||window.currentRomHash()!=firstHash||!action("Pause")->isChecked())return menuFailure(__LINE__);
    QFile changed(path);if(!changed.open(QIODevice::ReadWrite)||!changed.seek(0x200)||changed.write("Z",1)!=1)return menuFailure(__LINE__);changed.close();
    if(MainWindow::openRequestedRom(path,&window)!=&window||window.currentRomHash()==firstHash||action("Pause")->isChecked())return menuFailure(__LINE__);
    const auto copyPath=folder.filePath("same-hash-different-path.gb");if(!QFile::copy(path,copyPath))return menuFailure(__LINE__);
    auto*separate=MainWindow::openRequestedRom(copyPath,&window);if(!separate||separate==&window||separate->currentRomHash()!=window.currentRomHash())return menuFailure(__LINE__);separate->close();app.sendPostedEvents(nullptr,QEvent::DeferredDelete);app.processEvents();
    const auto collision=QDir(QFileInfo(path).absolutePath()).filePath(QFileInfo(path).completeBaseName()+".gbc");QFile::remove(collision);if(!QFile::copy(path,collision))return menuFailure(__LINE__);
    auto*sameBase=MainWindow::openRequestedRom(collision,&window);if(!sameBase||sameBase==&window||batterySavePath(path)==batterySavePath(collision)||stateSavePath(path,1)==stateSavePath(collision,1))return menuFailure(__LINE__);sameBase->close();app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
    // Burst output notifications are coalesced; input-only notifications do not rebuild output.
    auto*devices=window.findChild<QMediaDevices*>("audioDevices");if(!devices)return menuFailure(__LINE__);const auto audioHash=window.currentRomHash();
    for(int i=0;i<50;++i){QMetaObject::invokeMethod(devices,"audioInputsChanged",Qt::QueuedConnection);QMetaObject::invokeMethod(devices,"audioOutputsChanged",Qt::QueuedConnection);}
    QElapsedTimer hotplug;hotplug.start();while(hotplug.elapsed()<250){app.processEvents();QThread::msleep(2);}if(window.currentRomHash()!=audioHash)return menuFailure(__LINE__);
    auto switchOutput=[&](bool missing){bool selected=false;QTimer reply;QObject::connect(&reply,&QTimer::timeout,&window,[&]{for(auto*widget:app.topLevelWidgets())if(auto*dialog=dynamic_cast<SettingsDialog*>(widget);dialog&&dialog->isVisible()&&dialog->isModal()){
        auto*outputs=dialog->findChild<QComboBox*>("audioOutputDevice");if(!outputs){dialog->reject();reply.stop();return;}
        if(missing){outputs->addItem("QA disconnected output",QByteArray("qa-output-not-present"));outputs->setCurrentIndex(outputs->count()-1);}else outputs->setCurrentIndex(0);
        selected=true;reply.stop();dialog->accept();return;
    }});reply.start(10);action(preferencesTitle)->trigger();return selected;};
    MENU_STAGE();for(int cycle=0;cycle<3;++cycle)if(!switchOutput(true)||!switchOutput(false)||window.currentRomHash()!=audioHash)return menuFailure(__LINE__);
    action(closeRomTitle)->trigger();
    MENU_STAGE();if(!aiIntegration(window,path))return menuFailure(__LINE__);
    MENU_STAGE();if(!autoPairIntegration(window))return menuFailure(__LINE__);
    std::puts("Cocoa menu structure, shortcuts, no conflicts, recent ROMs, pause, save/load states, resets and close passed.");
    return unexpectedDialog?1:0;
}
