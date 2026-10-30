#include "boundedfile.h"
#include "mainwindow.h"
#include "uistyle.h"
#include "emulator.h"
#include "videorecorder.h"
#include "screenwidget.h"
#include "settingsdialog.h"
#include "cheatsdialog.h"
#include "audiobuffer.h"
#include "controllers.h"
#include "printerwindow.h"
#include "storagepaths.h"
#include "cartridgeinstance.h"
#include "locallink.h"
#include "gbsplayer.h"
#include "camerasource.h"
#include "debugviews.h"
#include "debugconsole.h"
#include "aidebugserver.h"
#include "coreallocation.h"
#include <QUuid>
#include <QDateTime>
#include "windowplacement.h"
#include "restartsession.h"
#include <QContextMenuEvent>
#include <QStackedWidget>
#include <QMouseEvent>
#include <QLabel>
#include <QDir>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include "audiooutput.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMediaDevices>
#include <QMenuBar>
#include <QMenu>
#include <QResizeEvent>
#include <QShowEvent>
#include <QCloseEvent>
#include <QPushButton>
#include <QLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QTimer>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>

namespace {
QList<QPointer<MainWindow>> controllerWindows; // Most recently activated game first.
class RomPathLabel final:public QLabel{
 QString path_;
 void updateText(){setText(fontMetrics().elidedText(path_,Qt::ElideMiddle,width()));}
public:
 RomPathLabel(){setMinimumWidth(0);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);setObjectName("statusRomPath");}
 void setPath(const QString&path){path_=path;setToolTip(path);setProperty("romPath",path);updateText();}
protected:void resizeEvent(QResizeEvent*e)override{QLabel::resizeEvent(e);updateText();}
};

// Hidden music controls must not constrain the game window (or vice versa).
class CartridgeViews final:public QStackedWidget{
public:
 explicit CartridgeViews(QWidget*parent):QStackedWidget(parent){connect(this,&QStackedWidget::currentChanged,this,[this]{
  for(int i=0;i<count();++i)widget(i)->setSizePolicy(i==currentIndex()?QSizePolicy::Preferred:QSizePolicy::Ignored,i==currentIndex()?QSizePolicy::Preferred:QSizePolicy::Ignored);
  updateGeometry();
 });}
 QSize minimumSizeHint()const override{return currentWidget()?currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()):QSize();}
 QSize sizeHint()const override{return currentWidget()?currentWidget()->sizeHint():QSize();}
};

QString droppedRom(const QMimeData *mime)
{
    const auto urls = mime->urls();
    if (urls.size() != 1 || !urls.first().isLocalFile()) return {};
    const QFileInfo file(urls.first().toLocalFile());
    const auto suffix = file.suffix().toLower();
    if (file.isDir() && suffix == "gbcart" && QFileInfo::exists(file.filePath() + "/rom.gbl")) return file.absoluteFilePath();
    if (!file.isFile() || (suffix != "gb" && suffix != "gbc" && suffix != "gbs")) return {};
    return file.absoluteFilePath();
}

}

struct MainWindow::PreparedRom {
    QString path,source;QByteArray rom,boot;bool color=false,isGbs=false;GB_gbs_info_t gbsInfo{};quint64 generation=0;bool readyToReplace=false;
};

MainWindow::MainWindow() : screen_(new ScreenWidget(this)), timer_(new QTimer(this))
{
    controllerWindows.append(this);
    aiId_=QUuid::createUuid().toString(QUuid::WithoutBraces);consoleSideClock_.start();
    setWindowTitle("SameBoy Qt[*]");
    setAcceptDrops(true);
    views_=new CartridgeViews(this);views_->addWidget(screen_);gbsPlayer_=new GbsPlayer;gbsPlayer_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);views_->addWidget(gbsPlayer_);setCentralWidget(views_);
    gbsPlayer_->pauseRequested=[this]{pause_->toggle();};
    gbsPlayer_->trackRequested=[this](int track){gbsTrack_=track;if(emulator_){const bool paused=pause_->isChecked();pause_->setChecked(true);emulator_->selectGbsTrack(track);applyDebugOptions();pause_->setChecked(paused);}};
    screen_->setSettings(settings_);
    buildMenus();
    timer_->setObjectName("emulationUiPoll");
    cameraInput_=std::make_shared<CameraInput>();cameraSource_=std::make_unique<CameraSource>(cameraInput_);
    cameraSource_->message=[this](const QString&text){statusBar()->showMessage(text,15000);};cameraSource_->configure(settings_);
    controllers_=std::make_unique<Controllers>();
    fpsLabel_=new QLabel(tr("— FPS"));fpsLabel_->setObjectName("statusFps");
    fpsLabel_->setToolTip(tr("Frames presented to the game view per second"));
    romPathLabel_=new RomPathLabel;statusBar()->addWidget(romPathLabel_,1);
    statusBar()->addPermanentWidget(fpsLabel_);fpsClock_.start();
    QSettings ui(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");
    showStatusBar_->setChecked(ui.value("view/statusBar",true).toBool());statusBar()->setVisible(showStatusBar_->isChecked());
    showMenuBar_->setChecked(ui.value("view/menuBar",true).toBool());menuBar()->setVisible(showMenuBar_->isChecked());
    screen_->setMouseTracking(true);
    for(auto*child:screen_->findChildren<QWidget*>())child->setMouseTracking(true);
    statusBar()->showMessage(tr("Ready — arrows: D-pad | X: A | Z: B | Enter: Start | Backspace: Select"));
    qApp->installEventFilter(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::poll);
    timer_->setTimerType(Qt::PreciseTimer);
    timer_->start(100);
    setFocusProxy(screen_);
    resize(480,432+(showMenuBar_->isChecked()?menuBar()->sizeHint().height():0)+(showStatusBar_->isChecked()?statusBar()->sizeHint().height():0));
}

