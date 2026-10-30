#pragma once
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QJsonObject>
#include <functional>
#include <memory>
class MainWindow;
class RequestSession;
class AiDebugServer final:public QObject{
public:
 explicit AiDebugServer(QString name={},QObject*parent=nullptr);
 ~AiDebugServer()override;
 static AiDebugServer*instance();
 bool setEnabled(bool);
 void shutdown();
 bool enabled()const{return server_.isListening();}
 QString endpoint()const{return server_.isListening()?server_.fullServerName():name_;}
 QString error()const{return error_.isEmpty()?server_.errorString():error_;}
private:
 using Reply=std::function<void(QJsonValue,int,QString)>;
 QSet<QLocalSocket*> clients_;
 QString name_,error_;QLocalServer server_;std::shared_ptr<RequestSession> session_;QObject*sessionContext_=nullptr;
 void dispatch(const QString&,const QJsonObject&,Reply,const std::shared_ptr<RequestSession>&);
 QJsonObject describe(MainWindow*)const;
};
