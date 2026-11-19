#pragma once
#include <Core/gb.h>
#include <QFont>
#include <QString>
#include <QMap>
#include <QStringList>
#include <array>
#include "palettes.h"

inline constexpr int KeyboardActionCount=15;
// Cocoa Document.m bestModelForROM: CGB features -> CGB; SGB features -> SGB; Nintendo-licensed
// (likely to have CGB boot ROM palettes) -> CGB; otherwise DMG. Returns 0 DMG, 1 CGB, 2 SGB.
inline int romModelFamily(const QByteArray&rom){
    if(rom.size()<0x150)return 1;
    const auto at=[&](int offset){return uint8_t(rom[offset]);};
    if(at(0x143)&0x80)return 1;
    if(at(0x146)==3)return 2;
    if(at(0x14b)==1)return 1;
    if(at(0x14b)==0x33&&at(0x144)=='0'&&at(0x145)=='1')return 1;
    return 0;
}
struct AppSettings {
    QString bootFolder;
    QString language="en";
    bool languageChosen=false;
    int theme=0; // 0 system, 1 light, 2 dark
    bool developerMode=false; // Session only; never persisted.
    int audioSampleRate=96000;
    QByteArray audioOutputDevice; // Empty follows the system default.
    int screenshotScale=1;
    int hardware = 0, dmgModel = GB_MODEL_DMG_B, cgbModel = GB_MODEL_CGB_E;
    int sgbModel = GB_MODEL_SGB2, agbModel = GB_MODEL_AGB_A;
    int rewindSeconds = 120, rtc = GB_RTC_MODE_SYNC_TO_HOST;
    int cpuFrequency = 0; // Session only: 0 = automatic, 1 = 4 MHz, 2 = 8 MHz
    bool capTurbo = false;
    int turboPercent = 200;
    // Output defaults follow Cocoa's registerDefaults (GBApp.m): accurate blending, modern balanced
    // colour correction, an unregistered GBBorderMode (0 = SGB borders) and the accurate highpass.
    int scaling = 0, blending = 2, colorCorrection = GB_COLOR_CORRECTION_MODERN_BALANCED;
    int temperature = 0, palette = 1, border = GB_BORDER_SGB;
    QVector<UserPalette> palettes = defaultPalettes();
    bool filterScreenshots = false, aspect = true, integerScale = true, osd = false;
    QFont monospace{"Consolas", 12};
    int cameraMode=1;
    QByteArray cameraDevice;
    QString cameraImage;
    bool cameraMirror=false;
    int volume = 100, highpass = GB_HIGHPASS_ACCURATE, interference = 0;
    std::array<int, KeyboardActionCount> keys{Qt::Key_Right, Qt::Key_Left, Qt::Key_Up, Qt::Key_Down,
        Qt::Key_X, Qt::Key_Z, Qt::Key_Backspace, Qt::Key_Return,
        Qt::Key_A, Qt::Key_S, Qt::Key_Tab, Qt::Key_R, Qt::Key_Shift};
    // Player 1 keeps the original keys field for settings compatibility.
    std::array<std::array<int,KeyboardActionCount>,3> extraKeys{};
    std::array<QString,4> controllers{}; // Empty = automatic slot; "none" disables it.
    std::array<std::array<int,15>,4> controllerButtons{{
        {{14,13,11,12,0,1,4,6,2,3,10,9,8,7,5}},
        {{14,13,11,12,0,1,4,6,2,3,10,9,8,7,5}},
        {{14,13,11,12,0,1,4,6,2,3,10,9,8,7,5}},
        {{14,13,11,12,0,1,4,6,2,3,10,9,8,7,5}}}};
    bool preferJoystick = false, allowMouse = true, backgroundControllers = false;
    bool analogControls = false, fauxAnalog = false;
    int deadzone = 20, motionSensitivity = 100, rumbleMode = GB_RUMBLE_CARTRIDGE_ONLY, rumbleStrength = 100;
    bool joyConAutoPair = true, joyConHorizontal = true;
    QMap<QString,QString> joyConPairs; // Stable left ID -> stable right ID.
    QMap<QString,int> joyConGrips; // Missing = default; 0 = vertical; 1 = horizontal.
    QStringList joyConSingles; // Explicitly separated devices stay single until paired again.
    std::array<int,2> controllerHotkeys{0,0};
    std::array<int,KeyboardActionCount> &playerKeys(int player) { return player ? extraKeys[player-1] : keys; }
    const std::array<int,KeyboardActionCount> &playerKeys(int player) const { return player ? extraKeys[player-1] : keys; }
    // family: the ROM's automatic choice (romModelFamily), 0 = DMG, 1 = CGB, 2 = SGB.
    GB_model_t model(int family) const;
    QString bootName(int family) const;
    static AppSettings load();
    QString save() const;
};
