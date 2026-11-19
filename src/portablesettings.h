#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryFile>

// Portable mode keeps the INI beside the executable (<exe dir>/config/SameBoyQt/SameBoyQt.ini) instead of
// %APPDATA%. It must run after QCoreApplication exists and before the first QSettings is constructed, since
// QSettings caches its search path. If the folder is not writable (e.g. under Program Files) the per-user
// location stays in use. A first portable launch copies an existing per-user INI so settings are not lost.
inline void enablePortableSettings(){
    const auto userFile=QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").fileName();
    const auto root=QDir(QCoreApplication::applicationDirPath()).filePath("config");
    const auto folder=QDir(root).filePath("SameBoyQt");
    if(!QDir().mkpath(folder))return;
    {QTemporaryFile probe(QDir(folder).filePath("write-test-XXXXXX"));if(!probe.open())return;}
    const auto portableFile=QDir(folder).filePath("SameBoyQt.ini");
    if(!QFile::exists(portableFile)&&QFile::exists(userFile))QFile::copy(userFile,portableFile);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
}
