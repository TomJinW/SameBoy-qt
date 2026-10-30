#include "../src/emulator.h"
#include "../src/audiobuffer.h"
#include "../src/locallink.h"
#include "test_rom.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <cstdio>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
#endif
struct CpuSample{double seconds=-1;quint64 cycles=0;};
static CpuSample cpuSample(){
#ifdef _WIN32
    FILETIME created,exited,kernel,user;CpuSample out;
    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)){
        ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;out.seconds=(k.QuadPart+u.QuadPart)/1e7;
    }
    ULONG64 cycles=0;if(QueryProcessCycleTime(GetCurrentProcess(),&cycles))out.cycles=cycles;return out;
#else
    return {double(std::clock())/CLOCKS_PER_SEC,0};
#endif
}
static quint64 frameSequence(Emulator&worker){const auto samples=worker.cpuLoadSamples();return samples.isEmpty()?0:samples.last().sequence;}
int main(int argc,char**argv){
    QCoreApplication app(argc,argv);QTemporaryDir temp(QDir::currentPath()+"/perf-XXXXXX");if(!temp.isValid())return 1;
    QByteArray rom(32768,0);make_test_rom(reinterpret_cast<uint8_t*>(rom.data()),true);
    const bool synthetic=argc==1;
    if(!synthetic){QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly)||file.size()<0x150||file.size()>8*1024*1024)return 1;const auto expected=file.size();rom=file.readAll();if(rom.size()!=expected||file.error()!=QFileDevice::NoError||rom.startsWith(QByteArray("GBS\1",4)))return 1;}
    const bool color=uint8_t(rom[0x143])&0x80;
    QFile boot(color?BOOT_ROM_DIR "/cgb_boot.bin":BOOT_ROM_DIR "/dmg_boot.bin");if(!boot.open(QIODevice::ReadOnly))return 1;const auto bootData=boot.readAll();
#ifdef NDEBUG
    std::puts("Build assertions: NDEBUG");
#else
    std::puts("Build assertions: enabled");
#endif
    std::puts("Core probe; 2.2 s warmup (rewind: 4.2 s) + 3 s wall measurement; CPU percent is one logical core, both workers included for link. No GPU/real audio device. Synthetic boot skipped.");
    const char*names[]={"normal","no-rewind","48k","turbo2x","slow-half","slow-eighth","slow-32nd","debug-running","debug-stopped","linked","rewind"};
    for(int mode=0;mode<11;++mode){
        AppSettings settings;settings.rewindSeconds=mode==1?0:120;settings.capTurbo=mode==3;settings.turboPercent=200;settings.developerMode=mode==7||mode==8;
        const unsigned rate=mode==2?48000:96000;auto buffer=std::make_shared<AudioBuffer>(rate);
        Emulator worker(rom,bootData,temp.filePath(QString::number(mode)+".sav"),color,settings,rate,buffer);
        worker.setPaused(true);worker.start();
        auto initialize=[&](Emulator&emu){return emu.execute([&](GB_gameboy_t*g){if(synthetic){GB_write_memory(g,0xff50,1);GB_get_registers(g)->pc=0x100;}return QString();}).isEmpty();};
        if(!initialize(worker))return 1;
        std::unique_ptr<Emulator>peer;std::shared_ptr<AudioBuffer>peerBuffer;
        if(mode==9){peerBuffer=std::make_shared<AudioBuffer>(rate);peer=std::make_unique<Emulator>(rom,bootData,temp.filePath("peer.sav"),color,settings,rate,peerBuffer);peer->setPaused(true);peer->start();if(!initialize(*peer))return 1;auto link=std::make_shared<LocalLink>();if(!worker.setLocalLink(link,0).isEmpty()||!peer->setLocalLink(link,1).isEmpty())return 1;peer->setPaused(false);}
        worker.setActions(mode==3?4:mode==4?16:mode==5?32:mode==6?64:0);worker.setPaused(false);
        QByteArray output(65536,0);
        auto drain=[&](qint64 target,qint64&consumed){while(target>consumed){const auto amount=qMin<qint64>(output.size(),target-consumed);buffer->read(output.data(),amount);if(peerBuffer)peerBuffer->read(output.data(),amount);consumed+=amount;}};
        qint64 warmedAudio=0;QElapsedTimer warm;warm.start();while(warm.elapsed()<(mode==10?4200:2200)){QCoreApplication::processEvents();drain((warm.nsecsElapsed()*qint64(rate)*4/1000000000)&~qint64(3),warmedAudio);QThread::msleep(2);}
        if(mode==10)worker.setActions(8);
        if(mode==8){worker.sendDebugCommand("interrupt");warm.restart();while(!worker.debuggerStopped()&&warm.elapsed()<2000){QCoreApplication::processEvents();QThread::msleep(1);}if(!worker.debuggerStopped())return 1;}
        worker.takeFrame();const auto before=cpuSample();const auto startFrame=frameSequence(worker),peerStart=peer?frameSequence(*peer):0;
        const auto audioBefore=buffer->statistics();QElapsedTimer wall;wall.start();int snapshots=0;qint64 consumed=0;
        while(wall.elapsed()<3000){
            QCoreApplication::processEvents();if(!worker.takeFrame().isNull())++snapshots;
            const qint64 target=(wall.nsecsElapsed()*qint64(rate)*4/1000000000)&~qint64(3);
            drain(target,consumed);
            QThread::msleep(1);
        }
        const auto after=cpuSample();const double seconds=wall.nsecsElapsed()/1e9;const auto frames=frameSequence(worker)-startFrame;
        const auto audioAfter=buffer->statistics();const double delta=after.seconds-before.seconds;
        const bool validCpu=before.seconds>=0&&after.seconds>=before.seconds&&(delta>0||(frames==0&&after.cycles==before.cycles));
        const QByteArray usage=validCpu?QByteArray::number(delta*100/seconds,'f',2):QByteArray("unavailable");
        std::printf("mode=%s rate=%u rewind=%d wall=%.3fs cpu_one_core=%s%% cycles=%llu core_vblank_fps=%.2f snapshot_fps=%.2f peer_fps=%.2f reported_speed=%.3fx audio_underruns=%llu audio_dropped=%llu\n",names[mode],rate,settings.rewindSeconds,seconds,usage.constData(),static_cast<unsigned long long>(after.cycles>=before.cycles?after.cycles-before.cycles:0),frames/seconds,snapshots/seconds,peer?(frameSequence(*peer)-peerStart)/seconds:0,worker.emulationSpeed(),static_cast<unsigned long long>(audioAfter.underruns-audioBefore.underruns),static_cast<unsigned long long>(audioAfter.droppedFrames-audioBefore.droppedFrames));std::fflush(stdout);
        worker.requestInterruption();if(peer)peer->requestInterruption();if(!worker.wait(3000)||(peer&&!peer->wait(3000)))return 1;
    }
}
