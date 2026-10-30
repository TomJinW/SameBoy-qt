#include "../src/settings.h"
#include "../src/uistyle.h"
#include "../src/language.h"
#include "../src/restartsession.h"
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
#include <QMenu>
#include <QTimer>
#include <QComboBox>
#include <QTableWidget>
#include <QThread>
#include <SDL3/SDL.h>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary(QDir::currentPath()+"/settings-smoke-XXXXXX");
    if (!temporary.isValid()) return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    AppSettings settings;if(settings.theme!=0)return 1;settings.theme=2;
    settings.cpuFrequency=2;
    settings.language="zh_CN";settings.audioSampleRate=44100;
    settings.bootFolder = QString::fromUtf8("C:/自定义启动 ROM");
    settings.hardware = 4; settings.agbModel = GB_MODEL_GBP_A;
    settings.palette = 3; settings.volume = 35; settings.temperature = -40;
    settings.keys[4] = Qt::Key_L; settings.osd = true; settings.integerScale = false;
    settings.monospace = QFont("Courier New", 14);
    settings.extraKeys[2][4]=Qt::Key_J;settings.controllers[2]="test-pad";
    settings.controllerButtons[2][4]=31;settings.preferJoystick=true;settings.deadzone=35;
    settings.joyConAutoPair=false;settings.joyConHorizontal=false;settings.joyConPairs["L:test"]="R:test";settings.joyConGrips["L:test"]=1;settings.joyConSingles={"L:solo"};
    settings.cameraMode=2;settings.cameraImage="camera.png";settings.cameraMirror=true;
    settings.save();
    const auto loaded = AppSettings::load();
    if(loaded.theme!=2)return 1;
    if(loaded.language!="zh_CN"||loaded.audioSampleRate!=44100||loaded.cpuFrequency!=2)return 1;
    if (loaded.bootFolder != settings.bootFolder || loaded.volume != 35 || loaded.temperature != -40 ||
        loaded.keys[4] != Qt::Key_L || loaded.model(true) != GB_MODEL_GBP_A || loaded.bootName(true) != "agb_boot.bin" ||
        loaded.monospace != settings.monospace || loaded.integerScale || !loaded.osd) return 1;
    if(loaded.extraKeys[2][4]!=Qt::Key_J||loaded.controllers[2]!="test-pad"||loaded.controllerButtons[2][4]!=31||!loaded.preferJoystick||loaded.deadzone!=35)return 1;
    if(loaded.joyConAutoPair||loaded.joyConHorizontal||loaded.joyConPairs.value("L:test")!="R:test"||loaded.joyConGrips.value("L:test")!=1||!loaded.joyConSingles.contains("L:solo"))return 1;
    if(loaded.cameraMode!=2||loaded.cameraImage!="camera.png"||!loaded.cameraMirror)return 1;
    AppSettings pocket;
    pocket.hardware = 5; pocket.save();
    if (AppSettings::load().model(false) != GB_MODEL_MGB || AppSettings::load().bootName(false) != "mgb_boot.bin") return 1;
    pocket.hardware = 1; pocket.dmgModel = GB_MODEL_MGB; pocket.save();
    if (AppSettings::load().hardware != 5 || AppSettings::load().dmgModel != GB_MODEL_DMG_B) return 1;
    // Render all four pages through the actual Widgets layouts.
    SettingsDialog dialog(loaded);
    dialog.setAttribute(Qt::WA_DontShowOnScreen);
    dialog.show(); app.processEvents();
    auto *tabs = dialog.findChild<QTabWidget *>();
    if (!tabs || tabs->count() != 6) return 1;
    auto*rate=dialog.findChild<QComboBox*>("audioSampleRate");auto*language=dialog.findChild<QComboBox*>("interfaceLanguage");if(!rate||rate->count()!=3||!language||language->count()!=3)return 1;
    rate->setCurrentIndex(rate->findData(48000));language->setCurrentIndex(language->findData("en"));if(dialog.settings().audioSampleRate!=48000||dialog.settings().language!="en")return 1;
    if(resolvedLanguage("system",{"zh-Hans-CN"})!="zh_CN"||resolvedLanguage("system",{"en-US"})!="en"||resolvedLanguage("system",{"zh-Hant-TW"})!="en"||resolvedLanguage("en",{"zh-CN"})!="en"||resolvedLanguage("zh_CN",{"en"})!="zh_CN")return 1;
    {AppTranslator translator;if(!translator.load(":/translations/zh_CN.qm"))return 1;app.installTranslator(&translator);SettingsDialog chinese(loaded);auto*t=chinese.findChild<QTabWidget*>();if(!t||t->tabText(0)!=QString::fromUtf8("模拟")||chinese.windowTitle()!=QString::fromUtf8("偏好设置"))return 1;app.removeTranslator(&translator);}
    auto*theme=dialog.findChild<QComboBox*>("interfaceTheme");if(!theme||theme->count()!=3||theme->currentData().toInt()!=2)return 1;
    theme->setCurrentIndex(theme->findData(1));app.processEvents();if(dialog.settings().theme!=1||(QGuiApplication::platformName()!="offscreen"&&QGuiApplication::styleHints()->colorScheme()!=Qt::ColorScheme::Light))return 1;
    theme->setCurrentIndex(theme->findData(2));app.processEvents();if(dialog.settings().theme!=2||(QGuiApplication::platformName()!="offscreen"&&QGuiApplication::styleHints()->colorScheme()!=Qt::ColorScheme::Dark))return 1;
    if(argc>1){tabs->setCurrentIndex(5);app.processEvents();if(!dialog.grab().save(QString::fromLocal8Bit(argv[1])+"/general-dark.png"))return 1;}
    {auto original=loaded;original.theme=1;SettingsDialog canceled(original);auto*choice=canceled.findChild<QComboBox*>("interfaceTheme");choice->setCurrentIndex(choice->findData(2));canceled.reject();app.processEvents();if(QGuiApplication::platformName()!="offscreen"&&QGuiApplication::styleHints()->colorScheme()!=Qt::ColorScheme::Light)return 1;}
    applyInterfaceTheme(0);
    if(!dialog.findChild<QComboBox*>("cameraSource")||!dialog.findChild<QComboBox*>("cameraDevice"))return 1;
    auto*bindings=dialog.findChild<QTableWidget*>("inputBindings");if(!bindings||bindings->rowCount()!=17||dialog.settings().keys[13]||dialog.settings().keys[14])return 1;
    auto*slowKey=qobject_cast<QPushButton*>(bindings->cellWidget(13,1));if(!slowKey)return 1;slowKey->click();QKeyEvent assign(QEvent::KeyPress,Qt::Key_F6,Qt::NoModifier);QApplication::sendEvent(slowKey,&assign);if(dialog.settings().keys[13]!=Qt::Key_F6)return 1;
    auto clearBinding=[&](QWidget*widget){QTimer::singleShot(0,&dialog,[]{if(auto*menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){menu->setActiveAction(menu->actions().first());QKeyEvent choose(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&choose);}});return QMetaObject::invokeMethod(widget,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(2,2)));};
    if(!clearBinding(slowKey)||dialog.settings().keys[13])return 1;auto*padBinding=bindings->cellWidget(0,2);if(!padBinding||!clearBinding(padBinding)||dialog.settings().controllerButtons[0][0]!=-1)return 1;
    bool filtersFound=false,blendingFound=false,palettesFound=false;
    for(auto *combo:dialog.findChildren<QComboBox *>()){
        if(combo->findText("LCD display")>=0){filtersFound=true;if(combo->count()!=16)return 1;}
        if(combo->findText("Accurate")>=0){blendingFound=true;if(combo->count()!=3)return 1;}
        if(combo->findText("Custom…")>=0){palettesFound=true;if(combo->findText("Fog")<0||combo->findText("Twilight")<0)return 1;}
    }
    if(!filtersFound||!blendingFound||!palettesFound)return 1;
    bool dmgFound = false;
    for (auto *combo : dialog.findChildren<QComboBox *>()) if (combo->findText("DMG-CPU B") >= 0) {
        dmgFound = true;
        if (combo->count() != 4 || combo->findText("MGB (Pocket)") >= 0) return 1;
    }
    if (!dmgFound) return 1;
    const QStringList unsupported{"DMG-CPU 0","DMG-CPU A","DMG-CPU C","CPU AGB 0 (Early GBA)",
        "CPU AGB B (GBA SP)","CPU AGB E (Late GBA SP)","CPU AGB E (Late Game Boy Player)"};
    for (const auto &name : unsupported) {
        bool found = false;
        for (auto *combo : dialog.findChildren<QComboBox *>()) {
            const int index = combo->findText(name);
            if (index < 0) continue;
            found = true;
            if (combo->model()->flags(combo->model()->index(index,0)) & Qt::ItemIsEnabled) return 1;
        }
        if (!found) return 1;
    }
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i); app.processEvents();
        if (argc > 1 && !dialog.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("settings-%1.png").arg(i)))) return 1;
    }
    if(argc>1){PaletteEditor editor(loaded.palettes,loaded.palettes.size()-1);editor.setAttribute(Qt::WA_DontShowOnScreen);editor.show();app.processEvents();
        if(!editor.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("palette-editor.png")))return 1;}
    QPushButton *binding = nullptr;
    for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == "Right") binding = button;
    if (!binding) return 1;
    auto press = [&](int key) {
        binding->click(); QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(binding, &event);
    };
    press(Qt::Key_Tab);
    if (dialog.settings().keys[0] != Qt::Key_Tab) return 1;
    press(Qt::Key_Escape);
    if (dialog.settings().keys[0] != Qt::Key_Tab) return 1;
    press(Qt::Key_Delete);
    if (dialog.settings().keys[0] != 0) return 1;
    auto *player=dialog.findChild<QComboBox*>("inputPlayer");if(!player||player->count()!=4)return 1;
    player->setCurrentIndex(3);app.processEvents();
    if(!dialog.findChild<QPushButton*>("arrangeJoyCons"))return 1;
    JoyConDialog joycons(loaded,std::make_shared<Controllers>());joycons.setAttribute(Qt::WA_DontShowOnScreen);joycons.show();app.processEvents();
    if(argc>1&&!joycons.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("joycons.png")))return 1;
    joycons.reject();if(joycons.settings().joyConPairs!=loaded.joyConPairs)return 1;
    // Run the actual Joy-Con configuration UI against SDL virtual left/right devices.
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=6;desc.nbuttons=26;desc.axis_mask=63;desc.button_mask=(1u<<26)-1;desc.vendor_id=0x057e;
    desc.name="Virtual Joy-Con L";desc.product_id=0x2006;auto left=SDL_AttachVirtualJoystick(&desc);
    desc.name="Virtual Joy-Con R";desc.product_id=0x2007;auto right=SDL_AttachVirtualJoystick(&desc);
    if(!left||!right)return 1;
    auto mapping=[](const char*type){return QByteArray("*,Virtual Joy-Con,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,paddle1:b16,paddle2:b17,paddle3:b18,paddle4:b19,type:")+type+",";};
    if(!SDL_SetGamepadMapping(left,mapping("joyconleft").constData())||!SDL_SetGamepadMapping(right,mapping("joyconright").constData()))return 1;
    auto pads=std::make_shared<Controllers>();AppSettings arrangeSettings;arrangeSettings.joyConAutoPair=false;
    JoyConDialog arrangement(arrangeSettings,pads);arrangement.setAttribute(Qt::WA_DontShowOnScreen);arrangement.show();app.processEvents();
    auto*devices=arrangement.findChild<QTableWidget*>("joyConDevices");if(!devices||devices->rowCount()!=2)return 1;
    if(argc>1&&!arrangement.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("joycons-single.png")))return 1;
    auto tick=[&]{QThread::msleep(80);app.processEvents();};
    auto button=[&](const QString&text)->QPushButton*{for(auto*b:arrangement.findChildren<QPushButton*>())if(b->text()==text&&b->isVisible())return b;return nullptr;};
    auto*combine=button("Combine");if(!combine||!combine->isEnabled())return 1;combine->click();tick();if(devices->rowCount()!=1||arrangement.settings().joyConPairs.size()!=1)return 1;
    if(argc>1&&!arrangement.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("joycons-pair.png")))return 1;
    auto*split=button("Separate Joy-Cons");if(!split)return 1;split->click();tick();if(devices->rowCount()!=2)return 1;
    auto*jl=SDL_OpenJoystick(left),*jr=SDL_OpenJoystick(right);if(!jl||!jr)return 1;
    SDL_SetJoystickVirtualButton(jl,9,true);SDL_SetJoystickVirtualButton(jr,10,true);tick();
    if(devices->rowCount()!=1)return 1;
    split=button("Separate Joy-Cons");if(!split)return 1;split->click();tick();if(devices->rowCount()!=2)return 1;
    tick();if(devices->rowCount()!=2)return 1; // Held L+R must not undo Separate.
    arrangement.reject();SDL_DetachVirtualJoystick(left);SDL_DetachVirtualJoystick(right);SDL_CloseJoystick(jl);SDL_CloseJoystick(jr);
    // Video options change exported pixels and dimensions without involving the core.
    ScreenWidget screen;
    desc.vendor_id=0;desc.product_id=0;desc.name="Identical controller";
    auto identicalA=SDL_AttachVirtualJoystick(&desc),identicalB=SDL_AttachVirtualJoystick(&desc);if(!identicalA||!identicalB)return 1;
    {SettingsDialog identical(AppSettings{},nullptr,3);identical.setAttribute(Qt::WA_DontShowOnScreen);identical.show();app.processEvents();
     auto*choices=identical.findChild<QComboBox*>("inputDevice");QVector<int> matching;
     if(choices)for(int i=0;i<choices->count();++i)if(choices->itemText(i).startsWith("Identical controller"))matching.append(i);
     if(matching.size()!=2||choices->itemText(matching[0])==choices->itemText(matching[1])||choices->itemData(matching[0])==choices->itemData(matching[1])){std::fputs("Identical controller choices cannot be distinguished\n",stderr);return 1;}
     if(argc>1){choices->showPopup();app.processEvents();auto*popup=app.activePopupWidget();if(!popup||!popup->grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("identical-controller-options.png")))return 1;choices->hidePopup();}
     identical.reject();}
    SDL_DetachVirtualJoystick(identicalA);SDL_DetachVirtualJoystick(identicalB);
    ScreenWidget empty;empty.setAttribute(Qt::WA_DontShowOnScreen);empty.resize(480,432);empty.show();
    for(bool dark:{false,true}){auto palette=empty.palette();palette.setColor(QPalette::Window,dark?QColor(30,30,30):QColor(245,245,245));palette.setColor(QPalette::WindowText,dark?Qt::white:Qt::black);empty.setPalette(palette);app.processEvents();auto capture=empty.grab().toImage();if(capture.pixelColor(4,4)!=palette.color(QPalette::Window)){std::fprintf(stderr,"Empty game view ignores system palette\n");return 1;}if(argc>1&&!capture.save(QString::fromLocal8Bit(argv[1])+(dark?"/empty-dark.png":"/empty-light.png")))return 1;}
    QImage frame(160, 144, QImage::Format_RGB32); frame.fill(Qt::red);
    screen.setFrame(frame);
    if (screen.screenshot().size() != frame.size()) return 1;
    settings.filterScreenshots = true; settings.scaling = 2;
    screen.setSettings(settings);
    const auto filtered = screen.screenshot();
    if (filtered.size() != QSize(640, 576) || filtered.pixel(4, 4) == frame.pixel(0, 0)) return 1;
    auto altSettings=loaded;altSettings.keys[4]=Qt::Key_Alt;altSettings.save();if(AppSettings::load().keys[4]!=Qt::Key_Alt)return 1;
    SettingsDialog restartDialog(altSettings,nullptr,5);auto*restart=restartDialog.findChild<QPushButton*>("restartApplication");if(!restart||restartDialog.restartRequested())return 1;restart->click();if(restartDialog.result()!=QDialog::Accepted||!restartDialog.restartRequested())return 1;
    const QVector<RestartWindow> session{{QString::fromUtf8("C:/游戏/精灵.gbc"),QByteArray("geometry\0one",12)},{QString(),QByteArray("second")}};
    const auto restored=decodeRestartSession(encodeRestartSession(session));if(restored.size()!=2||restored[0].rom!=session[0].rom||restored[0].geometry!=session[0].geometry||!restored[1].rom.isEmpty()||!decodeRestartSession("invalid").isEmpty())return 1;
    // A failed write must not leak uncommitted values through QSettings' shared cache.
    const auto config=QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").fileName();
    {QSettings extra(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");extra.setValue("unrelatedAuditKey","preserve");extra.sync();}
    QFile originalFile(config);if(!originalFile.open(QIODevice::ReadOnly))return 1;const auto originalBytes=originalFile.readAll();originalFile.close();
    const auto permissions=QFile::permissions(config);if(!QFile::setPermissions(config,QFileDevice::ReadOwner|QFileDevice::ReadUser))return 1;
    auto failed=altSettings;failed.volume=(altSettings.volume+1)%101;const auto writeError=failed.save();
    if(AppSettings::load().volume!=altSettings.volume){std::fputs("Failed preferences write leaked uncommitted values\n",stderr);return 1;}
    if(writeError.isEmpty()){std::fputs("Preferences write failure was not reported\n",stderr);return 1;}
    if(!originalFile.open(QIODevice::ReadOnly)||originalFile.readAll()!=originalBytes){std::fputs("Failed preferences save modified the original file\n",stderr);return 1;}originalFile.close();
    if(!QFile::setPermissions(config,permissions))return 1;
    if(!failed.save().isEmpty()||AppSettings::load().volume!=failed.volume)return 1;
    if(QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").value("unrelatedAuditKey").toString()!="preserve")return 1;
    std::puts("Settings persistence, model mapping, five preference pages and filtered screenshot passed.");
    return 0;
}
