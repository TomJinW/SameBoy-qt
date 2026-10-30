#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>
#include <memory>
#include "emulator.h"
class VideoRecorder final:public QObject{
public:
 explicit VideoRecorder(QObject*parent=nullptr);~VideoRecorder();
 QString start(const QString&path,QSize nativeSize,double fps,unsigned sampleRate,int scale,bool filters);
 bool canAccept()const;
 bool append(VideoFramePacket);
 QString finish(QVector<VideoFramePacket>remaining={});
 QString error()const;
 bool active()const;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