void MainWindow::showEvent(QShowEvent*event){
    QMainWindow::showEvent(event);if(!initialViewportPending_)return;initialViewportPending_=false;
    QTimer::singleShot(0,this,[this]{if(!isMaximized()&&!isFullScreen()&&!gbsMode_){layout()->activate();resize(size()+QSize(480,432)-screen_->size());layout()->activate();}});
}

MainWindow::~MainWindow() { controllerWindows.removeAll(this);qApp->removeEventFilter(this); stop(); }
void MainWindow::setRomModified(bool modified){if(romModified_==modified)return;romModified_=modified;setWindowModified(modified);refreshMenus();}
MainWindow* MainWindow::controllerInputOwner(){
    for(const auto&candidate:controllerWindows)if(candidate&&candidate->isVisible()&&candidate->emulator_&&!candidate->closing_)return candidate;
    return nullptr;
}
void MainWindow::clearInput() {
    keys_=actions_=0;keyboardKeys_.fill(0);keyboardActions_.fill(0);mouseMotion_.leftButton=false;
    // Release keyboard/mouse without interrupting the designated background pad or script lease.
    const bool retainPad=controllers_&&settings_.backgroundControllers&&controllerInputOwner()==this&&!QApplication::activeModalWidget()&&!pause_->isChecked();
    if(emulator_){EmulatorInput input;input.lease=aiInput_;
        if(retainPad){const auto mapped=mapControllers(controllers_->devices(),settings_);
            for(int p=0;p<4;++p){input.keys[p]=mapped.players[p].keys;input.actions[p]=mapped.players[p].actions;input.x[p]=mapped.players[p].x;input.y[p]=mapped.players[p].y;input.analog[p]=mapped.players[p].analog;}
            input.speed=mapped.speed;input.motionX=mapped.motionX;input.motionY=mapped.motionY;
        }
        emulator_->setKeys(0);emulator_->setActions(0);emulator_->setInput(input);
    }
    if(controllers_&&!retainPad)controllers_->rumble(settings_,0);
}
void MainWindow::updateInput(){
    controllers_->poll();
    if(!QApplication::activeModalWidget())autoPairing_.update(controllers_->devices(),settings_,[this](const AppSettings&candidate){return applySettings(candidate);});
    const bool foreground=QApplication::activeWindow()==this;
    const bool allowed=!QApplication::activeModalWidget()&&(foreground||(settings_.backgroundControllers&&controllerInputOwner()==this));
    QMap<QString,quint32> currentButtons;
    for(const auto&pad:controllers_->devices()){
        auto buttons=pad.buttons;
        // Cocoa also emits digital direction events when a stick crosses a direction threshold.
        if(pad.x>.45)buttons|=1u<<26;if(pad.x<-.45)buttons|=1u<<27;
        if(pad.y>.45)buttons|=1u<<28;if(pad.y<-.45)buttons|=1u<<29;
        if(allowed&&buttons!=previousPadButtons_.value(pad.id))mouseMotion_.controllerActivity();
        currentButtons.insert(pad.id,buttons);
    }
    previousPadButtons_=std::move(currentButtons);
    auto mapped=allowed?mapControllers(controllers_->devices(),settings_):ControllerInput{};
    const auto pressed=mapped.hotkeys&~previousHotkeys_;previousHotkeys_=mapped.hotkeys;
    // Hotkeys are accepted while paused, but never from inside Preferences or file dialogs.
    for(int i=0;i<2;++i)if(pressed&(1u<<i))switch(settings_.controllerHotkeys[i]){
        case 1:pause_->toggle();break;case 2:resetGame(false);break;case 3:saveState(1,false);break;
        case 4:saveState(1,true);break;case 5:saveScreenshot(false);break;default:break;
    }
    if(!emulator_)return;
    EmulatorInput input;
    if(!pause_->isChecked()){
        for(int p=0;p<4;++p){input.keys[p]=mapped.players[p].keys|(foreground?keyboardKeys_[p]:0);
            input.actions[p]=mapped.players[p].actions|(foreground?keyboardActions_[p]:0);
            input.x[p]=mapped.players[p].x;input.y[p]=mapped.players[p].y;input.analog[p]=mapped.players[p].analog;}
        input.speed=mapped.speed;input.motionX=mapped.motionX;input.motionY=mapped.motionY;
        if(foreground&&settings_.allowMouse&&mouseMotion_.enabled&&emulator_->hasMotion()){
            input.motionX=mouseMotion_.tilt.x();input.motionY=mouseMotion_.tilt.y();if(mouseMotion_.leftButton)input.keys[0]|=GB_KEY_A_MASK;
        }
    }
    aiInput_.keys=aiInput_.active();input.lease=aiInput_;
    emulator_->setInput(input);
    controllers_->rumble(settings_,allowed&&!pause_->isChecked()?emulator_->rumble():0);
}

void MainWindow::disconnectLink(){
    auto*partner=linkPartner_.data();if(!partner)return;
    linkPartner_.clear();partner->linkPartner_.clear();
    if(emulator_)emulator_->setLocalLink({});
    if(partner->emulator_)partner->emulator_->setLocalLink({});
    noneAccessory_->setChecked(true);partner->noneAccessory_->setChecked(true);
    statusBar()->showMessage(tr("Link disconnected"),4000);partner->statusBar()->showMessage(tr("Link disconnected"),4000);
}
void MainWindow::connectTo(MainWindow*partner){
    if(!partner||partner==this||!emulator_||!partner->emulator_)return;
    disconnectLink();partner->disconnectLink();
    const bool running=!pause_->isChecked()||!partner->pause_->isChecked();
    pause_->setChecked(true);partner->pause_->setChecked(true);
    printerAction_->setChecked(false);partner->printerAction_->setChecked(false);
    emulator_->setPrinterConnected(false);partner->emulator_->setPrinterConnected(false);
    auto connection=std::make_shared<LocalLink>();
    emulator_->setLocalLink(connection,0);partner->emulator_->setLocalLink(connection,1);
    linkPartner_=partner;partner->linkPartner_=this;
    noneAccessory_->setChecked(false);partner->noneAccessory_->setChecked(false);
    statusBar()->showMessage(tr("Link cable and infrared connected to %1").arg(partner->windowTitle()),5000);
    partner->statusBar()->showMessage(tr("Link cable and infrared connected to %1").arg(windowTitle()),5000);
    pause_->setChecked(!running);partner->pause_->setChecked(!running);
}

