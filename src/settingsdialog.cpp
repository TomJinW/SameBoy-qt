#include "settingsdialog.h"
#include "uistyle.h"
#include "paletteeditor.h"
#include "controllers.h"
#include "joycondialog.h"
#include <QTimer>
#include <QMenu>
#include <QMediaDevices>
#include <QCameraDevice>
#include <QTableWidget>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QSignalBlocker>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QKeySequence>
#include <QStandardItemModel>
#include <functional>

namespace {
class KeyButton final : public QPushButton {
public:
    KeyButton(int &key, QWidget *parent) : QPushButton(parent), key_(key) {
        refresh();setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this,&QWidget::customContextMenuRequested,this,[this](QPoint point){capturing_=false;refresh();QMenu menu(this);auto*clear=menu.addAction(tr("Clear binding"));clear->setEnabled(key_!=0);if(menu.exec(mapToGlobal(point))==clear){key_=0;refresh();}});
        connect(this, &QPushButton::clicked, this, [this] { capturing_ = true; setText(tr("Press a key…")); setFocus(); });
    }
protected:
    bool event(QEvent *event) override {
        if (capturing_ && event->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent *>(event)); return true;
        }
        return QPushButton::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (!capturing_) { QPushButton::keyPressEvent(event); return; }
        if (event->key() != Qt::Key_Escape) key_ = event->key() == Qt::Key_Delete ? 0 : event->key();
        capturing_ = false; refresh(); event->accept();
    }
    void focusOutEvent(QFocusEvent *event) override { capturing_ = false; refresh(); QPushButton::focusOutEvent(event); }
