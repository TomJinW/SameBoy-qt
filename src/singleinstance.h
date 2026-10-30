#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QSet>
#include <QQueue>
#include <functional>
#include <memory>
class SingleInstance final:public QObject{
public:
 enum Result{Primary,Forwarded,Failed};
 explicit SingleInstance(QString name={},QObject*parent=nullptr);
 Result start(const QString&path,bool aiDebug=false);
 void shutdown();
 void setHandler(std::function<void(QString)>handler);
 void setAiHandler(std::function<void()> handler);
 QString error()const{return error_;}
private:
 QString name_,error_,requestId_;QSet<QString>seen_;QQueue<QString>order_;std::unique_ptr<QLockFile>lock_;QLocalServer server_;QStringList pending_;std::function<void(QString)>handler_;
 bool pendingAi_=false;std::function<void()>aiHandler_;
 bool forward(const QString&path,bool aiDebug);
};
