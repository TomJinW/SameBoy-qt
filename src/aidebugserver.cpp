#include "aidebugserver.h"
#include "aiprotocol.h"
#include "requestsession.h"
#include "mainwindow.h"
#include "emulator.h"
#include "debugviews.h"
#include "debuggerbridge.h"
#include "storagepaths.h"
#include "stateio.h"
#include "audiobuffer.h"
#include "audiooutput.h"
#include "printerwindow.h"
#include <QApplication>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QAction>
#include <QSignalBlocker>
#include <QBuffer>
#include <QFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <cmath>
namespace {AiDebugServer*service=nullptr;
struct RequestState{bool completed=false;QPointer<QTimer> timeout;};
bool integer(const QJsonValue&value,int low,int high,int&out){if(!value.isDouble())return false;const double number=value.toDouble();if(!std::isfinite(number)||std::floor(number)!=number||number<low||number>high)return false;out=int(number);return true;}
}
AiDebugServer*AiDebugServer::instance(){return service;}
AiDebugServer::AiDebugServer(QString name,QObject*parent):QObject(parent),name_(name.isEmpty()?defaultAiEndpoint():std::move(name)),server_(this){
 service=this;server_.setSocketOptions(QLocalServer::UserAccessOption);
 connect(&server_,&QLocalServer::newConnection,this,[this]{while(auto*socket=server_.nextPendingConnection()){
  const auto session=session_;QPointer<QObject>context(sessionContext_);
  if(clients_.size()>=32||!session||!session->active()||!context){socket->abort();socket->deleteLater();continue;}
  clients_.insert(socket);connect(socket,&QLocalSocket::disconnected,this,[this,socket]{clients_.remove(socket);});
  // Drain bounded transport chunks; the separate framing buffer retains its overflow probe.
  socket->setReadBufferSize(64*1024);connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
  auto*pump=new QTimer(socket);pump->setObjectName("rpcReadPump");pump->setSingleShot(true);pump->setInterval(0);
  auto*partial=new QTimer(socket);partial->setSingleShot(true);partial->setInterval(5000);partial->setTimerType(Qt::PreciseTimer);
  connect(partial,&QTimer::timeout,socket,&QLocalSocket::abort);
  auto read=[this,socket,session,context,pump,partial]{if(!session->active()||!context||socket->state()!=QLocalSocket::ConnectedState){socket->abort();return;}
   auto buffer=socket->property("buffer").toByteArray();buffer+=socket->read(1024*1024+1-buffer.size());
   QElapsedTimer budget;budget.start();
   int end=0,processed=0;while(processed<32&&(processed==0||budget.elapsed()<4)&&session->active()&&socket->state()==QLocalSocket::ConnectedState&&(end=buffer.indexOf('\n'))>=0){++processed;partial->stop();if(end>1024*1024){socket->abort();return;}auto line=buffer.left(end);buffer.remove(0,end+1);QJsonParseError parseError;const auto document=QJsonDocument::fromJson(line,&parseError);const auto request=document.object();const auto id=request.value("id");
    auto state=std::make_shared<RequestState>();QPointer<QLocalSocket>receiver(socket);
    Reply reply=[receiver,id,state,session](QJsonValue result,int code,QString message){if(state->completed)return;state->completed=true;if(state->timeout){state->timeout->stop();state->timeout->deleteLater();state->timeout.clear();}--session->pending;if(!session->active()||!receiver||receiver->state()!=QLocalSocket::ConnectedState)return;QJsonObject response{{"jsonrpc","2.0"},{"id",id.isUndefined()?QJsonValue():id}};if(code)response.insert("error",QJsonObject{{"code",code},{"message",message}});else response.insert("result",result);auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact)+'\n';if(receiver->bytesToWrite()+bytes.size()>4*1024*1024){receiver->abort();return;}receiver->write(bytes);receiver->flush();};
    ++session->pending;if(session->pending>128){reply({},-32000,"Too many pending requests");continue;}
    if(parseError.error!=QJsonParseError::NoError){reply({},-32700,"Invalid JSON");continue;}
    if(!document.isObject()||request.value("jsonrpc")!="2.0"||!request.value("method").isString()||(!id.isDouble()&&!id.isString())){reply({},-32600,"Invalid JSON-RPC request (an id is required)");continue;}
    if(request.contains("params")&&!request.value("params").isObject()){reply({},-32602,"params must be an object");continue;}
    auto*timeout=new QTimer(context.data());timeout->setSingleShot(true);state->timeout=timeout;connect(timeout,&QTimer::timeout,context.data(),[reply]{reply({},-32000,"Request timed out; already accepted operations are not rolled back");});timeout->start(15000);dispatch(request.value("method").toString(),request.value("params").toObject(),std::move(reply),session);
   }if(buffer.size()>1024*1024&&!buffer.contains('\n')){socket->abort();return;}socket->setProperty("buffer",buffer);
   if(socket->state()!=QLocalSocket::ConnectedState)return;
   if(buffer.contains('\n')||socket->bytesAvailable())pump->start();
   else if(!buffer.isEmpty()){if(!partial->isActive())partial->start();}else partial->stop();
  };connect(pump,&QTimer::timeout,context.data(),read);
  connect(socket,&QLocalSocket::readyRead,pump,[pump]{if(!pump->isActive())pump->start();});if(socket->bytesAvailable())pump->start();
 }});
}
AiDebugServer::~AiDebugServer(){shutdown();if(service==this)service=nullptr;}
void AiDebugServer::shutdown(){setEnabled(false);}
bool AiDebugServer::setEnabled(bool enabled){
 error_.clear();
 if(enabled&&!server_.isListening()){
  session_=std::make_shared<RequestSession>();sessionContext_=new QObject(this);sessionContext_->setObjectName("debugRequestSession");
  // A crashed process leaves its Unix socket file behind (a no-op for Windows pipes). Only the
  // single-instance primary uses the default endpoint, so a stale file cannot belong to a live peer.
  if(!server_.listen(name_)&&server_.serverError()==QAbstractSocket::AddressInUseError){QLocalServer::removeServer(name_);server_.listen(name_);}
  if(!server_.isListening()){error_=tr("Could not start the debug interface: %1").arg(server_.errorString());session_->close();delete sessionContext_;sessionContext_=nullptr;session_.reset();return false;}
 }
 if(!enabled){
  if(session_)session_->close();delete sessionContext_;sessionContext_=nullptr;session_.reset();
  server_.close();for(auto*socket:server_.findChildren<QLocalSocket*>())socket->abort();
 }
 if(!enabled&&qApp&&!QCoreApplication::closingDown())for(auto*widget:qApp->topLevelWidgets())if(auto*window=dynamic_cast<MainWindow*>(widget)){
  window->aiInput_.keys.fill(0);window->aiInput_.deadlines.fill({});
  if(window->aiAdvancing_){++window->stepRequest_;window->aiAdvancing_=false;window->pause_->setChecked(true);if(window->emulator_)window->emulator_->setPaused(true);}
  // Do not poll controllers/automatic pairing while shutting down the interface.
  // Preserve the GUI keyboard state; the next ordinary poll restores physical input.
  if(window->emulator_)window->emulator_->setInput({});
 }
 if(enabled)for(auto*widget:qApp->topLevelWidgets())if(auto*window=dynamic_cast<MainWindow*>(widget)){error_=window->setDeveloperMode(true,false);if(!error_.isEmpty()){const auto failure=error_;setEnabled(false);error_=failure;return false;}break;}
 return true;
}
QJsonObject AiDebugServer::describe(MainWindow*w)const{return {{"id",w->aiId_},{"generation",double(w->generation_)},{"title",w->windowTitle()},{"rom",w->romPath_},{"paused",w->emulator_?w->emulator_->paused():true},{"debuggerStopped",w->emulator_&&w->emulator_->debuggerStopped()},{"developerMode",w->settings_.developerMode},{"linked",!w->linkPartner_.isNull()},{"gbs",w->gbsMode_}};}
void AiDebugServer::dispatch(const QString&method,const QJsonObject&p,Reply reply,const std::shared_ptr<RequestSession>&session){
 if(!session->active()){reply({},-32005,"Debug session closed");return;}
 QPointer<QObject> context(sessionContext_);
 if(method=="rpc.discover"){reply(QJsonObject{{"name","SameBoy Qt Debug API"},{"version",1},{"transport","JSON-RPC 2.0, one UTF-8 object per line over user-local IPC"},{"endpoint",endpoint()},{"methods",QJsonArray{"windows.list","debug.status","debug.break","debug.command","debug.logs","debug.evaluate","registers.read","memory.read","memory.write","screen.capture","input.set","input.release","emulation.pause","emulation.resume","emulation.advance","emulation.step","state.save","state.load","symbols.load","link.connect","link.disconnect","link.status","infrared.status","printer.connect","printer.disconnect","printer.status","printer.capture","printer.clear"}}},0,{});return;}
 QList<MainWindow*>windows;for(auto*widget:qApp->topLevelWidgets())if(auto*window=dynamic_cast<MainWindow*>(widget))windows.append(window);
 if(method=="windows.list"){QJsonArray list;for(auto*window:windows)list.append(describe(window));reply(QJsonObject{{"windows",list}},0,{});return;}
 if(p.contains("window")&&(!p.value("window").isString()||p.value("window").toString().isEmpty())){reply({},-32602,"window must be a nonempty id");return;}
 if(p.contains("generation")&&(!p.value("generation").isDouble()||p.value("generation").toDouble()<0||std::floor(p.value("generation").toDouble())!=p.value("generation").toDouble())){reply({},-32602,"generation must be a nonnegative integer");return;}
 MainWindow*w=nullptr;const auto id=p.value("window").toString();if(!id.isEmpty()){for(auto*window:windows)if(window->aiId_==id)w=window;}else{for(auto*window:windows)if(window->emulator_){if(w){reply({},-32602,"Specify window when multiple ROM windows are open");return;}w=window;}}
 if(!w||!w->emulator_){reply({},-32001,"No matching running ROM window");return;}if(p.contains("generation")&&p.value("generation").toDouble(-1)!=double(w->generation_)){reply({},-32002,"ROM generation changed");return;}
 if(w->closing_){reply({},-32005,"Window is closing or changing cartridges");return;}
 auto*worker=w->emulator_.get();QPointer<MainWindow>window(w);const auto generation=w->generation_;
 auto core=[context,session,window,generation,worker,reply](std::function<QString(GB_gameboy_t*,QJsonObject&)>function,std::function<void()>success={}){auto data=std::make_shared<QJsonObject>();worker->executeAsync(context.data(),[function,data,session](GB_gameboy_t*g){return session->run([&]{return function(g,*data);});},[window,generation,data,reply,success](QString error){if(!window||window->generation_!=generation){reply({},-32002,"ROM changed or window closed");return;}if(!error.isEmpty()){reply({},-32602,error);return;}if(success)success();data->insert("generation",double(generation));reply(*data,0,{});});};
 if(method=="link.connect"){
  if(!p.value("peer").isString()||p.value("peer").toString().isEmpty()){reply({},-32602,"peer window id is required");return;}
  MainWindow*peer=nullptr;for(auto*candidate:windows)if(candidate->aiId_==p.value("peer").toString())peer=candidate;
  if(!peer||!peer->emulator_||peer->closing_){reply({},-32001,"No matching peer ROM window");return;}
  if(peer==w||w->gbsMode_||peer->gbsMode_){reply({},-32602,"Link requires two different ROM windows (not GBS)");return;}
  if(p.contains("peerGeneration")&&p.value("peerGeneration").toDouble(-1)!=double(peer->generation_)){reply({},-32002,"Peer ROM generation changed");return;}
  w->connectTo(peer);reply(QJsonObject{{"connected",w->linkPartner_==peer},{"peer",peer->aiId_},{"peerGeneration",double(peer->generation_)},{"cable",true},{"infrared",true}},0,{});return;
 }
 if(method=="link.disconnect"){w->disconnectLink();reply(QJsonObject{{"connected",false}},0,{});return;}
 if(method=="link.status"||method=="infrared.status"){
  QJsonObject status{{"connected",bool(w->linkPartner_)},{"cable",bool(w->linkPartner_)},{"infrared",bool(w->linkPartner_)},{"peer",w->linkPartner_?QJsonValue(w->linkPartner_->aiId_):QJsonValue()}};
  if(w->linkPartner_)status.insert("peerGeneration",double(w->linkPartner_->generation_));
  if(method=="infrared.status")core([status](GB_gameboy_t*g,QJsonObject&out){out=status;out.insert("supported",GB_is_cgb(g));if(GB_is_cgb(g)){auto byte=debugMemory(g,0,0,0xff56,1).bytes;out.insert("register",byte.isEmpty()?QJsonValue():QJsonValue(uint8_t(byte[0])));}return QString();});else reply(status,0,{});return;
 }
 if(method=="printer.connect"||method=="printer.disconnect"||method=="printer.status"){
  if(method=="printer.connect"){if(w->gbsMode_){reply({},-32602,"Printer requires a ROM window, not GBS");return;}w->printerAction_->trigger();}
  if(method=="printer.disconnect"&&w->printerAction_->isChecked())w->noneAccessory_->trigger();
  w->pollPrinterOutput();auto*printer=w->printerWindow_.data();QJsonObject status{{"requested",w->printerAction_->isChecked()},{"printing",printer&&printer->printing()},{"hasFeed",printer&&!printer->feed().isNull()},{"height",printer?printer->feed().height():0}};
  core([status](GB_gameboy_t*g,QJsonObject&out){out=status;out.insert("connected",GB_get_built_in_accessory(g)==GB_ACCESSORY_PRINTER);return QString();});return;
 }
 if(method=="printer.clear"){w->pollPrinterOutput();if(w->printerWindow_)w->printerWindow_->clearFeed();reply(QJsonObject{{"cleared",true}},0,{});return;}
 if(method=="printer.capture"){
  w->pollPrinterOutput();auto image=w->printerWindow_?w->printerWindow_->feed():QImage();if(image.isNull()){reply({},-32004,"No printed feed available");return;}
  QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG")){reply({},-32000,"Could not encode printer image");return;}reply(QJsonObject{{"width",image.width()},{"height",image.height()},{"format","png"},{"base64",QString::fromLatin1(bytes.toBase64())},{"generation",double(generation)}},0,{});return;
 }
 if(method=="debug.status"||method=="registers.read"){auto info=describe(w);core([worker,info](GB_gameboy_t*g,QJsonObject&out){out=info;out.insert("model",int(GB_get_model(g)));auto*r=GB_get_registers(g);out.insert("registers",QJsonObject{{"af",r->af},{"bc",r->bc},{"de",r->de},{"hl",r->hl},{"sp",r->sp},{"pc",r->pc}});out.insert("debuggerStopped",GB_debugger_is_stopped(g));out.insert("completedCommand",double(worker->completedDebugCommand()));out.insert("cpuUsage",GB_debugger_get_second_cpu_usage(g));return QString();});return;}
 if(method=="debug.break"||method=="debug.command"){
  if(!w->settings_.developerMode){reply({},-32003,"Enable Developer Mode");return;}const auto command=method=="debug.break"?QString("interrupt"):p.value("command").toString();if(command.isEmpty()||command.toUtf8().size()>8192||command.contains('\n')||command.contains('\r')||command.contains(QChar(0))){reply({},-32602,"One nonempty debugger command (at most 8192 bytes) is required");return;}
  const auto cursor=worker->debugLogs(0,1).next;const auto ticket=worker->sendDebugCommand(command,session);if(!ticket){reply({},-32000,"Debugger command queue is full or emulation is stopping");return;}w->pause_->setChecked(false);reply(QJsonObject{{"accepted",true},{"commandId",double(ticket)},{"afterLog",double(cursor)},{"generation",double(generation)}},0,{});return;
 }
 if(method=="debug.logs"){int limit=500;if(p.contains("limit")&&!integer(p.value("limit"),1,1000,limit)){reply({},-32602,"limit must be 1..1000");return;}const double after=p.value("after").toDouble(0);if((p.contains("after")&&!p.value("after").isDouble())||after<0||after>9007199254740991.0||std::floor(after)!=after){reply({},-32602,"Invalid log cursor");return;}auto logs=worker->debugLogs(quint64(after),limit);QJsonArray entries;for(const auto&line:logs.entries)entries.append(QJsonObject{{"sequence",double(line.sequence)},{"text",line.text},{"attributes",line.attributes}});reply(QJsonObject{{"entries",entries},{"first",double(logs.first)},{"next",double(logs.entries.isEmpty()?quint64(after):logs.entries.last().sequence)},{"latest",double(logs.next)},{"truncated",after>0&&after+1<double(logs.first)}},0,{});return;}
 if(method=="debug.evaluate"){const auto expression=p.value("expression").toString();if(expression.isEmpty()||expression.toUtf8().size()>8192||expression.contains(QChar(0))){reply({},-32602,"Invalid expression");return;}core([expression](GB_gameboy_t*g,QJsonObject&out){uint16_t value=0,bank=0;if(GB_debugger_evaluate(g,expression.toUtf8().constData(),&value,&bank))return QString("Invalid debugger expression");out={{"value",value},{"bank",bank==0xffff?-1:int(bank)}};return QString();});return;}
 if(method=="memory.read"||method=="memory.write"){
  const QStringList spaces{"entire","rom","vram","cartridge","ram"};const auto spaceName=p.value("space").toString("entire");const int space=spaces.indexOf(spaceName);int address=0,bank=0,count=1;if((p.contains("space")&&!p.value("space").isString())||space<0||!integer(p.value("address"),0,65535,address)||(p.contains("bank")&&!integer(p.value("bank"),0,16383,bank))){reply({},-32602,"Invalid memory space, address or bank");return;}
  QByteArray bytes;if(method=="memory.write"){const auto text=p.value("hex").toString();if(text.isEmpty()||text.size()>8192||text.size()%2||!QRegularExpression("^[0-9a-fA-F]+$").match(text).hasMatch()){reply({},-32602,"hex must encode 1..4096 bytes");return;}bytes=QByteArray::fromHex(text.toLatin1());count=bytes.size();}else if(p.contains("count")&&!integer(p.value("count"),1,4096,count)){reply({},-32602,"count must be 1..4096");return;}
  const bool write=method=="memory.write",writesRom=write&&(space==1||(space==0&&address<0x8000));core([space,address,bank,count,bytes,write](GB_gameboy_t*g,QJsonObject&out){auto info=debugMemory(g,space,bank,0,0);if(address<int(info.base)||address-int(info.base)+count>info.totalSize||bank>=info.banks)return QString("Address range or bank is outside the selected space");const int offset=address-int(info.base);if(write)for(int i=0;i<count;++i){auto error=writeDebugMemory(g,space,bank,offset+i,uint8_t(bytes[i]));if(!error.isEmpty())return error;}auto data=debugMemory(g,space,bank,offset,count);out={{"address",address},{"bank",bank},{"count",data.bytes.size()},{"hex",QString::fromLatin1(data.bytes.toHex().toUpper())}};return QString();},writesRom?[window]{if(window)window->setRomModified(true);}:std::function<void()>{});return;
 }
 if(method=="screen.capture"){auto image=worker->currentFrame();if(image.isNull()){reply({},-32004,"No frame available yet");return;}QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");reply(QJsonObject{{"width",image.width()},{"height",image.height()},{"format","png"},{"base64",QString::fromLatin1(bytes.toBase64())},{"generation",double(generation)}},0,{});return;}
 if(method=="input.set"||method=="input.release"){
  int player=1,ttl=250;if(p.contains("player")&&!integer(p.value("player"),1,4,player)){reply({},-32602,"player must be 1..4");return;}
  if(method=="input.release"){if(p.contains("player")){w->aiInput_.keys[player-1]=0;w->aiInput_.deadlines[player-1]={};}else w->aiInput_={};w->updateInput();reply(QJsonObject{{"released",true}},0,{});return;}
  if(!p.value("buttons").isArray()||(p.contains("ttlMs")&&!integer(p.value("ttlMs"),1,5000,ttl))){reply({},-32602,"buttons array and ttlMs 1..5000 required");return;}const QStringList keys{"right","left","up","down","a","b","select","start"};uint8_t mask=0;for(const auto&value:p.value("buttons").toArray()){const int index=keys.indexOf(value.toString());if(index<0){reply({},-32602,"Unknown button");return;}mask|=1u<<index;}w->aiInput_.keys[player-1]=mask;w->aiInput_.deadlines[player-1]=InputClock::now()+std::chrono::milliseconds(ttl);w->updateInput();reply(QJsonObject{{"accepted",true},{"expiresInMs",ttl}},0,{});return;
 }
 if(method=="emulation.pause"||method=="emulation.resume"){const bool paused=method=="emulation.pause";++w->stepRequest_;w->aiAdvancing_=false;w->pause_->setChecked(paused);worker->setPaused(paused);reply(describe(w),0,{});return;}
 if(method=="emulation.advance"||method=="emulation.step"){
  int frames=0;if(!integer(p.value("frames"),1,600,frames)){reply({},-32602,"frames must be 1..600");return;}
  if(w->linkPartner_||worker->debuggerStopped()||w->aiAdvancing_){reply({},-32003,"Frame step requires an unlinked window outside the debugger with no pending step");return;}
  const auto lease=w->aiInput_;auto keys=lease.active();if(method=="emulation.step"){
   keys.fill(0);int player=1;if(p.contains("player")&&!integer(p.value("player"),1,4,player)){reply({},-32602,"player must be 1..4");return;}
   if(p.contains("buttons")&&!p.value("buttons").isArray()){reply({},-32602,"buttons must be an array");return;}
   const QStringList buttons{"right","left","up","down","a","b","select","start"};
   for(auto value:p.value("buttons").toArray()){const int index=buttons.indexOf(value.toString());if(index<0){reply({},-32602,"Unknown button");return;}keys[player-1]|=1u<<index;}
  }
  w->aiInput_.keys.fill(0);w->aiInput_.deadlines.fill({});w->clearInput();
  if(w->audioBuffer_)w->audioBuffer_->clear();
  // Only the worker may unpause when it arms the exact frame budget.
  // GUI/audio callbacks between a normal resume and enqueueing would run extra frames.
  {QSignalBlocker block(w->pause_);w->pause_->setChecked(false);}
  w->aiAdvancing_=true;const auto ticket=++w->stepRequest_;
  worker->stepFrames(context.data(),frames,keys,[window,generation,ticket,reply](QString error,QImage image,int completed){
   if(!window||window->generation_!=generation){reply({},-32002,"ROM changed or window closed");return;}
   if(window->stepRequest_==ticket){window->aiAdvancing_=false;if(error.isEmpty()||(window->emulator_&&window->emulator_->paused()))window->pause_->setChecked(true);}
   if(!error.isEmpty()){reply({},-32003,error);return;}
   QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(image.isNull()||!image.save(&buffer,"PNG")){reply({},-32004,"No completed frame available");return;}
   reply(QJsonObject{{"frames",completed},{"paused",true},{"generation",double(generation)},{"width",image.width()},{"height",image.height()},{"format","png"},{"base64",QString::fromLatin1(bytes.toBase64())}},0,{});
  },method=="emulation.advance"?std::optional<InputLease>(lease):std::nullopt);if(w->audio_)w->audio_->resume();return;
 }
 if(method=="state.save"||method=="state.load"){
  int slot=0;if(!integer(p.value("slot"),1,10,slot)){reply({},-32602,"slot must be 1..10");return;}const auto path=stateSavePath(w->romPath_,slot);const auto legacy=w->storageBase_+QString(".s%1").arg(slot);const bool load=method=="state.load";if(load&&w->audioBuffer_)w->audioBuffer_->clear();const bool bg=w->debugBackground_->isChecked(),objects=w->debugObjects_->isChecked();std::array<bool,4>channels;for(int i=0;i<4;++i)channels[i]=w->debugChannels_[i]->isChecked();
  core([path,legacy,load,bg,objects,channels](GB_gameboy_t*g,QJsonObject&out){if(load){QFile file(QFile::exists(path)?path:legacy);QByteArray data;const auto readError=readStateFile(file,data);if(!readError.isEmpty())return readError;GB_model_t model;if(GB_get_state_model_from_buffer(reinterpret_cast<const uint8_t*>(data.constData()),data.size(),&model)||model!=GB_get_model(g))return QString("State uses a different hardware revision");if(loadStatePreservingOnFailure(g,data))return QString("Invalid or incompatible state");GB_set_background_rendering_disabled(g,!bg);GB_set_object_rendering_disabled(g,!objects);for(int i=0;i<4;++i)GB_set_channel_muted(g,GB_channel_t(i),!channels[i]);}else{QByteArray data(GB_get_save_state_size(g),0);GB_save_state_to_buffer(g,reinterpret_cast<uint8_t*>(data.data()));QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit())return file.errorString();}out={{"path",path},{"loaded",load}};return QString();});return;
 }
 if(method=="symbols.load"){const auto text=p.value("text").toString().toUtf8();if(text.isEmpty()||text.size()>256*1024||text.contains(char(0))||(p.contains("clear")&&!p.value("clear").isBool())){reply({},-32602,"text must contain up to 256 KiB of RGBDS .sym records");return;}const bool clear=p.value("clear").toBool(false);core([text,clear](GB_gameboy_t*g,QJsonObject&out){if(clear)GB_debugger_clear_symbols(g);sameboy_debug_load_symbols(g,text.constData(),text.size());out={{"loaded",true}};return QString();});return;}
 reply({},-32601,"Unknown method");
}