private:
    void refresh() { setText(key_ ? QKeySequence(key_).toString(QKeySequence::NativeText) : tr("Unassigned")); }
    int &key_; bool capturing_ = false;
};
}

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent,int initialTab)
    : QDialog(parent), settings_(settings)
{
#ifdef Q_OS_MACOS
    setWindowTitle(tr("Settings"));
#else
    setWindowTitle(tr("Preferences"));
#endif
    resize(680, 680);
    setMinimumSize(560, 320);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        resize(index==3?1060:680, index == 2 ? 390 : index == 3 ? 820 : index==4?480:680);
    });
    auto page = [&](const QString &title) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        auto *form = new QFormLayout(body);
        form->setContentsMargins(24, 24, 24, 24); form->setVerticalSpacing(18);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        scroll->setWidget(body); tabs->addTab(scroll, title); return form;
    };
    auto combo = [&](QFormLayout *form, const QString &label, int &value,
                     std::initializer_list<std::pair<QString, int>> items) {
        auto *box = new QComboBox;
        for (const auto &item : items) box->addItem(item.first, item.second);
        box->setCurrentIndex(box->findData(value));
        connect(box, &QComboBox::currentIndexChanged, this, [box, &value] { value = box->currentData().toInt(); });
        form->addRow(label, box); return box;
    };
    auto check = [&](QFormLayout *form, const QString &label, bool &value) {
        auto *box = new QCheckBox(label); box->setChecked(value);
        connect(box, &QCheckBox::toggled, this, [&value](bool on) { value = on; }); form->addRow(box); return box;
    };
    auto slider = [&](QFormLayout *form, const QString &label, int &value, int min, int max, const QString &unit) {
        auto *body = new QWidget; auto *row = new QHBoxLayout(body); row->setContentsMargins(0, 0, 0, 0);
        auto *bar = new QSlider(Qt::Horizontal); bar->setRange(min, max); bar->setValue(value);
        auto *text = new QLabel(QString::number(value) + unit); text->setMinimumWidth(65);
        row->addWidget(bar, 1); row->addWidget(text);
        connect(bar, &QSlider::valueChanged, this, [&value, text, unit](int v) { value = v; text->setText(QString::number(v) + unit); });
        form->addRow(label, body); return body;
    };
    auto note = [](QFormLayout *form, const QString &text) {
        auto *label = new QLabel(text); label->setWordWrap(true); label->setForegroundRole(QPalette::PlaceholderText); form->addRow(label);
    };
    auto unsupported = [this](QComboBox *box, std::initializer_list<int> values) {
        auto *model = qobject_cast<QStandardItemModel *>(box->model());
        for (int value : values) if (auto *item = model->item(box->findData(value))) {
            item->setEnabled(false); item->setSelectable(false);
            item->setToolTip(tr("This hardware revision is not implemented by the SameBoy core yet."));
        }
    };

    auto *emulation = page(tr("Emulation"));
    auto *folderRow = new QWidget; auto *folderLayout = new QHBoxLayout(folderRow); folderLayout->setContentsMargins(0,0,0,0);
    auto *folder = new QLineEdit(settings_.bootFolder);folder->setObjectName("bootRomFolder"); folder->setPlaceholderText(tr("Use built-in boot ROMs"));
    auto *browse = new QPushButton(tr("Browse…")); auto *builtin = new QPushButton(tr("Built-in"));
    folderLayout->addWidget(folder, 1); folderLayout->addWidget(browse); folderLayout->addWidget(builtin);
    connect(folder, &QLineEdit::textChanged, this, [this](const QString &path) { settings_.bootFolder = path.trimmed(); });
    connect(browse, &QPushButton::clicked, this, [this, folder] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Boot ROM folder"), folder->text());
        if (!path.isEmpty()) folder->setText(path);
    });
    connect(builtin, &QPushButton::clicked, folder, &QLineEdit::clear);
    emulation->addRow(tr("Boot ROMs location:"), folderRow);
    combo(emulation, tr("Rewinding duration:"), settings_.rewindSeconds,
          {{tr("Disabled"),0},{tr("10 seconds"),10},{tr("30 seconds"),30},{tr("1 minute"),60},{tr("2 minutes"),120},{tr("5 minutes"),300},{tr("10 minutes"),600}});
    combo(emulation, tr("Real Time Clock:"), settings_.rtc, {{tr("Sync to system clock"),0},{tr("Accurate emulation"),1}});
    auto *cap = check(emulation, tr("Cap turbo speed"), settings_.capTurbo);
    auto *turbo = slider(emulation, tr("Turbo speed:"), settings_.turboPercent, 100, 1000, "%");
    turbo->setEnabled(settings_.capTurbo); connect(cap, &QCheckBox::toggled, turbo, &QWidget::setEnabled);
    auto*hardware=combo(emulation, tr("Hardware:"), settings_.hardware,
          {{tr("Automatic (GB / GBC)"),0},{"Game Boy",1},{"Game Boy Pocket/Light",5},{"Game Boy Color",2},{"Super Game Boy",3},{"Game Boy Advance",4}});hardware->setObjectName("emulationHardware");
    auto *dmg = combo(emulation, tr("Game Boy revision:"), settings_.dmgModel,
          {{"DMG-CPU 0",0x000},{"DMG-CPU A",0x001},{"DMG-CPU B",GB_MODEL_DMG_B},{"DMG-CPU C",0x003}});
    unsupported(dmg,{0x000,0x001,0x003});
    combo(emulation, tr("Super Game Boy model:"), settings_.sgbModel,
          {{"Super Game Boy (NTSC)",GB_MODEL_SGB},{"Super Game Boy (PAL)",GB_MODEL_SGB_PAL},{"Super Game Boy 2",GB_MODEL_SGB2}});
    auto*cgbRevision=combo(emulation, tr("Game Boy Color revision:"), settings_.cgbModel,
          {{"CPU CGB 0",GB_MODEL_CGB_0},{"CPU CGB A",GB_MODEL_CGB_A},{"CPU CGB B",GB_MODEL_CGB_B},{"CPU CGB C",GB_MODEL_CGB_C},{"CPU CGB D",GB_MODEL_CGB_D},{"CPU CGB E",GB_MODEL_CGB_E}});cgbRevision->setObjectName("cgbRevision");
    auto *agb = combo(emulation, tr("Game Boy Advance revision:"), settings_.agbModel,
          {{"CPU AGB 0 (Early GBA)",0x206},{"CPU AGB A (GBA)",GB_MODEL_AGB_A},{"CPU AGB A (Game Boy Player)",GB_MODEL_GBP_A},
           {"CPU AGB B (GBA SP)",0x208},{"CPU AGB E (Late GBA SP)",0x209},{"CPU AGB E (Late Game Boy Player)",0x229}});
    unsupported(agb,{0x206,0x208,0x209,0x229});
    note(emulation, tr("Hardware changes reset affected games. Boot ROM changes take effect when you reopen a ROM."));

    auto *video = page(tr("Video"));
    combo(video, tr("Scaling filter:"), settings_.scaling, {{tr("Nearest neighbor"),0},{tr("Pixelated"),3},{tr("Bilinear (Blurry)"),1},
        {tr("Smooth bilinear (Less blurry)"),4},{tr("Monochrome LCD display"),5},{tr("LCD display"),2},{tr("CRT display"),6},{tr("Flat CRT display"),7},
        {"Scale2x",8},{"Scale4x",9},{tr("Anti-aliased Scale2x"),10},{tr("Anti-aliased Scale4x"),11},{"HQ2x",12},{tr("OmniScale (Any factor)"),13},
        {tr("OmniScale Legacy"),14},{tr("Anti-aliased OmniScale Legacy"),15}});
    combo(video, tr("Frame blending:"), settings_.blending, {{tr("Disabled"),0},{tr("Simple"),1},{tr("Accurate"),2}});
    check(video, tr("Apply filters to screenshots"), settings_.filterScreenshots);
    combo(video, tr("Color correction:"), settings_.colorCorrection,
          {{tr("Disabled"),0},{tr("Correct curves"),1},{tr("Modern — Balanced"),2},{tr("Modern — Boost contrast"),3},{tr("Reduce contrast"),4},{tr("Low contrast"),5},{tr("Modern — Accurate"),6}});
    slider(video, tr("Ambient light temperature:"), settings_.temperature, -100, 100, "");
    auto *palette = new QComboBox;
    auto fillPalettes = [this,palette] {
        QSignalBlocker blocker(palette);palette->clear();palette->addItem(tr("Greyscale"),0);palette->addItem(tr("Lime (Game Boy)"),1);palette->addItem(tr("Olive (Pocket)"),2);palette->addItem(tr("Teal (Light)"),3);
        palette->insertSeparator(palette->count());for(int i=0;i<settings_.palettes.size();++i)palette->addItem(settings_.palettes[i].name,i+4);
        palette->insertSeparator(palette->count());palette->addItem(tr("Custom…"),-1);palette->setCurrentIndex(palette->findData(settings_.palette));
    };
    fillPalettes();video->addRow(tr("Color palette for monochrome models:"),palette);
    connect(palette,&QComboBox::currentIndexChanged,this,[this,palette,fillPalettes] {
        if(palette->currentData().toInt()>=0){settings_.palette=palette->currentData().toInt();return;}
        PaletteEditor editor(settings_.palettes,qMax(0,settings_.palette-4),this);
        if(editor.exec()==QDialog::Accepted){settings_.palettes=editor.palettes();settings_.palette=editor.selectedIndex()+4;}
        fillPalettes();
    });
    combo(video, tr("Display border:"), settings_.border, {{tr("Only for Super Game Boy"),GB_BORDER_SGB},{tr("Never"),GB_BORDER_NEVER},{tr("Always"),GB_BORDER_ALWAYS}});
    check(video, tr("Keep aspect ratio"), settings_.aspect);
    check(video, tr("Force integer scale"), settings_.integerScale);
    check(video, tr("On-screen display (frame rate and events)"), settings_.osd);
    auto fontLabel = [](const QFont &font) { return QString("%1, %2 pt").arg(font.family()).arg(font.pointSizeF()); };
    auto *font = new QPushButton(fontLabel(settings_.monospace)); video->addRow(tr("Monospace font:"), font);
    connect(font, &QPushButton::clicked, this, [this, font, fontLabel] {
        bool ok = false; auto chosen = QFontDialog::getFont(&ok, settings_.monospace, this, tr("Monospace font"), QFontDialog::MonospacedFonts);
        if (ok) { settings_.monospace = chosen; font->setText(fontLabel(chosen)); }
    });
    note(video, tr("Accurate blending follows the LCD's alternating frame/scanline pattern. Super Game Boy uses simple blending."));

    auto *audio = page(tr("Audio"));
    auto*outputDevice=new QComboBox;outputDevice->setObjectName("audioOutputDevice");audio->addRow(tr("Output device:"),outputDevice);
    auto*mediaDevices=new QMediaDevices(this);
    auto refreshOutputs=[this,outputDevice]{
        QSignalBlocker block(outputDevice);outputDevice->clear();outputDevice->addItem(tr("Follow system"),QByteArray());
        for(const auto&device:QMediaDevices::audioOutputs())outputDevice->addItem(device.description(),device.id());
        int index=outputDevice->findData(settings_.audioOutputDevice);
        if(index<0){outputDevice->addItem(tr("Disconnected device (using system default)"),settings_.audioOutputDevice);index=outputDevice->count()-1;}
        outputDevice->setCurrentIndex(index);
    };
    refreshOutputs();connect(mediaDevices,&QMediaDevices::audioOutputsChanged,this,refreshOutputs);
    connect(outputDevice,&QComboBox::currentIndexChanged,this,[this,outputDevice]{settings_.audioOutputDevice=outputDevice->currentData().toByteArray();});
    combo(audio,tr("Sample rate:"),settings_.audioSampleRate,{{"44.1 kHz",44100},{"48 kHz",48000},{"96 kHz",96000}})->setObjectName("audioSampleRate");
    slider(audio, tr("Volume:"), settings_.volume, 0, 100, "%");
    combo(audio, tr("High-pass filter:"), settings_.highpass, {{tr("Disabled (keep DC offset)"),0},{tr("Accurate (hardware)"),1},{tr("Remove DC offset"),2}});
    slider(audio, tr("Interference volume:"), settings_.interference, 0, 100, "%");
    note(audio, tr("The hardware high-pass filter removes DC offset and attenuates bass. Remove DC offset preserves the waveform."));

    auto *controlsPage = page(tr("Controls"));
    auto *columns=new QWidget;auto *columnsLayout=new QHBoxLayout(columns);columnsLayout->setContentsMargins(0,0,0,0);
    auto *left=new QWidget,*right=new QWidget;auto *controls=new QFormLayout(left),*motionControls=new QFormLayout(right);
    for(auto *form:{controls,motionControls}){form->setVerticalSpacing(12);form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);}
    columnsLayout->addWidget(left,1);columnsLayout->addSpacing(20);columnsLayout->addWidget(right,1);
    columnsLayout->setAlignment(left,Qt::AlignTop);columnsLayout->setAlignment(right,Qt::AlignTop);controlsPage->addRow(columns);
    note(controls, tr("Four independent players for Super Game Boy games. Ordinary GB/GBC games use Player 1. Click a key binding; Delete clears it, Escape cancels. Ctrl+P pauses."));
    auto pads=std::make_shared<Controllers>();
    auto *player=new QComboBox;player->setObjectName("inputPlayer");
    for(int p=0;p<4;++p)player->addItem(tr("Player %1").arg(p+1));
    controls->addRow(tr("Control settings for:"),player);
    auto *device=new QComboBox;device->setObjectName("inputDevice");controls->addRow(tr("Controller:"),device);
    auto *table=new QTableWidget(17,3);table->setObjectName("inputBindings");
    table->setHorizontalHeaderLabels({tr("Action"),tr("Keyboard"),tr("Controller button")});
    table->verticalHeader()->hide();table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setMinimumHeight(520);table->setMaximumHeight(520);controls->addRow(table);
    const QStringList names{"Right","Left","Up","Down","A","B","Select","Start","Rapid A","Rapid B","Turbo","Rewind","Slow motion","1/8 speed","1/32 speed","Hotkey 1","Hotkey 2"};
    auto rebuild=[this,player,device,table,pads,names]{
        const int p=player->currentIndex();QSignalBlocker blocked(device);device->clear();
        device->addItem(tr("Automatic (next available controller)"),QString());device->addItem(tr("None"),"none");
        for(const auto &pad:pads->logicalDevices(settings_))device->addItem(pad.name,pad.id);
        int index=device->findData(settings_.controllers[p]);
        if(index<0){device->addItem(tr("Disconnected controller"),settings_.controllers[p]);index=device->count()-1;}device->setCurrentIndex(index);
        table->clearContents();
        for(int i=0;i<17;++i){auto*item=new QTableWidgetItem(tr(names[i].toUtf8().constData()));item->setFlags(Qt::ItemIsEnabled);table->setItem(i,0,item);
            if(i<KeyboardActionCount)table->setCellWidget(i,1,new KeyButton(settings_.playerKeys(p)[i],table));
            if(i==13||i==14)continue;const int controllerIndex=i>=15?i-2:i;
            auto*binding=new QComboBox;binding->setContextMenuPolicy(Qt::CustomContextMenu);connect(binding,&QWidget::customContextMenuRequested,this,[binding](QPoint point){QMenu menu(binding);auto*clear=menu.addAction(tr("Clear binding"));clear->setEnabled(binding->currentData().toInt()!=-1);if(menu.exec(binding->mapToGlobal(point))==clear)binding->setCurrentIndex(binding->findData(-1));});binding->addItem(tr("Unassigned"),-1);
            for(int b=0;b<26;++b)binding->addItem(Controllers::buttonName(b),b);
            binding->addItem(Controllers::buttonName(30),30);binding->addItem(Controllers::buttonName(31),31);
            binding->setCurrentIndex(binding->findData(settings_.controllerButtons[p][controllerIndex]));table->setCellWidget(i,2,binding);
            connect(binding,&QComboBox::currentIndexChanged,this,[this,p,controllerIndex,binding]{settings_.controllerButtons[p][controllerIndex]=binding->currentData().toInt();});
        }
    };
    rebuild();connect(player,&QComboBox::currentIndexChanged,this,[rebuild]{rebuild();});
    connect(device,&QComboBox::currentIndexChanged,this,[this,device,player]{settings_.controllers[player->currentIndex()]=device->currentData().toString();});
    auto *configure=new QPushButton(tr("Configure a controller…"));controls->addRow(configure);
    const auto controllerNames=names.mid(0,13)+names.mid(15,2);
    connect(configure,&QPushButton::clicked,this,[this,pads,player,rebuild,controllerNames]{
        QDialog wizard(this);wizard.setWindowTitle(tr("Configure controller"));auto*l=new QVBoxLayout(&wizard);
        auto*prompt=new QLabel;prompt->setWordWrap(true);l->addWidget(prompt);
        auto*skip=new QPushButton(tr("Skip / unassign"));l->addWidget(skip);auto*cancel=new QPushButton(tr("Cancel"));l->addWidget(cancel);
        const int p=player->currentIndex();auto mapping=settings_.controllerButtons[p];int action=0;QString selected;
        auto advance=[&]{if(++action==15)wizard.accept();else prompt->setText(tr("Press controller button for %1 (%2/15)").arg(tr(controllerNames[action].toUtf8().constData())).arg(action+1));};
        prompt->setText(tr("Press controller button for %1 (1/15)").arg(tr(controllerNames[0].toUtf8().constData())));
        connect(skip,&QPushButton::clicked,&wizard,[&]{mapping[action]=-1;advance();});connect(cancel,&QPushButton::clicked,&wizard,&QDialog::reject);
        QTimer timer;quint32 previous=0;for(auto&pad:pads->logicalDevices(settings_))previous|=pad.buttons;
        connect(&timer,&QTimer::timeout,&wizard,[&]{pads->poll();quint32 current=0;
            for(auto&pad:pads->logicalDevices(settings_)){if(!selected.isEmpty()&&selected!=pad.id)continue;current|=pad.buttons;auto pressed=pad.buttons&~previous;
                if(pressed){int b=0;while(!(pressed&(1u<<b)))++b;selected=pad.id;mapping[action]=b;advance();break;}}
            previous=current;
        });timer.start(16);
        if(wizard.exec()==QDialog::Accepted){settings_.controllers[p]=selected;settings_.controllerButtons[p]=mapping;rebuild();}
    });
    auto*refresh=new QTimer(this);connect(refresh,&QTimer::timeout,this,[this,pads,rebuild]{
        QStringList before;for(auto&d:pads->logicalDevices(settings_))before<<d.id;pads->poll();autoPairJoyCons(pads->devices(),settings_);QStringList after;for(auto&d:pads->logicalDevices(settings_))after<<d.id;
        if(before!=after)rebuild();
    });refresh->start(500);
    check(motionControls,tr("Prefer joysticks over motion sensors"),settings_.preferJoystick);
    check(motionControls,tr("Allow mouse controls for motion games"),settings_.allowMouse);
    slider(motionControls,tr("Motion sensitivity:"),settings_.motionSensitivity,25,200,"%");
    slider(motionControls,tr("Stick deadzone:"),settings_.deadzone,5,80,"%");
    for(int i=0;i<2;++i)combo(motionControls,tr("Controller Hotkey %1 action:").arg(i+1),settings_.controllerHotkeys[i],
        // Cocoa's hotkey choices (Preferences.xib): pause, reset, mute, save/load slots 1-10. Stored ids 0-5 are unchanged.
        {{tr("None"),0},{tr("Pause / resume"),1},{tr("Reset"),2},{tr("Mute / unmute"),6},
         {tr("Quick save (slot 1)"),3},{tr("Quick save (slot %1)").arg(2),7},{tr("Quick save (slot %1)").arg(3),8},{tr("Quick save (slot %1)").arg(4),9},{tr("Quick save (slot %1)").arg(5),10},
         {tr("Quick save (slot %1)").arg(6),11},{tr("Quick save (slot %1)").arg(7),12},{tr("Quick save (slot %1)").arg(8),13},{tr("Quick save (slot %1)").arg(9),14},{tr("Quick save (slot %1)").arg(10),15},
         {tr("Quick load (slot 1)"),4},{tr("Quick load (slot %1)").arg(2),16},{tr("Quick load (slot %1)").arg(3),17},{tr("Quick load (slot %1)").arg(4),18},{tr("Quick load (slot %1)").arg(5),19},
         {tr("Quick load (slot %1)").arg(6),20},{tr("Quick load (slot %1)").arg(7),21},{tr("Quick load (slot %1)").arg(8),22},{tr("Quick load (slot %1)").arg(9),23},{tr("Quick load (slot %1)").arg(10),24},
         {tr("Screenshot"),5}});
    combo(motionControls,tr("Enable rumble:"),settings_.rumbleMode,{{tr("Disabled"),0},{tr("For rumble-enabled Game Paks"),1},{tr("For all games"),2}});
    slider(motionControls,tr("Rumble strength:"),settings_.rumbleStrength,0,100,"%");
    check(motionControls,tr("Analog turbo and slow-motion controls (triggers)"),settings_.analogControls);
    check(motionControls,tr("Use joysticks as faux analog controls"),settings_.fauxAnalog);
    check(motionControls,tr("Enable controllers while in background"),settings_.backgroundControllers);
    note(motionControls,tr("Motion cartridges use the left stick, or the controller accelerometer when available. Mouse control starts enabled. Controller buttons switch to the controller; click the game to return to mouse control. Left-click also presses A. Rumble requires a compatible controller. "));
    auto*arrange=new QPushButton(tr("Arrange Joy-Cons…"));arrange->setObjectName("arrangeJoyCons");motionControls->addRow(arrange);
    connect(arrange,&QPushButton::clicked,this,[this,pads,rebuild]{JoyConDialog dialog(settings_,pads,this);if(dialog.exec()==QDialog::Accepted){settings_=dialog.settings();rebuild();}});
    if(!pads->error().isEmpty())note(motionControls,tr("Controller initialization failed: %1").arg(pads->error()));

    auto*camera=page(tr("Camera"));
    auto*source=combo(camera,tr("Image source:"),settings_.cameraMode,{{tr("Live camera"),1},{tr("Image file"),2},{tr("Noise (no camera)"),0}});source->setObjectName("cameraSource");
    auto*cameraDeviceBox=new QComboBox;cameraDeviceBox->setObjectName("cameraDevice");cameraDeviceBox->addItem(tr("System default camera"),QByteArray());
    for(const auto&input:QMediaDevices::videoInputs())cameraDeviceBox->addItem(input.description(),input.id());
    int selected=cameraDeviceBox->findData(settings_.cameraDevice);if(selected<0){cameraDeviceBox->addItem(tr("Disconnected camera"),settings_.cameraDevice);selected=cameraDeviceBox->count()-1;}cameraDeviceBox->setCurrentIndex(selected);
    connect(cameraDeviceBox,&QComboBox::currentIndexChanged,this,[this,cameraDeviceBox]{settings_.cameraDevice=cameraDeviceBox->currentData().toByteArray();});camera->addRow(tr("Device:"),cameraDeviceBox);
    auto*imageRow=new QWidget;auto*imageLayout=new QHBoxLayout(imageRow);imageLayout->setContentsMargins(0,0,0,0);
    auto*imagePath=new QLineEdit(settings_.cameraImage);imagePath->setObjectName("cameraImage");auto*chooseImage=new QPushButton(tr("Choose image…"));imageLayout->addWidget(imagePath,1);imageLayout->addWidget(chooseImage);camera->addRow(tr("Image:"),imageRow);
    connect(imagePath,&QLineEdit::textChanged,this,[this](const QString&path){settings_.cameraImage=path;});
    connect(chooseImage,&QPushButton::clicked,this,[this,imagePath]{auto path=QFileDialog::getOpenFileName(this,tr("Camera image"),imagePath->text(),tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All files (*)"));if(!path.isEmpty())imagePath->setText(path);});
    check(camera,tr("Mirror horizontally"),settings_.cameraMirror);
    auto enableCamera=[this,cameraDeviceBox,imageRow]{cameraDeviceBox->setEnabled(settings_.cameraMode==1);imageRow->setEnabled(settings_.cameraMode==2);};enableCamera();connect(source,&QComboBox::currentIndexChanged,this,enableCamera);
    note(camera,tr("The camera starts only when a Game Boy Camera game requests a picture. Pausing or closing the game stops capture. Images are cropped to the center and converted to grayscale; the emulated camera handles exposure and dithering. If access fails, the game receives a blank image instead of waiting indefinitely."));
    auto*general=page(tr("General"));
    combo(general,tr("Screenshot scale:"),settings_.screenshotScale,{{"1×",1},{"2×",2},{"4×",4},{"8×",8},{"16×",16}})->setObjectName("screenshotScale");
    auto*language=new QComboBox;language->setObjectName("interfaceLanguage");language->addItem("English","en");language->addItem(QString::fromUtf8("简体中文"),"zh_CN");language->setCurrentIndex(language->findData(settings_.language));
    connect(language,&QComboBox::currentIndexChanged,this,[this,language]{settings_.language=language->currentData().toString();settings_.languageChosen=true;});auto*languageRow=new QWidget;auto*languageLayout=new QHBoxLayout(languageRow);languageLayout->setContentsMargins(0,0,0,0);languageLayout->addWidget(language,1);
    general->addRow(tr("Language:"),languageRow);note(general,tr("Language changes take effect the next time you launch the app."));
    auto*theme=combo(general,tr("Theme:"),settings_.theme,{{tr("Follow system"),0},{tr("Light"),1},{tr("Dark"),2}});theme->setObjectName("interfaceTheme");
    if(isWindows10Platform()){
        const int darkIndex=theme->findData(2);
        if(auto*model=qobject_cast<QStandardItemModel*>(theme->model());model&&darkIndex>=0)if(auto*item=model->item(darkIndex)){
            item->setEnabled(false);
            item->setToolTip(tr("Dark theme is temporarily unavailable on Windows 10."));
        }
        theme->setToolTip(tr("Windows 10 uses the light theme, including when following system appearance."));
        note(general,tr("Dark theme is unavailable on Windows 10. The app uses Light even when following system appearance."));
        if(settings_.theme!=1){settings_.theme=1;theme->setCurrentIndex(theme->findData(1));}
    }
    const int originalTheme=settings_.theme;
    connect(theme,&QComboBox::currentIndexChanged,this,[this]{applyInterfaceTheme(settings_.theme);});
    connect(this,&QDialog::rejected,this,[originalTheme]{applyInterfaceTheme(originalTheme);});
    tabs->setCurrentIndex(initialTab);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        for (int p=0;p<4;++p) for (unsigned i = 0; i < KeyboardActionCount; ++i) {
            const auto key = settings_.playerKeys(p)[i];
            if (!key) continue;
            if (key == Qt::Key_Control || key == Qt::Key_Meta) {
                QMessageBox::warning(this, tr("Reserved key"), tr("Control and Meta are reserved for application shortcuts.")); return;
            }
            for(int q=0;q<=p;++q) for (unsigned j = 0; j < (q==p?i:KeyboardActionCount); ++j) if (settings_.playerKeys(q)[j] == key) {
                QMessageBox::warning(this, tr("Duplicate binding"), tr("Assign a different key to each action.")); return;
            }
        }
        accept();
    });
}