void MainWindow::applyDebugOptions(){
    if(!emulator_)return;const bool background=debugBackground_->isChecked(),objects=debugObjects_->isChecked();std::array<bool,4>channels;
    for(int i=0;i<4;++i)channels[i]=debugChannels_[i]->isChecked();
    coreCommand([background,objects,channels](GB_gameboy_t*g){GB_set_background_rendering_disabled(g,!background);GB_set_object_rendering_disabled(g,!objects);for(int i=0;i<4;++i)GB_set_channel_muted(g,GB_channel_t(i),!channels[i]);return QString();});
}
void MainWindow::showMemory(){
    if(!emulator_)return;const bool created=!memoryDialog_;if(!memoryDialog_)memoryDialog_=new MemoryDialog([this](auto command){return emulator_?emulator_->execute(std::move(command)):tr("No cartridge running");},settings_.monospace,this,[this](auto*context,auto command,auto done){if(emulator_)emulator_->executeAsync(context,std::move(command),std::move(done));else done(tr("No cartridge running"));},[this]{setRomModified(true);});
    memoryDialog_->setWindowTitle(tr("Memory — %1").arg(QFileInfo(romPath_).fileName()));memoryDialog_->show();if(created)placeNewWindow(memoryDialog_,this);memoryDialog_->raise();memoryDialog_->activateWindow();
}
void MainWindow::showVram(){
    if(!emulator_)return;const bool created=!vramDialog_;if(!vramDialog_)vramDialog_=new VramDialog([this](auto command){return emulator_?emulator_->execute(std::move(command)):tr("No cartridge running");},settings_.monospace,this,[this](auto*context,auto command,auto done){if(emulator_)emulator_->executeAsync(context,std::move(command),std::move(done));else done(tr("No cartridge running"));});
    vramDialog_->setWindowTitle(tr("VRAM Viewer — %1").arg(QFileInfo(romPath_).fileName()));vramDialog_->show();if(created)placeNewWindow(vramDialog_,this);vramDialog_->raise();vramDialog_->activateWindow();
}

void MainWindow::refreshToolWindows(){
    if(romPathLabel_)static_cast<RomPathLabel*>(romPathLabel_)->setPath(romSource_);
    const auto name=QFileInfo(romPath_).fileName();
    if(console_){console_->setWindowTitle(tr("Debug Console — %1").arg(name));console_->clearOutput();console_->resetCpuLoad();consoleSequence_=0;consoleCpuSequence_=0;consoleSidePending_=false;}
    if(memoryDialog_){memoryDialog_->setWindowTitle(tr("Memory — %1").arg(name));memoryDialog_->cartridgeChanged();}
    if(vramDialog_){vramDialog_->setWindowTitle(tr("VRAM Viewer — %1").arg(name));vramDialog_->cartridgeChanged();}
    if(cheatsDialog_){cheatsDialog_->setWindowTitle(tr("Cheats — %1").arg(name));cheatsDialog_->setCartridge(storageBase_);}
    if(cheatSearchDialog_){cheatSearchDialog_->setWindowTitle(tr("Cheat Search — %1").arg(name));cheatSearchDialog_->setCartridge(storageBase_);}
}

void MainWindow::showPrinter(){const bool created=!printerWindow_;if(!printerWindow_)printerWindow_=new PrinterWindow(this);printerWindow_->show();if(created)placeNewWindow(printerWindow_,this);printerWindow_->raise();printerWindow_->activateWindow();}

