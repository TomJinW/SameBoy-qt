#include "audiobuffer.h"
#include <QMutexLocker>
#include <cstring>
#include <algorithm>
#include <QDeadlineTimer>
#include <cmath>
#include <QMetaObject>


AudioBuffer::AudioBuffer(unsigned rate):sampleRate_(rate){open(QIODevice::ReadOnly|QIODevice::Unbuffered);}
void AudioBuffer::setLatencyBudget(unsigned milliseconds){QMutexLocker lock(&mutex_);latencyMs_=std::clamp(milliseconds,50u,2500u);}
void AudioBuffer::append(const QByteArray &pcm){
    {QMutexLocker lock(&mutex_);buffer_.append(pcm);const qsizetype limit=qMax<qsizetype>(65536,qint64(sampleRate_)*4*(latencyMs_+40)/1000);
        if(buffer_.size()>limit){const auto removed=(buffer_.size()-limit+3)/4*4;buffer_.remove(0,removed);statistics_.droppedFrames+=removed/4;}available_.wakeAll();}
    // Emit on the source's own thread. A cross-thread readyRead queues Qt's
    // raw stream callback on this device; that callback can outlive a removed
    // USB device's stream. Queue our lifetime-bound notification instead.
    if(!notificationPending_.exchange(true))QMetaObject::invokeMethod(this,[this]{
        notificationPending_.store(false);emit readyRead();
    },Qt::QueuedConnection);
}
void AudioBuffer::clear(){QMutexLocker lock(&mutex_);buffer_.clear();++epoch_;available_.wakeAll();}
// Report only real PCM: a pull backend may refill until this becomes zero.
qint64 AudioBuffer::bytesAvailable() const {QMutexLocker lock(&mutex_);return buffer_.size()+QIODevice::bytesAvailable();}
AudioBuffer::Statistics AudioBuffer::statistics() const {QMutexLocker lock(&mutex_);return statistics_;}
qint64 AudioBuffer::readData(char *data,qint64 size){
    QMutexLocker lock(&mutex_);size-=size%4;if(size<=0)return 0;
    const auto epoch=epoch_;
    if(buffer_.size()<size){
        // Cocoa waits for missing samples for at most their playback duration.
        // Bound unusually large external reads so shutdown cannot stall indefinitely.
        const auto waitMs=qint64(std::clamp(std::ceil(double(size-buffer_.size())*1000/std::max(1.0,double(sampleRate_)*4)),1.0,100.0));
        const QDeadlineTimer deadline(waitMs,Qt::PreciseTimer);
        while(buffer_.size()<size&&epoch_==epoch&&!deadline.hasExpired())available_.wait(&mutex_,deadline);
        if(epoch_!=epoch){std::memset(data,0,size);return size;}
    }
    // Cocoa also jumps to the most recent block when latency exceeds about 50 ms.
    if(buffer_.size()>=size+qint64(sampleRate_)*4*latencyMs_/1000){const auto removed=(buffer_.size()-size)/4*4;buffer_.remove(0,removed);statistics_.droppedFrames+=removed/4;}
    const auto available=qMin<qint64>(size,buffer_.size());if(available)std::memcpy(data,buffer_.constData(),available);buffer_.remove(0,available);
    if(available<size){std::memset(data+available,0,size-available);++statistics_.underruns;}return size;
}
