#include "settings.h"
#include <QSettings>
#include <QSet>
#include <algorithm>

GB_model_t AppSettings::model(int family) const
{
    switch (hardware) {
    case 1: return static_cast<GB_model_t>(dmgModel);
    case 2: return static_cast<GB_model_t>(cgbModel);
    case 3: return static_cast<GB_model_t>(sgbModel);
    case 4: return static_cast<GB_model_t>(agbModel);
    case 5: return GB_MODEL_MGB;
    default: return static_cast<GB_model_t>(family == 2 ? sgbModel : family ? cgbModel : dmgModel);
    }
}
QString AppSettings::bootName(int family) const
{
    switch (model(family)) {
    case GB_MODEL_MGB: return "mgb_boot.bin";
    case GB_MODEL_CGB_0: return "cgb0_boot.bin";
    case GB_MODEL_AGB_A: case GB_MODEL_GBP_A: return "agb_boot.bin";
    case GB_MODEL_SGB: case GB_MODEL_SGB_PAL: return "sgb_boot.bin";
    case GB_MODEL_SGB2: return "sgb2_boot.bin";
    case GB_MODEL_DMG_B: return "dmg_boot.bin";
    default: return "cgb_boot.bin";
    }
}

namespace {
// Debug choices live only in this process; new windows share the active session.
bool sessionDeveloperMode=false;
int sessionCpuFrequency=0;
QSettings storage() { return QSettings(QSettings::IniFormat, QSettings::UserScope, "SameBoyQt", "SameBoyQt"); }
}
AppSettings AppSettings::load()
{
    AppSettings s;s.developerMode=sessionDeveloperMode;s.cpuFrequency=sessionCpuFrequency;
    auto store = storage();
#define READ(field) s.field = store.value(#field, s.field).value<decltype(s.field)>();
    READ(audioOutputDevice) READ(screenshotScale) READ(languageChosen) READ(language) READ(theme) READ(audioSampleRate) READ(cameraMode) READ(cameraDevice) READ(cameraImage) READ(cameraMirror)
    READ(bootFolder) READ(hardware) READ(dmgModel) READ(cgbModel) READ(sgbModel) READ(agbModel)
    READ(rewindSeconds) READ(rtc) READ(capTurbo) READ(turboPercent) READ(scaling) READ(blending)
    READ(colorCorrection) READ(temperature) READ(palette) READ(border) READ(filterScreenshots)
    READ(aspect) READ(integerScale) READ(osd) READ(monospace) READ(volume) READ(highpass) READ(interference) READ(preferJoystick) READ(allowMouse) READ(backgroundControllers) READ(analogControls) READ(fauxAnalog)
    READ(deadzone) READ(motionSensitivity) READ(rumbleMode) READ(rumbleStrength) READ(joyConAutoPair) READ(joyConHorizontal) READ(joyConSingles)
#undef READ
    const auto pairs=store.value("joyConPairs").toMap();QSet<QString> paired;
    for(auto it=pairs.begin();it!=pairs.end();++it){const auto right=it.value().toString();
        if(it.key().isEmpty()||right.isEmpty()||it.key()==right||paired.contains(it.key())||paired.contains(right))continue;
        s.joyConPairs.insert(it.key(),right);paired.insert(it.key());paired.insert(right);
    }
    const auto grips=store.value("joyConGrips").toMap();
    for(auto it=grips.begin();it!=grips.end();++it)if(!it.key().isEmpty()&&(it.value().toInt()==0||it.value().toInt()==1))s.joyConGrips.insert(it.key(),it.value().toInt());
    for (unsigned i = 0; i < s.keys.size(); ++i) s.keys[i] = store.value(QString("keys/%1").arg(i), s.keys[i]).toInt();
    for(int p=0;p<4;++p) {
        s.controllers[p]=store.value(QString("controllers/%1/device").arg(p)).toString();
        for(int i=0;i<15;++i) s.controllerButtons[p][i]=std::clamp(store.value(QString("controllers/%1/button%2").arg(p).arg(i),s.controllerButtons[p][i]).toInt(),-1,31);
        if(p)for(int i=0;i<KeyboardActionCount;++i)s.playerKeys(p)[i]=store.value(QString("player%1/keys/%2").arg(p+1).arg(i),0).toInt();
    }
    for(int i=0;i<2;++i)s.controllerHotkeys[i]=std::clamp(store.value(QString("controllerHotkey%1").arg(i),0).toInt(),0,24);
    s.deadzone=std::clamp(s.deadzone,5,80);s.motionSensitivity=std::clamp(s.motionSensitivity,25,200);
    s.rumbleMode=std::clamp(s.rumbleMode,0,2);s.rumbleStrength=std::clamp(s.rumbleStrength,0,100);
    if (store.contains("palettes")) {
        QVector<UserPalette> palettes;
        if (deserializePalettes(store.value("palettes").toByteArray(),palettes) && !palettes.isEmpty()) s.palettes=std::move(palettes);
    }
    // Ignore invalid persisted enum values rather than passing them to the C core.
    const bool explicitLanguage=s.language=="en"||s.language=="zh_CN";
    // Preserve explicit choices from older versions, but ask former "system" users.
    if(!store.contains("languageChosen"))s.languageChosen=store.contains("language")&&explicitLanguage;
    if(!explicitLanguage){s.language="en";s.languageChosen=false;}
    if(s.screenshotScale!=1&&s.screenshotScale!=2&&s.screenshotScale!=4&&s.screenshotScale!=8&&s.screenshotScale!=16)s.screenshotScale=1;
    if(s.audioSampleRate!=44100&&s.audioSampleRate!=48000&&s.audioSampleRate!=96000)s.audioSampleRate=96000;
    s.theme=std::clamp(s.theme,0,2);
    s.cameraMode=std::clamp(s.cameraMode,0,2);
    s.hardware = std::clamp(s.hardware, 0, 5);
    if (s.dmgModel == GB_MODEL_MGB && (s.hardware == 0 || s.hardware == 1)) s.hardware = 5;
    s.dmgModel = GB_MODEL_DMG_B;
    if (s.cgbModel < GB_MODEL_CGB_0 || s.cgbModel > GB_MODEL_CGB_E) s.cgbModel = GB_MODEL_CGB_E;
    if (s.sgbModel != GB_MODEL_SGB && s.sgbModel != GB_MODEL_SGB_PAL && s.sgbModel != GB_MODEL_SGB2) s.sgbModel = GB_MODEL_SGB2;
    if (s.agbModel != GB_MODEL_AGB_A && s.agbModel != GB_MODEL_GBP_A) s.agbModel = GB_MODEL_AGB_A;
    s.rewindSeconds = std::clamp(s.rewindSeconds, 0, 600);
    s.cpuFrequency = std::clamp(s.cpuFrequency, 0, 2);
    s.rtc = std::clamp(s.rtc, 0, 1); s.turboPercent = std::clamp(s.turboPercent, 100, 1000);
    s.scaling = std::clamp(s.scaling, 0, 15); s.blending = std::clamp(s.blending, 0, 2);
    s.colorCorrection = std::clamp(s.colorCorrection, 0, 6); s.temperature = std::clamp(s.temperature, -100, 100);
    s.palette = std::clamp(s.palette, 0, int(s.palettes.size()+3)); s.border = std::clamp(s.border, 0, 2);
    s.volume = std::clamp(s.volume, 0, 100); s.highpass = std::clamp(s.highpass, 0, 2);
    s.interference = std::clamp(s.interference, 0, 100);
    return s;
}
QString AppSettings::save() const
{
    auto store = storage();store.setFallbacksEnabled(false);store.sync();
    QMap<QString,QVariant> original;
    for(const auto&key:store.allKeys())original.insert(key,store.value(key));
    const auto failure=QObject::tr("Could not save preferences to %1.").arg(store.fileName());
    if(store.status()!=QSettings::NoError)return failure;
#define WRITE(field) store.setValue(#field, field);
    WRITE(audioOutputDevice) WRITE(screenshotScale) WRITE(languageChosen) WRITE(language) WRITE(theme) WRITE(audioSampleRate) WRITE(cameraMode) WRITE(cameraDevice) WRITE(cameraImage) WRITE(cameraMirror)
    WRITE(bootFolder) WRITE(hardware) WRITE(dmgModel) WRITE(cgbModel) WRITE(sgbModel) WRITE(agbModel)
    WRITE(rewindSeconds) WRITE(rtc) WRITE(capTurbo) WRITE(turboPercent) WRITE(scaling) WRITE(blending)
    WRITE(colorCorrection) WRITE(temperature) WRITE(palette) WRITE(border) WRITE(filterScreenshots)
    WRITE(aspect) WRITE(integerScale) WRITE(osd) WRITE(monospace) WRITE(volume) WRITE(highpass) WRITE(interference) WRITE(preferJoystick) WRITE(allowMouse) WRITE(backgroundControllers) WRITE(analogControls) WRITE(fauxAnalog)
    WRITE(deadzone) WRITE(motionSensitivity) WRITE(rumbleMode) WRITE(rumbleStrength) WRITE(joyConAutoPair) WRITE(joyConHorizontal) WRITE(joyConSingles)
#undef WRITE
    // Discard values persisted by older versions.
    store.remove("developerMode");store.remove("cpuFrequency");
    QVariantMap pairs,grips;
    for(auto it=joyConPairs.begin();it!=joyConPairs.end();++it)pairs.insert(it.key(),it.value());
    for(auto it=joyConGrips.begin();it!=joyConGrips.end();++it)grips.insert(it.key(),it.value());
    store.setValue("joyConPairs",pairs);store.setValue("joyConGrips",grips);
    for (unsigned i = 0; i < keys.size(); ++i) store.setValue(QString("keys/%1").arg(i), keys[i]);
    for(int p=0;p<4;++p) {
        store.setValue(QString("controllers/%1/device").arg(p),controllers[p]);
        for(int i=0;i<15;++i)store.setValue(QString("controllers/%1/button%2").arg(p).arg(i),controllerButtons[p][i]);
        if(p)for(int i=0;i<KeyboardActionCount;++i)store.setValue(QString("player%1/keys/%2").arg(p+1).arg(i),playerKeys(p)[i]);
    }
    for(int i=0;i<2;++i)store.setValue(QString("controllerHotkey%1").arg(i),controllerHotkeys[i]);
    store.setValue("palettes",serializePalettes(palettes));
    store.sync();
    if(store.status()==QSettings::NoError){sessionDeveloperMode=developerMode;sessionCpuFrequency=cpuFrequency;return {};}
    // Failed sync leaves mutations in QSettings' shared cache. Restore both the
    // cached values and pending writes so a later sync cannot commit rejected data.
    store.clear();for(auto it=original.cbegin();it!=original.cend();++it)store.setValue(it.key(),it.value());
    store.sync();return failure;
}