bool MainWindow::applySettings(const AppSettings&value){
    const auto originalTheme=settings_.theme;
    auto shared=value;shared.developerMode=settings_.developerMode;
    QList<QPointer<MainWindow>> windows;
    for(auto*widget:qApp->topLevelWidgets())if(auto*window=dynamic_cast<MainWindow*>(widget))windows.append(window);
    struct Change{QPointer<MainWindow> window;AppSettings settings;std::shared_ptr<PreparedRom> rom;bool paused;};
    QList<Change> changes;
    // Read every destructive reload's assets before persisting or touching any core.
    for(auto window:windows)if(window&&!window->closing_){
        auto local=shared;if(window!=this)local.hardware=window->settings_.hardware;
        std::shared_ptr<PreparedRom> rom;
        if(window->emulator_){
            auto effective=local;if(window->gbsMode_)effective.hardware=2;GB_model_t active=GB_MODEL_DMG_B;
            const auto error=window->emulator_->execute([&](GB_gameboy_t*g){active=GB_get_model(g);return QString();});
            if(!error.isEmpty()){applyInterfaceTheme(originalTheme);QMessageBox::warning(this,tr("Preferences"),error);return false;}
            if(active!=effective.model(window->romColor_)){
                rom=std::make_shared<PreparedRom>();
                if(!window->prepareRom(window->romPath_,local,*rom)){applyInterfaceTheme(originalTheme);return false;}rom->generation=window->generation_;
            }
        }
        changes.append({window,local,std::move(rom),window->pause_->isChecked()});
    }
    auto resumePrepared=[&](bool canceled){
        for(const auto&change:changes)if(change.window&&change.rom&&change.rom->readyToReplace){
            change.window->closing_=false;
            change.window->discardRomOnClose_=false;
            if(canceled&&change.window->emulator_)change.window->emulator_->discardBatteryOnClose(false);
        }
        // Restore each original state without propagating a second pause toggle through links.
        for(const auto&change:changes)if(auto window=change.window;window&&window->pause_->isChecked()!=change.paused){
            QSignalBlocker blocker(window->pause_);window->pause_->setChecked(change.paused);
            window->clearInput();if(window->emulator_)window->emulator_->setPaused(change.paused);
            if(window->audioBuffer_)window->audioBuffer_->clear();
            if(window->audio_){if(change.paused)window->audio_->suspend();else window->audio_->resume();}
        }
    };
    // Keep every old core alive until all battery saves/Discard choices are confirmed.
    for(const auto&change:changes)if(change.window&&change.rom){
        if(!change.window->prepareToClose()){resumePrepared(true);applyInterfaceTheme(originalTheme);return false;}
        change.rom->readyToReplace=true;
    }
    const auto persisted=AppSettings::load();
    // Commit before any destructive reset: a failed write must leave live cores untouched.
    const auto saveError=shared.save();
    if(!saveError.isEmpty()){
        resumePrepared(true);applyInterfaceTheme(originalTheme);refreshMenus();
        QMessageBox::warning(this,tr("Preferences"),saveError);return false;
    }
    QList<QPair<QPointer<MainWindow>,AppSettings>> applied;
    for(const auto&change:changes)if(auto window=change.window;window&&(!window->closing_||(change.rom&&change.rom->readyToReplace))){
        const auto previous=window->settings_;
        if(!window->adoptSettings(change.settings,change.rom.get())){
            bool restored=true;
            for(auto it=applied.crbegin();it!=applied.crend();++it)if(it->first&&!it->first->adoptSettings(it->second))restored=false;
            resumePrepared(true);const auto restoreError=persisted.save();
            applyInterfaceTheme(originalTheme);
            if(!restoreError.isEmpty())QMessageBox::warning(this,tr("Preferences"),restoreError);
            if(!restored)QMessageBox::warning(this,tr("Preferences"),tr("Could not restore all windows after a settings error. Reopen the affected ROMs."));
            return false;
        }
        applied.append({window,previous});
    }
    resumePrepared(false);
    for(auto window:windows)if(window)window->autoPairing_.reset();
    applyInterfaceTheme(shared.theme);return true;
}
bool MainWindow::adoptSettings(const AppSettings&value,const PreparedRom*prepared){
    const auto previous=settings_;settings_=value;
    if(emulator_){
        auto effective=settings_;if(gbsMode_)effective.hardware=2;
        GB_model_t active=GB_MODEL_DMG_B;
        const auto error=emulator_->execute([&](GB_gameboy_t*g){active=GB_get_model(g);return QString();});
        if(error.isEmpty()&&active!=effective.model(romColor_)){
            const bool paused=pause_->isChecked(),music=gbsMode_;const int track=gbsTrack_;const auto oldSize=size();const auto path=romPath_;
            if(!(prepared?openPreparedRom(*prepared):openRom(path))){settings_=previous;refreshMenus();return false;}
            if(music){gbsPlayer_->selectTrack(track);if(!isMaximized()&&!isFullScreen())resize(oldSize);}
            pause_->setChecked(paused);return true;
        }
        if(!error.isEmpty()){settings_=previous;statusBar()->showMessage(error,10000);return false;}
    }
    if(previous.cameraMode!=settings_.cameraMode||previous.cameraDevice!=settings_.cameraDevice||previous.cameraImage!=settings_.cameraImage||previous.cameraMirror!=settings_.cameraMirror)cameraSource_->configure(settings_);
    screen_->setSettings(settings_);
    if(settings_.audioSampleRate!=previous.audioSampleRate){stopVideoRecording();stopRecording();if(audioBuffer_)audioBuffer_->clear();if(audio_)audio_->stop();audio_.reset();audioBuffer_.reset();configureAudio();if(emulator_)emulator_->reconfigureAudio(audioSampleRate_,audioBuffer_);}
    if(audio_)audio_->setVolume(mute_->isChecked()?0:settings_.volume/100.0);if(emulator_)emulator_->updateSettings(settings_);clearInput();refreshMenus();return true;
}

