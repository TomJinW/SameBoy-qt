#pragma once
#include <QAudioSink>
#include <QAudioDevice>
#include <QThread>
#include <QMetaObject>
#include <QPointer>
#include <memory>
#include <utility>
#include <functional>

// Pull backends may dispatch reads on the source device's event thread.
// Keep both the source and sink independent of the GUI, like Cocoa's renderer.
// Stop the backend before returning the source to its owner for destruction.
class AudioOutput final {
public:
    AudioOutput(const QAudioDevice&device,const QAudioFormat&format,std::function<void()> failure={}){
        host_=new QObject;host_->moveToThread(&thread_);
        QObject::connect(&thread_,&QThread::finished,host_,&QObject::deleteLater);
        thread_.setObjectName("SameBoy audio output");thread_.start();
        run([&]{sink_=std::make_unique<QAudioSink>(device,format);Q_ASSERT(sink_->thread()==&thread_);
            QObject::connect(sink_.get(),&QAudioSink::stateChanged,host_,[this,failure](QAudio::State state){
                if(state==QAudio::StoppedState&&sink_&&sink_->error()!=QAudio::NoError){
                    // Qt can retire the CoreAudio stream before emitting Stopped.
                    // Its readyRead lambda uses a raw stream pointer: detach it
                    // synchronously, before another source notification runs.
                    detachNotifications();if(failure)failure();
                }
            });});
    }
    ~AudioOutput(){stop();run([&]{sink_.reset();});thread_.quit();thread_.wait();}
    AudioOutput(const AudioOutput&)=delete;
    AudioOutput&operator=(const AudioOutput&)=delete;
    void setVolume(qreal volume){run([&]{sink_->setVolume(volume);});}
    void setBufferSize(qsizetype bytes){run([&]{sink_->setBufferSize(bytes);});}
    void start(QIODevice*device){
        stop();home_=device->thread();Q_ASSERT(home_==QThread::currentThread());
        device->moveToThread(&thread_);device_=device;
        run([&]{sink_->start(device);});
    }
    // Qt's WASAPI worker re-waits forever while the stream is suspended (2 s timeouts are ignored when
    // suspended), so resetting a sink suspended for longer than that never joins it. Resume first.
    void stop(){run([&]{detachNotifications();if(sink_->state()==QAudio::SuspendedState)sink_->resume();sink_->reset();if(device_){device_->moveToThread(home_);device_.clear();}});}
    void suspend(){run([&]{sink_->suspend();});}
    void resume(){run([&]{sink_->resume();});}
    QAudio::State state(){QAudio::State result=QAudio::StoppedState;run([&]{result=sink_->state();});return result;}
    QAudio::Error error(){QAudio::Error result=QAudio::NoError;run([&]{result=sink_->error();});return result;}
private:
    void detachNotifications(){
        if(device_)QObject::disconnect(device_,&QIODevice::readyRead,nullptr,nullptr);
    }
    template<class Function>void run(Function&&function){
        if(QThread::currentThread()==&thread_)function();
        else QMetaObject::invokeMethod(host_,std::forward<Function>(function),Qt::BlockingQueuedConnection);
    }
    QThread thread_;QThread*home_=nullptr;QPointer<QIODevice>device_;QObject*host_=nullptr;std::unique_ptr<QAudioSink>sink_;
};
