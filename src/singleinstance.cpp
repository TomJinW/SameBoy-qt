#include "singleinstance.h"
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <utility>
#include <QUuid>
#include <QElapsedTimer>
#include <QTimer>
SingleInstance::SingleInstance(QString name,QObject*parent):QObject(parent),name_(std::move(name)){
 if(name_.isEmpty())name_="sameboy-qt-"+QString::fromLatin1(QCryptographicHash::hash(QDir::homePath().toUtf8(),QCryptographicHash::Sha256).toHex().left(20));
 server_.setSocketOptions(QLocalServer::UserAccessOption);
 connect(&server_,&QLocalServer::newConnection,this,[this]{while(auto*socket=server_.nextPendingConnection()){
  connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
  // Launch requests are small, one-shot messages. Bound stalled/partial clients too.
  int active=0;for(auto*client:server_.findChildren<QLocalSocket*>(QString(),Qt::FindDirectChildrenOnly))if(client->state()!=QLocalSocket::UnconnectedState)++active;
  if(active>32){socket->abort();continue;}
  socket->setReadBufferSize(65537);
  auto*deadline=new QTimer(socket);deadline->setSingleShot(true);
  connect(deadline,&QTimer::timeout,socket,&QLocalSocket::abort);deadline->start(5000);
  auto read=[this,socket,deadline]{
   if(socket->property("handled").toBool())return;
   if(!server_.isListening()){socket->abort();return;}
   auto bytes=socket->property("request").toByteArray();bytes+=socket->read(65537-bytes.size());
   if(bytes.size()>65536){socket->abort();return;}const int end=bytes.indexOf('\n');if(end<0){socket->setProperty("request",bytes);return;}
   const auto document=QJsonDocument::fromJson(bytes.left(end));const auto request=document.object();
   if(!document.isObject()||request.value("version").toInt()!=1||!request.value("path").isString()||!request.value("id").isString()){socket->abort();return;}
   socket->setProperty("handled",true);socket->setProperty("request",QVariant());deadline->stop();
   const auto path=request.value("path").toString();const bool aiDebug=request.value("aiDebug").toBool();socket->write("OK\n");socket->flush();socket->disconnectFromServer();
   const auto id=request.value("id").toString();if(seen_.contains(id))return;
   seen_.insert(id);order_.enqueue(id);if(order_.size()>256)seen_.remove(order_.dequeue());
   if(aiDebug){if(aiHandler_)aiHandler_();else pendingAi_=true;}
   if(!aiDebug||!path.isEmpty()){if(handler_)handler_(path);else pending_.append(path);}
  };connect(socket,&QLocalSocket::readyRead,this,read);
  if(socket->bytesAvailable())QTimer::singleShot(0,socket,read);
 }});
}
bool SingleInstance::forward(const QString&path,bool aiDebug){QLocalSocket socket;socket.setReadBufferSize(4);socket.connectToServer(name_);if(!socket.waitForConnected(100))return false;
 const auto request=QJsonDocument(QJsonObject{{"version",1},{"path",path},{"id",requestId_},{"aiDebug",aiDebug}}).toJson(QJsonDocument::Compact)+'\n';socket.write(request);
 if(socket.bytesToWrite()&&!socket.waitForBytesWritten(1000))return false;
 QElapsedTimer deadline;deadline.start();
 while(!socket.canReadLine()&&socket.bytesAvailable()<4&&socket.state()!=QLocalSocket::UnconnectedState&&deadline.elapsed()<3000){
  if(!socket.waitForReadyRead(3000-int(deadline.elapsed())))break;
 }
 return socket.canReadLine()&&socket.readLine(4)=="OK\n";
}
SingleInstance::Result SingleInstance::start(const QString&path,bool aiDebug){
 requestId_=QUuid::createUuid().toString(QUuid::WithoutBraces);
 if(forward(path,aiDebug))return Forwarded;
 lock_=std::make_unique<QLockFile>(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name_+".lock"));
 if(!lock_->tryLock(0)){QElapsedTimer deadline;deadline.start();while(deadline.elapsed()<3000){if(forward(path,aiDebug))return Forwarded;QThread::msleep(25);}error_=tr("The existing SameBoy Qt process is not responding.");return Failed;}
 if(!server_.listen(name_)){QLocalServer::removeServer(name_);if(!server_.listen(name_)){error_=tr("Could not start local IPC: %1").arg(server_.errorString());return Failed;}}
 return Primary;
}
void SingleInstance::setHandler(std::function<void(QString)>handler){handler_=std::move(handler);const auto pending=std::exchange(pending_,{});for(const auto&path:pending)handler_(path);}

void SingleInstance::setAiHandler(std::function<void()>handler){aiHandler_=std::move(handler);if(pendingAi_){pendingAi_=false;aiHandler_();}}

void SingleInstance::shutdown(){
 server_.close();
 for(auto*socket:server_.findChildren<QLocalSocket*>())socket->abort();
 lock_.reset();
}
