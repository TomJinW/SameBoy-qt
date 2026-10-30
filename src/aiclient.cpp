#include "aiprotocol.h"
#include "aiclient.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr qint64 MaxRequestBytes=1024*1024;
constexpr qint64 MaxResponseBytes=4*1024*1024;
constexpr int ConnectTimeoutMs=2000;
constexpr int WriteTimeoutMs=2000;
constexpr int ResponseTimeoutMs=16000;

int clientError(const char*message){std::fprintf(stderr,"%s\n",message);return 2;}
bool validRequest(const QJsonObject&request,QString&error){
    const auto id=request.value("id");
    if(request.value("jsonrpc")!="2.0"||!request.value("method").isString()||
       (!id.isDouble()&&!id.isString())){
        error="stdin must contain one JSON-RPC 2.0 request with a string/number id and method";return false;
    }
    if(request.contains("params")&&!request.value("params").isObject()){
        error="params must be a JSON object";return false;
    }
    return true;
}
}

void attachCommandConsole(){
#ifdef Q_OS_WIN
    const auto output=GetStdHandle(STD_OUTPUT_HANDLE),error=GetStdHandle(STD_ERROR_HANDLE);
    const bool needsOutput=!output||output==INVALID_HANDLE_VALUE,needsError=!error||error==INVALID_HANDLE_VALUE;
    if(needsOutput||needsError){
        // A GUI-subsystem executable has no console by default. Preserve redirected pipes.
        AttachConsole(ATTACH_PARENT_PROCESS);
        if(needsOutput){auto*stream=std::freopen("CONOUT$","w",stdout);(void)stream;}
        if(needsError){auto*stream=std::freopen("CONOUT$","w",stderr);(void)stream;}
    }
#endif
}

int runAiClient(QStringList args){
    QString endpoint=defaultAiEndpoint();
    if(!args.isEmpty()&&args.first()=="--endpoint"){
        if(args.size()<2)return clientError("--endpoint requires a name");
        endpoint=args[1];args.removeFirst();args.removeFirst();
    }

    QJsonObject request;
    if(!args.isEmpty()&&args.first()=="--stdin"){
        if(args.size()!=1)return clientError("--stdin cannot be combined with a method or params argument");
        QFile input;
        if(!input.open(stdin,QIODevice::ReadOnly))return clientError("Could not read stdin");
        const auto bytes=input.read(MaxRequestBytes+1);
        if(input.error()!=QFileDevice::NoError)return clientError("Could not read stdin");
        if(bytes.size()>MaxRequestBytes)return clientError("stdin request exceeds the 1 MiB limit");
        QJsonParseError parseError;
        const auto document=QJsonDocument::fromJson(bytes,&parseError);
        if(parseError.error!=QJsonParseError::NoError||!document.isObject())
            return clientError("stdin must contain one JSON object");
        request=document.object();
        QString error;if(!validRequest(request,error))return clientError(error.toUtf8().constData());
    }
    else{
        if(args.size()>2)return clientError("Usage: --rpc [--endpoint NAME] METHOD [JSON_PARAMS]");
        QJsonObject params;
        if(args.size()>1){
            QJsonParseError parseError;const auto document=QJsonDocument::fromJson(args[1].toUtf8(),&parseError);
            if(parseError.error!=QJsonParseError::NoError||!document.isObject())return clientError("params must be a JSON object");
            params=document.object();
        }
        request={{"jsonrpc","2.0"},{"id",1},{"method",args.isEmpty()?QString("rpc.discover"):args[0]},{"params",params}};
    }

    auto frame=QJsonDocument(request).toJson(QJsonDocument::Compact)+'\n';
    if(frame.size()-1>MaxRequestBytes)return clientError("request exceeds the 1 MiB limit");

    QLocalSocket socket;socket.setReadBufferSize(MaxResponseBytes+1);socket.connectToServer(endpoint);
    if(!socket.waitForConnected(ConnectTimeoutMs))return clientError("Start SameBoy Qt with --debug-server first.");
    if(socket.write(frame)!=frame.size())return clientError("Could not send the complete RPC request");
    socket.flush();QElapsedTimer writeTimer;writeTimer.start();
    while(socket.bytesToWrite()>0){
        const int remaining=WriteTimeoutMs-int(writeTimer.elapsed());
        if(remaining<=0||!socket.waitForBytesWritten(remaining))return clientError("IPC request write timeout");
    }

    QByteArray response;QElapsedTimer responseTimer;responseTimer.start();int newline=-1;
    while(newline<0){
        const qint64 capacity=MaxResponseBytes+1-response.size();
        if(capacity<=0)return clientError("IPC response exceeds the 4 MiB limit");
        if(socket.bytesAvailable())response+=socket.read(capacity);
        else{
            const int remaining=ResponseTimeoutMs-int(responseTimer.elapsed());
            if(remaining<=0)return clientError("IPC response timeout");
            if(!socket.waitForReadyRead(remaining)){
                if(socket.bytesAvailable())continue;
                return clientError(socket.state()==QLocalSocket::UnconnectedState?"IPC connection closed before a response":"IPC response timeout");
            }
            continue;
        }
        newline=response.indexOf('\n');
        if(newline<0&&response.size()>MaxResponseBytes)return clientError("IPC response exceeds the 4 MiB limit");
    }
    if(newline+1>MaxResponseBytes||newline+1!=response.size())return clientError("IPC response must contain exactly one bounded line");
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(response.left(newline),&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject())return clientError("IPC returned invalid JSON");
    const auto object=document.object();
    if(object.value("jsonrpc")!="2.0"||object.value("id")!=request.value("id")||
       (object.contains("result")==object.contains("error"))||
       (object.contains("error")&&!object.value("error").isObject()))
        return clientError("IPC returned a mismatched or invalid JSON-RPC response");

    const auto bytes=document.toJson(QJsonDocument::Compact);
    if(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)!=size_t(bytes.size())||std::fputc('\n',stdout)==EOF)
        return clientError("Could not write the RPC response to stdout");
    return object.contains("error")?1:0;
}
