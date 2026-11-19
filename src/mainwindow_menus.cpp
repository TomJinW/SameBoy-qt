#include "boundedfile.h"
#include "mainwindow.h"
#include "core_version.h"
#include "emulator.h"
#include "videorecorder.h"
#include "debuggerbridge.h"
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include "screenwidget.h"
#include "cheatsdialog.h"
#include "audiobuffer.h"
#include "storagepaths.h"
#include "cartridgeinstance.h"
#include "stateio.h"
#include "debugviews.h"
#include "debugconsole.h"
#include "aidebugserver.h"
#include "windowplacement.h"
#include <QStackedWidget>
#include <QActionGroup>
#include <QApplication>
#include "audiooutput.h"
#include <QClipboard>
#include <QDataStream>
#include <QUuid>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>
#include <QTimer>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QVBoxLayout>

namespace {
QSettings history() { return QSettings(QSettings::IniFormat, QSettings::UserScope, "SameBoyQt", "SameBoyQt"); }
QString writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QObject::tr("Could not write %1: %2").arg(path, file.errorString());
    return {};
}
}

void MainWindow::buildMenus()
{
    auto add = [this](QMenu *menu, const QString &title, const QString &shortcut,
                      std::function<void()> callback, bool game = false) {
        auto *action = menu->addAction(title);
        if (!shortcut.isEmpty()) action->setShortcut(QKeySequence(shortcut));
        connect(action, &QAction::triggered, this, std::move(callback));
        if (game) gameActions_.append(action);
        return action;
    };
    auto unavailable = [](QMenu *menu, const QString &title, const QString &shortcut, const QString &reason) {
        auto *action = menu->addAction(title);
        if (!shortcut.isEmpty()) action->setShortcut(QKeySequence(shortcut));
        action->setEnabled(false); action->setToolTip(reason); action->setStatusTip(reason);
        menu->setToolTipsVisible(true); return action;
    };
    auto *file = menuBar()->addMenu(tr("&File"));
    add(file,tr("New Window"),"Ctrl+Shift+N",[this]{auto*window=new MainWindow;window->setAttribute(Qt::WA_DeleteOnClose);window->show();placeNewWindow(window,this);});
    openRomAction_=add(file, tr("Open…"), "Ctrl+O", [this] { chooseRom(); });
    add(file, tr("Hot Swap Cartridge…"), "Ctrl+Alt+O", [this] { hotSwap(); }, true);
    recent_ = file->addMenu(tr("Open Recent")); refreshRecent();
    file->addSeparator();
    add(file, tr("New Cartridge Instance…"), "Ctrl+N", [this] {
        const bool paused = pause_->isChecked(); pause_->setChecked(true);
        auto path = QFileDialog::getSaveFileName(this,tr("New Cartridge Instance"),QFileInfo(romSource_).completeBaseName()+".gbcart",tr("Cartridge instance (*.gbcart)"));
        if (!path.isEmpty()) {
            if (!path.endsWith(".gbcart",Qt::CaseInsensitive)) path += ".gbcart";
            if (QFileInfo::exists(path) || !QDir().mkpath(path)) QMessageBox::warning(this,tr("Cartridge Instance"),tr("Choose a new, unused cartridge instance path."));
            else {
                const auto error = writeFile(QDir(path).filePath("rom.gbl"),cartridgeInstanceDescriptor(path,romSource_));
                if (!error.isEmpty()) QMessageBox::warning(this,tr("Cartridge Instance"),error);
                else if (openRom(path)) return;
            }
        }
        pause_->setChecked(paused);
    }, true);
    file->addSeparator();
#ifdef Q_OS_MACOS
    add(file,tr("Close"),QKeySequence(QKeySequence::Close).toString(),[this]{close();});
    add(file,tr("Close ROM"),{},[this]{closeCartridge();},true);
#else
    add(file, tr("Close"), "Ctrl+W", [this] { closeCartridge(); }, true);
#endif
    saveRomAction_=add(file, tr("Save ROM Modifications"), "Ctrl+Alt+Shift+S", [this] { saveRom(false); }, true);
    saveRomAsAction_=add(file, tr("Save ROM Modifications As…"), "Ctrl+Alt+Shift+E", [this] { saveRom(true); }, true);
    auto *reload = add(file, tr("Reload ROM"), "Ctrl+Shift+R", [this] { if (!romPath_.isEmpty()) openRom(romPath_); }, true);
    file->addSeparator();
#ifdef Q_OS_MACOS
    preferencesAction_=add(file,tr("Settings…"),{},[this]{showPreferences();});
    preferencesAction_->setMenuRole(QAction::PreferencesRole);
    preferencesAction_->setShortcut(QKeySequence::Preferences);
#else
    preferencesAction_=add(file,tr("Preferences…"),"Ctrl+,",[this]{showPreferences();});
#endif
    preferencesAction_->setObjectName("preferencesAction");
    add(file,tr("Camera Settings…"),{},[this]{showPreferences(4);});
    file->addSeparator();
    add(file,tr("Exit"),"Ctrl+Q",[]{QApplication::closeAllWindows();});

    auto *emulation = menuBar()->addMenu(tr("&Emulation"));
    resetAction_=add(emulation, tr("Reset"), "Ctrl+R", [this] { resetGame(false); }, true);
    add(emulation, tr("Quick Reset"), "Ctrl+Alt+R", [this] { resetGame(true); }, true);
    emulation->addAction(reload);
    pause_ = add(emulation, tr("Pause"), "Ctrl+P", [] {}, true);
    pause_->setCheckable(true);
    connect(pause_, &QAction::toggled, this, [this](bool paused) {
        clearInput(); if (emulator_) emulator_->setPaused(paused);
        if(linkPartner_&&linkPartner_->pause_->isChecked()!=paused)linkPartner_->pause_->setChecked(paused);
        if(audioBuffer_)audioBuffer_->clear();
        if (audio_) { if (paused) audio_->suspend(); else audio_->resume(); }
    });
    auto *models = modelMenu_=emulation->addMenu(tr("Emulated Model"));
    auto *modelGroup = new QActionGroup(this);
    const QStringList names{tr("Pick Automatically"),"Game Boy","Game Boy Pocket/Light","Super Game Boy","Game Boy Color","Game Boy Advance"};
    const int hardware[] = {0,1,1,3,2,4};
    for (int i = 0; i < names.size(); ++i) {
        auto *action = add(models, names[i], {}, [this, i, value = hardware[i]] { changeModel(value, i == 2); });
        action->setCheckable(true); action->setData(i); modelGroup->addAction(action); modelActions_.append(action);
        if (i == 0) models->addSeparator();
    }
    emulation->addSeparator();
    auto *save = saveStateMenu_=emulation->addMenu(tr("Save State")); auto *load = loadStateMenu_=emulation->addMenu(tr("Load State"));
    gameActions_.append(save->menuAction());gameActions_.append(load->menuAction());
    for (int slot = 1; slot <= 10; ++slot) {
        const auto digit = QString::number(slot % 10);
        add(save, tr("Slot %1").arg(slot), "Ctrl+" + digit, [this, slot] { saveState(slot, false); }, true);
        add(load, tr("Slot %1").arg(slot), "Ctrl+Shift+" + digit, [this, slot] { saveState(slot, true); }, true);
    }
    emulation->addSeparator();
    screenshotAction_=add(emulation, tr("Save Screenshot"), "Ctrl+S", [this] { saveScreenshot(false); }, true);
    add(emulation, tr("Save Screenshot As…"), "Ctrl+Alt+S", [this] { saveScreenshot(true); }, true);
    add(emulation, tr("Copy Screenshot"), "Ctrl+Shift+S", [this] { QApplication::clipboard()->setImage(screen_->screenshot()); screen_->showMessage(tr("Screenshot copied")); }, true);
    emulation->addSeparator();
    record_ = add(emulation, tr("Start Audio Recording…"), "Ctrl+Shift+A", [this] { toggleRecording(); }, true);
    videoRecordAction_=add(emulation,tr("Start Video Recording…"),"Ctrl+Shift+V",[this]{toggleVideoRecording();},true);
    mute_ = add(emulation, tr("Mute Sound"), "Ctrl+M", [] {});
    mute_->setCheckable(true);
    connect(mute_, &QAction::toggled, this, [this](bool muted) { if (audio_) audio_->setVolume(muted ? 0 : settings_.volume / 100.0); });

    auto *cheats = cheatsMenu_=menuBar()->addMenu(tr("&Cheats"));
    cheatsEnabled_ = add(cheats, tr("Enable Cheats"), "Ctrl+Shift+C", [] {}, true);
    cheatsEnabled_->setCheckable(true); cheatsEnabled_->setChecked(true);
    connect(cheatsEnabled_, &QAction::toggled, this, [this](bool enabled) { if (emulator_) coreCommand([enabled](GB_gameboy_t *gb) { GB_set_cheats_enabled(gb, enabled); return QString(); }); });
    add(cheats, tr("Show Cheats"), {}, [this] { showCheats(); }, true);
    add(cheats, tr("Search Cheats"), {}, [this] { showCheatSearch(); }, true);
    auto *connectMenu = connectMenu_=menuBar()->addMenu(tr("&Connect"));
    auto *accessories=new QActionGroup(this);
    auto *none = add(connectMenu, tr("None"), {}, [this]{disconnectLink();if(emulator_)emulator_->setPrinterConnected(false);});noneAccessory_=none; none->setCheckable(true); none->setChecked(true);accessories->addAction(none);
    linkMenu_=connectMenu->addMenu(tr("Game Link Cable && Infrared"));
    connect(linkMenu_,&QMenu::aboutToShow,this,[this]{
        linkMenu_->clear();
        for(auto*widget:QApplication::topLevelWidgets())if(auto*other=dynamic_cast<MainWindow*>(widget)){
            if(other==this||!other->emulator_||other->gbsMode_)continue;
            auto*action=linkMenu_->addAction(other->windowTitle().replace("&","&&"));action->setCheckable(true);action->setChecked(linkPartner_==other);
            action->setEnabled(bool(emulator_)&&!gbsMode_);
            QPointer<MainWindow> peer=other;connect(action,&QAction::triggered,this,[this,peer]{if(peer)connectTo(peer);});
        }
        if(linkMenu_->isEmpty()){auto*empty=linkMenu_->addAction(tr("Open another ROM in a New Window to connect"));empty->setEnabled(false);}
    });
    printerAction_=add(connectMenu,tr("Game Boy Printer"),{},[this]{disconnectLink();printerAction_->setChecked(true);if(emulator_)emulator_->setPrinterConnected(true);});
    printerAction_->setCheckable(true);accessories->addAction(printerAction_);

    auto *window = menuBar()->addMenu(tr("&Window"));
    showStatusBar_=add(window,tr("Show Status Bar"),{},[]{});showStatusBar_->setCheckable(true);showStatusBar_->setChecked(true);
    connect(showStatusBar_,&QAction::toggled,this,[this](bool visible){setChromeVisible(statusBar(),visible);history().setValue("view/statusBar",visible);});
    showMenuBar_=new QAction(tr("Show Menu Bar"),this);showMenuBar_->setCheckable(true);showMenuBar_->setChecked(true);
    connect(showMenuBar_,&QAction::toggled,this,[this](bool visible){setChromeVisible(menuBar(),visible);history().setValue("view/menuBar",visible);});
    window->addSeparator();
    add(window,tr("Show Printer"),{},[this]{showPrinter();});
    add(window, tr("Minimize"), "Ctrl+Alt+M", [this] { showMinimized(); });
    auto resizeBy = [this](int step) {
        if (isFullScreen() || isMaximized()) showNormal();
        const auto image = screen_->screenshot();
        const QSize base = image.isNull() ? QSize(160,144) : (settings_.filterScreenshots ? image.size() / 4 : image.size());
        const int scale = qBound(1, qMin(screen_->width() / base.width(), screen_->height() / base.height()) + step, 12);
        resize(base.width() * scale, base.height() * scale + height() - screen_->height());
    };
    auto *increase = add(window, tr("Increase Window Size"), "Ctrl++", [resizeBy] { resizeBy(1); });
    increase->setShortcuts({QKeySequence("Ctrl++"),QKeySequence("Ctrl+=")});
    add(window, tr("Decrease Window Size"), "Ctrl+-", [resizeBy] { resizeBy(-1); });
    add(window, tr("Zoom"), {}, [this] { isMaximized() ? showNormal() : showMaximized(); });
    auto *full = fullScreenAction_=add(window, tr("Enter Full Screen"), "Ctrl+Alt+F", [this] { isFullScreen()?showNormal():showFullScreen();fullScreenAction_->setText(isFullScreen()?tr("Exit Full Screen"):tr("Enter Full Screen"));fullScreenAction_->setChecked(isFullScreen()); });full->setCheckable(true);
    connect(window, &QMenu::aboutToShow, full, [this,full] { full->setText(isFullScreen() ? tr("Exit Full Screen") : tr("Enter Full Screen")); });
    window->addSeparator();
    add(window, tr("Bring All to Front"), {}, [] {
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = dynamic_cast<MainWindow *>(widget)) { if (window->isMinimized()) window->showNormal(); window->raise(); }
    });
    auto*debug=debugMenu_=menuBar()->addMenu(tr("&Debug"));
    developerModeAction_=debug->addAction(tr("Developer Mode"));developerModeAction_->setCheckable(true);developerModeAction_->setChecked(settings_.developerMode);connect(developerModeAction_,&QAction::toggled,this,[this](bool enabled){setDeveloperMode(enabled);});
    debug->addSeparator();cpuFrequencyMenu_=debug->addMenu(tr("Force CPU Frequency"));cpuFrequencyMenu_->setObjectName("cpuFrequencyMenu");
    currentCpuFrequency_=cpuFrequencyMenu_->addAction(tr("Current CPU frequency: —"));currentCpuFrequency_->setObjectName("currentCpuFrequency");currentCpuFrequency_->setEnabled(false);cpuFrequencyMenu_->addSeparator();
    {auto*group=new QActionGroup(this);group->setExclusive(true);const QStringList labels{tr("Automatic"),tr("4 MHz"),tr("8 MHz")};for(int i=0;i<labels.size();++i){auto*choice=cpuFrequencyMenu_->addAction(labels[i]);choice->setCheckable(true);choice->setData(i);choice->setChecked(settings_.cpuFrequency==i);group->addAction(choice);cpuFrequencyActions_[i]=choice;connect(choice,&QAction::triggered,this,[this,i]{auto next=settings_;next.cpuFrequency=i;applySettings(next);});}}
    add(debug,tr("Show Console"),{},[this]{showConsole();});clearConsoleAction_=add(debug,tr("Clear Console"),"Ctrl+K",[this]{clearConsole();});
