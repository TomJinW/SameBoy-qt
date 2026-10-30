#include "mainwindow.h"
#include "aiclient.h"
#include "nativemodalpump.h"
#include <QCoreApplication>
#include <cstdio>
#include "singleinstance.h"
#include "aidebugserver.h"
#include <QStatusBar>
#include "language.h"
#include "settings.h"
#include "restartsession.h"
#include <QProcess>
#include "windowplacement.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QTimer>
#include <QFileOpenEvent>
#include <QStringList>
#include <functional>
#include "uistyle.h"

class SameBoyApplication final:public QApplication {
public:
    using QApplication::QApplication;
    std::function<void(const QString&)> openFile;
    QStringList pendingFiles;
    bool event(QEvent*event)override{
        if(event->type()==QEvent::FileOpen){const auto path=static_cast<QFileOpenEvent*>(event)->file();
            if(!path.isEmpty()){if(openFile)openFile(path);else pendingFiles.append(path);return true;}}
        return QApplication::event(event);
    }
};

int main(int argc, char *argv[])
{
    // Handle automation without initializing GUI/platform/audio plugins.
    if(argc>1&&QByteArray(argv[1])=="--rpc"){attachCommandConsole();QCoreApplication client(argc,argv);auto args=client.arguments();args.removeFirst();args.removeFirst();return runAiClient(args);}
    if(argc>1&&(QByteArray(argv[1])=="--help"||QByteArray(argv[1])=="-h")){attachCommandConsole();std::puts("SameBoy Qt [--debug-server] [ROM]\nSameBoy Qt --rpc [--endpoint NAME] METHOD [JSON_PARAMS]\nSameBoy Qt --rpc --stdin");return 0;}
    SameBoyApplication app(argc, argv);
    applyInterfaceTheme(AppSettings::load().theme);
    app.setStyleSheet(compactMenuStyle());
    QApplication::setApplicationName("SameBoy Qt");
    QApplication::setApplicationVersion("0.1.0");
    AppTranslator translator;const auto language=resolvedLanguage(AppSettings::load().language,QLocale::system().uiLanguages());
    if(language=="zh_CN"&&translator.load(":/translations/zh_CN.qm"))app.installTranslator(&translator);
    auto args=app.arguments();const bool debugServerRequested=args.removeAll("--debug-server")>0;const bool restoring=args.size()==3&&args[1]=="--restore-session";
    if(!restoring&&(args.size()>2||(args.size()>1&&args[1].startsWith("--")))){std::fputs("Invalid arguments. Use --help.\n",stderr);return 2;}
    const auto session=restoring?decodeRestartSession(args[2]):QVector<RestartWindow>();
    SingleInstance instance;const auto requested=restoring?(session.isEmpty()?QString():session.first().rom):args.size()>1?QFileInfo(args[1]).absoluteFilePath():QString();
    const auto result=instance.start(requested,debugServerRequested);if(result==SingleInstance::Forwarded){for(int i=1;i<session.size();++i){SingleInstance relay;relay.start(session[i].rom);}return 0;}if(result==SingleInstance::Failed){QMessageBox::warning(nullptr,QObject::tr("SameBoy Qt"),instance.error());return 1;}
    NativeModalPump nativeModal([]{QList<QPointer<MainWindow>>games;for(auto*widget:QApplication::topLevelWidgets())if(auto*game=dynamic_cast<MainWindow*>(widget))games.append(game);for(auto game:games)if(game)game->refreshDuringNativeMove();});
    AiDebugServer ai;
    MainWindow window;
    window.show();
    auto enableDebugServer=[&]{if(!ai.setEnabled(true))window.statusBar()->showMessage(ai.error(),10000);};
    if(debugServerRequested)enableDebugServer();
    instance.setAiHandler(enableDebugServer);
    instance.setHandler([&window](const QString&path){auto*owner=window.isVisible()?&window:QApplication::activeWindow();auto*next=new MainWindow;next->setAttribute(Qt::WA_DeleteOnClose);next->show();if(!path.isEmpty()&&!next->openRom(path)){next->deleteLater();return;}placeNewWindow(next,owner);next->raise();next->activateWindow();});
    app.openFile=[&window](const QString&path){window.show();window.raise();window.activateWindow();window.openRom(path);};
    for(const auto&path:app.pendingFiles)QTimer::singleShot(0,&window,[&window,path]{window.openRom(path);});
    app.pendingFiles.clear();
    if(restoring&&!session.isEmpty()){
        QTimer::singleShot(0,&window,[&window,session]{for(int i=0;i<session.size();++i){auto*target=i?new MainWindow:&window;if(i)target->setAttribute(Qt::WA_DeleteOnClose);target->show();if(!session[i].rom.isEmpty())target->openRom(session[i].rom);target->restoreGeometry(session[i].geometry);}});
    }else if(!requested.isEmpty())QTimer::singleShot(0,&window,[&window,requested]{window.openRom(requested);});
    const int exitCode=app.exec();
    if(!app.property("restartRequested").toBool())return exitCode;
    QVector<MainWindow*> windows;const auto restore=decodeRestartSession(app.property("restartSession").toString());
    for(auto*widget:app.topLevelWidgets())if(auto*game=dynamic_cast<MainWindow*>(widget);game){windows.append(game);}
    // Finish battery/WAV writes before the new process can open the same files; release IPC before launch.
    for(auto*game:windows)game->shutdownForRestart();
    ai.shutdown();instance.shutdown();
    if(!QProcess::startDetached(app.applicationFilePath(),{"--restore-session",encodeRestartSession(restore)},app.applicationDirPath())){
        QMessageBox::warning(nullptr,QObject::tr("Restart Application"),QObject::tr("Could not restart the application. Please launch it again manually."));return 1;
    }
    return exitCode;
}
