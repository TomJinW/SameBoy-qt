#include "../src/settings.h"
#include "../src/uistyle.h"
#include "../src/language.h"
#include "../src/firstlanguage.h"
#include "../src/settingsdialog.h"
#include "../src/screenwidget.h"
#include "../src/paletteeditor.h"
#include "../src/joycondialog.h"
#include "../src/controllers.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QDir>
#include <QPushButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QTimer>
#include <QComboBox>
#include <QTableWidget>
#include <QThread>
#include <SDL3/SDL.h>
#include <cstdio>

static int settingsFailure(int line){std::fprintf(stderr,"settings-smoke failure at line %d\n",line);return 1;}

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    // Exercise QWidget menu actions deterministically instead of native menu tracking.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuWindows);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
#endif
    QApplication app(argc, argv);
    // Run under QT_SCALE_FACTOR: integer scaling must give whole *physical* pixels per Game Boy pixel.
    if(argc>1&&QByteArray(argv[1])=="--integer-scale"){
        ScreenWidget view;view.setAttribute(Qt::WA_DontShowOnScreen);AppSettings scaled;scaled.integerScale=true;view.setSettings(scaled);
        QImage pixels(160,144,QImage::Format_RGB32);pixels.fill(Qt::black);view.resize(480,432);view.show();view.setFrame(pixels);app.processEvents();
        const qreal dpr=view.devicePixelRatioF();
        for(const QSize size:{QSize(480,432),QSize(500,450),QSize(640,576),QSize(721,649),QSize(1000,900),QSize(1281,1153)}){
            view.resize(size);app.processEvents();const auto rect=view.gameRect();
            const qreal w=rect.width()*dpr,h=rect.height()*dpr;const int scale=qRound(w/160);
            // Exact when the scale factor allows it; otherwise Qt's integer geometry leaves under one physical pixel of rounding.
            if(scale<1||std::abs(w-160*scale)>=1||std::abs(h-144*scale)>=1||!view.rect().contains(rect)){
                std::fprintf(stderr,"Integer scale at dpr %.2f, view %dx%d: game %dx%d is %.2fx%.2f physical pixels\n",dpr,size.width(),size.height(),rect.width(),rect.height(),w,h);return settingsFailure(__LINE__);}
        }
        if(argc>2){for(int y=0;y<144;++y)for(int x=0;x<160;++x)pixels.setPixel(x,y,(x+y)%2?0xffffffff:0xff000000);
            view.resize(480,432);view.setFrame(pixels);app.processEvents();if(!view.grab().save(QString::fromLocal8Bit(argv[2])))return settingsFailure(__LINE__);}
        std::printf("Integer scaling uses whole physical pixels at dpr %.2f.\n",dpr);return 0;
    }
    {const AppSettings fresh; // Emulation output defaults of Cocoa's registerDefaults (GBApp.m).
     if(fresh.blending!=2||fresh.colorCorrection!=GB_COLOR_CORRECTION_MODERN_BALANCED||fresh.border!=GB_BORDER_SGB||fresh.highpass!=GB_HIGHPASS_ACCURATE||
        fresh.rewindSeconds!=120||fresh.volume!=100||fresh.rumbleMode!=GB_RUMBLE_CARTRIDGE_ONLY||fresh.rtc!=GB_RTC_MODE_SYNC_TO_HOST){std::fputs("Defaults differ from Cocoa\n",stderr);return settingsFailure(__LINE__);}}
    {// Automatic hardware follows Cocoa Document.m bestModelForROM.
     auto header=[](uint8_t cgb,uint8_t sgb,uint8_t licensee,const char*newLicensee){QByteArray rom(0x8000,0);rom[0x143]=char(cgb);rom[0x146]=char(sgb);rom[0x14b]=char(licensee);rom[0x144]=newLicensee[0];rom[0x145]=newLicensee[1];return rom;};
     const AppSettings automatic;
     const struct{QByteArray rom;GB_model_t model;const char*boot;const char*what;}cases[]{
        {header(0x80,0,0,"00"),GB_MODEL_CGB_E,"cgb_boot.bin","CGB features"},{header(0,3,0,"00"),GB_MODEL_SGB2,"sgb2_boot.bin","SGB features"},
        {header(0,0,1,"00"),GB_MODEL_CGB_E,"cgb_boot.bin","Nintendo licensee"},{header(0,0,0x33,"01"),GB_MODEL_CGB_E,"cgb_boot.bin","new Nintendo licensee"},
        {header(0,0,0x33,"08"),GB_MODEL_DMG_B,"dmg_boot.bin","other publisher"}};
     for(const auto&c:cases)if(automatic.model(romModelFamily(c.rom))!=c.model||automatic.bootName(romModelFamily(c.rom))!=c.boot){std::fprintf(stderr,"Automatic model for %s differs from Cocoa bestModelForROM\n",c.what);return settingsFailure(__LINE__);}}
    {// Cocoa hotkey choices: pause, reset, mute and save/load slots 1-10 (Qt keeps its extra Screenshot).
     SettingsDialog hotkeys(AppSettings{},nullptr,3);int choices=0;
     for(auto*box:hotkeys.findChildren<QComboBox*>())if(box->findText("Quick load (slot 10)")>=0&&box->findText("Mute / unmute")>=0&&box->findText("Quick save (slot 10)")>=0)choices=box->count();
     if(choices!=25){std::fprintf(stderr,"Controller hotkeys offer %d choices instead of Cocoa's slots 1-10, mute and Qt's screenshot\n",choices);return settingsFailure(__LINE__);}}
    QTemporaryDir temporary(QDir::currentPath()+"/settings-smoke-XXXXXX");
    if (!temporary.isValid()) return settingsFailure(__LINE__);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    // Old persistent debug choices must never affect a new process.
    {QSettings legacy(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");legacy.setValue("developerMode",true);legacy.setValue("cpuFrequency",2);legacy.sync();}
    if(AppSettings::load().developerMode||AppSettings::load().cpuFrequency!=0)return settingsFailure(__LINE__);
    // Fresh installs start in English; legacy automatic language requires an explicit choice.
    if(AppSettings::load().language!="en"||AppSettings::load().languageChosen)return settingsFailure(__LINE__);
    {
        QSettings legacy(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");
        legacy.setValue("language","system");legacy.sync();
        if(AppSettings::load().language!="en"||AppSettings::load().languageChosen)return settingsFailure(__LINE__);
        legacy.setValue("language","zh_CN");legacy.sync();
        if(AppSettings::load().language!="zh_CN"||!AppSettings::load().languageChosen)return settingsFailure(__LINE__);
        legacy.clear();legacy.sync();
    }
    for(const auto&language:{QStringLiteral("en"),QStringLiteral("zh_CN")}){
        FirstLanguageDialog choice;
        if(argc>1&&language=="en"){choice.show();app.processEvents();if(!choice.grab().save(QString::fromLocal8Bit(argv[1])+"/first-language.png"))return settingsFailure(__LINE__);}
        if(choice.selection()!="en"||choice.windowTitle()!="Welcome to SameBoy Qt")return settingsFailure(__LINE__);
        auto*button=choice.findChild<QPushButton*>(language=="en"?"chooseEnglish":"chooseChinese");
        if(!button)return settingsFailure(__LINE__);button->click();
        if(choice.result()!=QDialog::Accepted||choice.selection()!=language)return settingsFailure(__LINE__);
        AppSettings selected;selected.language=choice.selection();selected.languageChosen=true;
        if(!selected.save().isEmpty()||!AppSettings::load().languageChosen||AppSettings::load().language!=language)return settingsFailure(__LINE__);
    }
    AppSettings settings;if(settings.theme!=0)return settingsFailure(__LINE__);settings.theme=2;
    settings.developerMode=true;settings.cpuFrequency=2;settings.screenshotScale=8;settings.audioOutputDevice="test-output";
    settings.language="zh_CN";settings.audioSampleRate=44100;
    settings.bootFolder = QString::fromUtf8("C:/自定义启动 ROM");
    settings.hardware = 4; settings.agbModel = GB_MODEL_GBP_A;
    settings.palette = 3; settings.volume = 35; settings.temperature = -40;
    settings.keys[4] = Qt::Key_L; settings.osd = true; settings.controllerHotkeys = {24, 6}; settings.integerScale = false;
    settings.monospace = QFont("Courier New", 14);
    settings.extraKeys[2][4]=Qt::Key_J;settings.controllers[2]="test-pad";
    settings.controllerButtons[2][4]=31;settings.preferJoystick=true;settings.deadzone=35;
    settings.joyConAutoPair=false;settings.joyConHorizontal=false;settings.joyConPairs["L:test"]="R:test";settings.joyConGrips["L:test"]=1;settings.joyConSingles={"L:solo"};
    settings.cameraMode=2;settings.cameraImage="camera.png";settings.cameraMirror=true;
    settings.save();
    {QSettings disk(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");if(disk.contains("developerMode")||disk.contains("cpuFrequency"))return settingsFailure(__LINE__);}
    const auto loaded = AppSettings::load();
    if(!loaded.developerMode)return settingsFailure(__LINE__);
    if(loaded.theme!=2)return settingsFailure(__LINE__);
    if(loaded.screenshotScale!=8||loaded.audioOutputDevice!="test-output")return settingsFailure(__LINE__);
    if(loaded.language!="zh_CN"||loaded.audioSampleRate!=44100||loaded.cpuFrequency!=2)return settingsFailure(__LINE__);
    if (loaded.bootFolder != settings.bootFolder || loaded.volume != 35 || loaded.temperature != -40 ||
        loaded.keys[4] != Qt::Key_L || loaded.controllerHotkeys[0] != 24 || loaded.controllerHotkeys[1] != 6 || loaded.model(true) != GB_MODEL_GBP_A || loaded.bootName(true) != "agb_boot.bin" ||
        loaded.monospace != settings.monospace || loaded.integerScale || !loaded.osd) return settingsFailure(__LINE__);
    if(loaded.extraKeys[2][4]!=Qt::Key_J||loaded.controllers[2]!="test-pad"||loaded.controllerButtons[2][4]!=31||!loaded.preferJoystick||loaded.deadzone!=35)return settingsFailure(__LINE__);
    if(loaded.joyConAutoPair||loaded.joyConHorizontal||loaded.joyConPairs.value("L:test")!="R:test"||loaded.joyConGrips.value("L:test")!=1||!loaded.joyConSingles.contains("L:solo"))return settingsFailure(__LINE__);
    if(loaded.cameraMode!=2||loaded.cameraImage!="camera.png"||!loaded.cameraMirror)return settingsFailure(__LINE__);
    AppSettings pocket;
    pocket.hardware = 5; pocket.save();
    if (AppSettings::load().model(false) != GB_MODEL_MGB || AppSettings::load().bootName(false) != "mgb_boot.bin") return settingsFailure(__LINE__);
    pocket.hardware = 1; pocket.dmgModel = GB_MODEL_MGB; pocket.save();
    if (AppSettings::load().hardware != 5 || AppSettings::load().dmgModel != GB_MODEL_DMG_B) return settingsFailure(__LINE__);
    // Render all four pages through the actual Widgets layouts.
    SettingsDialog dialog(loaded);
    // Cocoa popups need a window registered with the window server.
#ifndef Q_OS_MACOS
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
#endif
    dialog.show(); app.processEvents();
    auto *tabs = dialog.findChild<QTabWidget *>();
    if (!tabs || tabs->count() != 6) return settingsFailure(__LINE__);
    auto*rate=dialog.findChild<QComboBox*>("audioSampleRate");auto*language=dialog.findChild<QComboBox*>("interfaceLanguage");if(!rate||rate->count()!=3||!language||language->count()!=2)return settingsFailure(__LINE__);
    rate->setCurrentIndex(rate->findData(48000));language->setCurrentIndex(language->findData("en"));if(dialog.settings().audioSampleRate!=48000||dialog.settings().language!="en")return settingsFailure(__LINE__);
    if(resolvedLanguage("system")!="en"||resolvedLanguage("invalid")!="en"||resolvedLanguage("en")!="en"||resolvedLanguage("zh_CN")!="zh_CN"||language->findData("system")!=-1||!dialog.settings().languageChosen)return settingsFailure(__LINE__);
    {AppTranslator translator;if(!translator.load(":/translations/zh_CN.qm"))return settingsFailure(__LINE__);app.installTranslator(&translator);SettingsDialog chinese(loaded);auto*t=chinese.findChild<QTabWidget*>();if(!t||t->tabText(0)!=QString::fromUtf8("模拟")||chinese.windowTitle()!=QString::fromUtf8(
#ifdef Q_OS_MACOS
"设置"
#else
"偏好设置"
#endif
))return settingsFailure(__LINE__);app.removeTranslator(&translator);}
    auto*theme=dialog.findChild<QComboBox*>("interfaceTheme");if(!theme||theme->count()!=3||theme->currentData().toInt()!=2)return settingsFailure(__LINE__);
    theme->setCurrentIndex(theme->findData(1));app.processEvents();if(dialog.settings().theme!=1||(qApp->palette().color(QPalette::Window).lightness()<128))return settingsFailure(__LINE__);
    theme->setCurrentIndex(theme->findData(2));app.processEvents();if(dialog.settings().theme!=2||(qApp->palette().color(QPalette::Window).lightness()>=128))return settingsFailure(__LINE__);
    if(argc>1){tabs->setCurrentIndex(5);app.processEvents();if(!dialog.grab().save(QString::fromLocal8Bit(argv[1])+"/general-dark.png"))return settingsFailure(__LINE__);}
    {auto original=loaded;original.theme=1;SettingsDialog canceled(original);auto*choice=canceled.findChild<QComboBox*>("interfaceTheme");choice->setCurrentIndex(choice->findData(2));canceled.reject();app.processEvents();if(qApp->palette().color(QPalette::Window).lightness()<128)return settingsFailure(__LINE__);}
    applyInterfaceTheme(0);
    if(!dialog.findChild<QComboBox*>("cameraSource")||!dialog.findChild<QComboBox*>("cameraDevice"))return settingsFailure(__LINE__);
    auto*bindings=dialog.findChild<QTableWidget*>("inputBindings");if(!bindings||bindings->rowCount()!=17||dialog.settings().keys[13]||dialog.settings().keys[14])return settingsFailure(__LINE__);
    auto*slowKey=qobject_cast<QPushButton*>(bindings->cellWidget(13,1));if(!slowKey)return settingsFailure(__LINE__);slowKey->click();QKeyEvent assign(QEvent::KeyPress,Qt::Key_F6,Qt::NoModifier);QApplication::sendEvent(slowKey,&assign);if(dialog.settings().keys[13]!=Qt::Key_F6)return settingsFailure(__LINE__);
    auto clearBinding=[&](QWidget*widget){QTimer::singleShot(30,&dialog,[]{if(auto*menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){menu->setActiveAction(menu->actions().first());QKeyEvent choose(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&choose);}});return QMetaObject::invokeMethod(widget,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(2,2)));};
    if(!clearBinding(slowKey)||dialog.settings().keys[13])return settingsFailure(__LINE__);auto*padBinding=bindings->cellWidget(0,2);if(!padBinding||!clearBinding(padBinding)||dialog.settings().controllerButtons[0][0]!=-1)return settingsFailure(__LINE__);
    bool filtersFound=false,blendingFound=false,palettesFound=false;
    for(auto *combo:dialog.findChildren<QComboBox *>()){
        if(combo->findText("LCD display")>=0){filtersFound=true;if(combo->count()!=16)return settingsFailure(__LINE__);}
        if(combo->findText("Accurate")>=0){blendingFound=true;if(combo->count()!=3)return settingsFailure(__LINE__);}
        if(combo->findText("Custom…")>=0){palettesFound=true;if(combo->findText("Fog")<0||combo->findText("Twilight")<0)return settingsFailure(__LINE__);}
    }
    if(!filtersFound||!blendingFound||!palettesFound)return settingsFailure(__LINE__);
    bool dmgFound = false;
    for (auto *combo : dialog.findChildren<QComboBox *>()) if (combo->findText("DMG-CPU B") >= 0) {
        dmgFound = true;
        if (combo->count() != 4 || combo->findText("MGB (Pocket)") >= 0) return settingsFailure(__LINE__);
    }
    if (!dmgFound) return settingsFailure(__LINE__);
    const QStringList unsupported{"DMG-CPU 0","DMG-CPU A","DMG-CPU C","CPU AGB 0 (Early GBA)",
        "CPU AGB B (GBA SP)","CPU AGB E (Late GBA SP)","CPU AGB E (Late Game Boy Player)"};
    for (const auto &name : unsupported) {
        bool found = false;
        for (auto *combo : dialog.findChildren<QComboBox *>()) {
            const int index = combo->findText(name);
            if (index < 0) continue;
            found = true;
            if (combo->model()->flags(combo->model()->index(index,0)) & Qt::ItemIsEnabled) return settingsFailure(__LINE__);
        }
        if (!found) return settingsFailure(__LINE__);
    }
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i); app.processEvents();
        if (argc > 1 && !dialog.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("settings-%1.png").arg(i)))) return settingsFailure(__LINE__);
    }
    if(argc>1){PaletteEditor editor(loaded.palettes,loaded.palettes.size()-1);editor.setAttribute(Qt::WA_DontShowOnScreen);editor.show();app.processEvents();
        if(!editor.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("palette-editor.png")))return settingsFailure(__LINE__);}
    auto *binding = qobject_cast<QPushButton *>(bindings->cellWidget(0,1));
    if (!binding) return settingsFailure(__LINE__);
    auto press = [&](int key) {
        binding->click(); QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(binding, &event);
    };
    press(Qt::Key_Tab);
    if (dialog.settings().keys[0] != Qt::Key_Tab) return settingsFailure(__LINE__);
    press(Qt::Key_Escape);
    if (dialog.settings().keys[0] != Qt::Key_Tab) return settingsFailure(__LINE__);
    press(Qt::Key_Delete);
    if (dialog.settings().keys[0] != 0) return settingsFailure(__LINE__);
    auto *player=dialog.findChild<QComboBox*>("inputPlayer");if(!player||player->count()!=4)return settingsFailure(__LINE__);
    player->setCurrentIndex(3);app.processEvents();
    if(!dialog.findChild<QPushButton*>("arrangeJoyCons"))return settingsFailure(__LINE__);
    JoyConDialog joycons(loaded,std::make_shared<Controllers>());joycons.setAttribute(Qt::WA_DontShowOnScreen);joycons.show();app.processEvents();
    if(argc>1&&!joycons.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("joycons.png")))return settingsFailure(__LINE__);
    joycons.reject();if(joycons.settings().joyConPairs!=loaded.joyConPairs)return settingsFailure(__LINE__);
    // Run the actual Joy-Con configuration UI against SDL virtual left/right devices.
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=6;desc.nbuttons=26;desc.axis_mask=63;desc.button_mask=(1u<<26)-1;desc.vendor_id=0x057e;
    desc.name="Virtual Joy-Con L";desc.product_id=0x2006;auto left=SDL_AttachVirtualJoystick(&desc);
    desc.name="Virtual Joy-Con R";desc.product_id=0x2007;auto right=SDL_AttachVirtualJoystick(&desc);
    if(!left||!right)return settingsFailure(__LINE__);
    auto mapping=[](const char*type){return QByteArray("*,Virtual Joy-Con,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,paddle1:b16,paddle2:b17,paddle3:b18,paddle4:b19,type:")+type+",";};
    if(!SDL_SetGamepadMapping(left,mapping("joyconleft").constData())||!SDL_SetGamepadMapping(right,mapping("joyconright").constData()))return settingsFailure(__LINE__);
    auto pads=std::make_shared<Controllers>();AppSettings arrangeSettings;arrangeSettings.joyConAutoPair=false;
    JoyConDialog arrangement(arrangeSettings,pads);arrangement.setAttribute(Qt::WA_DontShowOnScreen);arrangement.show();app.processEvents();
    auto*devices=arrangement.findChild<QTableWidget*>("joyConDevices");if(!devices||devices->rowCount()!=2)return settingsFailure(__LINE__);
    if(argc>1&&!arrangement.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("joycons-single.png")))return settingsFailure(__LINE__);
    auto tick=[&]{QThread::msleep(80);app.processEvents();};
    auto button=[&](const QString&text)->QPushButton*{for(auto*b:arrangement.findChildren<QPushButton*>())if(b->text()==text&&b->isVisible())return b;return nullptr;};
    auto*combine=button("Combine");if(!combine||!combine->isEnabled())return settingsFailure(__LINE__);combine->click();tick();if(devices->rowCount()!=1||arrangement.settings().joyConPairs.size()!=1)return settingsFailure(__LINE__);
    if(argc>1&&!arrangement.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("joycons-pair.png")))return settingsFailure(__LINE__);
    auto*split=button("Separate Joy-Cons");if(!split)return settingsFailure(__LINE__);split->click();tick();if(devices->rowCount()!=2)return settingsFailure(__LINE__);
    auto*jl=SDL_OpenJoystick(left),*jr=SDL_OpenJoystick(right);if(!jl||!jr)return settingsFailure(__LINE__);
    SDL_SetJoystickVirtualButton(jl,9,true);SDL_SetJoystickVirtualButton(jr,10,true);tick();
    if(devices->rowCount()!=1)return settingsFailure(__LINE__);
    split=button("Separate Joy-Cons");if(!split)return settingsFailure(__LINE__);split->click();tick();if(devices->rowCount()!=2)return settingsFailure(__LINE__);
    tick();if(devices->rowCount()!=2)return settingsFailure(__LINE__); // Held L+R must not undo Separate.
    arrangement.reject();SDL_DetachVirtualJoystick(left);SDL_DetachVirtualJoystick(right);SDL_CloseJoystick(jl);SDL_CloseJoystick(jr);
    // Video options change exported pixels and dimensions without involving the core.
    ScreenWidget clickable;int opens=0;clickable.openRomRequested=[&]{++opens;};
    QMouseEvent emptyClick(QEvent::MouseButtonPress,QPointF(20,20),QPointF(20,20),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&clickable,&emptyClick);if(opens!=0)return settingsFailure(__LINE__);
    auto*openButton=clickable.findChild<QPushButton*>("openRomButton");if(!openButton)return settingsFailure(__LINE__);openButton->click();if(opens!=1)return settingsFailure(__LINE__);
    clickable.beginLoading();if(openButton->isVisibleTo(&clickable))return settingsFailure(__LINE__);
    clickable.resize(640,576);app.processEvents();if(openButton->isVisibleTo(&clickable))return settingsFailure(__LINE__);
    clickable.setFrame({});if(!openButton->isVisibleTo(&clickable))return settingsFailure(__LINE__);
    QImage liveFrame(160,144,QImage::Format_RGB32);liveFrame.fill(Qt::black);clickable.setFrame(liveFrame);
    QApplication::sendEvent(&clickable,&emptyClick);if(opens!=1||!openButton->isHidden()&&openButton->isVisibleTo(&clickable))return settingsFailure(__LINE__);
    ScreenWidget screen;
    desc.vendor_id=0;desc.product_id=0;desc.name="Identical controller";
    auto identicalA=SDL_AttachVirtualJoystick(&desc),identicalB=SDL_AttachVirtualJoystick(&desc);if(!identicalA||!identicalB)return settingsFailure(__LINE__);
    {SettingsDialog identical(AppSettings{},nullptr,3);identical.setAttribute(Qt::WA_DontShowOnScreen);identical.show();app.processEvents();
     auto*choices=identical.findChild<QComboBox*>("inputDevice");QVector<int> matching;
     if(choices)for(int i=0;i<choices->count();++i)if(choices->itemText(i).startsWith("Identical controller"))matching.append(i);
     if(matching.size()!=2||choices->itemText(matching[0])==choices->itemText(matching[1])||choices->itemData(matching[0])==choices->itemData(matching[1])){std::fputs("Identical controller choices cannot be distinguished\n",stderr);return settingsFailure(__LINE__);}
     if(argc>1){choices->showPopup();app.processEvents();auto*popup=app.activePopupWidget();if(!popup||!popup->grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("identical-controller-options.png")))return settingsFailure(__LINE__);choices->hidePopup();}
     identical.reject();}
    SDL_DetachVirtualJoystick(identicalA);SDL_DetachVirtualJoystick(identicalB);
    ScreenWidget empty;empty.setAttribute(Qt::WA_DontShowOnScreen);empty.resize(480,432);empty.show();
    for(bool dark:{false,true}){auto palette=empty.palette();palette.setColor(QPalette::Window,dark?QColor(30,30,30):QColor(245,245,245));palette.setColor(QPalette::WindowText,dark?Qt::white:Qt::black);empty.setPalette(palette);app.processEvents();auto capture=empty.grab().toImage();if(capture.pixelColor(4,4)!=palette.color(QPalette::Window)){std::fprintf(stderr,"Empty game view ignores system palette\n");return settingsFailure(__LINE__);}if(argc>1&&!capture.save(QString::fromLocal8Bit(argv[1])+(dark?"/empty-dark.png":"/empty-light.png")))return settingsFailure(__LINE__);}
    QImage frame(160, 144, QImage::Format_RGB32); frame.fill(Qt::red);
    screen.setFrame(frame);
    if (screen.screenshot().size() != frame.size()) return settingsFailure(__LINE__);
    for(int scale:{1,2,4,8,16}){AppSettings shot;shot.screenshotScale=scale;screen.setSettings(shot);const auto image=screen.screenshot();if(image.size()!=frame.size()*scale||image.pixel(0,0)!=frame.pixel(0,0))return settingsFailure(__LINE__);}
    auto*shotScale=dialog.findChild<QComboBox*>("screenshotScale");auto*outputDevice=dialog.findChild<QComboBox*>("audioOutputDevice");
    if(!shotScale||shotScale->count()!=5||!outputDevice||!outputDevice->itemData(0).toByteArray().isEmpty())return settingsFailure(__LINE__);
    shotScale->setCurrentIndex(shotScale->findData(16));outputDevice->setCurrentIndex(0);
    if(dialog.settings().screenshotScale!=16||!dialog.settings().audioOutputDevice.isEmpty())return settingsFailure(__LINE__);
    settings.filterScreenshots = true; settings.scaling = 2;settings.screenshotScale=4;
    screen.setSettings(settings);
    const auto filtered = screen.screenshot();
    if (filtered.size() != QSize(640, 576) || filtered.pixel(4, 4) == frame.pixel(0, 0)) return settingsFailure(__LINE__);
    auto altSettings=loaded;altSettings.keys[4]=Qt::Key_Alt;altSettings.save();if(AppSettings::load().keys[4]!=Qt::Key_Alt)return settingsFailure(__LINE__);
    // A failed write must not leak uncommitted values through QSettings' shared cache.
    const auto config=QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").fileName();
    {QSettings extra(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");extra.setValue("unrelatedAuditKey","preserve");extra.sync();}
    QFile originalFile(config);if(!originalFile.open(QIODevice::ReadOnly))return settingsFailure(__LINE__);const auto originalBytes=originalFile.readAll();originalFile.close();
    const auto permissions=QFile::permissions(config);if(!QFile::setPermissions(config,QFileDevice::ReadOwner|QFileDevice::ReadUser))return settingsFailure(__LINE__);
    auto failed=altSettings;failed.volume=(altSettings.volume+1)%101;const auto writeError=failed.save();
    if(AppSettings::load().volume!=altSettings.volume){std::fputs("Failed preferences write leaked uncommitted values\n",stderr);return settingsFailure(__LINE__);}
    if(writeError.isEmpty()){std::fputs("Preferences write failure was not reported\n",stderr);return settingsFailure(__LINE__);}
    if(!originalFile.open(QIODevice::ReadOnly)||originalFile.readAll()!=originalBytes){std::fputs("Failed preferences save modified the original file\n",stderr);return settingsFailure(__LINE__);}originalFile.close();
    if(!QFile::setPermissions(config,permissions))return settingsFailure(__LINE__);
    if(!failed.save().isEmpty()||AppSettings::load().volume!=failed.volume)return settingsFailure(__LINE__);
    if(QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").value("unrelatedAuditKey").toString()!="preserve")return settingsFailure(__LINE__);
    std::puts("Settings persistence, model mapping, five preference pages and filtered screenshot passed.");
    return 0;
}