QString MainWindow::setDeveloperMode(bool enabled,bool reportError){
    if(enabled==settings_.developerMode)return {};
    auto candidate=settings_;candidate.developerMode=enabled;const auto error=candidate.save();
    if(!error.isEmpty()){
        {QSignalBlocker block(developerModeAction_);developerModeAction_->setChecked(settings_.developerMode);}
        if(reportError)QMessageBox::warning(this,tr("Preferences"),error);return error;
    }
    settings_.developerMode=enabled;if(!enabled){if(auto*server=AiDebugServer::instance())server->setEnabled(false);}
    for(auto*widget:qApp->topLevelWidgets())if(auto*game=dynamic_cast<MainWindow*>(widget)){game->settings_.developerMode=enabled;QSignalBlocker block(game->developerModeAction_);game->developerModeAction_->setChecked(enabled);if(game->emulator_)game->emulator_->updateSettings(game->settings_);game->refreshMenus();}
    return {};
}
void MainWindow::showConsole(){const bool created=!console_;if(!console_)console_=new DebugConsole(settings_.monospace,[this](QString command){submitDebugCommand(command);},[this]{clearConsole();},[this]{loadConsoleSymbols();},this);console_->setWindowTitle(tr("Debug Console — %1").arg(QFileInfo(romPath_).fileName()));console_->show();if(created)placeNewWindow(console_,this);console_->raise();console_->activateWindow();pollConsole();}
void MainWindow::clearConsole(){if(emulator_)consoleSequence_=emulator_->debugLogs().next;if(console_)console_->clearOutput();}
void MainWindow::submitDebugCommand(const QString&command){if(!emulator_||!settings_.developerMode)return;if(!emulator_->sendDebugCommand(command)){statusBar()->showMessage(tr("Debugger command queue is full or emulation is stopping."),5000);return;}pause_->setChecked(false);}
void MainWindow::loadConsoleSymbols(){if(!emulator_)return;const auto path=QFileDialog::getOpenFileName(this,tr("Load Symbols"),QFileInfo(romSource_).path(),tr("Symbol files (*.sym);;All files (*)"));if(path.isEmpty())return;QFile symbols(path),registers(":/debug/registers.sym");QByteArray bytes;if(!readBoundedFile(symbols,bytes,4*1024*1024)){QMessageBox::warning(this,tr("Load Symbols"),tr("Cannot read symbol file."));return;}emulator_->loadDebugSymbols(romSource_,registers.open(QIODevice::ReadOnly)?registers.readAll():QByteArray(),bytes,path);}
void MainWindow::pollConsole(){
    if(!emulator_){if(console_)console_->setState(false,false);return;}
    const auto batch=emulator_->debugLogs(consoleSequence_,500);if(!batch.entries.isEmpty()){
        if(!console_&&settings_.developerMode){showConsole();return;}if(console_){console_->append(batch.entries);consoleSequence_=batch.entries.last().sequence;}
    }
    if(console_&&console_->isVisible()){auto samples=emulator_->cpuLoadSamples(consoleCpuSequence_);if(!samples.isEmpty())consoleCpuSequence_=samples.last().sequence;console_->updateCpuLoad(samples,emulator_->paused()||emulator_->debuggerStopped());}
    const bool stopped=emulator_->debuggerStopped();breakDebuggerAction_->setEnabled(settings_.developerMode&&!stopped);pause_->setEnabled(!stopped);
    if(console_)console_->setState(settings_.developerMode,stopped,{},-1,emulator_->paused());
    if(console_&&console_->isVisible()&&stopped&&!consoleSidePending_&&consoleSideClock_.elapsed()>=200){consoleSideClock_.restart();consoleSidePending_=true;auto data=std::make_shared<QString>();auto cpu=std::make_shared<double>(0);const auto generation=generation_;auto*worker=emulator_.get();
        worker->executeAsync(this,[worker,data,cpu](GB_gameboy_t*g){*data=worker->captureDebugOutput(g,"registers")+"\n"+worker->captureDebugOutput(g,"backtrace");*cpu=GB_debugger_get_second_cpu_usage(g);return QString();},[this,data,cpu,generation](QString error){if(generation!=generation_)return;consoleSidePending_=false;if(error.isEmpty()&&console_)console_->setState(settings_.developerMode,emulator_&&emulator_->debuggerStopped(),*data,*cpu);});
    }
}