#ifdef Q_OS_MACOS
    // Cocoa uses the Control key (Qt's Meta) so Cmd+C stays Copy.
    const char*breakShortcut="Meta+C";
#else
    const char*breakShortcut="Ctrl+C";
#endif
    breakDebuggerAction_=add(debug,tr("Break Debugger"),breakShortcut,[this]{showConsole();submitDebugCommand("interrupt");},true);debug->addSeparator();auto*channels=debug->addMenu(tr("Audio Channels"));channels->addAction(mute_);channels->addSeparator();
    const QStringList channelNames{tr("Channel 1 — Pulse 1"),tr("Channel 2 — Pulse 2"),tr("Channel 3 — Wave"),tr("Channel 4 — Noise")};
    // MainMenu.xib binds the channel toggles to Option+1..4; Qt maps Alt to Option on macOS.
    for(int i=0;i<4;++i){auto*action=add(channels,channelNames[i],QString("Alt+%1").arg(i+1),[this]{applyDebugOptions();if(audioBuffer_)audioBuffer_->clear();},true);action->setCheckable(true);action->setChecked(true);debugChannels_[i]=action;}
    debug->addSeparator();debugBackground_=add(debug,tr("Show Background and Window"),{},[this]{applyDebugOptions();},true);debugBackground_->setCheckable(true);debugBackground_->setChecked(true);
    debugObjects_=add(debug,tr("Show Objects"),{},[this]{applyDebugOptions();},true);debugObjects_->setCheckable(true);debugObjects_->setChecked(true);debug->addSeparator();
    add(debug,tr("Show Memory"),{},[this]{showMemory();},true);add(debug,tr("Show VRAM Viewer"),{},[this]{showVram();},true);
    auto *help = menuBar()->addMenu(tr("&Help"));
    add(help,tr("About SameBoy Qt"),{},[this]{
        const auto description=tr("SameBoy Qt 0.1.0\nQt Widgets frontend for SameBoy.\nSameBoy © 2015–2026 Lior Halphon (Expat license).").toHtmlEscaped().replace('\n',"<br>");const auto commit=QStringLiteral(SAMEBOY_CORE_COMMIT);
        const auto text=description+"<br><br>"+tr("SameBoy core commit:").toHtmlEscaped()+"<br><tt>"+commit+"</tt><br><a href=\"https://github.com/LIJI32/SameBoy\">"+tr("Upstream GitHub repository").toHtmlEscaped()+"</a><br><a href=\"https://github.com/LIJI32/SameBoy/commit/"+commit+"\">"+tr("View core commit on GitHub").toHtmlEscaped()+"</a>";
        QMessageBox about(QMessageBox::Information,tr("About SameBoy Qt"),text,QMessageBox::Ok,this);about.setTextFormat(Qt::RichText);about.setTextInteractionFlags(Qt::TextBrowserInteraction);about.exec();
    });
    add(help,tr("Sponsor Original SameBoy"),{},[]{QDesktopServices::openUrl(QUrl("https://github.com/sponsors/LIJI32"));});
    help->addSeparator();
    add(help, tr("Debugger Help"), {}, [] { QDesktopServices::openUrl(QUrl("https://sameboy.github.io/debugger/")); });
    add(help,tr("Audio and Performance…"),{},[this]{
        const auto stats=audioBuffer_?audioBuffer_->statistics():AudioBuffer::Statistics{};
        QMessageBox::information(this,tr("Audio and Performance"),tr("Output: %1 Hz, 16-bit stereo\nEmulation speed: %2×\nAudio underflows: %3\nDiscarded stereo frames: %4\n\nA lower sample rate and dropped audio are different issues. Underflows during startup or pause are normal; sustained underflows during play can indicate scheduling or CPU pressure.")
            .arg(audioSampleRate_).arg(emulator_?emulator_->emulationSpeed():0,0,'f',2).arg(stats.underruns).arg(stats.droppedFrames));
    },true);
    add(help, tr("Controls"), {}, [this] {
        const bool paused = pause_->isChecked(); pause_->setChecked(true);
        QString text;
        const QStringList labels{"Right","Left","Up","Down","A","B","Select","Start","Rapid A","Rapid B","Turbo","Rewind","Slow motion"};
        for (int i = 0; i < labels.size(); ++i) text += tr(labels[i].toUtf8().constData()) + ": " + (settings_.keys[i] ? QKeySequence(settings_.keys[i]).toString(QKeySequence::NativeText) : tr("Unassigned")) + '\n';
        QMessageBox::information(this, tr("Controls"), text + tr("Pause: %1\n%2: %3\nSave Screenshot: %4").arg(pause_->shortcut().toString(QKeySequence::NativeText),QString(preferencesAction_->text()).remove('&').remove(QChar(0x2026)),preferencesAction_->shortcut().toString(QKeySequence::NativeText),screenshotAction_->shortcut().toString(QKeySequence::NativeText)));
        pause_->setChecked(paused);
    });
    refreshMenus();
    for(auto*menu:findChildren<QMenu*>()){connect(menu,&QMenu::aboutToShow,this,[this]{clearInput();});connect(menu,&QMenu::aboutToHide,this,[this]{QTimer::singleShot(0,this,[this]{if(!QApplication::activePopupWidget()&&QApplication::activeWindow()==this)screen_->setFocus(Qt::MenuBarFocusReason);});});}
}

