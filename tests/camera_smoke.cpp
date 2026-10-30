#include "../src/camerainput.h"
#include "../src/camerasource.h"
#include "../src/emulator.h"
#include "test_rom.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <cstdio>
#include <QVideoSink>
#include <QVideoFrame>
#include <QEvent>
#include <QPainter>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Camera check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
static QString trigger(GB_gameboy_t*g){
 GB_write_memory(g,0,0x0a);GB_write_memory(g,0x4000,0x10);GB_write_memory(g,0xa001,4);GB_write_memory(g,0xa002,0x10);GB_write_memory(g,0xa003,0);
 for(int i=0;i<16;++i){GB_write_memory(g,0xa006+i*3,64);GB_write_memory(g,0xa007+i*3,128);GB_write_memory(g,0xa008+i*3,192);}GB_write_memory(g,0xa000,1);return {};
}
static QString done(GB_gameboy_t*g){GB_write_memory(g,0x4000,0x10);return (GB_read_memory(g,0xa000)&1)?QString("Camera remained busy"):QString();}
static QString checkTiles(GB_gameboy_t*g,int black){GB_write_memory(g,0x4000,0);return GB_read_memory(g,0xa100)==(black?255:0)&&GB_read_memory(g,0xa1f0)==0?QString():QString("Captured pixels mismatch");}
int main(int argc,char**argv){QApplication app(argc,argv);
 if(app.arguments().contains("--image-memory")){
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS_EX before{},after{};
  CHECK(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before)));
#endif
  QImage narrow(1,2048,QImage::Format_RGB32);narrow.fill(Qt::white);
  const auto result=prepareCameraImage(narrow);CHECK(result.size()==QSize(128,112)&&result.constScanLine(0)[0]==255);
#ifdef Q_OS_WIN
  CHECK(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after)));
  const auto growth=after.PeakPagefileUsage>before.PeakPagefileUsage?after.PeakPagefileUsage-before.PeakPagefileUsage:0;
  std::printf("Camera image peak commit growth: %llu bytes\n",static_cast<unsigned long long>(growth));
  CHECK(growth<=32*1024*1024);
