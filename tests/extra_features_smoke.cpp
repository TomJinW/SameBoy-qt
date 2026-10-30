#include "../src/emulator.h"
#include "../src/locallink.h"
#include "../src/gbsplayer.h"
#include "test_rom.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QComboBox>
#include <QPushButton>
#include <QFile>
#include <QElapsedTimer>
#include <cstdio>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Extra feature failure line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
 QApplication app(argc,argv);QTemporaryDir temp(QDir::currentPath()+"/features-XXXXXX");CHECK(temp.isValid());
 QByteArray rom(32768,0);make_test_rom(reinterpret_cast<uint8_t*>(rom.data()),true);AppSettings settings;settings.hardware=2;settings.rewindSeconds=0;settings.capTurbo=true;settings.turboPercent=200;
 Emulator first(rom,{},temp.filePath("one.sav"),true,settings),second(rom,{},temp.filePath("two.sav"),true,settings);
 first.setPaused(true);second.setPaused(true);first.start();second.start();QThread::msleep(30);
 auto setup=[](GB_gameboy_t*g){GB_write_memory(g,0xff50,1);GB_get_registers(g)->pc=0x150;GB_write_memory(g,0xff40,0x91);return QString();};
 CHECK(first.execute(setup).isEmpty());CHECK(second.execute(setup).isEmpty());
 auto link=std::make_shared<LocalLink>();CHECK(first.setLocalLink(link,0).isEmpty());CHECK(second.setLocalLink(link,1).isEmpty());
 CHECK(second.execute([](GB_gameboy_t*g){GB_write_memory(g,0xff01,0xa5);GB_write_memory(g,0xff02,0x80);return QString();}).isEmpty());
 CHECK(first.execute([](GB_gameboy_t*g){GB_write_memory(g,0xff01,0x3c);GB_write_memory(g,0xff02,0x81);return QString();}).isEmpty());
 first.setPaused(false);second.setPaused(false);QThread::msleep(150);first.setPaused(true);second.setPaused(true);
 CHECK(first.execute([](GB_gameboy_t*g){return GB_read_memory(g,0xff01)==0xa5&&!(GB_read_memory(g,0xff02)&0x80)?QString():QString("First serial mismatch");}).isEmpty());
 CHECK(second.execute([](GB_gameboy_t*g){return GB_read_memory(g,0xff01)==0x3c&&!(GB_read_memory(g,0xff02)&0x80)?QString():QString("Second serial mismatch");}).isEmpty());
 // Either console can request Turbo; both must observe the same pacing policy.
 first.setActions(4);first.setPaused(false);second.setPaused(false);
 auto waitSpeed=[&](bool turbo){QElapsedTimer clock;clock.start();while(clock.elapsed()<3000){QThread::msleep(50);if(turbo?(first.emulationSpeed()>1.3&&second.emulationSpeed()>1.3):(first.emulationSpeed()<1.25&&second.emulationSpeed()<1.25))return true;}return false;};CHECK(waitSpeed(true));
 std::printf("Linked Turbo side 0: %.2fx / %.2fx\n",first.emulationSpeed(),second.emulationSpeed());
 CHECK(first.emulationSpeed()>1.3&&second.emulationSpeed()>1.3);
 {auto guard=link->lock();CHECK(link->speedLocked()==2);}
 first.setActions(0);CHECK(waitSpeed(false));
 CHECK(first.emulationSpeed()<1.25&&second.emulationSpeed()<1.25);
 second.setActions(4);CHECK(waitSpeed(true));
 first.setPaused(true);second.setPaused(true);second.setActions(0);
 CHECK(first.execute([](GB_gameboy_t*g){GB_write_memory(g,0xff01,0x5a);GB_write_memory(g,0xff02,0x80);return QString();}).isEmpty());
 CHECK(second.execute([](GB_gameboy_t*g){GB_write_memory(g,0xff01,0xc3);GB_write_memory(g,0xff02,0x81);return QString();}).isEmpty());
 second.setActions(4);first.setPaused(false);second.setPaused(false);QThread::msleep(100);first.setPaused(true);second.setPaused(true);
 CHECK(first.execute([](GB_gameboy_t*g){return GB_read_memory(g,0xff01)==0xc3?QString():QString("Turbo serial mismatch");}).isEmpty());
 CHECK(second.execute([](GB_gameboy_t*g){return GB_read_memory(g,0xff01)==0x5a?QString():QString("Turbo peer serial mismatch");}).isEmpty());
 second.setActions(0);
 CHECK(second.execute([](GB_gameboy_t*g){GB_write_memory(g,0xff56,0xc0);return QString();}).isEmpty());
 CHECK(first.execute([](GB_gameboy_t*g){GB_write_memory(g,0xff56,1);return QString();}).isEmpty());
 CHECK(second.execute([](GB_gameboy_t*g){for(int i=0;i<20000;++i){GB_run(g);if(!(GB_read_memory(g,0xff56)&2))return QString();}return QString("Infrared input not received");}).isEmpty());
 settings.developerMode=true;first.updateSettings(settings);second.updateSettings(settings);
 first.setPaused(false);second.setPaused(false);first.sendDebugCommand("interrupt");
 QElapsedTimer debugWait;debugWait.start();while(!first.debuggerStopped()&&debugWait.elapsed()<2000){app.processEvents();QThread::msleep(2);}CHECK(first.debuggerStopped());
 CHECK(second.execute([](GB_gameboy_t*){return QString();}).isEmpty());
 const auto step=first.sendDebugCommand("step");debugWait.restart();while(first.completedDebugCommand()<step&&debugWait.elapsed()<2000){app.processEvents();QThread::msleep(2);}CHECK(first.completedDebugCommand()>=step&&first.debuggerStopped());
 first.sendDebugCommand("continue");debugWait.restart();while(first.debuggerStopped()&&debugWait.elapsed()<2000){app.processEvents();QThread::msleep(2);}CHECK(!first.debuggerStopped());
 CHECK(first.setLocalLink({}).isEmpty());CHECK(second.setLocalLink({}).isEmpty());
 first.setPaused(false);second.setPaused(false);QThread::msleep(50);first.requestInterruption();second.requestInterruption();CHECK(first.wait(2000)&&second.wait(2000));
 QByteArray gbs(112+64,0);gbs.replace(0,4,QByteArray("GBS\1",4));gbs[4]=2;gbs[5]=1;
 auto word=[&](int offset,unsigned value){gbs[offset]=char(value);gbs[offset+1]=char(value>>8);};word(6,0x400);word(8,0x400);word(10,0x430);word(12,0xfffe);
 gbs.replace(16,15,"SameBoy Qt Test");gbs.replace(48,16,"Original fixture");gbs.replace(80,15,"Public test GBS");
 const auto code=QByteArray::fromHex("f53e80e0263e77e0243effe0253e80e0113ef3e0123e00e0133e87e014f1ea00c0c9");gbs.replace(112,code.size(),code);gbs[112+48]=char(0xc9);
 auto*probe=GB_init(GB_alloc(),GB_MODEL_CGB_E);GB_gbs_info_t info{};CHECK(!GB_load_gbs_from_buffer(probe,reinterpret_cast<const uint8_t*>(gbs.constData()),gbs.size(),&info));GB_free(probe);GB_dealloc(probe);CHECK(info.track_count==2&&info.first_track==0);
 Emulator music(gbs,{},temp.filePath("music.sav"),true,settings);music.start();QThread::msleep(200);auto audio=music.takeAudio();CHECK(audio.size()>1000);bool nonzero=false;for(auto c:audio)nonzero|=c!=0;CHECK(nonzero);
 CHECK(music.selectGbsTrack(1).isEmpty());QThread::msleep(50);CHECK(music.execute([](GB_gameboy_t*g){return GB_read_memory(g,0xc000)==1?QString():QString("GBS track not switched");}).isEmpty());
 CHECK(!music.selectGbsTrack(2).isEmpty());music.setPaused(true);QThread::msleep(30);music.takeAudio();QThread::msleep(30);CHECK(music.takeAudio().isEmpty());music.requestInterruption();CHECK(music.wait(2000));
 GbsPlayer player;player.setAttribute(Qt::WA_DontShowOnScreen);player.setInfo(info);player.setSamples(audio);player.resize(700,400);player.show();app.processEvents();auto*tracks=player.findChild<QComboBox*>("gbsTracks");CHECK(tracks&&tracks->count()==2);int selected=-1;player.trackRequested=[&](int t){selected=t;};tracks->setCurrentIndex(1);CHECK(selected==1);
 if(argc>1){CHECK(player.grab().save(QString::fromLocal8Bit(argv[1])+"/gbs-player.png"));QFile f(QString::fromLocal8Bit(argv[1])+"/test.gbs");CHECK(f.open(QIODevice::WriteOnly)&&f.write(gbs)==gbs.size());}
 std::puts("Two worker cores: serial byte exchange, infrared, detach/resume; GBS metadata, PCM, track selection and UI passed.");
}