void MainWindow::refreshMenus()
{
    const bool running = bool(emulator_);
    for (auto *action : gameActions_) action->setEnabled(running);
    if(breakDebuggerAction_)breakDebuggerAction_->setEnabled(running&&settings_.developerMode&&!emulator_->debuggerStopped());
    if(videoRecordAction_)videoRecordAction_->setEnabled(bool(emulator_)&&!gbsMode_);
    for(auto*action:{saveRomAction_,saveRomAsAction_})if(action)action->setEnabled(running&&!gbsMode_&&romModified_);
    for(int i=0;i<int(cpuFrequencyActions_.size());++i)if(cpuFrequencyActions_[i])cpuFrequencyActions_[i]->setChecked(settings_.cpuFrequency==i);
    for (auto *action : modelActions_) {
        const int index = action->data().toInt();
        const int current = settings_.hardware == 0 ? 0 : settings_.hardware == 5 ? 2 : settings_.hardware == 1 ? 1 : settings_.hardware == 2 ? 4 : settings_.hardware == 3 ? 3 : 5;
        action->setChecked(index == current);
    }
}
void MainWindow::refreshRecent()
{
    if (!recent_) return;
    recent_->clear();
    const auto paths = history().value("recentROMs").toStringList();
    for (const auto &path : paths) {
        auto *action = recent_->addAction(QDir::toNativeSeparators(path));
        action->setEnabled(QFileInfo::exists(path));
        connect(action, &QAction::triggered, this, [this,path] { openRequestedRom(path,this); });
    }
    if (!paths.isEmpty()) recent_->addSeparator();
    auto *clear = recent_->addAction(tr("Clear Menu")); clear->setEnabled(!paths.isEmpty());
    connect(clear, &QAction::triggered, this, [this] { history().remove("recentROMs"); refreshRecent(); });
}
void MainWindow::coreCommand(const std::function<QString(GB_gameboy_t *)> &command)
{
    if (!emulator_) return;
    const auto error = emulator_->execute(command);
    if (!error.isEmpty()) QMessageBox::warning(this, tr("Emulation"), error);
}
void MainWindow::closeCartridge()
{
    if(!prepareToClose(true))return;
    stop(); romPath_.clear(); romSource_.clear(); storageBase_.clear();refreshToolWindows(); pause_->setChecked(false);
    gbsMode_=false;views_->setCurrentWidget(screen_);screen_->setFrame({}); setWindowTitle("SameBoy Qt[*]"); statusBar()->showMessage(tr("Ready")); refreshMenus();
}
void MainWindow::resetGame(bool quick)
{
    if(gbsMode_){if(emulator_)emulator_->selectGbsTrack(gbsTrack_);applyDebugOptions();return;}
    clearInput(); if(audioBuffer_)audioBuffer_->clear();
    coreCommand([quick](GB_gameboy_t *gb) { if (quick) GB_quick_reset(gb); else GB_reset(gb); return QString(); });
    applyDebugOptions();
}
void MainWindow::changeModel(int hardware, bool pocket)
{
    const auto old = settings_;
    settings_.hardware = pocket ? 5 : hardware;
    settings_.dmgModel = GB_MODEL_DMG_B;
    if (emulator_ && !openRom(romPath_)) settings_ = old;
    applySettings(settings_); refreshMenus();
}
void MainWindow::saveState(int slot, bool load)
{
    const auto path = stateSavePath(romPath_,slot);
    const auto legacyPath = storageBase_ + QString(".s%1").arg(slot);
    if(load){loadStateFile(QFile::exists(path)?path:legacyPath);return;}
    const bool paused = pause_->isChecked(); pause_->setChecked(true);
    bool saved=false;
    coreCommand([path,&saved](GB_gameboy_t *gb) {
        QByteArray bytes(GB_get_save_state_size(gb), '\0');
        GB_save_state_to_buffer(gb, reinterpret_cast<uint8_t *>(bytes.data()));
        const auto error=writeFile(path, bytes);saved=error.isEmpty();return error;
    });
    if(saved)screen_->showMessage(tr("State saved"));
    if(audioBuffer_)audioBuffer_->clear(); pause_->setChecked(paused);
}
void MainWindow::loadStateFile(const QString&source)
{
    const bool paused = pause_->isChecked(); pause_->setChecked(true);
    bool loaded=false;
    coreCommand([source,&loaded](GB_gameboy_t *gb) {
        QFile file(source);QByteArray bytes;const auto readError=readStateFile(file,bytes);if(!readError.isEmpty())return readError;
        GB_model_t model;
        if (GB_get_state_model_from_buffer(reinterpret_cast<const uint8_t *>(bytes.constData()),bytes.size(),&model) || model != GB_get_model(gb))
            return QObject::tr("This state is invalid or uses a different hardware revision. Select the matching model before loading it.");
        if (loadStatePreservingOnFailure(gb,bytes)) return QObject::tr("Incompatible or invalid save state.");
        loaded=true;return QString();
    });
    if(loaded)screen_->showMessage(tr("State loaded"));
    applyDebugOptions();
    if(audioBuffer_)audioBuffer_->clear(); pause_->setChecked(paused);
}
void MainWindow::saveScreenshot(bool choosePath)
{
    const auto image = screen_->screenshot(); if (image.isNull()) return;
    const bool paused = pause_->isChecked(); pause_->setChecked(true);
    QString path;
    if (choosePath) path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), screenshotPath_.isEmpty() ? "screenshot.png" : screenshotPath_, tr("PNG image (*.png)"));
    else {
        const auto folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/SameBoy";
        QDir().mkpath(folder);
        path = folder + '/' + QFileInfo(romPath_).completeBaseName() + '-' + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".png";
    }
    if (!path.isEmpty()) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file,"PNG") || !file.commit()) QMessageBox::warning(this, tr("Screenshot"), tr("Could not save %1").arg(path));
        else { screenshotPath_ = path; statusBar()->showMessage(tr("Saved %1").arg(path), 6000); screen_->showMessage(tr("Screenshot saved")); }
    }
    pause_->setChecked(paused);
}
bool MainWindow::saveRom(bool choosePath)
{
    if(!emulator_||gbsMode_||closing_||!romModified_)return false;
    const bool paused = pause_->isChecked(); pause_->setChecked(true);
    const auto path = choosePath ? QFileDialog::getSaveFileName(this,tr("Save ROM Modifications As"),romSource_,tr("Game Boy ROM (*.gb *.gbc)")) : romSource_;
    if(path.isEmpty()){pause_->setChecked(paused);return false;}
    QByteArray bytes;auto error=emulator_->execute([&bytes](GB_gameboy_t*gb){size_t size=0;auto*data=static_cast<const char*>(GB_get_direct_access(gb,GB_DIRECT_ACCESS_ROM,&size,nullptr));if(!data||!size)return QObject::tr("The cartridge ROM is not available.");bytes=QByteArray(data,qsizetype(size));return QString();});
    if(error.isEmpty())error=writeFile(path,bytes);
    if(!error.isEmpty())QMessageBox::warning(this,tr("Save ROM Modifications"),error);
    else if(QFileInfo(path).absoluteFilePath()==QFileInfo(romSource_).absoluteFilePath())setRomModified(false);
    pause_->setChecked(paused);
    return error.isEmpty();
}
void MainWindow::hotSwap()
{
    if (!emulator_||closing_) return;
    const bool paused = pause_->isChecked(); pause_->setChecked(true);
    const auto path = QFileDialog::getOpenFileName(this,tr("Hot Swap Cartridge"),{},tr("Game Boy ROM (*.gb *.gbc)"));
    if(!path.isEmpty()&&focusStorageOwner(path)){pause_->setChecked(paused);return;}
    if(gbsMode_){if(!path.isEmpty())openRom(path);pause_->setChecked(paused);return;}
    if (!path.isEmpty()) {
        QFile file(path);
        QByteArray bytes;if(!readBoundedFile(file,bytes,8*1024*1024,336)) QMessageBox::warning(this,tr("Hot Swap"),tr("Cannot read a valid cartridge ROM."));
        else {
            if(!prepareToClose()){pause_->setChecked(paused);return;}
            QScopedValueRollback closing(closing_,true);emulator_->setPaused(true);
            const auto error = emulator_->swapCartridge(bytes,batterySavePath(path),QFileInfo(path).absoluteFilePath()+".sav");
            if (!error.isEmpty()) {QMessageBox::warning(this,tr("Hot Swap"),error);closing_=false;pause_->setChecked(paused);emulator_->setPaused(paused);}
            else {
                stopVideoRecording();stopRecording();
                romPath_ = romSource_ = storageBase_ = QFileInfo(path).absoluteFilePath();
                ++generation_;
    {QFile registers(":/debug/registers.sym"),symbols(QFileInfo(romSource_).path()+"/"+QFileInfo(romSource_).completeBaseName()+".sym");const auto common=registers.open(QIODevice::ReadOnly)?registers.readAll():QByteArray();QByteArray local;readBoundedFile(symbols,local,4*1024*1024);emulator_->loadDebugSymbols(romSource_,common,local);}
                coreCommand([base=storageBase_,enabled=cheatsEnabled_->isChecked()](GB_gameboy_t *gb){const auto error=restoreCheats(gb,base);GB_set_cheats_enabled(gb,enabled);return error;});
                setWindowTitle(QFileInfo(path).fileName()+" — SameBoy Qt[*]");setRomModified(false);discardRomOnClose_=false;refreshToolWindows();
                auto paths = history().value("recentROMs").toStringList(); paths.removeAll(romPath_); paths.prepend(romPath_);
                while (paths.size() > 10) paths.removeLast(); history().setValue("recentROMs",paths); refreshRecent();
            }
        }
    }
    pause_->setChecked(paused);
}
void MainWindow::showCheats()
{
    if (!emulator_) return;
    const bool created=!cheatsDialog_;
    if (!cheatsDialog_) cheatsDialog_ = new CheatsDialog([this](auto command) {
        return emulator_ ? emulator_->execute(std::move(command)) : tr("No cartridge is running.");
    },storageBase_,this);
    else cheatsDialog_->reload();
    cheatsDialog_->show(); if(created)placeNewWindow(cheatsDialog_,this);cheatsDialog_->raise(); cheatsDialog_->activateWindow();
}
void MainWindow::showCheatSearch()
{
    if (!emulator_) return;
    const bool created=!cheatSearchDialog_;
    if (!cheatSearchDialog_) cheatSearchDialog_ = new CheatSearchDialog([this](auto command) {
        return emulator_ ? emulator_->execute(std::move(command)) : tr("No cartridge is running.");
    },storageBase_,[this] { showCheats(); },this);
    cheatSearchDialog_->show(); if(created)placeNewWindow(cheatSearchDialog_,this);cheatSearchDialog_->raise(); cheatSearchDialog_->activateWindow();
}
void MainWindow::toggleVideoRecording(){
 if(videoRecorder_){stopVideoRecording();return;}if(!emulator_||gbsMode_)return;
 const auto unavailable=VideoRecorder::availabilityError();if(!unavailable.isEmpty()){QMessageBox::warning(this,tr("Video Recording"),unavailable);return;}
 const bool wasPaused=pause_->isChecked();pause_->setChecked(true);
 const auto path=QFileDialog::getSaveFileName(this,tr("Start Video Recording"),QFileInfo(romPath_).completeBaseName()+".mp4",tr("MP4 video (*.mp4)"));
 if(path.isEmpty()){pause_->setChecked(wasPaused);return;}
 QDialog options(this);options.setObjectName("videoRecordingOptions");options.setWindowTitle(tr("Video Recording"));auto*form=new QFormLayout(&options);auto*scale=new QComboBox(&options);for(int factor:{1,2,4,6,8})scale->addItem(QString::number(factor)+QString::fromUtf8("×"),factor);scale->setCurrentIndex(2);form->addRow(tr("Output scale:"),scale);auto*filters=new QCheckBox(tr("Keep current filters and frame blending"),&options);filters->setChecked(true);form->addRow(filters);auto*note=new QLabel(tr("H.264 video with game audio, at the original emulated frame rate."),&options);note->setWordWrap(true);form->addRow(note);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&options);form->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&options,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&options,&QDialog::reject);
 if(options.exec()==QDialog::Accepted){
  QSize size;double fps=60;coreCommand([&](GB_gameboy_t*g){size={int(GB_get_screen_width(g)),int(GB_get_screen_height(g))};fps=GB_get_usual_frame_rate(g)*GB_get_unmultiplied_clock_rate(g)/GB_get_clock_rate(g);return QString();});
  auto recorder=std::make_unique<VideoRecorder>();const auto error=recorder->start(QFileInfo(path).suffix().isEmpty()?path+".mp4":path,size,fps,audioSampleRate_,scale->currentData().toInt(),filters->isChecked());
  if(!error.isEmpty())QMessageBox::warning(this,tr("Video Recording"),error);else{videoRecorder_=std::move(recorder);emulator_->setVideoCapture(true);videoRecordAction_->setText(tr("Stop Video Recording"));}
 }
 pause_->setChecked(wasPaused);
}
void MainWindow::pollVideoRecording(){
 if(!videoRecorder_||videoFinishing_||!emulator_)return;auto error=emulator_->takeVideoError();if(error.isEmpty())error=videoRecorder_->error();
 if(!error.isEmpty()){stopVideoRecording();statusBar()->showMessage(error,15000);return;}
 for(int i=0;i<4&&videoRecorder_->canAccept();++i){auto packets=emulator_->takeVideoFrames(1);if(packets.isEmpty())break;if(!videoRecorder_->append(std::move(packets.front()))){stopVideoRecording();break;}}
}
void MainWindow::stopVideoRecording(){
 if(!videoRecorder_||videoFinishing_)return;videoFinishing_=true;QVector<VideoFramePacket>remaining;if(emulator_){emulator_->setVideoCapture(false);remaining=emulator_->takeVideoFrames();}
 const auto error=videoRecorder_->finish(std::move(remaining));videoRecorder_.reset();videoFinishing_=false;videoRecordAction_->setText(tr("Start Video Recording…"));if(!error.isEmpty())statusBar()->showMessage(tr("Video recording stopped: %1").arg(error),15000);
}

