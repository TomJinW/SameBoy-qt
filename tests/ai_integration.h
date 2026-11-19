#pragma once
#include "../src/aidebugserver.h"
#include "../src/debugconsole.h"
#include "../src/settingsdialog.h"
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QUuid>
#include <QKeyEvent>
#include <QCheckBox>
#include <SDL3/SDL.h>
inline bool aiFailure(int line){std::fprintf(stderr,"AI integration failure at line %d\n",line);return false;}
inline bool aiIntegration(MainWindow&window,const QString&romPath){
#ifdef Q_OS_MACOS
 const QString preferencesTitle=QString::fromUtf8("Settings…");
#else
 const QString preferencesTitle=QString::fromUtf8("Preferences…");
#endif
 AiDebugServer server("sameboy-ai-test-"+QString::number(QCoreApplication::applicationPid()));if(!server.setEnabled(true)||!window.openRom(romPath)){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 QLocalSocket socket;socket.connectToServer(server.endpoint());if(!socket.waitForConnected(1000)){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 int sequence=1;bool error=false;auto rpc=[&](QString method,QJsonObject params=QJsonObject()){
  socket.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",sequence++},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');socket.flush();QElapsedTimer timer;timer.start();while(!socket.canReadLine()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(2);}return QJsonDocument::fromJson(socket.readLine()).object();
 };
 auto call=[&](QString method,QJsonObject params=QJsonObject()){auto response=rpc(method,params);if(!response.contains("result")){std::fprintf(stderr,"AI %s: %s\n",method.toUtf8().constData(),QJsonDocument(response).toJson(QJsonDocument::Compact).constData());error=true;}return response.value("result").toObject();};
 {
  QLocalSocket batch;batch.connectToServer(server.endpoint());if(!batch.waitForConnected(1000))return aiFailure(__LINE__);
  auto frame=[](int id){return QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method","rpc.discover"},{"padding",QString(800000,'x')}}).toJson(QJsonDocument::Compact)+'\n';};
  const auto first=frame(901),second=frame(902);batch.write(first.left(400000));batch.flush();QElapsedTimer wait;wait.start();while(wait.elapsed()<30){QCoreApplication::processEvents();QThread::msleep(1);}
  batch.write(first.mid(400000)+second);batch.flush();int replies=0,readTurns=0;wait.restart();while(replies<2&&wait.elapsed()<5000){++readTurns;batch.flush();QCoreApplication::processEvents();while(batch.canReadLine()){const auto response=QJsonDocument::fromJson(batch.readLine()).object();if(response.contains("result"))++replies;}QThread::msleep(1);}
  if(replies!=2){std::fprintf(stderr,"RPC partial/coalesced frames: %d replies, %d event turns, state %d, unsent %lld, unread %lld, error %s\n",replies,readTurns,int(batch.state()),(long long)batch.bytesToWrite(),(long long)batch.bytesAvailable(),qPrintable(batch.errorString()));for(auto*peer:server.findChildren<QLocalSocket*>())std::fprintf(stderr,"Server socket: frame=%lld available=%lld state=%d\n",(long long)peer->property("buffer").toByteArray().size(),(long long)peer->bytesAvailable(),int(peer->state()));return aiFailure(__LINE__);}
  for(auto*timeout:server.findChildren<QTimer*>())if(timeout->isActive()){std::fputs("Completed RPC request retained an active deadline\n",stderr);return aiFailure(__LINE__);}
  QLocalSocket oversized;oversized.connectToServer(server.endpoint());if(!oversized.waitForConnected(1000))return aiFailure(__LINE__);
  oversized.write(QByteArray(1024*1024+1,'x'));oversized.flush();wait.restart();
  while(oversized.state()!=QLocalSocket::UnconnectedState&&wait.elapsed()<5000){oversized.flush();QCoreApplication::processEvents();QThread::msleep(1);}
  if(oversized.state()!=QLocalSocket::UnconnectedState){std::fprintf(stderr,"Oversized incomplete RPC frame was not disconnected (unsent %lld bytes)\n",(long long)oversized.bytesToWrite());return aiFailure(__LINE__);}
 }
 {
  std::vector<std::unique_ptr<QLocalSocket>> peers;
  for(int i=0;i<40;++i){auto peer=std::make_unique<QLocalSocket>();peer->connectToServer(server.endpoint());peer->waitForConnected(1000);peers.push_back(std::move(peer));QCoreApplication::processEvents();}
  QElapsedTimer timer;timer.start();int disconnected=0;
  do{QCoreApplication::processEvents();disconnected=0;for(const auto&peer:peers)if(peer->state()==QLocalSocket::UnconnectedState)++disconnected;if(disconnected>=9)break;QThread::msleep(1);}while(timer.elapsed()<1000);
  if(disconnected<9){std::fputs("RPC connection count was not bounded\n",stderr);return aiFailure(__LINE__);}
  for(auto&peer:peers)peer->abort();QCoreApplication::processEvents();
 }
 {
  QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);const auto previousPumps=server.findChildren<QTimer*>("rpcReadPump").size();
  QLocalSocket burst;burst.connectToServer(server.endpoint());if(!burst.waitForConnected(1000))return aiFailure(__LINE__);
  QElapsedTimer accepted;accepted.start();while(server.findChildren<QTimer*>("rpcReadPump").size()<=previousPumps&&accepted.elapsed()<1000){QCoreApplication::processEvents();QThread::msleep(1);}
  QObject observation;int turns=0;
  for(auto*pump:server.findChildren<QTimer*>("rpcReadPump"))QObject::connect(pump,&QTimer::timeout,&observation,[&]{++turns;});
  QByteArray frames;for(int i=0;i<256;++i)frames+=QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",i},{"method","rpc.discover"}}).toJson(QJsonDocument::Compact)+'\n';
  burst.write(frames);burst.flush();QSet<int> received;QElapsedTimer timer;timer.start();
  while(received.size()<256&&timer.elapsed()<2000){QCoreApplication::processEvents();while(burst.canReadLine()){auto response=QJsonDocument::fromJson(burst.readLine()).object();if(!response.contains("result"))return aiFailure(__LINE__);received.insert(response.value("id").toInt());}QThread::msleep(1);}
  if(received.size()!=256||turns<8){std::fprintf(stderr,"RPC burst was lost or not spread across bounded event-loop turns: %d replies, %d turns\n",int(received.size()),turns);return aiFailure(__LINE__);}
 }
 {
  QLocalSocket partial;partial.connectToServer(server.endpoint());if(!partial.waitForConnected(1000))return aiFailure(__LINE__);
  partial.write("{\"jsonrpc\":");partial.flush();QElapsedTimer timer;timer.start();qint64 lastByte=0;
  while(partial.state()!=QLocalSocket::UnconnectedState&&timer.elapsed()<5500){QCoreApplication::processEvents();if(timer.elapsed()-lastByte>=1000){lastByte=timer.elapsed();partial.write(" ");partial.flush();}QThread::msleep(2);}
  if(partial.state()!=QLocalSocket::UnconnectedState){std::fputs("Incomplete RPC frame retained its connection indefinitely\n",stderr);return aiFailure(__LINE__);}
 }
 auto discovery=call("rpc.discover");if(discovery.value("methods").toArray().size()<15){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 for(const auto&entry:QList<QPair<QString,QJsonObject>>{
  {"memory.read",{{"space",42},{"address",0xc000}}},
  {"symbols.load",{{"text","00:C123 InvalidFlag\n"},{"clear","true"}}},
  {"debug.evaluate",{{"expression",QString("1")+QChar(0)+"garbage"}}},
  {"symbols.load",{{"text",QString("00:C123 Prefix")+QChar(0)+"invalid"}}}}){
   if(rpc(entry.first,entry.second).value("error").toObject().value("code")!=-32602){std::fprintf(stderr,"RPC accepted invalid parameter type or embedded NUL: %s\n",qPrintable(entry.first));return aiFailure(__LINE__);}
 }
 auto windows=call("windows.list").value("windows").toArray();if(windows.size()!=1){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}const auto id=windows[0].toObject().value("id").toString();const auto generation=windows[0].toObject().value("generation");
 if(call("emulation.advance",{{"frames",2}}).value("frames")!=2){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto image=call("screen.capture");if(image.value("width")!=160||!QByteArray::fromBase64(image.value("base64").toString().toLatin1()).startsWith("\x89PNG")){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("memory.write",{{"address",0xc123},{"hex","5A"}});if(call("memory.read",{{"address",0xc123}}).value("hex")!="5A"){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("state.save",{{"slot",2}});call("memory.write",{{"address",0xc123},{"hex","A6"}});call("state.load",{{"slot",2}});if(call("memory.read",{{"address",0xc123}}).value("hex")!="5A"){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("symbols.load",{{"text","00:C123 AiMarker\n"}});if(call("debug.evaluate",{{"expression","AiMarker"}}).value("value")!=0xc123){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(call("debug.evaluate",{{"expression","rJOYP"}}).value("value")!=0xff00){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(rpc("memory.read",{{"address",0xffff},{"count",2}}).value("error").toObject().value("code")!=-32602){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(rpc("memory.read",{{"address",0},{"generation",generation.toDouble()+1}}).value("error").toObject().value("code")!=-32002){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("debug.break");auto waitStopped=[&](bool stopped){QElapsedTimer timer;timer.start();while(timer.elapsed()<2000){if(call("debug.status").value("debuggerStopped").toBool()==stopped)return true;QThread::msleep(5);}{std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}};if(!waitStopped(true)){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("memory.read",{{"address",0xff00},{"count",4}});auto command=call("debug.command",{{"command","step"}}).value("commandId").toDouble();
 QElapsedTimer timer;timer.start();bool completed=false;while(timer.elapsed()<2000){auto status=call("debug.status");if(status.value("completedCommand").toDouble()>=command){completed=status.value("debuggerStopped").toBool();break;}QThread::msleep(2);}if(!completed){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto log=call("debug.logs");if(log.value("entries").toArray().isEmpty()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("debug.evaluate",{{"expression","pc=$15e"}});call("debug.command",{{"command","continue"}});if(!waitStopped(false)){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("input.set",{{"buttons",QJsonArray{"right"}},{"ttlMs",2000}});call("emulation.advance",{{"frames",2}});auto joypad=call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);if((joypad&1)!=0){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}call("input.release");call("emulation.advance",{{"frames",1}});joypad=call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);if((joypad&1)!=1){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto*uiPoll=window.findChild<QTimer*>("emulationUiPoll");if(!uiPoll)return aiFailure(__LINE__);uiPoll->stop();
 call("emulation.resume");call("input.set",{{"buttons",QJsonArray{"right"}},{"ttlMs",2000}});
 QApplication::setActiveWindow(&window);QKeyEvent physicalUp(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(&window,&physicalUp);
 QThread::msleep(40);const auto beforeFocusLoss=call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);
 if((beforeFocusLoss&5)!=0){std::fputs("Physical and script input were not both held before focus loss\n",stderr);return aiFailure(__LINE__);}
 QEvent focusLoss(QEvent::WindowDeactivate);QApplication::sendEvent(&window,&focusLoss);
 QThread::msleep(80);const auto afterFocusLoss=call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);
 if((afterFocusLoss&5)!=4){std::fputs("Window focus loss lost script input or retained physical input\n",stderr);return aiFailure(__LINE__);}
 call("input.release");
 call("emulation.resume");call("input.set",{{"buttons",QJsonArray{"right"}},{"ttlMs",40}});
 QThread::msleep(200);const auto afterGuiStall=call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);
 call("emulation.advance",{{"frames",1}});
 const auto advancedAfterExpiry=call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);
 call("emulation.pause");uiPoll->start();
 if((advancedAfterExpiry&1)!=1){std::fputs("Frame advance revived an expired lease\n",stderr);return aiFailure(__LINE__);}
 if((afterGuiStall&1)!=1){std::fputs("Input lease did not expire without GUI polling\n",stderr);return aiFailure(__LINE__);}
 call("input.set",{{"buttons",QJsonArray{"right"}},{"ttlMs",5000}});
 QPointer<QObject> oldSession=server.findChild<QObject*>("debugRequestSession");if(!oldSession)return aiFailure(__LINE__);
 server.shutdown();if(oldSession||!server.setEnabled(true)||!server.findChild<QObject*>("debugRequestSession"))return aiFailure(__LINE__);
 socket.abort();socket.connectToServer(server.endpoint());if(!socket.waitForConnected(1000))return aiFailure(__LINE__);
 call("emulation.advance",{{"frames",1}});
 if((call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16)&1)!=1){std::fputs("Debug server shutdown retained leased input\n",stderr);return aiFailure(__LINE__);}
 auto stepResult=call("emulation.step",{{"buttons",QJsonArray{"right"}},{"frames",3}});if(stepResult.value("frames")!=3||!stepResult.value("paused").toBool()||!QByteArray::fromBase64(stepResult.value("base64").toString().toLatin1()).startsWith("\x89PNG")){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if((call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16)&1)!=0){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
// SameBoy 1.0.5 applies key changes to JOYP on the next emulated cycle, so check it after the next step.
 call("emulation.step",{{"frames",1}});if((call("memory.read",{{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16)&1)!=1){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if((call("memory.read",{{"address",0xff00}}).value("hex").toString().toUInt(nullptr,16)&1)!=1){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}

 // Count real VBlank interrupts independently of the frontend step counter.
 call("memory.write",{{"address",0xff50},{"hex","01"}});
 call("memory.write",{{"space","rom"},{"address",0x40},{"hex","F5E52100C134E1F1D9"}});call("memory.write",{{"space","rom"},{"address",0x150},{"hex","31FEFF3E01E0FFAFE00FFB18FE"}});call("memory.write",{{"address",0xc100},{"hex","00"}});call("debug.evaluate",{{"expression","pc=$150"}});call("emulation.step",{{"frames",2}});
 auto before=call("memory.read",{{"address",0xc100}}).value("hex").toString().toUInt(nullptr,16);QAction*pauseAction=nullptr;for(auto*action:window.findChildren<QAction*>())if(QString(action->text()).remove('&')=="Pause")pauseAction=action;if(!pauseAction)return aiFailure(__LINE__);
 // A slow GUI observer must not allow free-running frames before the budget arms.
 auto delayedResume=QObject::connect(pauseAction,&QAction::toggled,&window,[](bool paused){if(!paused)QThread::msleep(60);});
 auto exact=call("emulation.step",{{"frames",5}});QObject::disconnect(delayedResume);auto after=call("memory.read",{{"address",0xc100}}).value("hex").toString().toUInt(nullptr,16);const bool sameFrame=exact.value("base64")==call("screen.capture").value("base64");if(((after-before)&255)!=5||!sameFrame){std::fprintf(stderr,"RPC exact step failed: before=%u after=%u delta=%u sameFrame=%d\n",before,after,(after-before)&255,int(sameFrame));return aiFailure(__LINE__);}
 // A pause arriving before the queued step arms must cancel it, not get lost.
 const int stepId=sequence++,pauseId=sequence++;
 auto request=[&](int value,QString method,QJsonObject params){socket.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",value},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');};
 request(stepId,"emulation.step",{{"frames",600}});request(pauseId,"emulation.pause",{});socket.flush();bool canceled=false,pausedReply=false;timer.restart();while(timer.elapsed()<2000&&(!canceled||!pausedReply)){QCoreApplication::processEvents();while(socket.canReadLine()){auto response=QJsonDocument::fromJson(socket.readLine()).object();if(response.value("id")==stepId)canceled=response.value("error").toObject().value("code")==-32003;if(response.value("id")==pauseId)pausedReply=response.contains("result");}QThread::msleep(1);}if(!canceled||!pausedReply||!call("debug.status").value("paused").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(rpc("emulation.step",{{"buttons",QJsonArray{"invalid"}},{"frames",1}}).value("error").toObject().value("code")!=-32602){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto action=[&](QString name)->QAction*{for(auto*a:window.findChildren<QAction*>())if(QString(a->text()).remove('&')==name)return a;return nullptr;};
 action("Show Console")->trigger();DebugConsole*console=nullptr;for(auto*dialog:window.findChildren<QDialog*>())if(auto*value=dynamic_cast<DebugConsole*>(dialog))console=value;if(!console){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}action("Clear Console")->trigger();if(!console->findChild<QPlainTextEdit*>("debugOutput")->toPlainText().isEmpty()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}if(call("debug.logs").value("entries").toArray().isEmpty()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 QProcess client;const auto executable=QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
 "sameboy-qt.exe"
#elif defined(Q_OS_MACOS)
 "sameboy-qt.app/Contents/MacOS/sameboy-qt"
#else
 "sameboy-qt"
#endif
 );client.start(executable,{"--rpc","--endpoint",server.endpoint(),"windows.list"});timer.restart();while(client.state()!=QProcess::NotRunning&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(2);}if(client.state()!=QProcess::NotRunning||client.exitCode()!=0||QJsonDocument::fromJson(client.readAllStandardOutput()).object().value("result").toObject().value("windows").toArray().size()!=1){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}

 auto waitClient=[&](QProcess&process,int timeout){timer.restart();while(process.state()!=QProcess::NotRunning&&timer.elapsed()<timeout){QCoreApplication::processEvents();QThread::msleep(1);}return process.state()==QProcess::NotRunning;};
 QProcess stdinClient;stdinClient.start(executable,{"--rpc","--endpoint",server.endpoint(),"--stdin"});if(!stdinClient.waitForStarted(2000))return aiFailure(__LINE__);
 stdinClient.write("{\"jsonrpc\":\"2.0\",\"id\":\"stdin-test\",\"method\":\"windows.list\",\"params\":{}}\n");stdinClient.closeWriteChannel();
 if(!waitClient(stdinClient,5000)||stdinClient.exitCode()!=0||QJsonDocument::fromJson(stdinClient.readAllStandardOutput()).object().value("id").toString()!="stdin-test"){
  std::fputs("Bounded stdin RPC request failed\n",stderr);return aiFailure(__LINE__);
 }
 QProcess oversizedInput;oversizedInput.start(executable,{"--rpc","--endpoint",server.endpoint(),"--stdin"});if(!oversizedInput.waitForStarted(2000))return aiFailure(__LINE__);
 oversizedInput.write(QByteArray(1024*1024+1,' '));oversizedInput.closeWriteChannel();
 if(!waitClient(oversizedInput,5000)||oversizedInput.exitCode()!=2||!oversizedInput.readAllStandardError().contains("1 MiB")){
  std::fputs("Oversized CLI stdin was not rejected at its bound\n",stderr);return aiFailure(__LINE__);
 }
 const auto fakeName="sameboy-qt-cli-test-"+QUuid::createUuid().toString(QUuid::Id128).left(16);QLocalServer fakeServer;
 if(!fakeServer.listen(fakeName))return aiFailure(__LINE__);
 QObject::connect(&fakeServer,&QLocalServer::newConnection,&fakeServer,[&]{while(auto*fake=fakeServer.nextPendingConnection()){
  QObject::connect(fake,&QLocalSocket::readyRead,fake,[fake]{if(!fake->canReadLine())return;const auto request=QJsonDocument::fromJson(fake->readLine()).object();
   if(request.value("method")=="large-response"){fake->write(QByteArray(4*1024*1024+1,'x'));fake->flush();}
   else{fake->write("{\"jsonrpc\":\"2.0\",\"id\":999,\"result\":{}}\n");fake->flush();}
  });
 }});
 QProcess largeResponse;largeResponse.start(executable,{"--rpc","--endpoint",fakeServer.fullServerName(),"large-response"});
 if(!waitClient(largeResponse,7000)||largeResponse.exitCode()!=2||!largeResponse.readAllStandardError().contains("4 MiB")){
  std::fputs("Oversized CLI response was not rejected at its bound\n",stderr);return aiFailure(__LINE__);
 }
 QProcess mismatchedResponse;mismatchedResponse.start(executable,{"--rpc","--endpoint",fakeServer.fullServerName(),"rpc.discover"});
 if(!waitClient(mismatchedResponse,5000)||mismatchedResponse.exitCode()!=2||!mismatchedResponse.readAllStandardError().contains("mismatched")){
  std::fputs("CLI accepted a JSON-RPC response with a mismatched id\n",stderr);return aiFailure(__LINE__);
 }

 const auto python=QStandardPaths::findExecutable("python");if(!python.isEmpty()){
  QProcess check;check.start(python,{QStringLiteral(SAMEBOY_SOURCE_DIR)+"/tests/python_client_smoke.py",executable,server.endpoint()});timer.restart();while(check.state()!=QProcess::NotRunning&&timer.elapsed()<10000){QCoreApplication::processEvents();QThread::msleep(2);}if(check.state()!=QProcess::NotRunning||check.exitCode()!=0){std::fprintf(stderr,"Python client failed: %s\n",check.readAllStandardError().constData());{std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}}
 }
 auto*graph=console->findChild<QWidget*>("debugCpuLoad");if(!graph){std::fprintf(stderr,"CPU graph missing\n");return aiFailure(__LINE__);}const auto cpuSamples=graph->property("sampleCount").toDouble();call("emulation.resume");timer.restart();while(timer.elapsed()<200){QCoreApplication::processEvents();QThread::msleep(2);}if(graph->property("sampleCount").toDouble()<=cpuSamples){std::fprintf(stderr,"CPU graph did not update while running\n");return aiFailure(__LINE__);}call("emulation.pause");
 if(!qEnvironmentVariableIsEmpty("SAMEBOY_QA_DIR"))console->grab().save(qEnvironmentVariable("SAMEBOY_QA_DIR")+"/debug-console.png");
 // Printer requests use the same GUI actions and capture an actual core-generated feed.
 call("printer.connect");bool printerConnected=false;timer.restart();while(timer.elapsed()<1000){if(call("printer.status").value("connected").toBool()){printerConnected=true;break;}}if(!printerConnected){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(rpc("printer.capture").value("error").toObject().value("code")!=-32004){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 QByteArray payload;auto packet=[&](int command,int compression,QByteArray data){QByteArray header;header.append(char(command));header.append(char(compression));header.append(char(data.size()));header.append(char(data.size()>>8));unsigned sum=0;for(auto byte:header+data)sum+=uint8_t(byte);payload+=QByteArray::fromHex("8833")+header+data;payload.append(char(sum));payload.append(char(sum>>8));payload.append(char(0));payload.append(char(0));};
 packet(1,0,{});packet(4,1,QByteArray::fromHex("ffaa ffaaffaaffaafaaa"));packet(2,0,QByteArray::fromHex("0123e440"));
 QByteArray program=QByteArray::fromHex("31feff21000401");program.append(char(payload.size()));program.append(char(payload.size()>>8));program+=QByteArray::fromHex("2ae0013e81e002f002cb7f20fa0b78b120ee18fe");
 call("memory.write",{{"space","rom"},{"address",0x150},{"hex",QString::fromLatin1(program.toHex())}});call("memory.write",{{"space","rom"},{"address",0x400},{"hex",QString::fromLatin1(payload.toHex())}});call("memory.write",{{"address",0xff50},{"hex","01"}});call("debug.evaluate",{{"expression","pc=$150"}});call("emulation.advance",{{"frames",10}});
 auto feed=call("printer.capture");if(feed.value("width")!=160||feed.value("height")!=21||!QByteArray::fromBase64(feed.value("base64").toString().toLatin1()).startsWith("\x89PNG")){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 call("printer.clear");if(call("printer.status").value("hasFeed").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 // Link cable and infrared share the existing two-core scheduler.
 MainWindow peer;peer.setAttribute(Qt::WA_DontShowOnScreen);const auto peerPath=romPath+"-peer.gb";if(!QFile::copy(romPath,peerPath)||!peer.openRom(peerPath)){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto list=call("windows.list").value("windows").toArray();QString peerId;QJsonValue peerGeneration;for(auto value:list){auto info=value.toObject();if(info.value("rom")==peerPath){peerId=info.value("id").toString();peerGeneration=info.value("generation");}}if(peerId.isEmpty()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto target=[&](QString method,QJsonObject params=QJsonObject()){params.insert("window",id);params.insert("generation",generation);return call(method,params);};
 {
  auto backgroundPreference=[&](bool enabled){bool found=false;QTimer response;QObject::connect(&response,&QTimer::timeout,&window,[&]{for(auto*widget:qApp->topLevelWidgets())if(auto*dialog=dynamic_cast<SettingsDialog*>(widget);dialog&&dialog->isVisible()&&dialog->isModal()){for(auto*check:dialog->findChildren<QCheckBox*>())if(check->text()=="Enable controllers while in background"){check->setChecked(enabled);found=true;}response.stop();dialog->accept();break;}});response.start(5);action(preferencesTitle)->trigger();return found;};
  if(!backgroundPreference(true))return aiFailure(__LINE__);
  SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=26;desc.name="Background routing audit pad";desc.button_mask=(1u<<26)-1;desc.axis_mask=63;
  auto pad=SDL_AttachVirtualJoystick(&desc);if(!pad)return aiFailure(__LINE__);auto*handle=SDL_OpenJoystick(pad);if(!handle)return aiFailure(__LINE__);
  struct PadCleanup{SDL_JoystickID id;SDL_Joystick*handle;~PadCleanup(){SDL_CloseJoystick(handle);SDL_DetachVirtualJoystick(id);}} cleanup{pad,handle};
  SDL_SetJoystickVirtualAxis(handle,0,32767);SDL_SetJoystickVirtualAxis(handle,4,-32768);SDL_SetJoystickVirtualAxis(handle,5,-32768);QThread::msleep(510);
  peer.show();QCoreApplication::processEvents();QApplication::setActiveWindow(&window);
  for(const auto&targetId:{id,peerId}){call("memory.write",{{"window",targetId},{"space","rom"},{"address",0x15e},{"hex","3e20e000f000ea00c018f9"}});call("memory.write",{{"window",targetId},{"address",0xff40},{"hex","91"}});}
  call("memory.write",{{"window",id},{"address",0xff50},{"hex","01"}});call("memory.write",{{"window",peerId},{"address",0xff50},{"hex","01"}});
  call("debug.evaluate",{{"window",id},{"expression","pc=$15e"}});call("debug.evaluate",{{"window",peerId},{"expression","pc=$15e"}});
  call("emulation.resume",{{"window",id}});call("emulation.resume",{{"window",peerId}});
  auto*firstPoll=window.findChild<QTimer*>("emulationUiPoll");auto*peerPoll=peer.findChild<QTimer*>("emulationUiPoll");firstPoll->stop();peerPoll->stop();
  window.refreshDuringNativeMove();peer.refreshDuringNativeMove();QThread::msleep(50);
  auto held=[&](QString target){const auto value=call("memory.read",{{"window",target},{"address",0xc000}}).value("hex").toString().toUInt(nullptr,16);return (value&1)==0;};
  if(!held(id)||held(peerId)){std::fputs("Foreground controller routing was not exclusive\n",stderr);return aiFailure(__LINE__);}
  QApplication::setActiveWindow(nullptr);window.refreshDuringNativeMove();peer.refreshDuringNativeMove();QThread::msleep(50);
  if(!held(id)||held(peerId)){std::fputs("Background controller input reached multiple ROM windows\n",stderr);return aiFailure(__LINE__);}
  QApplication::setActiveWindow(&peer);window.refreshDuringNativeMove();peer.refreshDuringNativeMove();QThread::msleep(50);
  if(held(id)||!held(peerId)){std::fputs("Controller ownership did not follow game activation\n",stderr);return aiFailure(__LINE__);}
  QApplication::setActiveWindow(nullptr);QThread::msleep(50);
  if(!held(peerId)){std::fputs("Focus loss interrupted background controller input before the next poll\n",stderr);return aiFailure(__LINE__);}
  QDialog modal(&peer);modal.setAttribute(Qt::WA_DontShowOnScreen);modal.setModal(true);modal.show();QApplication::setActiveWindow(&modal);window.refreshDuringNativeMove();peer.refreshDuringNativeMove();QThread::msleep(50);
  if(held(id)||held(peerId)){std::fputs("Modal dialog did not block controller input\n",stderr);return aiFailure(__LINE__);}
  modal.hide();QApplication::setActiveWindow(nullptr);peer.hide();window.refreshDuringNativeMove();peer.refreshDuringNativeMove();QThread::msleep(50);
  if(!held(id)||held(peerId)){std::fputs("Hidden background controller owner prevented fallback to a live game\n",stderr);return aiFailure(__LINE__);}
  firstPoll->start();peerPoll->start();if(!backgroundPreference(false))return aiFailure(__LINE__);peer.hide();QApplication::setActiveWindow(&window);
 }
 if(rpc("link.connect",{{"window",id},{"peer",id}}).value("error").toObject().value("code")!=-32602){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(!target("link.connect",{{"peer",peerId},{"peerGeneration",peerGeneration}}).value("connected").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 auto connection=target("link.status");if(connection.value("peer")!=peerId||!connection.value("infrared").toBool()||!target("infrared.status").value("connected").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(call("link.status",{{"window",peerId}}).value("peer")!=id){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 target("printer.connect");if(target("link.status").value("connected").toBool()||call("link.status",{{"window",peerId}}).value("connected").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 target("link.connect",{{"peer",peerId}});if(target("printer.status").value("requested").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 target("link.disconnect");if(target("link.status").value("connected").toBool()||call("link.status",{{"window",peerId}}).value("connected").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 target("printer.connect");if(target("link.status").value("connected").toBool()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}target("printer.disconnect");
 auto preference=[&](const char*objectName,int value,QString bootFolder=QString(),MainWindow*settingsOwner=nullptr){bool selected=false;QTimer response;QObject::connect(&response,&QTimer::timeout,&window,[&]{for(auto*widget:QApplication::topLevelWidgets())if(auto*dialog=dynamic_cast<SettingsDialog*>(widget);dialog&&dialog->isVisible()&&dialog->isModal()){response.stop();if(auto*box=dialog->findChild<QComboBox*>(objectName)){box->setCurrentIndex(box->findData(value));if(!bootFolder.isNull())if(auto*folder=dialog->findChild<QLineEdit*>("bootRomFolder"))folder->setText(bootFolder);selected=true;dialog->accept();}else dialog->reject();break;}});response.start(10);QAction*open=action(preferencesTitle);if(settingsOwner)for(auto*candidate:settingsOwner->findChildren<QAction*>())if(QString(candidate->text()).remove('&')==preferencesTitle)open=candidate;open->trigger();return selected;};
 const auto beforeWriteFailure=call("debug.status",{{"window",id}});
 const auto settingsFile=QSettings(QSettings::IniFormat,QSettings::UserScope,"SameBoyQt","SameBoyQt").fileName();
 const auto settingsPermissions=QFile::permissions(settingsFile);if(!QFile::setPermissions(settingsFile,QFileDevice::ReadOwner|QFileDevice::ReadUser))return aiFailure(__LINE__);
 qApp->setProperty("expectedTestDialog",settingsFile);qApp->setProperty("expectedTestDialogSeen",false);
 if(!preference("emulationHardware",2)||!qApp->property("expectedTestDialogSeen").toBool()){std::fputs("Failed preference write was reported as success\n",stderr);return aiFailure(__LINE__);}
 const auto afterWriteFailure=call("debug.status",{{"window",id}});
 if(afterWriteFailure.value("model")!=beforeWriteFailure.value("model")||afterWriteFailure.value("generation")!=beforeWriteFailure.value("generation")){std::fputs("Failed preference write reset the active core\n",stderr);return aiFailure(__LINE__);}
 qApp->setProperty("expectedTestDialogSeen",false);action("Developer Mode")->setChecked(false);
 if(!qApp->property("expectedTestDialogSeen").toBool()||!action("Developer Mode")->isChecked()||!server.enabled()){std::fputs("Failed developer preference write changed runtime state\n",stderr);return aiFailure(__LINE__);}
 qApp->setProperty("expectedTestDialog",QVariant());
 if(!QFile::setPermissions(settingsFile,settingsPermissions))return aiFailure(__LINE__);
 // Inject a disk failure only after the model action reflects a completed reload.
 const auto beforeLateFailure=call("debug.status",{{"window",id}});const auto savedHardware=AppSettings::load().hardware;
 call("memory.write",{{"window",id},{"address",0xc234},{"hex","7A"}});
 bool injected=false;auto*colorAction=action("Game Boy Color");if(!colorAction)return aiFailure(__LINE__);
 auto diskFailure=QObject::connect(colorAction,&QAction::changed,&window,[&]{if(colorAction->isChecked()&&!injected){injected=QFile::setPermissions(settingsFile,QFileDevice::ReadOwner|QFileDevice::ReadUser);}});
 qApp->setProperty("expectedTestDialog",settingsFile);qApp->setProperty("expectedTestDialogSeen",false);
 const bool selectedLate=preference("emulationHardware",2);QObject::disconnect(diskFailure);
 const bool rejectedLate=qApp->property("expectedTestDialogSeen").toBool();qApp->setProperty("expectedTestDialog",QVariant());
 if(!QFile::setPermissions(settingsFile,settingsPermissions)||!selectedLate||!injected)return aiFailure(__LINE__);
 const auto afterLateFailure=call("debug.status",{{"window",id}});
 if(rejectedLate){
  if(afterLateFailure.value("generation")!=beforeLateFailure.value("generation")||AppSettings::load().hardware!=savedHardware||call("memory.read",{{"window",id},{"address",0xc234}}).value("hex")!="7A"){std::fputs("Late preference write failure reset live game state\n",stderr);return aiFailure(__LINE__);}
 }else{
  QFile committed(settingsFile);if(!committed.open(QIODevice::ReadOnly))return aiFailure(__LINE__);const auto bytes=committed.readAll();
  if(afterLateFailure.value("model").toInt()!=GB_MODEL_CGB_E||AppSettings::load().hardware!=2||!bytes.contains("\nhardware=2")){std::fputs("Accepted preference change was not committed before core reload\n",stderr);return aiFailure(__LINE__);}
 }
 if(!preference("emulationHardware",savedHardware))return aiFailure(__LINE__);
 const auto originalPreferences=AppSettings::load();const auto missingBoot=QFileInfo(romPath).dir().filePath("missing-boot");
 qApp->setProperty("expectedTestDialog",missingBoot);qApp->setProperty("expectedTestDialogSeen",false);
 if(!preference("emulationHardware",2,missingBoot)||!qApp->property("expectedTestDialogSeen").toBool())return aiFailure(__LINE__);
 qApp->setProperty("expectedTestDialog",QVariant());
 const auto afterFailedPreference=AppSettings::load();
 if(afterFailedPreference.hardware!=originalPreferences.hardware||afterFailedPreference.bootFolder!=originalPreferences.bootFolder){std::fputs("Failed model reload persisted rejected preferences\n",stderr);return aiFailure(__LINE__);}
 const bool wasPaused=call("debug.status",{{"window",id}}).value("paused").toBool();
 if(!preference("emulationHardware",2)||call("debug.status",{{"window",id}}).value("model").toInt()!=GB_MODEL_CGB_E){std::fputs("Hardware preference did not change the active core\n",stderr);return aiFailure(__LINE__);}
 if(!preference("cgbRevision",GB_MODEL_CGB_0)||call("debug.status",{{"window",id}}).value("model").toInt()!=GB_MODEL_CGB_0){std::fputs("Revision preference did not reset the affected core\n",stderr);return aiFailure(__LINE__);}
 if(call("debug.status",{{"window",id}}).value("paused").toBool()!=wasPaused||call("debug.status",{{"window",peerId}}).value("model").toInt()!=GB_MODEL_DMG_B){std::fputs("Preference reset changed pause state or unrelated hardware family\n",stderr);return aiFailure(__LINE__);}
 // A bad ROM in a later window must be detected before any earlier core resets.
 if(!preference("emulationHardware",2,{},&peer))return aiFailure(__LINE__);
 call("emulation.pause",{{"window",id}});call("emulation.pause",{{"window",peerId}});
 QList<MainWindow*> order;for(auto*widget:qApp->topLevelWidgets())if(widget==&window||widget==&peer)order.append(static_cast<MainWindow*>(widget));if(order.size()!=2)return aiFailure(__LINE__);
 const auto firstId=order[0]==&window?id:peerId;const auto missingPath=order[1]->currentRomPath();
 const auto firstBefore=call("debug.status",{{"window",firstId}});call("memory.write",{{"window",firstId},{"address",0xc234},{"hex","6B"}});
 const auto heldPath=missingPath+".audit-held";if(!QFile::rename(missingPath,heldPath))return aiFailure(__LINE__);
 struct RestoreRom{QString from,to;~RestoreRom(){QFile::rename(from,to);}} restoreRom{heldPath,missingPath};
 qApp->setProperty("expectedTestDialog",missingPath);qApp->setProperty("expectedTestDialogSeen",false);
 const bool batchSelected=preference("cgbRevision",GB_MODEL_CGB_E);const bool batchFailed=qApp->property("expectedTestDialogSeen").toBool();qApp->setProperty("expectedTestDialog",QVariant());
 const auto firstAfter=call("debug.status",{{"window",firstId}});const auto preserved=call("memory.read",{{"window",firstId},{"address",0xc234}}).value("hex");
 if(!QFile::rename(heldPath,missingPath)||!batchSelected||!batchFailed)return aiFailure(__LINE__);
 if(firstBefore.value("generation")!=firstAfter.value("generation")||preserved!="6B"||AppSettings::load().cgbModel!=GB_MODEL_CGB_0){std::fputs("Later window ROM failure reset an earlier game\n",stderr);return aiFailure(__LINE__);}
 // Cancellation of a later battery error must not reset earlier windows either.
 const auto batteryPath=batterySavePath(missingPath);const auto batteryPermissions=QFile::permissions(batteryPath);if(!QFile::setPermissions(batteryPath,QFileDevice::ReadOwner|QFileDevice::ReadUser))return aiFailure(__LINE__);
 struct RestoreBatteryPermissions{QString path;QFileDevice::Permissions permissions;~RestoreBatteryPermissions(){QFile::setPermissions(path,permissions);}} restoreBatteryPermissions{batteryPath,batteryPermissions};
 call("memory.write",{{"window",firstId},{"address",0xff50},{"hex","01"}});call("memory.write",{{"window",firstId},{"address",0xff40},{"hex","91"}});call("debug.evaluate",{{"window",firstId},{"expression","pc=$15e"}});call("emulation.resume",{{"window",firstId}});
 const auto beforeBatteryCancel=call("debug.status",{{"window",firstId}});call("memory.write",{{"window",firstId},{"address",0xc234},{"hex","5C"}});
 qApp->setProperty("expectedTestDialog",batteryPath);qApp->setProperty("expectedTestDialogSeen",false);
 const bool batterySelected=preference("cgbRevision",GB_MODEL_CGB_E);const bool batteryCanceled=qApp->property("expectedTestDialogSeen").toBool();qApp->setProperty("expectedTestDialog",QVariant());
 const auto afterBatteryCancel=call("debug.status",{{"window",firstId}});const auto batteryPreserved=call("memory.read",{{"window",firstId},{"address",0xc234}}).value("hex");
 if(!QFile::setPermissions(batteryPath,batteryPermissions)||!batterySelected||!batteryCanceled)return aiFailure(__LINE__);
 if(beforeBatteryCancel.value("generation")!=afterBatteryCancel.value("generation")||beforeBatteryCancel.value("paused")!=afterBatteryCancel.value("paused")||batteryPreserved!="5C"||AppSettings::load().cgbModel!=GB_MODEL_CGB_0){std::fputs("Later window battery cancellation reset an earlier game\n",stderr);return aiFailure(__LINE__);}
 peer.shutdownForRestart();
 action("Developer Mode")->setChecked(false);if(server.enabled()){std::fprintf(stderr,"RPC test failure at %d\n",__LINE__);return aiFailure(__LINE__);}
 if(!QFile::setPermissions(settingsFile,QFileDevice::ReadOwner|QFileDevice::ReadUser))return aiFailure(__LINE__);
 if(server.setEnabled(true)||server.enabled()||server.error().isEmpty()||action("Developer Mode")->isChecked()){std::fputs("Debug server reported enabled after developer settings failed\n",stderr);return aiFailure(__LINE__);}
 if(!QFile::setPermissions(settingsFile,settingsPermissions))return aiFailure(__LINE__);
 window.shutdownForRestart();
 action("Close")->trigger();std::puts("AI: discovery, Unicode target, generation guard, raw memory, states, symbols, debugger stop/step, screenshot, timed keys and cross-process CLI passed.");return !error;
}
