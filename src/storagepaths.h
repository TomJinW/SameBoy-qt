#pragma once
#include <QDir>
#include <QFileInfo>
inline QString batterySavePath(const QString&romPath){
    const QFileInfo info(romPath);
    return info.isDir()?QDir(info.absoluteFilePath()).filePath("cartridge.sav"):
        QDir(info.absolutePath()).filePath(info.completeBaseName()+".sav");
}
inline QString stateSavePath(const QString&romPath,int slot){
    const QFileInfo info(romPath);
    const auto base=info.isDir()?QDir(info.absoluteFilePath()).filePath("cartridge"):
        QDir(info.absolutePath()).filePath(info.completeBaseName());
    return base+QString(".s%1").arg(slot);
}
inline QString cheatSavePath(const QString&base){
    const QFileInfo info(base);
    if(info.fileName()=="cartridge"&&info.dir().dirName().endsWith(".gbcart",Qt::CaseInsensitive))return info.dir().filePath("cheats.cht");
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
    resolved=resolved.toCaseFolded();
#endif
    return resolved;
}