void MainWindow::showPreferences(int tab)
{
    const bool wasPaused = pause_->isChecked();
    pause_->setChecked(true);
    SettingsDialog dialog(settings_, this,tab);dialog.show();placeNewWindow(&dialog,this);
    if (dialog.exec() == QDialog::Accepted) {
        if(!applySettings(dialog.settings())){pause_->setChecked(wasPaused);refreshMenus();return;}
        if(dialog.restartRequested())QTimer::singleShot(0,qApp,[]{
            QVector<RestartWindow> session;for(auto*widget:qApp->topLevelWidgets())if(auto*game=dynamic_cast<MainWindow*>(widget);game&&game->isVisible())session.append({game->currentRomPath(),game->saveGeometry()});
            QList<QPair<QPointer<MainWindow>,bool>> pending;
            for(auto*widget:qApp->topLevelWidgets())if(auto*game=dynamic_cast<MainWindow*>(widget))pending.append({game,game->pause_->isChecked()});
            for(const auto&entry:pending)if(entry.first&&!entry.first->prepareToClose()){
                for(const auto&restore:pending)if(restore.first){
                    restore.first->closing_=false;
                    if(restore.first->emulator_)restore.first->emulator_->discardBatteryOnClose(false);
                }
                for(const auto&restore:pending)if(auto game=restore.first;game&&game->pause_->isChecked()!=restore.second){
                    QSignalBlocker blocker(game->pause_);game->pause_->setChecked(restore.second);game->clearInput();
                    if(game->emulator_)game->emulator_->setPaused(restore.second);
                    if(game->audioBuffer_)game->audioBuffer_->clear();
                    if(game->audio_){if(restore.second)game->audio_->suspend();else game->audio_->resume();}
                }
                return;
            }
            qApp->setProperty("restartSession",encodeRestartSession(session));qApp->setProperty("restartRequested",true);qApp->quit();
        });
    }
    pause_->setChecked(wasPaused);
    refreshMenus();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!droppedRom(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto path = droppedRom(event->mimeData());
    if (path.isEmpty()) {
        event->ignore();
        return;
    }
    clearInput();
    if (openRom(path)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::chooseRom()
{
    const bool wasPaused = pause_->isChecked();
    pause_->setChecked(true);
    const auto path = QFileDialog::getOpenFileName(this, tr("Open ROM"), {},
        tr("Game Boy files (*.gb *.gbc *.gbs);;All files (*)"));
    if (path.isEmpty() || !openRom(path)) pause_->setChecked(wasPaused);
}

void MainWindow::configureAudio()
{
    QAudioFormat format;
    format.setSampleRate(settings_.audioSampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    const auto device = QMediaDevices::defaultAudioOutput();
    if(!device.isNull()&&!device.isFormatSupported(format)){
        for(int rate:{48000,44100,96000}){format.setSampleRate(rate);if(device.isFormatSupported(format))break;}
    }
    audioSampleRate_=format.sampleRate();
    if (device.isNull() || !device.isFormatSupported(format)) {
        statusBar()->showMessage(tr("Audio unavailable: no supported stereo PCM format. Running without audio."),15000);
        return;
    }
    audio_ = std::make_unique<AudioOutput>(device, format);
    audio_->setVolume(mute_->isChecked() ? 0 : settings_.volume / 100.0);
    audio_->setBufferSize(audioSampleRate_*4/25);
    audioBuffer_=std::make_shared<AudioBuffer>(audioSampleRate_);
    audio_->start(audioBuffer_.get());
    if (audio_->error()!=QAudio::NoError) {
        statusBar()->showMessage(tr("Could not start audio output. Running without audio."),15000);
        audio_.reset();
        audioBuffer_.reset();
    }
}

bool MainWindow::focusStorageOwner(const QString &path)
{
    const auto target=storageIdentity(batterySavePath(path));
    for(auto*widget:QApplication::topLevelWidgets()){
        auto*other=dynamic_cast<MainWindow*>(widget);
        if(!other||other==this||!other->emulator_||other->romPath_.isEmpty())continue;
        if(storageIdentity(batterySavePath(other->romPath_))!=target)continue;
        if(other->isMinimized())other->showNormal();else other->show();
        other->raise();other->activateWindow();
        other->statusBar()->showMessage(tr("This cartridge's save files are already in use by this window."),10000);
        return true;
    }
    return false;
}

bool MainWindow::prepareRom(const QString&path,const AppSettings&settings,PreparedRom&prepared)
{
    const QFileInfo cartridge(path);
    QString source = cartridge.absoluteFilePath();
    if (cartridge.isDir() && cartridge.suffix().compare("gbcart",Qt::CaseInsensitive) == 0) {
        const auto instanceDirectory=source;
        QFile link(QDir(instanceDirectory).filePath("rom.gbl"));
        QByteArray descriptor;if(!readBoundedFile(link,descriptor,1024*1024,1)){QMessageBox::warning(this,tr("Open Cartridge"),tr("Cannot read cartridge instance."));return false;}
        link.close();
        bool needsRewrite=false;QString resolvedSource;
        if(!resolveCartridgeInstance(descriptor,instanceDirectory,resolvedSource,needsRewrite)){
            QMessageBox::warning(this,tr("Open Cartridge"),tr("Cannot read cartridge instance."));return false;
        }
        source=resolvedSource;
        if(needsRewrite)repairCartridgeInstanceDescriptor(instanceDirectory,source);
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 112 || file.size() > 8 * 1024 * 1024) {
        QMessageBox::warning(this, tr("Open ROM"), tr("Cannot read a valid Game Boy ROM or GBS file:\n%1").arg(path));
        return false;
    }
    QByteArray rom;
    if(!readBoundedFile(file,rom,8*1024*1024,112)){
        QMessageBox::warning(this, tr("Open ROM"), tr("The ROM could not be read completely."));
        return false;
    }
    const bool isGbs=rom.startsWith(QByteArray("GBS\1",4));GB_gbs_info_t gbsInfo{};
    if(isGbs&&(uint8_t(rom[4])==0||uint8_t(rom[5])==0||uint8_t(rom[5])>uint8_t(rom[4]))){QMessageBox::warning(this,tr("GBS Player"),tr("Invalid GBS track table."));return false;}
    if(isGbs){auto*probe=allocateGameBoy(GB_MODEL_CGB_E);if(!probe){QMessageBox::critical(this,tr("GBS Player"),tr("Could not allocate a temporary emulator core."));return false;}const int result=GB_load_gbs_from_buffer(probe,reinterpret_cast<const uint8_t*>(rom.constData()),rom.size(),&gbsInfo);GB_free(probe);GB_dealloc(probe);
        if(result||!gbsInfo.track_count||gbsInfo.first_track>=gbsInfo.track_count){QMessageBox::warning(this,tr("GBS Player"),tr("Invalid GBS music file."));return false;}}
    else if(rom.size()<0x150){QMessageBox::warning(this,tr("Open"),tr("Invalid ROM or GBS file."));return false;}
    const bool color = isGbs||(static_cast<unsigned char>(rom[0x143]) & 0x80) != 0;
    const auto name = settings.bootName(color);
    QFile boot(settings.bootFolder.isEmpty() ? ":/bootroms/" + name : QDir(settings.bootFolder).filePath(name));
    if (!isGbs&&!boot.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Boot ROM"), tr("Cannot read boot ROM:\n%1").arg(boot.fileName()));
        return false;
    }
    if (!isGbs&&(boot.size() < 256 || boot.size() > 2304)) {
        QMessageBox::warning(this, tr("Boot ROM"), tr("Boot ROM must be between 256 and 2304 bytes.")); return false;
    }
    QByteArray bootBytes;
    if(!isGbs&&!readBoundedFile(boot,bootBytes,2304,256)){
        QMessageBox::warning(this,tr("Boot ROM"),tr("Cannot read boot ROM:\n%1").arg(boot.fileName()));return false;
    }
    prepared={path,source,rom,bootBytes,color,isGbs,gbsInfo};return true;
}

bool MainWindow::openRom(const QString &path)
{
    if(closing_||focusStorageOwner(path))return false;
    PreparedRom prepared;if(!prepareRom(path,settings_,prepared))return false;
    return openPreparedRom(prepared);
}

bool MainWindow::openPreparedRom(const PreparedRom&prepared)
{
    if((closing_&&!prepared.readyToReplace)||(prepared.readyToReplace&&(!closing_||prepared.generation!=generation_))||focusStorageOwner(prepared.path))return false;
    const auto&path=prepared.path;const auto&source=prepared.source;const auto&rom=prepared.rom;
    const bool isGbs=prepared.isGbs,color=prepared.color;const auto&gbsInfo=prepared.gbsInfo;
    if(!prepared.readyToReplace&&!prepareToClose())return false;
    stop(true);
    mouseMotion_={};
    const bool wasGbs=gbsMode_;if(isGbs&&!wasGbs)gameWindowSize_=size();
    gbsMode_=isGbs;views_->setCurrentWidget(isGbs?static_cast<QWidget*>(gbsPlayer_):screen_);
    if(!isGbs&&wasGbs&&!gameWindowSize_.isEmpty()&&!isMaximized()&&!isFullScreen())resize(gameWindowSize_);
    if(isGbs){gbsTrack_=gbsInfo.first_track;gbsPlayer_->setInfo(gbsInfo);resize(500,340);}
    pause_->setChecked(false);
    pause_->setEnabled(true);
    const QFileInfo info(path);
    const QString base = info.isDir() ? QDir(info.absoluteFilePath()).filePath("cartridge") : info.absoluteFilePath();
    const QString savePath = batterySavePath(path);
    configureAudio();
    cameraSource_->configure(settings_);
    auto workerSettings=settings_;if(isGbs)workerSettings.hardware=2;
    emulator_ = std::make_unique<Emulator>(rom, prepared.boot, savePath, color, workerSettings,audioSampleRate_,audioBuffer_,base+".sav",cameraInput_);
    setWindowTitle(info.fileName() + " — SameBoy Qt[*]");
    setRomModified(false);discardRomOnClose_=false;
    statusBar()->clearMessage();
    timer_->setInterval(16);
    emulator_->setPrinterConnected(printerAction_->isChecked());
    emulator_->setCaptureAudio(gbsMode_);
    emulator_->start();
    romColor_=color;romPath_ = info.absoluteFilePath();
    romSource_ = QFileInfo(source).absoluteFilePath(); storageBase_ = base;
    coreCommand([base,enabled = cheatsEnabled_->isChecked()](GB_gameboy_t *gb) {
        const auto error = restoreCheats(gb,base); GB_set_cheats_enabled(gb,enabled); return error;
    });
    {QFile registers(":/debug/registers.sym"),symbols(QFileInfo(romSource_).path()+"/"+QFileInfo(romSource_).completeBaseName()+".sym");const auto common=registers.open(QIODevice::ReadOnly)?registers.readAll():QByteArray();QByteArray local;readBoundedFile(symbols,local,4*1024*1024);emulator_->loadDebugSymbols(romSource_,common,local);}
    applyDebugOptions();refreshToolWindows();
    QSettings history(QSettings::IniFormat, QSettings::UserScope, "SameBoyQt", "SameBoyQt");
    auto recent = history.value("recentROMs").toStringList();
    recent.removeAll(romPath_); recent.prepend(romPath_);
    while (recent.size() > 10) recent.removeLast();
    history.setValue("recentROMs", recent);
    refreshRecent(); refreshMenus();
    return true;
}

bool MainWindow::prepareToClose(){
    if(closing_)return false;
    if(!emulator_)return true;
    bool discardRom=false;
    if(romModified_){
        QMessageBox prompt(QMessageBox::Warning,tr("Unsaved ROM Modifications"),tr("Save changes to the ROM before closing it?"),QMessageBox::NoButton,this);
        prompt.setObjectName("unsavedRomModifications");
        QPushButton*save=prompt.addButton(QMessageBox::Save);QPushButton*discard=prompt.addButton(QMessageBox::Discard);QPushButton*cancel=prompt.addButton(QMessageBox::Cancel);prompt.setDefaultButton(save);prompt.exec();
        if(prompt.clickedButton()==cancel)return false;
        if(prompt.clickedButton()==save&&!saveRom(false))return false;
        discardRom=prompt.clickedButton()==discard;
    }
    closing_=true;
    const bool paused=pause_->isChecked();pause_->setChecked(true);emulator_->setPaused(true);
    for(;;){
        const auto error=emulator_->flushBattery();if(error.isEmpty()){discardRomOnClose_=discardRom;return true;}
        const auto choice=QMessageBox::warning(this,tr("Battery save"),
            tr("The battery save failed. Retry, cancel to keep this cartridge open, or discard the unsaved progress.")+"\n\n"+error,
            QMessageBox::Retry|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
        if(choice==QMessageBox::Retry)continue;
        if(choice==QMessageBox::Discard){emulator_->discardBatteryOnClose();emulator_->takeError();discardRomOnClose_=discardRom;return true;}
        closing_=false;pause_->setChecked(paused);emulator_->setPaused(paused);return false;
    }
}
void MainWindow::closeEvent(QCloseEvent*event){
    if(!prepareToClose()){event->ignore();return;}
    stop();QMainWindow::closeEvent(event);
}

void MainWindow::stop(bool keepTools)
{
    setRomModified(false);discardRomOnClose_=false;
    closing_=true;
    ++generation_;aiInput_.keys.fill(0);aiInput_.deadlines.fill({});aiAdvancing_=false;consoleSequence_=0;consoleCpuSequence_=0;consoleSidePending_=false;
    disconnectLink();
    if(cameraSource_)cameraSource_->stop();
    timer_->setInterval(100);
    if(!keepTools){delete console_.data();delete memoryDialog_.data();delete vramDialog_.data();delete cheatSearchDialog_.data();delete cheatsDialog_.data();}
    stopVideoRecording();stopRecording();
    if (emulator_) {
        emulator_->requestInterruption();
        emulator_->wait();
        const auto error = emulator_->takeError();
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Battery save"), error);
        emulator_.reset();
    }
    if(printerWindow_)printerWindow_->setPrinting(false);
    if(audioBuffer_)audioBuffer_->clear();
    if (audio_) audio_->stop();
    audio_.reset();
    audioBuffer_.reset();
    clearInput();closing_=false;
}

void MainWindow::pollPrinterOutput(){
    if(!emulator_)return;
    for(const auto&output:emulator_->takePrinterOutput()){
        if(output.done){if(printerWindow_)printerWindow_->setPrinting(false);}
        else {const bool created=!printerWindow_;if(!printerWindow_)printerWindow_=new PrinterWindow(this);
            if(!printerWindow_->append(output.image))statusBar()->showMessage(tr("Printer feed is full. Save and clear it before printing more."),15000);if(created&&printerWindow_->isVisible())placeNewWindow(printerWindow_,this);}
    }
}

void MainWindow::poll()
{
    if(closing_)return;
    updateInput();pollConsole();pollVideoRecording();
    cameraSource_->poll(emulator_&&emulator_->hasCameraCartridge()&&!pause_->isChecked());
    if (!emulator_) {fpsLabel_->setText(tr("— FPS"));displayedFrames_=0;fpsClock_.restart();return;}
    pollPrinterOutput();
    bool odd=false,sgb=false;QImage previous;
    auto frame = emulator_->takeFrame(&odd,&sgb,&previous);
    if (!frame.isNull()) {screen_->setFrame(std::move(frame),odd,sgb,std::move(previous));++displayedFrames_;if(vramDialog_&&vramDialog_->isVisible())vramDialog_->requestRefresh();}
    if(fpsClock_.elapsed()>=1000){
        fpsLabel_->setText(tr("%1 FPS").arg(displayedFrames_*1000.0/fpsClock_.elapsed(),0,'f',1));
        displayedFrames_=0;fpsClock_.restart();
    }
    emulator_->setCaptureAudio(gbsMode_||bool(recording_));
    const auto samples = emulator_->takeAudio();
    if(gbsMode_){gbsPlayer_->setSamples(samples);gbsPlayer_->setPaused(pause_->isChecked());}
    if (recording_ && !samples.isEmpty() && !pause_->isChecked()) {
        if (recordedBytes_ > 0xffffffffu - 44u - samples.size() || recording_->write(samples) != samples.size()) {
            statusBar()->showMessage(tr("Audio recording stopped: file limit or write error."), 10000);
            stopRecording();
        } else recordedBytes_ += samples.size();
    }
    const auto error = emulator_->takeError();
    if (!error.isEmpty()) statusBar()->showMessage(error, 15000);
}

void MainWindow::setChromeVisible(QWidget*bar,bool visible){
    if(bar->isHidden()==!visible)return;const auto viewport=centralWidget()->size();bar->setVisible(visible);
    if(isVisible()){layout()->activate();if(!isMaximized()&&!isFullScreen()){const auto delta=viewport-centralWidget()->size();resize(size()+delta);layout()->activate();}}
    screen_->setFocus(Qt::OtherFocusReason);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if(event->type()==QEvent::ContextMenu&&(object==screen_||screen_->isAncestorOf(qobject_cast<QWidget*>(object)))){
        showGameContextMenu(static_cast<QContextMenuEvent*>(event)->globalPos());return true;
    }
    if(object==this&&event->type()==QEvent::WindowActivate){
        auto*previous=controllerInputOwner();controllerWindows.removeAll(this);controllerWindows.prepend(this);
        if(previous&&previous!=this)previous->clearInput();
    }
    if (event->type() == QEvent::ApplicationDeactivate ||
        (object == this && event->type() == QEvent::WindowDeactivate)) clearInput();
    const bool gameTarget=object==screen_||screen_->isAncestorOf(qobject_cast<QWidget*>(object));
    if(gameTarget&&event->type()==QEvent::MouseButtonPress){activateWindow();screen_->setFocus(Qt::MouseFocusReason);}
    if(object==this&&event->type()==QEvent::WindowActivate&&!gbsMode_)QTimer::singleShot(0,this,[this]{if(QApplication::activeWindow()==this&&!QApplication::activePopupWidget())screen_->setFocus(Qt::ActiveWindowFocusReason);});
    const bool keyEvent=event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease;
    const bool bareAlt=keyEvent&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Alt&&!(static_cast<QKeyEvent*>(event)->modifiers()&(Qt::ControlModifier|Qt::MetaModifier));
    if(QApplication::activeWindow()==this&&!QApplication::activePopupWidget()&&emulator_&&!gbsMode_&&!pause_->isChecked()){
        if(settings_.allowMouse&&emulator_->hasMotion()&&(object==screen_||screen_->isAncestorOf(qobject_cast<QWidget*>(object)))){
            if(event->type()==QEvent::MouseMove){
                auto*mouse=static_cast<QMouseEvent*>(event);
                const auto point=screen_->mapFromGlobal(mouse->globalPosition());
                mouseMotion_.move(point,screen_->gameRect(),screen_->hasBorder(),settings_.motionSensitivity,mouse->buttons()!=Qt::NoButton);
            }
            if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease){
                auto*mouse=static_cast<QMouseEvent*>(event);
                if(mouse->button()==Qt::LeftButton){mouseMotion_.click(true,event->type()==QEvent::MouseButtonPress);updateInput();return true;}
            }
            // Leaving the view retains the last tilt, just like Cocoa.

        }
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
            auto*key=static_cast<QKeyEvent*>(event);
            if(event->type()==QEvent::KeyRelease||bareAlt||!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){
                bool matched=false;
                for(int p=0;p<4;++p)for(int i=0;i<KeyboardActionCount;++i)if(settings_.playerKeys(p)[i]&&settings_.playerKeys(p)[i]==key->key()){
                    matched=true;if(!key->isAutoRepeat()){auto&mask=i<8?keyboardKeys_[p]:keyboardActions_[p];int bit=i<8?i:i-8;
                        if(event->type()==QEvent::KeyPress)mask|=1u<<bit;else mask&=~(1u<<bit);}
                }
                if(matched){updateInput();return true;}
            }
        }
    }
    if(bareAlt&&QApplication::activeWindow()==this&&!QApplication::activePopupWidget())return true;
    return QMainWindow::eventFilter(object, event);
}
