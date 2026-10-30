#pragma once
#include "boundedfile_io.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <future>
#include "../src/stateio.h"
#include "../src/coreallocation.h"
#include "../src/requestsession.h"
inline bool workerLifecycle(QCoreApplication&app,const QByteArray&rom,const QString&directory){
 auto failAllocation=[]()->GB_gameboy_t*{return nullptr;};
 if(allocateGameBoy(GB_MODEL_DMG_B,failAllocation)!=nullptr){std::fputs("Core allocation failure was not propagated\n",stderr);return false;}
 if(!boundedFileIo(directory))return false;
 static_assert(InputClock::is_steady);
 InputLease lease;const auto moment=InputClock::time_point{}+std::chrono::seconds(100);
 lease.keys={1,2,4,8};lease.deadlines={moment-std::chrono::milliseconds(1),moment,moment+std::chrono::milliseconds(1),InputClock::time_point{}};
 if(lease.active(moment)!=std::array<uint8_t,4>{0,0,4,0}||lease.active(moment+std::chrono::seconds(1))!=std::array<uint8_t,4>{})return false;

 {
  const auto reloadPath=QDir(directory).filePath("reload.gb");QFile source(reloadPath);if(!source.open(QIODevice::WriteOnly)||source.write(QByteArray(112,'x'))!=112)return false;source.close();
  AppSettings settings;settings.developerMode=true;
  Emulator engine(rom,{},QDir(directory).filePath("reload.sav"),true,settings);engine.setPaused(true);engine.start();
  if(!engine.loadDebugSymbols(reloadPath,{},{}).isEmpty())return false;
  const auto error=engine.execute([&](GB_gameboy_t*g){engine.captureDebugOutput(g,"reset reload");size_t size=0;const auto*data=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_ROM,&size,nullptr));return size==size_t(rom.size())&&std::memcmp(data,rom.constData(),rom.size())==0?QString():QString("Invalid reload replaced the running cartridge");});
  if(!error.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(error));return false;}
  const auto damagedState=engine.execute([&](GB_gameboy_t*g){
   size_t length=0;auto*ram=static_cast<uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_RAM,&length,nullptr));std::memset(ram,0x42,length);
   QByteArray state(GB_get_save_state_size(g),0);GB_save_state_to_buffer(g,reinterpret_cast<uint8_t*>(state.data()));
   const auto start=state.indexOf(QByteArray(4096,0x42));if(start<0)return QString("Cannot find state RAM fixture");
   state.truncate(start+length/2);std::memset(ram,0x77,length);
   if(!loadStatePreservingOnFailure(g,state))return QString("Truncated state was accepted");
   for(size_t i=0;i<length;++i)if(ram[i]!=0x77)return QString("Failed state load modified live RAM");
   return QString();
  });
  if(!damagedState.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(damagedState));return false;}
  auto updated=rom;updated[0x150]=char(uint8_t(updated[0x150])^0xff);
  if(!source.open(QIODevice::WriteOnly|QIODevice::Truncate)||source.write(updated)!=updated.size())return false;source.close();
  const auto success=engine.execute([&](GB_gameboy_t*g){GB_write_memory(g,0,0x0a);GB_write_memory(g,0xa000,0x69);engine.captureDebugOutput(g,"reset reload");size_t size=0;const auto*data=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_ROM,&size,nullptr));if(size!=size_t(updated.size())||std::memcmp(data,updated.constData(),updated.size()))return QString("Valid reload did not update ROM");const auto*ram=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_CART_RAM,&size,nullptr));return size&&ram[0]==0x69?QString():QString("Reload lost live battery RAM");});
  if(!success.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(success));return false;}
  engine.requestInterruption();if(!engine.wait(2000))return false;
 }
 const auto sameSave=QDir(directory).filePath("same-battery.sav");QFile disk(sameSave);if(!disk.open(QIODevice::WriteOnly)||disk.write(QByteArray(8192,'\x17'))!=8192)return false;disk.close();
 {Emulator swapped(rom,{},sameSave,true);swapped.setPaused(true);swapped.start();if(!swapped.execute([](GB_gameboy_t*g){GB_write_memory(g,0,0x0a);GB_write_memory(g,0xa000,0x42);return QString();}).isEmpty())return false;
  if(swapped.swapCartridge(QByteArray(16,'x'),sameSave).isEmpty())return false;
  const auto blocked=QDir(directory).filePath("blocked-target.sav");if(!QDir().mkdir(blocked))return false;
  if(swapped.swapCartridge(rom,blocked).isEmpty())return false;
  if(!swapped.swapCartridge(rom,QDir(directory).filePath("./same-battery.sav")).isEmpty())return false;
  auto retained=swapped.execute([](GB_gameboy_t*g){size_t size=0;auto*ram=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_CART_RAM,&size,nullptr));return size&&ram[0]==0x42?QString():QString("Hot swap reloaded stale disk battery over current progress");});if(!retained.isEmpty()){std::fprintf(stderr,"%s\n",qPrintable(retained));return false;}
  swapped.requestInterruption();if(!swapped.wait(2000))return false;
 }
 const auto largePath=QDir(directory).filePath("oversized.sav");const qint64 oversized=16*1024*1024+1;
 {QFile large(largePath);if(!large.open(QIODevice::WriteOnly)||large.write("preserve this file")!=18||!large.resize(oversized))return false;}
 const auto migratedPath=QDir(directory).filePath("oversized-legacy-new.sav");
 for(bool legacy:{false,true}){Emulator large(rom,{},legacy?migratedPath:largePath,true,{},48000,{},legacy?largePath:QString());large.setPaused(true);large.start();if(!large.execute([](GB_gameboy_t*){return QString();}).isEmpty())return false;
  const bool reported=!large.takeError().isEmpty();large.requestInterruption();if(!large.wait(2000)||!reported)return false;}
 if(QFile::exists(migratedPath)){std::fputs("Invalid oversized legacy battery was migrated to a new save\n",stderr);return false;}
 {QFile large(largePath);if(!large.open(QIODevice::ReadOnly)||large.size()!=oversized||large.read(18)!="preserve this file"){std::fputs("Oversized battery file was silently overwritten on exit\n",stderr);return false;}}
 const auto shortPath=QDir(directory).filePath("legacy-short.sav");{QFile file(shortPath);if(!file.open(QIODevice::WriteOnly)||file.write(QByteArray(1,char(0x56)))!=1)return false;}
 {Emulator shortSave(rom,{},shortPath,true);shortSave.setPaused(true);shortSave.start();
  const auto error=shortSave.execute([](GB_gameboy_t*g){size_t size=0;const auto*ram=static_cast<const uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_CART_RAM,&size,nullptr));return size&&ram[0]==0x56?QString():QString("Compatible short battery was rejected");});
  shortSave.requestInterruption();if(!shortSave.wait(2000)||!error.isEmpty())return false;}
 const auto path=QDir(directory).filePath("unreadable.sav");if(!QDir().mkdir(path))return false;
 {Emulator engine(rom,{},path,true);engine.setPaused(true);engine.start();if(!engine.execute([](GB_gameboy_t*){return QString();}).isEmpty())return false;
  if(engine.takeError().isEmpty())return false;
  if(!QDir().rmdir(path))return false;QFile recovered(path);if(!recovered.open(QIODevice::WriteOnly)||recovered.write("original battery")!=16)return false;recovered.close();
  engine.requestInterruption();if(!engine.wait(2000))return false;
 }
 QFile original(path);if(!original.open(QIODevice::ReadOnly)||original.readAll()!="original battery"){std::fputs("Failed battery read overwrote the existing save on shutdown\n",stderr);return false;}
 Emulator engine(rom,{},QDir(directory).filePath("queued.sav"),true);engine.setPaused(true);engine.start();if(!engine.execute([](GB_gameboy_t*){return QString();}).isEmpty())return false;
 std::atomic<bool>entered=false,release=false;int callbacks=0;QString cancelError;
 engine.executeAsync(&app,[&](GB_gameboy_t*){entered=true;while(!release.load())QThread::msleep(1);return QString();},[&](QString){++callbacks;});
 QElapsedTimer deadline;deadline.start();while(!entered&&deadline.elapsed()<2000)QThread::msleep(1);if(!entered){release=true;return false;}
 engine.executeAsync(&app,[](GB_gameboy_t*){return QString();},[&](QString error){cancelError=error;++callbacks;});
 auto synchronous=std::async(std::launch::async,[&]{return engine.execute([](GB_gameboy_t*){return QString();});});
 engine.requestInterruption();release=true;if(!engine.wait(2000))return false;
 if(synchronous.wait_for(std::chrono::seconds(1))!=std::future_status::ready||synchronous.get().isEmpty())return false;
 deadline.restart();while(callbacks<2&&deadline.elapsed()<1000){app.processEvents();QThread::msleep(1);}
 if(callbacks!=2||cancelError.isEmpty())return false;
 Emulator captured(rom,{},QDir(directory).filePath("capture.sav"),true);captured.setPaused(true);captured.start();if(!captured.execute([](GB_gameboy_t*){return QString();}).isEmpty())return false;captured.setVideoCapture(true);bool stepped=false;QString stepError;
 captured.stepFrames(&app,10,{},[&](QString error,QImage,int frames){stepError=error;stepped=frames==10;});deadline.restart();while(!stepped&&stepError.isEmpty()&&deadline.elapsed()<2000){app.processEvents();QThread::msleep(1);}if(!stepped||!stepError.isEmpty())return false;auto packets=captured.takeVideoFrames();if(packets.size()!=10)return false;for(const auto&packet:packets)if(packet.image.isNull()||packet.pcm.isEmpty()||packet.frameRate<59||packet.frameRate>62)return false;captured.setVideoCapture(false);
 EmulatorInput leasedInput;leasedInput.keys[0]=GB_KEY_UP_MASK;leasedInput.lease.keys[0]=GB_KEY_RIGHT_MASK;
 leasedInput.lease.deadlines[0]=InputClock::now()+std::chrono::milliseconds(400);captured.setInput(leasedInput);captured.setPaused(false);
 auto joypad=[](GB_gameboy_t*g){GB_write_memory(g,0xff00,0x20);return GB_read_memory(g,0xff00);};
 QThread::msleep(40);uint8_t held=255;captured.execute([&](GB_gameboy_t*g){held=joypad(g);return QString();});
 if((held&5)!=0){std::fputs("Leased or physical input was not applied\n",stderr);captured.requestInterruption();captured.wait();return false;}
 QThread::msleep(450);uint8_t released=255;captured.execute([&](GB_gameboy_t*g){released=joypad(g);return QString();});
 if((released&5)!=1){std::fputs("Lease expiration lost physical input or retained script input\n",stderr);captured.requestInterruption();captured.wait();return false;}
 captured.requestInterruption();if(!captured.wait(2000))return false;
 // Hold the real core queue, end a request session, then release queued work.
 Emulator requests(rom,{},QDir(directory).filePath("requests.sav"),true);requests.setPaused(true);requests.start();
 if(!requests.execute([](GB_gameboy_t*g){GB_write_memory(g,0xc222,0x19);return QString();}).isEmpty())return false;
 std::atomic<bool> blocked=false,unblock=false;requests.executeAsync(&app,[&](GB_gameboy_t*){blocked=true;while(!unblock.load())QThread::msleep(1);return QString();},[](QString){});
 deadline.restart();while(!blocked&&deadline.elapsed()<1000)QThread::msleep(1);if(!blocked){unblock=true;requests.requestInterruption();requests.wait();return false;}
 auto oldSession=std::make_shared<RequestSession>();
 requests.executeAsync(&app,[oldSession](GB_gameboy_t*g){return oldSession->run([&]{GB_write_memory(g,0xc222,0x88);return QString();});},[](QString){});
 oldSession->close();unblock=true;uint8_t afterClose=0;
 requests.execute([&](GB_gameboy_t*g){afterClose=GB_read_memory(g,0xc222);return QString();});
 auto startedSession=std::make_shared<RequestSession>();blocked=false;unblock=false;
 requests.executeAsync(&app,[&](GB_gameboy_t*g){return startedSession->run([&]{blocked=true;while(!unblock.load())QThread::msleep(1);GB_write_memory(g,0xc223,0x77);return QString();});},[](QString){});
 deadline.restart();while(!blocked&&deadline.elapsed()<1000)QThread::msleep(1);if(!blocked){unblock=true;requests.requestInterruption();requests.wait();return false;}
 startedSession->close();unblock=true;uint8_t finished=0;requests.execute([&](GB_gameboy_t*g){finished=GB_read_memory(g,0xc223);return QString();});
 requests.requestInterruption();const auto afterStopCommand=requests.sendDebugCommand("continue");if(afterStopCommand){requests.wait(2000);std::fputs("Debugger accepted a command after worker shutdown began\n",stderr);return false;}if(!requests.wait(2000)||finished!=0x77)return false;
 if(afterClose!=0x19){std::fputs("Closed request session still executed queued core mutation\n",stderr);return false;}
 // Session churn cannot bypass the worker's total queue capacity.
 Emulator bounded(rom,{},QDir(directory).filePath("bounded.sav"),true);bounded.setPaused(true);bounded.start();
 if(!bounded.execute([](GB_gameboy_t*){return QString();}).isEmpty())return false;
 blocked=false;unblock=false;int rejected=0,delivered=0,executed=0;
 bounded.executeAsync(&app,[&](GB_gameboy_t*){blocked=true;while(!unblock.load())QThread::msleep(1);return QString();},[](QString){});
 deadline.restart();while(!blocked&&deadline.elapsed()<1000)QThread::msleep(1);
 if(!blocked){unblock=true;bounded.requestInterruption();bounded.wait();return false;}
 for(int i=0;i<512;++i)bounded.executeAsync(&app,[&](GB_gameboy_t*){++executed;return QString();},[&](QString error){++delivered;if(!error.isEmpty())++rejected;});
 app.processEvents();const int rejectedWhileBlocked=rejected;unblock=true;
 deadline.restart();while(delivered<512&&deadline.elapsed()<2000){app.processEvents();QThread::msleep(1);}
 bounded.requestInterruption();if(!bounded.wait(2000))return false;
 if(rejectedWhileBlocked!=256||delivered!=512||executed!=256){std::fputs("Core queue capacity was not bounded or requests were lost\n",stderr);return false;}
 const auto discardPath=QDir(directory).filePath("canceled-discard.sav");
 {Emulator discarded(rom,{},discardPath,true);discarded.setPaused(true);discarded.start();
  if(!discarded.execute([](GB_gameboy_t*g){GB_write_memory(g,0,0x0a);GB_write_memory(g,0xa000,0x7a);return QString();}).isEmpty())return false;
  discarded.discardBatteryOnClose();discarded.discardBatteryOnClose(false);discarded.requestInterruption();if(!discarded.wait(2000))return false;
 }
 QFile finalBattery(discardPath);if(!finalBattery.open(QIODevice::ReadOnly)||finalBattery.read(1)!=QByteArray(1,char(0x7a))){std::fputs("Canceled discard suppressed a subsequent normal battery save\n",stderr);return false;}
 RequestSession fresh;bool ran=false;if(!fresh.run([&]{ran=true;return QString();}).isEmpty()||!ran)return false;
 std::puts("Worker lifecycle: failed-read save preservation and shutdown request completion passed.");return true;
}
