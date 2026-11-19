#include "../src/videorecorder.h"
#include <QGuiApplication>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QUrl>
#include <cmath>
#include <cstring>
#include <cstdio>
int main(int argc,char**argv){
 QGuiApplication app(argc,argv);QTemporaryDir temporary(QDir::currentPath()+"/video-XXXXXX");
 const QString path=argc>1?QString::fromLocal8Bit(argv[1]):temporary.filePath("video.mp4");
 const auto unavailable=VideoRecorder::availabilityError();if(!unavailable.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(unavailable));return 77;}
 QMediaFormat format(QMediaFormat::MPEG4);format.setVideoCodec(QMediaFormat::VideoCodec::H264);format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
 if(!format.isSupported(QMediaFormat::Encode)){std::fprintf(stderr,"H.264 + AAC MP4 encoding unavailable.\n");return 77;}
 constexpr double fps=4194304.0/70224;constexpr int count=60,rate=96000;
 VideoRecorder recorder;auto error=recorder.start(path,{160,144},fps,rate,2,true);if(!error.isEmpty()){std::fprintf(stderr,"Start: %s\n",qPrintable(error));return 1;}
 for(int i=0;i<count;++i){QElapsedTimer deadline;deadline.start();while(!recorder.canAccept()&&deadline.elapsed()<3000){app.processEvents();QThread::msleep(1);}if(!recorder.canAccept())return 1;
  VideoFramePacket packet;packet.image=QImage(160,144,QImage::Format_RGB32);packet.image.fill(QColor::fromHsv(i*5,255,255));packet.previous=packet.image;packet.sampleRate=rate;packet.frameRate=fps;packet.filter=2;packet.blending=1;
  qint64 first=qint64(i*rate/fps),last=qint64((i+1)*rate/fps);packet.pcm.resize((last-first)*4);for(int n=0;n<packet.pcm.size()/4;++n){const int16_t value=((first+n)%200)<100?1500:-1500;std::memcpy(packet.pcm.data()+n*4,&value,2);std::memcpy(packet.pcm.data()+n*4+2,&value,2);}
  if(!recorder.append(std::move(packet))){std::fprintf(stderr,"Frame: %s\n",qPrintable(recorder.error()));return 1;}app.processEvents();
 }
 error=recorder.finish();if(!error.isEmpty()){std::fprintf(stderr,"Finish: %s\n",qPrintable(error));return 1;}
 QFile output(path);if(!output.open(QIODevice::ReadOnly))return 1;const auto bytes=output.readAll();output.close();if(!bytes.contains("avc1")||!bytes.contains("mp4a"))return 1;
 QMediaPlayer player;QVideoSink sink;player.setVideoSink(&sink);QImage decoded;int frames=0;qint64 firstVideoTime=-1,lastVideoTime=-1;bool ended=false;QString decodeError;
 QObject::connect(&sink,&QVideoSink::videoFrameChanged,&app,[&](const QVideoFrame&frame){if(frame.isValid()){if(firstVideoTime<0)firstVideoTime=frame.startTime();lastVideoTime=frame.endTime();++frames;if(decoded.isNull())decoded=frame.toImage();}});
 QObject::connect(&player,&QMediaPlayer::mediaStatusChanged,&app,[&](auto status){if(status==QMediaPlayer::EndOfMedia){ended=true;app.quit();}});
 QObject::connect(&player,&QMediaPlayer::errorOccurred,&app,[&](auto,const QString&message){decodeError=message;app.quit();});
 player.setSource(QUrl::fromLocalFile(path));QTimer::singleShot(10000,&app,&QCoreApplication::quit);player.play();app.exec();player.stop();
 std::printf("Video: H264/AAC, input96k, LCD filter2x, decoded=%dx%d frames=%d duration=%lldms expected=%.1fms bytes=%lld\n",decoded.width(),decoded.height(),frames,(long long)player.duration(),count*1000/fps,(long long)bytes.size());
 if(argc>1)decoded.save(path+".png");
 // AAC priming/padding extends the container beyond the video stream. Validate
 // decoded frame timing separately, allowing two frame intervals for encoder rounding.
 const double videoDuration=(lastVideoTime-firstVideoTime)/1000.0;
 const double frameInterval=1000/fps,aacPadding=3*1024*1000.0/48000;
 if(!ended||!decodeError.isEmpty()||decoded.size()!=QSize(320,288)||frames!=count||firstVideoTime<0||lastVideoTime<=firstVideoTime||std::abs(videoDuration-count*frameInterval)>2*frameInterval||player.duration()<videoDuration-frameInterval||player.duration()>videoDuration+aacPadding+frameInterval)return 1;
 // Failed recordings must preserve an existing destination instead of replacing it.
 const auto protectedPath=temporary.filePath("protected.mp4");QFile sentinel(protectedPath);if(!sentinel.open(QIODevice::WriteOnly)||sentinel.write("original")!=8)return 1;sentinel.close();
 VideoRecorder empty;if(!empty.start(protectedPath,{160,144},fps,rate,1,false).isEmpty())return 1;if(empty.finish().isEmpty())return 1;if(!sentinel.open(QIODevice::ReadOnly)||sentinel.readAll()!="original")return 1;
 return 0;
}
