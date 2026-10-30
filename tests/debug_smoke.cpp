#include "../src/requestsession.h"
#include "../src/debugviews.h"
#include "test_rom.h"
#include "../src/emulator.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QCheckBox>
#include <QTimer>
#include <QTableWidget>
#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QTableView>
#include <QLineEdit>
#include <QTabWidget>
#include <QDir>
#include <cstdio>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Debug failure at %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){QApplication app(argc,argv);auto*gb=GB_init(GB_alloc(),GB_MODEL_CGB_E);GB_set_rgb_encode_callback(gb,[](GB_gameboy_t*,uint8_t r,uint8_t g,uint8_t b){return qRgb(r,g,b);});
 uint8_t rom[32768];make_test_rom(rom,true);GB_load_rom_from_buffer(gb,rom,sizeof(rom));GB_write_memory(gb,0xc123,0x5a);
 auto memory=debugMemory(gb,0,0);CHECK(memory.bytes.size()==65536&&uint8_t(memory.bytes[0xc123])==0x5a);
 for(int offset:{0,0x3ff0,0x7ff0,0x9ff0,0xbff0,0xcff0,0xeff0,0xfff0}){auto range=debugMemory(gb,0,0,offset,48);CHECK(range.totalSize==65536&&range.offset==offset&&range.bytes==memory.bytes.mid(offset,48));}
 size_t size=0;auto*vram=static_cast<uint8_t*>(GB_get_direct_access(gb,GB_DIRECT_ACCESS_VRAM,&size,nullptr));CHECK(size==16384);vram[0]=0xaa;vram[1]=0;vram[8192]=0x55;
 auto bank=debugMemory(gb,2,1);CHECK(bank.banks==2&&bank.base==0x8000&&bank.bytes.size()==8192&&uint8_t(bank.bytes[0])==0x55);
 auto*oam=static_cast<uint8_t*>(GB_get_direct_access(gb,GB_DIRECT_ACCESS_OAM,&size,nullptr));for(size_t i=0;i<size;++i)oam[i]=0;oam[0]=16;oam[1]=8;
 auto tiles=debugVram(gb,0,0,0,0);CHECK(tiles.image.size()==QSize(256,192)&&tiles.image.pixel(0,0)!=tiles.image.pixel(1,0));
 GB_write_memory(gb,0xff43,240);GB_write_memory(gb,0xff42,248);
 vram[0x1800]=0x19;vram[0x3800]=0x0b;
 auto mapSnapshot=debugVram(gb,1,1,1,2);CHECK(mapSnapshot.tiles.size()==1024&&mapSnapshot.tiles[0].tile==0x19&&mapSnapshot.tiles[0].bank==1&&mapSnapshot.tiles[0].address==0x9190&&mapSnapshot.tiles[0].mapAddress==0x9800);CHECK(debugVram(gb,1,1,1,1).tiles[0].address==0x8190);CHECK(mapSnapshot.image.size()==QSize(256,256));CHECK(mapSnapshot.scrollRect==QRect(240,248,160,144));auto objects=debugVram(gb,2,0,0,0);CHECK(objects.objects.size()==1&&objects.objects[0].address==0xfe00&&objects.objects[0].x==0&&objects.objects[0].y==0);
 auto*bgp=static_cast<uint8_t*>(GB_get_direct_access(gb,GB_DIRECT_ACCESS_BGP,nullptr,nullptr));bgp[0]=0x34;bgp[1]=0x92;
 auto paletteSnapshot=debugVram(gb,3,0,0,0);CHECK(paletteSnapshot.palettes.size()==16&&paletteSnapshot.rawPalettes[0][0]==0x9234);
 GB_set_background_rendering_disabled(gb,true);GB_set_object_rendering_disabled(gb,true);CHECK(GB_is_background_rendering_disabled(gb)&&GB_is_object_rendering_disabled(gb));
 for(int i=0;i<4;++i){GB_set_channel_muted(gb,GB_channel_t(i),true);CHECK(GB_is_channel_muted(gb,GB_channel_t(i)));GB_set_channel_muted(gb,GB_channel_t(i),false);}
 DebugExecutor execute=[gb](auto command){return command(gb);};auto*memoryDialog=new MemoryDialog(execute,QFont("Consolas",12));memoryDialog->setAttribute(Qt::WA_DontShowOnScreen);memoryDialog->show();app.processEvents();
 auto*spaces=memoryDialog->findChild<QComboBox*>("memorySpace");auto*banks=memoryDialog->findChild<QSpinBox*>("memoryBank");CHECK(spaces&&banks);CHECK(spaces->count()==5&&spaces->itemText(2)=="Video RAM"&&spaces->itemText(4)=="RAM");spaces->setCurrentIndex(2);banks->setValue(1);auto*table=memoryDialog->findChild<QTableView*>();CHECK(table->model()->property("snapshotBytes").toInt()<2048);CHECK(table->model()->data(table->model()->index(0,0)).toString()=="55");
 CHECK(table->model()->setData(table->model()->index(0,0),"7F",Qt::EditRole));CHECK(vram[8192]==0x7f&&vram[0]==0xaa);
 CHECK(!table->model()->setData(table->model()->index(0,0),"100",Qt::EditRole));CHECK(!table->model()->setData(table->model()->index(0,16),"00",Qt::EditRole));
 CHECK(writeDebugMemory(gb,0,0,0xc123,0xa6).isEmpty()&&GB_safe_read_memory(gb,0xc123)==0xa6);
 CHECK(writeDebugMemory(gb,0,0,0x150,0x99).isEmpty()&&uint8_t(debugMemory(gb,0,0).bytes[0x150])==0x99);
 CHECK(writeDebugMemory(gb,1,1,0x4000,0x42).isEmpty()&&uint8_t(debugMemory(gb,1,1).bytes[0x4000])==0x42);
 GB_clear_battery_dirty(gb);CHECK(writeDebugMemory(gb,3,0,0,0x66).isEmpty()&&GB_get_battery_dirty(gb));
 auto ram=debugMemory(gb,4,2);CHECK(ram.base==0xc000&&ram.bytes.size()==0x2000);
 CHECK(writeDebugMemory(gb,4,2,0x1000,0x35).isEmpty());CHECK(uint8_t(debugMemory(gb,4,2).bytes[0x1000])==0x35);CHECK(uint8_t(debugMemory(gb,4,1).bytes[0x1000])!=0x35);
 CHECK(debugMemory(gb,1,1).bytes.size()==0x8000&&debugMemory(gb,1,1).base==0);
 CHECK(!writeDebugMemory(gb,2,99,0,0).isEmpty());
 auto*go=memoryDialog->findChild<QLineEdit*>();go->setText("$8010");CHECK(QMetaObject::invokeMethod(go,"returnPressed",Qt::DirectConnection));CHECK(table->currentIndex().row()==1);
 if(argc>1)CHECK(memoryDialog->grab().save(QString::fromLocal8Bit(argv[1])+"/debug-memory.png"));delete memoryDialog;
 auto*viewer=new VramDialog(execute,QFont("Consolas",12));viewer->setAttribute(Qt::WA_DontShowOnScreen);viewer->show();auto*tabs=viewer->findChild<QTabWidget*>("vramTabs");CHECK(tabs&&tabs->count()==4);
 auto*tilesViewport=viewer->findChild<QWidget*>("vramTilesetViewport");auto*tilesGrid=viewer->findChild<QCheckBox*>("vramTilesetGrid");CHECK(tilesViewport&&tilesGrid);CHECK(tilesViewport->width()==512);
 for(bool grid:{false,true}){tilesGrid->setChecked(grid);app.processEvents();auto capture=tilesViewport->grab().toImage();CHECK(capture.pixelColor(capture.width()-2,capture.height()-2)==tilesViewport->palette().color(QPalette::Mid));if(argc>1)CHECK(viewer->grab().save(QString::fromLocal8Bit(argv[1])+(grid?"/tileset-grid.png":"/tileset-no-grid.png")));}
 for(int tab=0;tab<4;++tab){tabs->setCurrentIndex(tab);app.processEvents();if(tab==1){auto*scrolling=viewer->findChild<QCheckBox*>("vramScrolling");CHECK(scrolling);scrolling->setChecked(true);app.processEvents();}if(argc>1)CHECK(viewer->grab().save(QString::fromLocal8Bit(argv[1])+QString("/debug-vram-%1.png").arg(tab)));}
 auto*format=viewer->findChild<QComboBox*>("vramColorFormat");auto*paletteTable=viewer->findChild<QTableWidget*>("vramPaletteTable");CHECK(format&&format->currentIndex()==0&&paletteTable&&paletteTable->item(0,0)->text()=="$9234");format->setCurrentIndex(1);CHECK(paletteTable->item(0,0)->text().startsWith("#"));bgp[0]=0xcd;bgp[1]=0x12;format->setCurrentIndex(0);viewer->requestRefresh();CHECK(paletteTable->item(0,0)->text()=="$12CD");
 bgp[0]=0xff;bgp[1]=0x7f;bgp[2]=bgp[3]=0;viewer->requestRefresh();CHECK(paletteTable->item(0,0)->foreground().style()!=Qt::NoBrush&&paletteTable->item(0,0)->foreground().color()==Qt::black);CHECK(paletteTable->item(0,1)->foreground().color()==Qt::white&&paletteTable->item(0,1)->background().style()!=Qt::NoBrush&&paletteTable->item(0,1)->background().color()==Qt::black);
 bgp[0]=0x1f;bgp[1]=0;viewer->requestRefresh();CHECK(paletteTable->item(0,0)->foreground().color()==Qt::white);bgp[0]=0xcd;bgp[1]=0x12;
 // Dense objects use wrapping cards with a scrollable content height.
 for(int i=0;i<40;++i){oam[i*4]=16;oam[i*4+1]=8;}tabs->setCurrentIndex(2);app.processEvents();
 auto*objectCards=viewer->findChild<QWidget*>("vramObjects");CHECK(objectCards&&objectCards->property("objectCount").toInt()==40&&objectCards->property("objectColumns").toInt()==4);
 viewer->resize(590,680);app.processEvents();CHECK(objectCards->property("objectColumns").toInt()==4);viewer->resize(640,680);app.processEvents();CHECK(objectCards->property("objectColumns").toInt()==4);
 auto*objectScroll=dynamic_cast<QScrollArea*>(tabs->widget(2));CHECK(objectScroll&&objectScroll->verticalScrollBar()->maximum()>0);
 objectScroll->verticalScrollBar()->setValue(objectScroll->verticalScrollBar()->maximum());const int scrollValue=objectScroll->verticalScrollBar()->value();
 viewer->cartridgeChanged();app.processEvents();CHECK(objectScroll->verticalScrollBar()->value()==scrollValue);
 delete viewer;
 // A tab changed during an in-flight snapshot must request the new tab immediately, without waiting for its polling timer.
 std::function<QString(GB_gameboy_t*)> queued;std::function<void(QString)> deliver;int requests=0;
 DebugAsyncExecutor deferred=[&](QObject*,auto command,auto done){++requests;queued=std::move(command);deliver=std::move(done);};
 auto*pendingViewer=new VramDialog(execute,QFont("Consolas",12),nullptr,deferred);for(auto*timer:pendingViewer->findChildren<QTimer*>())timer->stop();
 auto*pendingTabs=pendingViewer->findChild<QTabWidget*>("vramTabs");pendingTabs->setCurrentIndex(3);CHECK(requests==1);
 {auto command=std::move(queued);auto done=std::move(deliver);done(command(gb));}app.processEvents();CHECK(requests==2&&bool(queued));
 {auto command=std::move(queued);auto done=std::move(deliver);done(command(gb));}auto*currentPalette=pendingViewer->findChild<QTableWidget*>("vramPaletteTable");CHECK(currentPalette->rowCount()==16&&currentPalette->item(0,0)->text()=="$12CD");delete pendingViewer;
 GB_free(gb);GB_dealloc(gb);gb=GB_init(GB_alloc(),GB_MODEL_DMG_B);CHECK(debugVram(gb,3,0,0,0).palettes.size()==3);GB_free(gb);GB_dealloc(gb);

 // Exercise real worker snapshots with both inspectors live; the GUI must never wait for a frame boundary.
 QTemporaryDir temp(QDir::currentPath()+"/debug-live-XXXXXX");CHECK(temp.isValid());
 QFile boot(BOOT_ROM_DIR "/cgb_boot.bin");CHECK(boot.open(QIODevice::ReadOnly));
 Emulator worker(QByteArray(reinterpret_cast<char*>(rom),sizeof(rom)),boot.readAll(),temp.filePath("game.sav"),true);worker.start();
 DebugExecutor sync=[&](auto command){return worker.execute(std::move(command));};
 int completed=0;std::atomic<qint64> coreNanos{0};
 DebugAsyncExecutor async=[&](QObject*context,auto command,auto done){worker.executeAsync(context,[&,command=std::move(command)](GB_gameboy_t*g){QElapsedTimer timer;timer.start();auto error=command(g);coreNanos.fetch_add(timer.nsecsElapsed());return error;},[&,done=std::move(done)](QString error){++completed;done(error);});};
 auto measure=[&](int milliseconds){QElapsedTimer timer;timer.start();int frames=0;while(timer.elapsed()<milliseconds){app.processEvents();if(!worker.takeFrame().isNull())++frames;QThread::msleep(1);}return frames;};
 measure(200);const int baseline=measure(1000);const double baselineSpeed=worker.emulationSpeed();
 auto*liveMemory=new MemoryDialog(sync,QFont("Consolas",12),nullptr,async);liveMemory->setAttribute(Qt::WA_DontShowOnScreen);liveMemory->show();
 auto*liveTable=liveMemory->findChild<QTableView*>();
 auto*liveVram=new VramDialog(sync,QFont("Consolas",12),nullptr,async);liveVram->setAttribute(Qt::WA_DontShowOnScreen);liveVram->show();
 const int inspected=measure(1000);const double inspectedSpeed=worker.emulationSpeed();CHECK(liveTable->model()->property("snapshotBytes").toInt()<2048);CHECK(completed>=10);CHECK(inspectedSpeed>=baselineSpeed*0.85);
 std::printf("Live inspectors: baseline %d fps (%.2fx), memory+VRAM %d fps (%.2fx), %d snapshots, %.3f ms average core snapshot\n",baseline,baselineSpeed,inspected,inspectedSpeed,completed,coreNanos.load()/1e6/completed);
 // Destroy while snapshots are queued: completions must not touch the closed dialogs.
 delete liveMemory;delete liveVram;measure(100);for(unsigned rate:{44100u,48000u,96000u}){CHECK(worker.reconfigureAudio(rate,{}).isEmpty());CHECK(worker.execute([rate](GB_gameboy_t*g){return GB_get_sample_rate(g)==rate?QString():QString("Wrong sample rate");}).isEmpty());}worker.requestInterruption();CHECK(worker.wait(2000));app.processEvents();

 AppSettings developer;developer.developerMode=true;Emulator debugger(QByteArray(reinterpret_cast<char*>(rom),sizeof(rom)),{},temp.filePath("debugger.sav"),true,developer);debugger.start();
 auto until=[&](auto predicate){QElapsedTimer timeout;timeout.start();while(timeout.elapsed()<3000){if(predicate())return true;app.processEvents();QThread::msleep(2);}return false;};
 // A service closed while a native command waits must not interrupt the game later.
 std::atomic<bool> blocked=false,release=false;
 debugger.executeAsync(&app,[&](GB_gameboy_t*){blocked=true;while(!release.load())QThread::msleep(1);return QString();},[](QString){});
 CHECK(until([&]{return blocked.load();}));
 auto endedSession=std::make_shared<RequestSession>();
 CHECK(debugger.sendDebugCommand("interrupt",endedSession));endedSession->close();
 const auto barrier=debugger.sendDebugCommand("registers");release=true;
 CHECK(until([&]{return debugger.completedDebugCommand()>=barrier;}));
 CHECK(!debugger.debuggerStopped());
 auto currentSession=std::make_shared<RequestSession>();
 CHECK(debugger.sendDebugCommand("interrupt",currentSession));CHECK(until([&]{return debugger.debuggerStopped();}));
 CHECK(debugger.execute([](GB_gameboy_t*g){auto*r=GB_get_registers(g);r->pc=0x150;size_t size=0;auto*data=static_cast<uint8_t*>(GB_get_direct_access(g,GB_DIRECT_ACCESS_ROM,&size,nullptr));data[0x150]=0x3e;data[0x151]=0x42;return QString();}).isEmpty());
 auto id=debugger.sendDebugCommand("step");CHECK(until([&]{return debugger.completedDebugCommand()>=id;}));
 CHECK(debugger.execute([](GB_gameboy_t*g){return GB_debugger_is_stopped(g)&&GB_get_registers(g)->pc==0x152?QString():QString("step failed");}).isEmpty());
 id=debugger.sendDebugCommand("registers");CHECK(until([&]{return debugger.completedDebugCommand()>=id;}));
 CHECK(debugger.loadDebugSymbols({},"00:C123 TestValue\n",{}).isEmpty());id=debugger.sendDebugCommand("print/x TestValue");CHECK(until([&]{return debugger.completedDebugCommand()>=id;}));
 CHECK(debugger.execute([](GB_gameboy_t*g){uint16_t value=0;return !GB_debugger_evaluate(g,"TestValue",&value,nullptr)&&value==0xc123?QString():QString("Symbol evaluation failed");}).isEmpty());
 QString transcript;for(const auto&line:debugger.debugLogs().entries)transcript+=line.text;CHECK(transcript.contains("AF")&&transcript.contains("TestValue"));
 id=debugger.sendDebugCommand("continue");CHECK(until([&]{return !debugger.debuggerStopped()&&debugger.completedDebugCommand()>=id;}));
 debugger.sendDebugCommand("interrupt");CHECK(until([&]{return debugger.debuggerStopped();}));developer.developerMode=false;debugger.updateSettings(developer);CHECK(until([&]{return !debugger.debuggerStopped();}));debugger.requestInterruption();CHECK(debugger.wait(2000));
 std::puts("Debug: safe memory, raw VRAM banks, tiles/maps, OAM, CGB/DMG palettes, rendering/audio switches and viewer UI passed.");}
