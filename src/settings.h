#pragma once
#include <Core/gb.h>
#include <QFont>
#include <QString>
#include <QMap>
#include <QStringList>
#include <array>
#include "palettes.h"

inline constexpr int KeyboardActionCount=15;
struct AppSettings {
    QString bootFolder;
    QString language="system";
    int theme=0; // 0 system, 1 light, 2 dark
    bool developerMode=false;
    int audioSampleRate=96000;
    int hardware = 0, dmgModel = GB_MODEL_DMG_B, cgbModel = GB_MODEL_CGB_E;
    int sgbModel = GB_MODEL_SGB2, agbModel = GB_MODEL_AGB_A;
    int rewindSeconds = 120, rtc = GB_RTC_MODE_SYNC_TO_HOST;
    int cpuFrequency = 0; // 0 = automatic, 1 = 4 MHz, 2 = 8 MHz
    bool capTurbo = false;
    int turboPercent = 200;
    int scaling = 0, blending = 0, colorCorrection = GB_COLOR_CORRECTION_MODERN_ACCURATE;
    int temperature = 0, palette = 1, border = GB_BORDER_NEVER;
    QVector<UserPalette> palettes = defaultPalettes();
    bool filterScreenshots = false, aspect = true, integerScale = true, osd = false;
    QFont monospace{"Consolas", 12};
    int cameraMode=1;
    QByteArray cameraDevice;
    QString cameraImage;
    bool cameraMirror=false;
    int volume = 100, highpass = GB_HIGHPASS_OFF, interference = 0;
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
    GB_model_t model(bool color) const;
    QString bootName(bool color) const;
    static AppSettings load();
    QString save() const;
};