void MainWindow::toggleRecording()
{
    if (recording_||!vgmTarget_.isEmpty()) { stopRecording(); return; }
    const bool paused = pause_->isChecked(); pause_->setChecked(true);
    // Cocoa offers WAV, AIFF, raw PCM and VGM (Document.m toggleAudioRecording:/audioFormatChanged:).
    const QStringList filters{tr("WAV audio (*.wav)"),tr("AIFF audio (*.aiff *.aif)"),tr("Raw PCM, 16-bit stereo little-endian (*.raw *.pcm)"),tr("VGM register log (*.vgm)")};
    QString filter;auto path = QFileDialog::getSaveFileName(this,tr("Start Audio Recording"),"recording.wav",filters.join(";;"),&filter);
    if (!path.isEmpty()) {
        const auto suffix=QFileInfo(path).suffix().toLower();
        int format=suffix=="aiff"||suffix=="aif"?1:suffix=="raw"||suffix=="pcm"?2:suffix=="vgm"?3:suffix=="wav"?0:qMax<int>(0,filters.indexOf(filter));
        if(format==3){
            // The core writes VGM through fopen(); record to an ASCII temporary path and move it into place on stop.
            const auto temporary=QDir(QDir::tempPath()).filePath(QString("sameboy-qt-%1.vgm").arg(QUuid::createUuid().toString(QUuid::Id128)));
            const auto local=QFile::encodeName(QDir::toNativeSeparators(temporary));int error=-1;
            if(emulator_)coreCommand([&](GB_gameboy_t*g){error=GB_start_audio_recording(g,local.constData(),GB_AUDIO_FORMAT_VGM);return QString();});
            if(error){QMessageBox::warning(this,tr("Audio Recording"),tr("Cannot create recording."));QFile::remove(temporary);}
            else{vgmTarget_=path;vgmTemporary_=temporary;record_->setText(tr("Stop Audio Recording"));screen_->showMessage(tr("Audio recording started"));}
        }else{
            auto file = std::make_unique<QSaveFile>(path);const int header=format==0?44:format==1?54:0;
            if (!file->open(QIODevice::WriteOnly) || file->write(QByteArray(header,'\0')) != header) QMessageBox::warning(this,tr("Audio Recording"),tr("Cannot create recording."));
            else { recording_ = std::move(file); recordingFormat_=format; if(emulator_)emulator_->setCaptureAudio(true); recordedBytes_ = 0; record_->setText(tr("Stop Audio Recording")); screen_->showMessage(tr("Audio recording started")); }
        }
    }
    pause_->setChecked(paused);
}
void MainWindow::stopRecording()
{
    if(!vgmTarget_.isEmpty()){
        int error=-1;if(emulator_)coreCommand([&](GB_gameboy_t*g){error=GB_stop_audio_recording(g);return QString();});
        QFile recorded(vgmTemporary_);QByteArray bytes;QSaveFile target(vgmTarget_);
        const bool saved=!error&&readBoundedFile(recorded,bytes,1024*1024*1024)&&target.open(QIODevice::WriteOnly)&&target.write(bytes)==bytes.size()&&target.commit();
        recorded.close();QFile::remove(vgmTemporary_);vgmTarget_.clear();vgmTemporary_.clear();record_->setText(tr("Start Audio Recording…"));
        if(saved)screen_->showMessage(tr("Audio recording ended"));else statusBar()->showMessage(tr("Could not finalize audio recording."),10000);
        return;
    }
    if (!recording_) return;
    recording_->seek(0);
    if(recordingFormat_==0){
        QDataStream stream(recording_.get()); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF",4); stream << quint32(recordedBytes_+36); stream.writeRawData("WAVEfmt ",8);
        stream << quint32(16) << quint16(1) << quint16(2) << quint32(audioSampleRate_) << quint32(audioSampleRate_*4) << quint16(4) << quint16(16);
        stream.writeRawData("data",4); stream << recordedBytes_;
    }else if(recordingFormat_==1){
        // AIFF: FORM/COMM/SSND, 16-bit stereo, sample rate as an 80-bit IEEE extended float.
        QDataStream stream(recording_.get()); stream.setByteOrder(QDataStream::BigEndian);
        stream.writeRawData("FORM",4); stream << quint32(46+recordedBytes_); stream.writeRawData("AIFFCOMM",8);
        stream << quint32(18) << quint16(2) << quint32(recordedBytes_/4) << quint16(16);
        int exponent=0;for(quint32 rate=audioSampleRate_;rate>1;rate>>=1)++exponent;
        stream << quint16(16383+exponent) << (quint64(audioSampleRate_)<<(63-exponent));
        stream.writeRawData("SSND",4); stream << quint32(8+recordedBytes_) << quint32(0) << quint32(0);
    }
    if (!recording_->commit()) statusBar()->showMessage(tr("Could not finalize audio recording."),10000);
    else screen_->showMessage(tr("Audio recording ended"));
    recording_.reset(); if(emulator_)emulator_->setCaptureAudio(gbsMode_); recordedBytes_ = 0; record_->setText(tr("Start Audio Recording…"));
}

