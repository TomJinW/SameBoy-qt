#include "../src/singleinstance.h"
#include <QCoreApplication>
#include <QProcess>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <vector>
#include <cstdio>
int main(int argc,char**argv){QCoreApplication app(argc,argv);auto args=app.arguments();
 if((args.size()==4||args.size()==5)&&args[1]=="--client"){SingleInstance client(args[2]);return client.start(args[3],args.size()==5)==SingleInstance::Forwarded?0:1;}
 auto spin=[&](auto condition,int milliseconds){QElapsedTimer clock;clock.start();while(!condition()&&clock.elapsed()<milliseconds){QCoreApplication::processEvents();QThread::msleep(1);}return condition();};
 const auto name="sameboy-instance-test-"+QString::number(app.applicationPid());
 {SingleInstance primary(name);if(primary.start({})!=SingleInstance::Primary){std::fprintf(stderr,"Primary failed: %s\n",primary.error().toUtf8().constData());return 1;}
  QEventLoop loop;QStringList received;bool aiEnabled=false;primary.setAiHandler([&]{aiEnabled=true;if(received.size()==2&&aiEnabled)QTimer::singleShot(100,&loop,&QEventLoop::quit);});primary.setHandler([&](QString path){received.append(path);if(received.size()==2&&aiEnabled)QTimer::singleShot(100,&loop,&QEventLoop::quit);});
  const auto path=QDir::currentPath()+QString::fromUtf8("/测试 ROM/精灵.gbc");QProcess first,second,ai;
  first.start(app.applicationFilePath(),{"--client",name,path});second.start(app.applicationFilePath(),{"--client",name,QString()});
  ai.start(app.applicationFilePath(),{"--client",name,QString(),"--debug-server"});
  QTimer::singleShot(10000,&loop,&QEventLoop::quit);loop.exec();
  if(!first.waitForFinished(1000)&&first.state()!=QProcess::NotRunning){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}if(!second.waitForFinished(1000)&&second.state()!=QProcess::NotRunning){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
  if(!ai.waitForFinished(1000)&&ai.state()!=QProcess::NotRunning)return 1;
  if(!aiEnabled||ai.exitCode()!=0||first.exitCode()!=0||second.exitCode()!=0||received.size()!=2||!received.contains(path)||!received.contains(QString())){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
  const auto fragmented=QJsonDocument(QJsonObject{{"version",1},{"path",path},{"id","fragmented"},{"aiDebug",false}}).toJson(QJsonDocument::Compact)+'\n';
  for(int attempt=0;attempt<2;++attempt){
   QLocalSocket client;client.connectToServer(name);if(!spin([&]{return client.state()==QLocalSocket::ConnectedState;},1000))return 1;
   client.write(fragmented.left(fragmented.size()/2));client.flush();
   {QEventLoop part;QTimer::singleShot(30,&part,&QEventLoop::quit);part.exec();}
   if(received.size()!=2+(attempt?1:0)){std::fputs("Partial IPC request was dispatched\n",stderr);return 1;}
   client.write(fragmented.mid(fragmented.size()/2));client.flush();
   if(!spin([&]{return client.canReadLine();},1000)||client.readLine()!="OK\n")return 1;
   if(received.size()!=3){std::fputs("Fragmented/retried launch request was lost or duplicated\n",stderr);return 1;}
  }
  QLocalSocket oversized;oversized.connectToServer(name);if(!spin([&]{return oversized.state()==QLocalSocket::ConnectedState;},1000))return 1;
  oversized.write(QByteArray(65537,'x'));oversized.flush();
  if(!spin([&]{return oversized.state()==QLocalSocket::UnconnectedState;},1000)){std::fputs("Oversized IPC request was retained\n",stderr);return 1;}
  std::vector<std::unique_ptr<QLocalSocket>> stalled;
  for(int i=0;i<40;++i){auto client=std::make_unique<QLocalSocket>();client->connectToServer(name);stalled.push_back(std::move(client));}
  if(!spin([&]{int rejected=0;for(const auto&client:stalled)if(client->state()==QLocalSocket::UnconnectedState)++rejected;return rejected>=8;},1000)){
   std::fputs("Launch server retained more than 32 unfinished connections\n",stderr);return 1;
  }
  for(auto&client:stalled)client->abort();stalled.clear();QCoreApplication::processEvents();
  QLocalSocket idle;idle.connectToServer(name);if(!spin([&]{return idle.state()==QLocalSocket::ConnectedState;},1000))return 1;
  idle.write("{");idle.flush();
  if(!spin([&]{return idle.state()==QLocalSocket::UnconnectedState;},6000)){std::fputs("Unfinished IPC request has no receive deadline\n",stderr);return 1;}
 }
 SingleInstance retiring(name);if(retiring.start({})!=SingleInstance::Primary)return 1;
 QLocalSocket incomplete;incomplete.connectToServer(name);
 if(!spin([&]{return incomplete.state()==QLocalSocket::ConnectedState;},1000))return 1;
 incomplete.write("{\"version\":1,");incomplete.flush();
 {QEventLoop accept;QTimer::singleShot(30,&accept,&QEventLoop::quit);accept.exec();}
 retiring.shutdown();
 if(!spin([&]{return incomplete.state()==QLocalSocket::UnconnectedState;},300)){
  std::fputs("Shutdown retained a partially received launch connection\n",stderr);return 1;
 }
 SingleInstance restart(name);if(restart.start({})!=SingleInstance::Primary){std::fprintf(stderr,"Failure at line %d\n",__LINE__);return 1;}
 const auto fragmentedName=name+"-ack";QLocalServer fragmentedServer;
 QLockFile fragmentedLock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(fragmentedName+".lock"));
 if(!fragmentedLock.tryLock(0)||!fragmentedServer.listen(fragmentedName))return 1;
 QObject::connect(&fragmentedServer,&QLocalServer::newConnection,&app,[&]{while(auto*peer=fragmentedServer.nextPendingConnection()){
  QObject::connect(peer,&QLocalSocket::disconnected,peer,&QObject::deleteLater);
  QObject::connect(peer,&QLocalSocket::readyRead,peer,[peer]{peer->readAll();peer->write("O");peer->flush();QTimer::singleShot(30,peer,[peer]{peer->write("K\n");peer->flush();peer->disconnectFromServer();});});
 }});
 QProcess fragmentedClient;fragmentedClient.start(app.applicationFilePath(),{"--client",fragmentedName,QString()});
 if(!spin([&]{return fragmentedClient.state()==QProcess::NotRunning;},7000)||fragmentedClient.exitCode()!=0){std::fputs("Fragmented launch acknowledgement was not accepted\n",stderr);return 1;}
 std::puts("Single instance: cross-process Unicode ROM request, empty new-window request, simultaneous clients, split/retried frames, bounded partial requests, shutdown and clean restart passed.");
}
