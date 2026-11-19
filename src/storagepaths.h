#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#include <string>
// Same declaration as <windows.h>, without pulling its macros into every includer.
extern "C" __declspec(dllimport) unsigned long __stdcall GetLongPathNameW(const wchar_t*,wchar_t*,unsigned long);
// QFileInfo::canonicalFilePath() keeps 8.3 short names (C:/PROGRA~1/...), so one file could have
// two identities. Expand them for an existing path, or for the existing parent of a new file.
inline QString longPathName(const QString&path){
    auto expand=[](const QString&value){const auto native=QDir::toNativeSeparators(value).toStdWString();
        const auto size=GetLongPathNameW(native.c_str(),nullptr,0);if(!size)return QString();
        std::wstring buffer(size,L'\0');const auto length=GetLongPathNameW(native.c_str(),buffer.data(),size);
        return length&&length<size?QDir::fromNativeSeparators(QString::fromWCharArray(buffer.c_str(),int(length))):QString();};
    if(auto full=expand(path);!full.isEmpty())return full;
    const QFileInfo info(path);if(auto parent=expand(info.absolutePath());!parent.isEmpty())return QDir(parent).filePath(info.fileName());
    return path;
}
#endif
#ifdef Q_OS_MACOS
#include <unistd.h>
// APFS/HFS+ volumes are case-insensitive by default, and Qt's canonical paths keep the spelling they were
// given, so "Game.gb" and "game.gb" would be two identities for one file. Ask the volume (or the parent of
// a file not yet created); case-sensitive volumes keep exact spelling.
inline bool caseInsensitiveVolume(const QString&path){
    const QFileInfo info(path);const auto existing=info.exists()?info.absoluteFilePath():info.absolutePath();
    return pathconf(QFile::encodeName(existing).constData(),_PC_CASE_SENSITIVE)==0;
}
#endif
inline QString romPathIdentity(const QString&path){
    auto value=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    value=QDir::cleanPath(longPathName(value)).toCaseFolded();
#elif defined(Q_OS_MACOS)
    if(caseInsensitiveVolume(value))value=value.toCaseFolded();
#endif
    return value;
}
inline QString romStorageKey(const QString&path){return "romStorage/"+QString::fromLatin1(QCryptographicHash::hash(romPathIdentity(path).toUtf8(),QCryptographicHash::Sha256).toHex());}
inline bool isolatedRomStorage(const QString&path){return QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").value(romStorageKey(path),false).toBool();}
inline bool isolateRomStorage(const QString&path){
    QSettings store(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt");const auto key=romStorageKey(path);const bool existed=store.contains(key);const auto previous=store.value(key);
    store.setValue(key,true);store.sync();if(store.status()==QSettings::NoError)return true;
    if(existed)store.setValue(key,previous);else store.remove(key);store.sync();return false;
}
inline QString batterySavePath(const QString&romPath){
    const QFileInfo info(romPath);
    if(isolatedRomStorage(romPath))return info.absoluteFilePath()+".sav";
    return info.isDir()?QDir(info.absoluteFilePath()).filePath("cartridge.sav"):
        QDir(info.absolutePath()).filePath(info.completeBaseName()+".sav");
}
inline QString stateSavePath(const QString&romPath,int slot){
    const QFileInfo info(romPath);
    const auto base=isolatedRomStorage(romPath)?info.absoluteFilePath():info.isDir()?QDir(info.absoluteFilePath()).filePath("cartridge"):
        QDir(info.absolutePath()).filePath(info.completeBaseName());
    return base+QString(".s%1").arg(slot);
}
inline QString cheatSavePath(const QString&base){
    const QFileInfo info(base);
    if(isolatedRomStorage(base))return info.absoluteFilePath()+".cht";
    if(info.fileName()=="cartridge"&&info.dir().dirName().endsWith(".gbcart",Qt::CaseInsensitive))return isolatedRomStorage(info.dir().absolutePath())?info.dir().absolutePath()+".cht":info.dir().filePath("cheats.cht");
    return QDir(info.absolutePath()).filePath(info.completeBaseName()+".cht");
}

// Resolve existing symlinks, including the parent of a save not created yet.
inline QString storageIdentity(const QString &path){
    const QFileInfo info(path);
    QString resolved=info.canonicalFilePath();
    if(resolved.isEmpty()){
        const QString parent=info.dir().canonicalPath();
        resolved=parent.isEmpty()?info.absoluteFilePath():QDir(parent).filePath(info.fileName());
    }
    resolved=QDir::cleanPath(resolved);
#ifdef Q_OS_WIN
    resolved=QDir::cleanPath(longPathName(resolved)).toCaseFolded();
#elif defined(Q_OS_MACOS)
    if(caseInsensitiveVolume(resolved))resolved=resolved.toCaseFolded();
#endif
    return resolved;
}
