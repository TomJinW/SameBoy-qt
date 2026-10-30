#pragma once
#include <QIODevice>
#include <QMutex>
#include <QByteArray>
#include <QWaitCondition>

// QAudioSink reads this directly on its audio thread; GUI painting cannot starve it.
class AudioBuffer final:public QIODevice {
public:
    explicit AudioBuffer(unsigned sampleRate);
    void setLatencyBudget(unsigned milliseconds);
    void append(const QByteArray &pcm);
    void clear();
    bool isSequential() const override{return true;}
    qint64 bytesAvailable() const override;
    struct Statistics {quint64 underruns=0,droppedFrames=0;};
    Statistics statistics() const;
protected:
    qint64 readData(char *data,qint64 size) override;
    qint64 writeData(const char *,qint64) override{return -1;}
private:
    const unsigned sampleRate_;
    mutable QMutex mutex_;
    QByteArray buffer_;
    QWaitCondition available_;quint64 epoch_=0;
    unsigned latencyMs_=50;
    Statistics statistics_;
};