#endif
  return 0;
 }
 QTemporaryDir temp(QDir::currentPath()+"/camera-XXXXXX");CHECK(temp.isValid());
 QImage source(130,114,QImage::Format_RGB32);source.fill(Qt::white);for(int y=0;y<114;++y)for(int x=0;x<65;++x)source.setPixelColor(x,y,Qt::black);
 const auto prepared=prepareCameraImage(source);CHECK(prepared.size()==QSize(128,112)&&prepared.format()==QImage::Format_Grayscale8);CHECK(prepared.constScanLine(0)[0]==0&&prepared.constScanLine(0)[127]==255);
 CHECK(prepareCameraImage(source,true).constScanLine(0)[0]==255);CHECK(prepareCameraImage({}).isNull());
 for(const QSize size:{QSize(640,480),QSize(480,640),QSize(1920,1080)}){
  const auto normal=source.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  const auto legacy=normal.scaled(QSize(130,114),Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
  CHECK(prepareCameraImage(normal)==legacy.copy((legacy.width()-128)/2,(legacy.height()-112)/2,128,112).convertToFormat(QImage::Format_Grayscale8));
 }
 for(const QSize size:{QSize(2048,1),QSize(1,2048)}){
  QImage extreme(size,QImage::Format_RGB32);extreme.fill(Qt::white);
  for(int i=960;i<1088;++i)extreme.setPixelColor(size.width()==1?0:i,size.height()==1?0:i,Qt::black);
  const auto crop=prepareCameraImage(extreme);CHECK(crop.size()==QSize(128,112));
  CHECK(crop.constScanLine(0)[0]==0&&crop.constScanLine(111)[127]==0);
 }
 if(app.arguments().contains("--preview")){
  QImage preview(256,112,QImage::Format_RGB32);{QPainter painter(&preview);painter.drawImage(0,0,prepared);painter.drawImage(128,0,prepareCameraImage(source,true));}
  CHECK(preview.scaled(1024,448,Qt::IgnoreAspectRatio,Qt::FastTransformation).save(app.arguments().last()));
 }
 auto input=std::make_shared<CameraInput>();input->configure(CameraInput::Image);input->publish(prepared);
 QByteArray rom(32768,0);make_test_rom(reinterpret_cast<uint8_t*>(rom.data()),false);rom[0x147]=char(0xfc);rom[0x149]=4;
 AppSettings settings;settings.rewindSeconds=0;Emulator worker(rom,{},temp.filePath("camera.sav"),false,settings,48000,{}, {},input);worker.setPaused(true);worker.start();QThread::msleep(40);CHECK(worker.hasCameraCartridge());
 CHECK(worker.execute(trigger).isEmpty());QThread::msleep(30);CHECK(worker.execute(done).isEmpty());CHECK(worker.execute([](GB_gameboy_t*g){return checkTiles(g,1);}).isEmpty());
 QImage white(128,112,QImage::Format_Grayscale8);white.fill(255);input->publish(white);CHECK(worker.execute([](GB_gameboy_t*g){return checkTiles(g,1);}).isEmpty()); // A shot never mixes later live frames.
 CHECK(worker.execute(trigger).isEmpty());QThread::msleep(30);CHECK(worker.execute([](GB_gameboy_t*g){return checkTiles(g,0);}).isEmpty());
 input->configure(CameraInput::Device);input->publish(prepared);CHECK(worker.execute(trigger).isEmpty());QThread::msleep(30);
 CHECK(worker.execute([](GB_gameboy_t*g){return GB_read_memory(g,0xa000)&1?QString():QString("Live shot did not wait for a new frame");}).isEmpty());
 input->publish(white);QThread::msleep(30);CHECK(worker.execute(done).isEmpty());CHECK(worker.execute([](GB_gameboy_t*g){return checkTiles(g,0);}).isEmpty());
 CHECK(worker.execute(trigger).isEmpty());QThread::msleep(20);input->fail();QThread::msleep(30);CHECK(worker.execute(done).isEmpty());
 worker.requestInterruption();CHECK(worker.wait(2000));
 const auto path=temp.filePath(QString::fromUtf8("摄像头.png"));CHECK(source.save(path));auto imported=std::make_shared<CameraInput>();CameraSource camera(imported);settings.cameraMode=2;settings.cameraImage=path;camera.configure(settings);CHECK(imported->snapshot().image==prepared);
 QImage narrowImport(1,2048,QImage::Format_RGB32);narrowImport.fill(Qt::black);
 const auto narrowPath=temp.filePath("narrow.png");CHECK(narrowImport.save(narrowPath));settings.cameraImage=narrowPath;camera.configure(settings);
 CHECK(!imported->snapshot().failed&&imported->snapshot().image.size()==QSize(128,112));
 CHECK(imported->snapshot().image.constScanLine(0)[0]==0);
 settings.cameraImage=temp.filePath("missing.png");camera.configure(settings);CHECK(imported->snapshot().failed);
 settings.cameraMode=0;camera.configure(settings);CHECK(imported->snapshot().mode==CameraInput::Noise&&!imported->snapshot().failed);
 settings.cameraMode=CameraInput::Device;camera.configure(settings);camera.poll(true);
 auto*oldSink=camera.findChild<QVideoSink*>();CHECK(oldSink);
 // Queue a frame from the old backend, then change source before GUI delivery.
 std::thread oldBackend([&]{oldSink->videoFrameChanged(QVideoFrame(source));});oldBackend.join();
 settings.cameraDevice="different-device";camera.configure(settings);camera.poll(true);
 QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
 CHECK(imported->snapshot().image.isNull());
 auto*currentSink=camera.findChild<QVideoSink*>();CHECK(currentSink);
 currentSink->videoFrameChanged(QVideoFrame(source));CHECK(imported->snapshot().image==prepared);
 const auto version=imported->snapshot().version;
 QImage obsolete(130,114,QImage::Format_RGB32);obsolete.fill(Qt::white);
 std::thread pausedBackend([&]{currentSink->videoFrameChanged(QVideoFrame(obsolete));});pausedBackend.join();
 camera.poll(false);camera.poll(true);QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
 CHECK(imported->snapshot().version==version);

 std::puts("Camera crop/grayscale/mirror, core busy/completion, frozen shot, fresh live frame, failure completion, Unicode image import and noise mode passed.");}