void MainWindow::showGameContextMenu(const QPoint&position){
    clearInput();QMenu menu(this);menu.setObjectName("gameContextMenu");menu.addAction(openRomAction_);
#ifndef Q_OS_MACOS
    menu.addAction(showMenuBar_);
#endif
    menu.addSeparator();
    menu.addAction(pause_);menu.addAction(resetAction_);menu.addMenu(modelMenu_);menu.addMenu(saveStateMenu_);menu.addMenu(loadStateMenu_);menu.addSeparator();
    auto*video=menu.addMenu(tr("Video"));video->setObjectName("gameVideo");
        auto choices=[&](QMenu*target,int current,std::initializer_list<std::pair<QString,int>> items,std::function<void(AppSettings&,int)> change){auto*group=new QActionGroup(target);for(const auto&entry:items){auto*action=target->addAction(entry.first);action->setCheckable(true);action->setChecked(entry.second==current);action->setData(entry.second);group->addAction(action);connect(action,&QAction::triggered,&menu,[this,change,id=entry.second]{auto settings=settings_;change(settings,id);applySettings(settings);});}};
        auto*filters=video->addMenu(tr("Scaling Filter"));filters->setObjectName("gameFilters");
        choices(filters,settings_.scaling,{{tr("Nearest neighbor"),0},{tr("Pixelated"),3},{tr("Bilinear (Blurry)"),1},{tr("Smooth bilinear (Less blurry)"),4},{tr("Monochrome LCD display"),5},{tr("LCD display"),2},{tr("CRT display"),6},{tr("Flat CRT display"),7},{"Scale2x",8},{"Scale4x",9},{tr("Anti-aliased Scale2x"),10},{tr("Anti-aliased Scale4x"),11},{"HQ2x",12},{tr("OmniScale (Any factor)"),13},{tr("OmniScale Legacy"),14},{tr("Anti-aliased OmniScale Legacy"),15}},[](AppSettings&s,int value){s.scaling=value;});
        auto*correction=video->addMenu(tr("Color Correction"));correction->setObjectName("gameColorCorrection");
        choices(correction,settings_.colorCorrection,{{tr("Disabled"),0},{tr("Correct curves"),1},{tr("Modern — Balanced"),2},{tr("Modern — Boost contrast"),3},{tr("Reduce contrast"),4},{tr("Low contrast"),5},{tr("Modern — Accurate"),6}},[](AppSettings&s,int value){s.colorCorrection=value;});
        auto*palettes=video->addMenu(tr("Monochrome Palette"));palettes->setObjectName("gamePalettes");
        choices(palettes,settings_.palette,{{tr("Greyscale"),0},{tr("Lime (Game Boy)"),1},{tr("Olive (Pocket)"),2},{tr("Teal (Light)"),3}},[](AppSettings&s,int value){s.palette=value;});
        auto*paletteGroup=palettes->findChild<QActionGroup*>();palettes->addSeparator();for(int i=0;i<settings_.palettes.size();++i){auto*action=palettes->addAction(QString(settings_.palettes[i].name).replace('&',"&&"));action->setCheckable(true);action->setChecked(settings_.palette==i+4);action->setData(i+4);paletteGroup->addAction(action);connect(action,&QAction::triggered,&menu,[this,i]{auto settings=settings_;settings.palette=i+4;applySettings(settings);});}

    auto*blending=video->addMenu(tr("Frame Blending"));blending->setObjectName("gameBlending");
    choices(blending,settings_.blending,{{tr("Disabled"),0},{tr("Simple"),1},{tr("Accurate"),2}},[](AppSettings&s,int value){s.blending=value;});
    auto*border=video->addMenu(tr("Display Border"));border->setObjectName("gameBorder");
    choices(border,settings_.border,{{tr("Only for Super Game Boy"),GB_BORDER_SGB},{tr("Never"),GB_BORDER_NEVER},{tr("Always"),GB_BORDER_ALWAYS}},[](AppSettings&s,int value){s.border=value;});
    auto toggle=[&](QMenu*target,const QString&name,const QString&id,bool checked,std::function<void(AppSettings&,bool)>change){auto*action=target->addAction(name);action->setObjectName(id);action->setCheckable(true);action->setChecked(checked);connect(action,&QAction::triggered,&menu,[this,change](bool on){auto settings=settings_;change(settings,on);applySettings(settings);});};
    video->addSeparator();toggle(video,tr("Keep aspect ratio"),"gameAspect",settings_.aspect,[](AppSettings&s,bool value){s.aspect=value;});toggle(video,tr("Force integer scale"),"gameIntegerScale",settings_.integerScale,[](AppSettings&s,bool value){s.integerScale=value;});
    auto*audio=menu.addMenu(tr("Audio"));audio->setObjectName("gameAudio");audio->addAction(mute_);auto*volume=audio->addMenu(tr("Volume"));volume->setObjectName("gameVolume");
    choices(volume,settings_.volume,{{"10%",10},{"25%",25},{"50%",50},{"75%",75},{"100%",100}},[](AppSettings&s,int value){s.volume=value;});
    auto*window=menu.addMenu(tr("Window"));window->setObjectName("gameWindow");fullScreenAction_->setText(isFullScreen()?tr("Exit Full Screen"):tr("Enter Full Screen"));fullScreenAction_->setChecked(isFullScreen());window->addAction(fullScreenAction_);
#ifndef Q_OS_MACOS
    window->addAction(showMenuBar_);
#endif
    window->addAction(showStatusBar_);
    menu.addMenu(cheatsMenu_);menu.addMenu(connectMenu_);menu.addMenu(debugMenu_);
    menu.addSeparator();menu.addAction(screenshotAction_);menu.addAction(videoRecordAction_);menu.addAction(preferencesAction_);
    menu.exec(position);menuBar()->setActiveAction(nullptr);screen_->setFocus(Qt::MouseFocusReason);
}
