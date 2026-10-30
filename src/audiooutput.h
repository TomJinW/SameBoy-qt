#pragma once
#include <QAudioSink>
#include <QAudioDevice>
#include <QThread>
#include <QMetaObject>
#include <QPointer>
#include <memory>
#include <utility>

// Pull backends may dispatch reads on the source device's event thread.
// Keep both the source and sink independent of the GUI, like Cocoa's renderer.
// Stop the backend before returning the source to its owner for destruction.
class AudioOutput final {
public:
    AudioOutput(const QAudioDevice&device,const QAudioFormat&format){
        host_=new QObject;host_->moveToThread(&thread_);
        QObject::connect(&thread_,&QThread::finished,host_,&QObject::deleteLater);
        thread_.setObjectName("SameBoy audio output");thread_.start();
        run([&]{sink_=std::make_unique<QAudioSink>(device,format);Q_ASSERT(sink_->thread()==&thread_);});
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
    void stop(){run([&]{sink_->stop();if(device_){device_->moveToThread(home_);device_.clear();}});}
    void suspend(){run([&]{sink_->suspend();});}
    void resume(){run([&]{sink_->resume();});}
    QAudio::Error error(){QAudio::Error result=QAudio::NoError;run([&]{result=sink_->error();});return result;}
private:
    template<class Function>void run(Function&&function){
        if(QThread::currentThread()==&thread_)function();
        else QMetaObject::invokeMethod(host_,std::forward<Function>(function),Qt::BlockingQueuedConnection);
    }
    QThread thread_;QThread*home_=nullptr;QPointer<QIODevice>device_;QObject*host_=nullptr;std::unique_ptr<QAudioSink>sink_;
};
