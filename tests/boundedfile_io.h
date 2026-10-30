#pragma once
#include "../src/boundedfile.h"
#include <QDir>
#include <cstdio>
inline bool boundedFileIo(const QString&directory){
    const auto path=QDir(directory).filePath("bounded-input.bin");
    auto reset=[&]{QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(QByteArray(16,'x'))==16;};
    if(!reset())return false;
    QByteArray data("previous");QFile file(path);
    if(!readBoundedFile(file,data,16,8)||data!=QByteArray(16,'x'))return false;
    data="previous";
    if(readBoundedFile(file,data,15)||data!="previous"||readBoundedFile(file,data,32,17)||data!="previous")return false;
    file.close();
    // Resize the real file precisely when I/O begins, after its size was checked.
    class ChangingFile final:public QFile{
    public:ChangingFile(const QString&path,qint64 target):QFile(path),target(target){}qint64 consumed=0;
    protected:qint64 readData(char*bytes,qint64 count)override{
        if(!changed){changed=true;QFile writer(fileName());if(!writer.open(QIODevice::ReadWrite)||!writer.resize(target))return -1;}
        const auto result=QFile::readData(bytes,count);if(result>0)consumed+=result;return result;
    }
    private:qint64 target;bool changed=false;
    };
    for(qint64 target:{qint64(4),qint64(1024*1024)}){
        if(!reset())return false;ChangingFile changed(path,target);data="previous";
        if(readBoundedFile(changed,data,32)||data!="previous"||changed.consumed>=1024*1024){std::fputs("Changing input was accepted, fully consumed or published partially\n",stderr);return false;}
    }
    QFile missing(path+".absent");return !readBoundedFile(missing,data,32)&&data=="previous";
}
