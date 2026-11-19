#include "../src/emulator.h"
#include "../src/debuggerbridge.h"
#include "test_rom.h"
#include "worker_lifecycle.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <cstdio>
#include <cstring>
#include <utility>
#include <thread>
#include <chrono>

// Cocoa (Document.m batteryTimerExpired) saves a battery once its writes have been quiet for a
// 0.25 s interval: soon after a save routine finishes, never halfway through one.
static bool batteryFollowsWrites(const QByteArray&rom,const QByteArray&boot,const QString&path,bool color,unsigned rate)
{
    QFile seed(path);if(!seed.open(QIODevice::WriteOnly)||seed.write(QByteArray(8192,'\x17'))!=8192)return false;seed.close();
    auto onDisk=[&](int offset){QFile file(path);return file.open(QIODevice::ReadOnly)&&file.seek(offset)?file.read(1):QByteArray();};
    auto waitFor=[&](int offset,char value,int timeout){QElapsedTimer wait;wait.start();while(onDisk(offset)!=QByteArray(1,value)){if(wait.elapsed()>timeout)return qint64(-1);QThread::msleep(10);}return wait.elapsed();};
    auto write=[](uint16_t address,uint8_t value){return [address,value](GB_gameboy_t*g){GB_write_memory(g,address,value);return QString();};};
    Emulator worker(rom,boot,path,color,{},rate);worker.start();
    // The cartridge enables its RAM and writes a marker once, after the boot ROM.
    if(waitFor(0,'\x42',12000)<0){std::fputs("Boot-time battery write was never persisted\n",stderr);return false;}
    if(!worker.execute(write(0xa001,0x55)).isEmpty())return false;
    const auto latency=waitFor(1,'\x55',6000);
    if(latency<0||latency>1500){std::fprintf(stderr,"A finished battery write took %lld ms to reach disk\n",(long long)latency);return false;}
    QElapsedTimer burst;burst.start();uint8_t value=0;
    while(burst.elapsed()<1500){if(!worker.execute(write(0xa002,++value)).isEmpty())return false;
        if(onDisk(2)!=QByteArray(1,'\x17')){std::fputs("Battery was saved in the middle of a write burst\n",stderr);return false;}QThread::msleep(40);}
    if(waitFor(2,char(value),1500)<0){std::fputs("The end of a write burst was not persisted\n",stderr);return false;}
#ifdef Q_OS_WIN
    // Another process holding the save open briefly (antivirus, indexer, backup) must not fail the save:
    // Windows refuses the replacing rename until that handle closes.
    {QFile holder(path);if(!holder.open(QIODevice::ReadOnly))return false;
     std::thread release([&]{std::this_thread::sleep_for(std::chrono::milliseconds(30));holder.close();});
     const auto error=worker.flushBattery();release.join();
     if(!error.isEmpty()){std::fprintf(stderr,"Battery save failed while the file was briefly held open: %s\n",qPrintable(error));return false;}}
#endif
    worker.requestInterruption();worker.wait();
    const auto error=worker.takeError();if(!error.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(error));return false;}
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory(QDir::currentPath()+"/worker-smoke-XXXXXX");
    if (!directory.isValid()) return 1;
    // Also exercises Qt buffer-based access through Unicode paths.
    const auto savePath = directory.filePath(QString::fromUtf8("测试电池.sav"));
    QByteArray rom(0x8000, '\0');
    const bool audio96=argc>1&&QByteArray(argv[1])=="audio96";
    const unsigned sampleRate=audio96?96000:48000;
    const bool external = argc > 1 && QByteArray(argv[1]) != "cgb"&&!audio96;
    bool color = argc > 1&&!audio96;
    make_test_rom(reinterpret_cast<uint8_t *>(rom.data()), color);
    if (external) {
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::ReadOnly)) return 1;
        rom = file.readAll();
        if (rom.size() < 0x150) return 1;
        color = (static_cast<unsigned char>(rom[0x143]) & 0x80) != 0;
    }
    QFile boot(color ? BOOT_ROM_DIR "/cgb_boot.bin" : BOOT_ROM_DIR "/dmg_boot.bin");
    if (!boot.open(QIODevice::ReadOnly)) return 1;
    if (!external) {
        QFile existing(savePath);
        if (!existing.open(QIODevice::WriteOnly) || existing.write(QByteArray(8192, '\x17')) != 8192) return 1;
    }
    unsigned frames = 0;
    bool renderedContent = false;
    qint64 audioBytes = 0;
    const int runMilliseconds = color ? 4000 : 2500;
    QElapsedTimer elapsed;
    QImage latest;
    elapsed.start();
    {
        Emulator worker(rom, boot.readAll(), savePath, color,{},sampleRate);
        // Count frames the worker published, not frames this poll loop happened to observe:
        // a starved or coarse-timer (Windows) poller merges frames and undercounts.
        auto published=[&]{const auto samples=worker.cpuLoadSamples();return samples.isEmpty()?quint64(0):samples.last().sequence;};
        worker.start();
        while (elapsed.elapsed() < runMilliseconds) {
            QThread::msleep(8);
            auto frame = worker.takeFrame();
            if (!frame.isNull()) {
                if (frame.size() != QSize(160, 144)) return 1;
                latest = frame;
                const auto first = frame.pixel(0, 0);
                for (int y = 0; y < 144 && !renderedContent; y++)
                    for (int x = 0; x < 160; x++)
                        if (frame.pixel(x, y) != first) { renderedContent = true; break; }
            }
            audioBytes += worker.takeAudio().size();
        }
        frames = unsigned(published());
        worker.setPaused(true);
        QThread::msleep(50);
        worker.takeFrame(); worker.takeAudio();
        QThread::msleep(50);
        if (!worker.takeFrame().isNull() || !worker.takeAudio().isEmpty()) {
            std::fprintf(stderr, "Pause failed\n"); return 1;
        }
        worker.setPaused(false);
        AppSettings updated;
        updated.border = GB_BORDER_ALWAYS; updated.palette = 3;
        updated.colorCorrection = GB_COLOR_CORRECTION_LOW_CONTRAST;
        updated.highpass = GB_HIGHPASS_ACCURATE;
        updated.capTurbo=true;updated.turboPercent=200;
        worker.updateSettings(updated);
        QThread::msleep(100);
        if (worker.takeFrame().size() != QSize(256, 224)) { std::fprintf(stderr, "Live settings failed\n"); return 1; }
        auto expectClock=[&](uint32_t hz){QElapsedTimer timeout;timeout.start();while(timeout.elapsed()<1500){if(worker.execute([hz](GB_gameboy_t*g){return GB_get_clock_rate(g)*sameboy_cpu_speed_factor(g)==hz?QString():QString("pending");}).isEmpty())return true;QThread::msleep(5);}return false;};
        updated.cpuFrequency=1;worker.updateSettings(updated);if(!expectClock(4194304)){std::fputs("Forced 4 MHz clock was not applied\n",stderr);return 1;}
        updated.cpuFrequency=2;worker.updateSettings(updated);if(!expectClock(8388608)){std::fputs("Forced 8 MHz clock was not applied\n",stderr);return 1;}
        // CPU speed must preserve video cadence and continuous wall-clock PCM.
        auto cadence=[&]{worker.takeAudio();const auto first=published();qint64 pcm=0;QElapsedTimer timer;timer.start();while(timer.elapsed()<450){pcm+=worker.takeAudio().size();QThread::msleep(2);}return std::pair<int,qint64>{int(published()-first),pcm};};
        updated.cpuFrequency=1;worker.updateSettings(updated);if(!expectClock(4194304))return 1;QThread::msleep(50);auto normalCadence=cadence();
        updated.cpuFrequency=2;worker.updateSettings(updated);if(!expectClock(8388608))return 1;QThread::msleep(50);auto fastCadence=cadence();
        if(std::abs(fastCadence.first-normalCadence.first)>5||normalCadence.second<sampleRate*4*.25||fastCadence.second<sampleRate*4*.25){std::fprintf(stderr,"Forced clock cadence/PCM mismatch: 4MHz %d/%lld, 8MHz %d/%lld\n",normalCadence.first,(long long)normalCadence.second,fastCadence.first,(long long)fastCadence.second);return 1;}
        updated.cpuFrequency=0;worker.updateSettings(updated);QElapsedTimer autoClock;autoClock.start();bool restoredClock=false;while(autoClock.elapsed()<1500){if(worker.execute([](GB_gameboy_t*g){return GB_get_clock_rate(g)==GB_get_unmultiplied_clock_rate(g)?QString():QString("pending");}).isEmpty()){restoredClock=true;break;}QThread::msleep(5);}if(!restoredClock){std::fputs("Automatic clock did not restore hardware rate\n",stderr);return 1;}
        if(color&&!external){
            updated.cpuFrequency=1;worker.updateSettings(updated);if(!expectClock(4194304))return 1;
            updated.cpuFrequency=0;worker.updateSettings(updated);QThread::msleep(40);
            if(!worker.execute([](GB_gameboy_t*g){size_t size=0;auto*data=static_cast<uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_ROM,&size,nullptr));if(size<0x208)return QString("ROM too short");const uint8_t stop[]={0x3e,1,0xe0,0x4d,0x10,0,0x18,0xfe};std::memcpy(data+0x200,stop,sizeof(stop));GB_get_registers(g)->pc=0x200;return QString();}).isEmpty())return 1;
            QElapsedTimer switched;switched.start();while(worker.cpuFrequencyHz()<8000000&&switched.elapsed()<1500)QThread::msleep(5);
            if(worker.cpuFrequencyHz()<8000000){std::fputs("CGB double-speed clock not reported\n",stderr);return 1;}
            updated.cpuFrequency=1;worker.updateSettings(updated);QElapsedTimer forced;forced.start();while(worker.cpuFrequencyHz()!=4194304&&forced.elapsed()<1500)QThread::msleep(5);
            if(worker.cpuFrequencyHz()!=4194304)return 1;
            updated.cpuFrequency=2;worker.updateSettings(updated);forced.restart();while(worker.cpuFrequencyHz()!=8388608&&forced.elapsed()<1500)QThread::msleep(5);
            if(worker.cpuFrequencyHz()!=8388608)return 1;
            updated.cpuFrequency=0;worker.updateSettings(updated);if(!expectClock(8388608))return 1;
        }
        worker.takeAudio();
        worker.setActions(4); // Turbo must continue producing bounded PCM.
        QThread::msleep(600);
        const auto turboAudio = worker.takeAudio();
        std::printf("rate=%u cappedTurboSpeed=%.2fx\n",sampleRate,worker.emulationSpeed());
        if (turboAudio.isEmpty() || turboAudio.size() > sampleRate*4/10) { std::fprintf(stderr, "Turbo audio failed\n"); return 1; }
        worker.setActions(0);
        QThread::msleep(100);
        if (worker.takeAudio().isEmpty()) { std::fprintf(stderr, "Audio did not resume\n"); return 1; }
        worker.setActions(8);
        const auto rewindStartFrame=worker.emulatedFrames();QElapsedTimer rewindWall;rewindWall.start();
        QThread::msleep(100);
        if (worker.takeAudio().isEmpty()) { std::fprintf(stderr, "Rewind audio failed: %llu frames in %lld ms\n",(unsigned long long)(worker.emulatedFrames()-rewindStartFrame),(long long)rewindWall.elapsed()); return 1; }
        worker.setActions(16);
        QThread::msleep(80);worker.takeAudio();QThread::msleep(65);
        const auto slowAudio=worker.takeAudio();
        if(slowAudio.size()<int(sampleRate*4/50)){std::fprintf(stderr,"Slow motion audio is missing or not stretched to wall time: %lld bytes\n",(long long)slowAudio.size());return 1;}
        if(!worker.execute([](GB_gameboy_t*g){return GB_get_clock_rate(g)*2==GB_get_unmultiplied_clock_rate(g)?QString():QString("Slow motion clock mismatch");}).isEmpty())return 1;
        worker.setActions(0);QThread::msleep(50);
        if(!worker.execute([](GB_gameboy_t*g){return GB_get_clock_rate(g)==GB_get_unmultiplied_clock_rate(g)?QString():QString("Normal clock did not resume");}).isEmpty())return 1;
        for(auto mode:{32,64}){worker.setActions(mode);QElapsedTimer deadline;deadline.start();bool applied=false;while(deadline.elapsed()<1500){const int divisor=mode==32?8:32;if(worker.execute([divisor](GB_gameboy_t*g){return GB_get_clock_rate(g)*divisor==GB_get_unmultiplied_clock_rate(g)?QString():QString("pending");}).isEmpty()){applied=true;break;}QThread::msleep(5);}if(!applied)return 1;QElapsedTimer latency;latency.start();if(!worker.execute([](GB_gameboy_t*){return QString();}).isEmpty()||latency.elapsed()>250){std::fprintf(stderr,"Underclock blocks debug requests\n");return 1;}}
        worker.setActions(0);
        worker.setPaused(true);
        if(!worker.execute([](GB_gameboy_t *gb){GB_add_cheat(gb,"Old cartridge",0xcfff,0,1,0,false,true);GB_cheat_search_filter(gb,"1",GB_CHEAT_SEARCH_DATA_TYPE_8BIT);return QString();}).isEmpty())return 1;
        if(!worker.swapCartridge(rom,directory.filePath("second.sav")).isEmpty())return 1;
        if(!worker.execute([](GB_gameboy_t *gb){size_t count;GB_get_cheats(gb,&count);return count||GB_cheat_search_result_count(gb)?QString("Cartridge state leaked"):QString();}).isEmpty())return 1;
        worker.requestInterruption();
        if (!worker.wait(2000)) { std::fprintf(stderr, "Stop timed out\n"); return 1; }
        const auto error = worker.takeError();
        if (!error.isEmpty()) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
    }
    if(!external&&!workerLifecycle(app,rom,directory.path()))return 1;
    if(!external){QFile again(color?BOOT_ROM_DIR "/cgb_boot.bin":BOOT_ROM_DIR "/dmg_boot.bin");if(!again.open(QIODevice::ReadOnly)||!batteryFollowsWrites(rom,again.readAll(),directory.filePath("debounce.sav"),color,sampleRate))return 1;}
    QFile save(savePath);
    const bool persisted = external || (save.open(QIODevice::ReadOnly) && save.read(2) == QByteArray("\x42\x17", 2));
    if (argc > 2 && !latest.save(QString::fromLocal8Bit(argv[2]))) return 1;
    std::printf("frames=%u audioBytes=%lld elapsed=%lld persisted=%d\n",
                frames, static_cast<long long>(audioBytes), static_cast<long long>(elapsed.elapsed()), persisted);
    // Wide bounds catch runaway or stalled pacing without demanding exact scheduling.
    return frames >= unsigned(runMilliseconds / 30) && frames <= unsigned(runMilliseconds / 12)
        && audioBytes > runMilliseconds * 130*(audio96?2:1) && audioBytes < runMilliseconds * 240*(audio96?2:1)
        && persisted && renderedContent ? 0 : 1;
}
