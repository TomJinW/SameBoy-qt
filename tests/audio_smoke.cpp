#include "../src/audiobuffer.h"
#include <QCoreApplication>
#include <QtEndian>
#include <cstdio>
#include <thread>
#include <chrono>
#include <future>

QByteArray samples(int count){QByteArray bytes(count*4,'\0');for(int i=0;i<count;++i){qToLittleEndian<qint16>(i%32768,reinterpret_cast<uchar *>(bytes.data()+i*4));qToLittleEndian<qint16>(-(i%32768),reinterpret_cast<uchar *>(bytes.data()+i*4+2));}return bytes;}
int main(int argc,char **argv){QCoreApplication app(argc,argv);AudioBuffer audio(96000);
    if(audio.bytesAvailable()!=0){std::fputs("Empty PCM device advertises readable data\n",stderr);return 1;}
    const auto initial=samples(1000);audio.append(initial);if(audio.bytesAvailable()!=initial.size()||audio.read(400)!=initial.left(400)||audio.bytesAvailable()!=initial.size()-400)return 1;
    const auto overflow=samples(24000);audio.append(overflow);const auto latest=audio.read(1024);
    if(latest!=overflow.right(1024))return 1;
    if(audio.statistics().droppedFrames==0)return 1;
    audio.clear();if(audio.bytesAvailable()!=0)return 1;if(audio.read(32)!=QByteArray(32,'\0')||audio.statistics().underruns!=1)return 1;
    audio.clear();audio.setLatencyBudget(1200);const auto slow=samples(52000);audio.append(slow);if(audio.read(400)!=slow.left(400))return 1;
    AudioBuffer waiting(96000);const auto arriving=samples(4800);
    std::thread producer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(5));waiting.append(arriving);});
    const auto consumed=waiting.read(arriving.size());producer.join();
    if(consumed!=arriving){std::fputs("Audio callback padded silence before the producer's samples arrived\n",stderr);return 1;}
    AudioBuffer canceled(96000);std::promise<void> started;auto callback=std::async(std::launch::async,[&]{started.set_value();return canceled.read(192000);});
    started.get_future().wait();std::this_thread::sleep_for(std::chrono::milliseconds(10));canceled.clear();
    if(callback.wait_for(std::chrono::milliseconds(60))!=std::future_status::ready||callback.get()!=QByteArray(192000,0)){std::fputs("Clearing audio did not cancel the waiting callback\n",stderr);return 1;}
    // A backend may destroy its stream on device removal while the source
    // remains alive. Disconnect must prevent already-produced data from
    // reaching that retired stream (Qt queued signal calls survive disconnect).
    AudioBuffer unplugged(48000);int retiredCallbacks=0;
    auto oldStream=QObject::connect(&unplugged,&QIODevice::readyRead,&unplugged,[&]{++retiredCallbacks;});
    std::thread queuedProducer([&]{for(int i=0;i<100;++i)unplugged.append(samples(8));});queuedProducer.join();
    QObject::disconnect(oldStream);QCoreApplication::processEvents();
    if(retiredCallbacks){std::fputs("Queued PCM notification reached a retired audio stream\n",stderr);return 1;}
    int replacementCallbacks=0;auto replacement=QObject::connect(&unplugged,&QIODevice::readyRead,&unplugged,[&]{++replacementCallbacks;});
    std::thread nextProducer([&]{unplugged.append(samples(8));});nextProducer.join();QCoreApplication::processEvents();
    if(replacementCallbacks!=1)return 1;
    QObject::disconnect(replacement);
    std::puts("96 kHz pull buffer, stereo alignment, latest-block overflow recovery and underrun padding passed.");return 0;
}
