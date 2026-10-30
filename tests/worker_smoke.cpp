#include "../src/emulator.h"
#include "test_rom.h"
#include "worker_lifecycle.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <cstdio>

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
        worker.start();
        while (elapsed.elapsed() < runMilliseconds) {
            QThread::msleep(8);
            auto frame = worker.takeFrame();
            if (!frame.isNull()) {
                if (frame.size() != QSize(160, 144)) return 1;
                frames++;
                latest = frame;
                const auto first = frame.pixel(0, 0);
                for (int y = 0; y < 144 && !renderedContent; y++)
                    for (int x = 0; x < 160; x++)
                        if (frame.pixel(x, y) != first) { renderedContent = true; break; }
            }
            audioBytes += worker.takeAudio().size();
        }
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
        auto expectClock=[&](uint32_t hz){QElapsedTimer timeout;timeout.start();while(timeout.elapsed()<1500){if(worker.execute([hz](GB_gameboy_t*g){return GB_get_clock_rate(g)==hz?QString():QString("pending");}).isEmpty())return true;QThread::msleep(5);}return false;};
        updated.cpuFrequency=1;worker.updateSettings(updated);if(!expectClock(4194304)){std::fputs("Forced 4 MHz clock was not applied\n",stderr);return 1;}
        updated.cpuFrequency=2;worker.updateSettings(updated);if(!expectClock(8388608)){std::fputs("Forced 8 MHz clock was not applied\n",stderr);return 1;}
        updated.cpuFrequency=0;worker.updateSettings(updated);QElapsedTimer autoClock;autoClock.start();bool restoredClock=false;while(autoClock.elapsed()<1500){if(worker.execute([](GB_gameboy_t*g){return GB_get_clock_rate(g)==GB_get_unmultiplied_clock_rate(g)?QString():QString("pending");}).isEmpty()){restoredClock=true;break;}QThread::msleep(5);}if(!restoredClock){std::fputs("Automatic clock did not restore hardware rate\n",stderr);return 1;}
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
        QThread::msleep(100);
        if (worker.takeAudio().isEmpty()) { std::fprintf(stderr, "Rewind audio failed\n"); return 1; }
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
