#include "videorecorder.h"
#include "filterrenderer.h"
#include <QMediaRecorder>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QVideoFrameInput>
#include <QAudioBufferInput>
#include <QVideoFrame>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QSaveFile>
#include <QUrl>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <deque>
#include <cmath>
#include <cstring>
struct VideoRecorder::Impl{
 VideoRecorder*owner;std::unique_ptr<QSaveFile>file;QMediaCaptureSession session;QMediaRecorder recorder;
 std::unique_ptr<QVideoFrameInput>video;std::unique_ptr<QAudioBufferInput>audio;
 std::unique_ptr<OffscreenFilterRenderer>filter;
 QAudioFormat audioFormat;QTimer timer;QSize nativeSize,outputSize;double fps=60;unsigned rate=48000;int scale=1;bool filters=true,running=false,finishing=false,pumping=false,videoEnd=false,audioEnd=false,started=false,stopped=false;
 qint64 frameCount=0,audioFrames=0;QString error;
 std::deque<VideoFramePacket>incoming;std::deque<QVideoFrame>videos;std::deque<QAudioBuffer>audios;
 explicit Impl(VideoRecorder*parent):owner(parent){
  session.setRecorder(&recorder);QObject::connect(&timer,&QTimer::timeout,owner,[this]{pump();});timer.setInterval(5);
  QObject::connect(&recorder,&QMediaRecorder::errorOccurred,owner,[this](auto,const QString&message){error=message;});
  QObject::connect(&recorder,&QMediaRecorder::recorderStateChanged,owner,[this](auto state){if(state==QMediaRecorder::RecordingState){started=true;pump();}if(started&&state==QMediaRecorder::StoppedState)stopped=true;});
 }
 ~Impl(){QObject::disconnect(&recorder,nullptr,owner,nullptr);session.setRecorder(nullptr);session.setVideoFrameInput(nullptr);session.setAudioBufferInput(nullptr);}
 bool prepare(VideoFramePacket packet){
  if(packet.image.size()!=nativeSize||packet.sampleRate!=rate||std::abs(packet.frameRate-fps)>.01){error=QStringLiteral("Game screen size, frame rate or audio format changed during recording.");return false;}
  QImage image;
  if(filters&&(packet.filter||packet.blending)){
   if(!filter)filter=std::make_unique<OffscreenFilterRenderer>();
   image=filter->render(packet.image,packet.previous,outputSize,packet.filter,packet.blending,&error);
  }else image=packet.image.scaled(outputSize,Qt::IgnoreAspectRatio,Qt::FastTransformation);
  if(image.isNull()){if(error.isEmpty())error=QStringLiteral("Could not render video frame.");return false;}
  QVideoFrame frame(image);frame.setStartTime(qint64(frameCount*1000000.0/fps));frame.setEndTime(qint64((frameCount+1)*1000000.0/fps));videos.push_back(frame);
  // Record on the original emulated timeline, independent of wall-clock turbo/slow motion.
  const qint64 expected=qint64((frameCount+1)*rate/fps)-audioFrames;const auto available=packet.pcm.size()/4;
  if(available<=0||expected<=0){error=QStringLiteral("No audio samples for recorded frame.");return false;}
  QByteArray pcm(expected*4,0);
  for(qint64 n=0;n<expected;++n){const double position=double(n)*available/expected;const auto first=qMin<qint64>(qint64(position),available-1),next=qMin<qint64>(first+1,available-1);const double fraction=position-first;
   for(int channel=0;channel<2;++channel){int16_t a,b;std::memcpy(&a,packet.pcm.constData()+first*4+channel*2,2);std::memcpy(&b,packet.pcm.constData()+next*4+channel*2,2);const int16_t value=qRound(a+(b-a)*fraction);std::memcpy(pcm.data()+n*4+channel*2,&value,2);}}
  audios.emplace_back(pcm,audioFormat,audioFrames*1000000/rate);audioFrames+=expected;++frameCount;return true;
 }
 void pump(){
  if(pumping||!running||!error.isEmpty()||recorder.recorderState()!=QMediaRecorder::RecordingState)return;pumping=true;
  auto send=[this]{while(!videos.empty()&&video->sendVideoFrame(videos.front()))videos.pop_front();while(!audios.empty()&&audio->sendAudioBuffer(audios.front()))audios.pop_front();};send();
  for(int batch=0;batch<4&&!incoming.empty()&&videos.size()<4&&audios.size()<4;++batch){auto packet=std::move(incoming.front());incoming.pop_front();if(!prepare(std::move(packet)))break;send();}
  if(finishing&&incoming.empty()&&videos.empty()&&audios.empty()&&error.isEmpty()){
   if(!videoEnd)videoEnd=video->sendVideoFrame({});if(!audioEnd)audioEnd=audio->sendAudioBuffer({});
  }pumping=false;
 }
};
VideoRecorder::VideoRecorder(QObject*parent):QObject(parent),impl_(std::make_unique<Impl>(this)){}
VideoRecorder::~VideoRecorder(){if(impl_->running)finish();}
QString VideoRecorder::start(const QString&path,QSize size,double fps,unsigned rate,int scale,bool filters){
 auto&i=*impl_;if(i.running)return QStringLiteral("A video is already being recorded.");if(!QVector<int>{1,2,4,6,8}.contains(scale)||size.isEmpty()||fps<=0||rate==0)return QStringLiteral("Invalid recording format.");
 QMediaFormat format(QMediaFormat::MPEG4);format.setVideoCodec(QMediaFormat::VideoCodec::H264);format.setAudioCodec(QMediaFormat::AudioCodec::AAC);if(!format.isSupported(QMediaFormat::Encode))return QStringLiteral("H.264/AAC MP4 recording is unavailable in this multimedia backend.");
 i.error.clear();i.finishing=i.started=i.stopped=i.videoEnd=i.audioEnd=false;i.frameCount=i.audioFrames=0;i.incoming.clear();i.videos.clear();i.audios.clear();
 i.file=std::make_unique<QSaveFile>(path);if(!i.file->open(QIODevice::WriteOnly))return i.file->errorString();i.nativeSize=size;i.outputSize=size*scale;i.fps=fps;i.rate=rate;i.scale=scale;i.filters=filters;
 i.audioFormat.setSampleRate(rate);i.audioFormat.setChannelCount(2);i.audioFormat.setSampleFormat(QAudioFormat::Int16);
 QVideoFrameFormat vf(i.outputSize,QVideoFrameFormat::Format_RGBA8888);vf.setFrameRate(fps);i.video=std::make_unique<QVideoFrameInput>(vf);i.audio=std::make_unique<QAudioBufferInput>(i.audioFormat);i.session.setVideoFrameInput(i.video.get());i.session.setAudioBufferInput(i.audio.get());
 connect(i.video.get(),&QVideoFrameInput::readyToSendVideoFrame,this,[&i]{i.pump();});connect(i.audio.get(),&QAudioBufferInput::readyToSendAudioBuffer,this,[&i]{i.pump();});
 i.recorder.setMediaFormat(format);i.recorder.setOutputDevice(i.file.get());i.recorder.setOutputLocation(QUrl::fromLocalFile(path));i.recorder.setVideoResolution(i.outputSize);i.recorder.setVideoFrameRate(fps);i.recorder.setAudioSampleRate(48000);i.recorder.setAudioChannelCount(2);i.recorder.setAudioBitRate(192000);i.recorder.setQuality(QMediaRecorder::VeryHighQuality);i.recorder.setEncodingMode(QMediaRecorder::AverageBitRateEncoding);i.recorder.setVideoBitRate(qBound(4000000,int(i.outputSize.width()*double(i.outputSize.height())*fps*0.4),64000000));i.recorder.setAutoStop(true);i.running=true;i.recorder.record();i.timer.start();return i.error;
}
bool VideoRecorder::active()const{return impl_->running&&!impl_->finishing;}
bool VideoRecorder::canAccept()const{return active()&&impl_->error.isEmpty()&&impl_->incoming.size()<8;}
bool VideoRecorder::append(VideoFramePacket packet){if(!canAccept())return false;impl_->incoming.push_back(std::move(packet));impl_->pump();return impl_->error.isEmpty();}
QString VideoRecorder::error()const{return impl_->error;}
QString VideoRecorder::finish(QVector<VideoFramePacket>remaining){
 auto&i=*impl_;if(!i.running)return i.error;i.finishing=true;for(auto&packet:remaining)i.incoming.push_back(std::move(packet));
 if(i.frameCount==0&&i.incoming.empty())i.error=QStringLiteral("No video frames were recorded.");
 QElapsedTimer deadline;deadline.start();while(!i.stopped&&i.error.isEmpty()&&deadline.elapsed()<15000){i.pump();QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents,10);QThread::msleep(1);}
 if(!i.stopped){if(i.error.isEmpty())i.error=QStringLiteral("Video encoder did not finish in time.");i.recorder.stop();QElapsedTimer stopping;stopping.start();while(i.recorder.recorderState()!=QMediaRecorder::StoppedState&&stopping.elapsed()<15000){QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents,10);QThread::msleep(1);}}
 i.timer.stop();i.running=false;i.recorder.setOutputDevice(nullptr);
 if(i.error.isEmpty()&&i.frameCount){if(!i.file->commit())i.error=i.file->errorString();}else{i.file->cancelWriting();if(i.error.isEmpty())i.error=QStringLiteral("No video frames were recorded.");}
 return i.error;
}
