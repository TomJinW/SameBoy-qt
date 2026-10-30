#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

// Cocoa's rom.gbl is UTF-8 text containing ordered URL/path fallbacks. Accept
// its relative URLs, file URLs, and the raw absolute paths written by older Qt
// builds. The first existing local reference is the cartridge to open.
inline QString cartridgeInstanceCandidate(QString line,const QString&directory)
{
    if(line.endsWith(QLatin1Char('\r')))line.chop(1);
    if(line.startsWith(QChar(0xFEFF)))line.remove(0,1);
    if(line.isEmpty())return {};

    const QFileInfo raw(line);
    if(raw.isAbsolute())return QDir::cleanPath(raw.absoluteFilePath());

    const QUrl url=QUrl::fromEncoded(line.toUtf8(),QUrl::TolerantMode);
    if(url.isLocalFile())return QDir::cleanPath(url.toLocalFile());
    if(!url.scheme().isEmpty())return {};
    if(url.isRelative()){
        const auto basePath=QDir::cleanPath(QFileInfo(directory).absoluteFilePath())+QLatin1Char('/');
        const auto resolved=QUrl::fromLocalFile(basePath).resolved(url);
        if(resolved.isLocalFile()){
            const auto candidate=QDir::cleanPath(resolved.toLocalFile());
            if(QFileInfo::exists(candidate))return candidate;
        }
    }

    // Preserve compatibility with the original Qt writer, which stored a raw
    // relative path (including literal '%' characters) on the first line.
    return QDir::cleanPath(QDir(directory).absoluteFilePath(line));
}

inline bool resolveCartridgeInstance(const QByteArray&descriptor,const QString&directory,
                                     QString&romPath,bool&needsRewrite)
{
    auto text=QString::fromUtf8(descriptor);
    if(text.startsWith(QChar(0xFEFF)))text.remove(0,1);
    QString selected;
    needsRewrite=false;
    for(auto line:text.split(QLatin1Char('\n'))){
        if(line.endsWith(QLatin1Char('\r')))line.chop(1);
        if(line.isEmpty())continue;
        const auto candidate=cartridgeInstanceCandidate(line,directory);
        if(candidate.isEmpty()||!QFileInfo::exists(candidate)){needsRewrite=true;continue;}
        if(selected.isEmpty())selected=candidate;
        else if(QDir::cleanPath(selected)!=QDir::cleanPath(candidate)){needsRewrite=true;break;}
    }
    if(selected.isEmpty())return false;
    romPath=selected;
    return true;
}

inline QByteArray cartridgeInstanceDescriptor(const QString&directory,const QString&romPath)
{
    const auto absolute=QDir::cleanPath(QFileInfo(romPath).absoluteFilePath());
    const auto relative=QDir::fromNativeSeparators(QDir(directory).relativeFilePath(absolute));
    const auto relativeUrl=QUrl::toPercentEncoding(relative,QByteArray("/"),QByteArray("%"));
    const auto fileUrl=QUrl::fromLocalFile(absolute).toString(QUrl::FullyEncoded).toUtf8();
    return relativeUrl+'\n'+absolute.toUtf8()+'\n'+fileUrl+'\n';
}

inline void repairCartridgeInstanceDescriptor(const QString&directory,const QString&romPath)
{
    QSaveFile file(QDir(directory).filePath("rom.gbl"));
    const auto descriptor=cartridgeInstanceDescriptor(directory,romPath);
    if(file.open(QIODevice::WriteOnly)&&file.write(descriptor)==descriptor.size())file.commit();
}
