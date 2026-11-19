#include "mainwindow.h"
#include "aiclient.h"
#include "nativemodalpump.h"
#include <QCoreApplication>
#include <cstdio>
#include "singleinstance.h"
#include "aidebugserver.h"
#include <QStatusBar>
#include "language.h"
#include "firstlanguage.h"
#include "settings.h"
#include <QProcess>
#include "windowplacement.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QIcon>
#include <QTimer>
#include <QFileOpenEvent>
#include <QSessionManager>
#include <QStringList>
#include <functional>
#include "uistyle.h"
#include "portablesettings.h"

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
#ifdef Q_OS_MACOS
    // Use Cocoa open/save panels in the application; test executables configure their own dialogs.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
#endif
    SameBoyApplication app(argc, argv);
#ifdef SAMEBOY_PORTABLE
    enablePortableSettings();
#endif
    applyInterfaceTheme(AppSettings::load().theme);
    app.setStyleSheet(compactMenuStyle());
#ifndef Q_OS_MACOS
    app.setWindowIcon(QIcon(":/icons/sameboy-qt.png")); // macOS uses the bundle icon
#endif
    QApplication::setApplicationName("SameBoy Qt");
    QApplication::setApplicationVersion("0.1.0");
    auto initialSettings=AppSettings::load();
    AppTranslator translator;const auto language=initialSettings.languageChosen?resolvedLanguage(initialSettings.language):QStringLiteral("en");
    if(language=="zh_CN"&&translator.load(":/translations/zh_CN.qm"))app.installTranslator(&translator);
    auto args=app.arguments();const bool debugServerRequested=args.removeAll("--debug-server")>0;
    if(args.size()>2||(args.size()>1&&args[1].startsWith("--"))){std::fputs("Invalid arguments. Use --help.\n",stderr);return 2;}
    SingleInstance instance;const auto requested=args.size()>1?QFileInfo(args[1]).absoluteFilePath():QString();
    const auto result=instance.start(requested,debugServerRequested);if(result==SingleInstance::Forwarded)return 0;if(result==SingleInstance::Failed){QMessageBox::warning(nullptr,QObject::tr("SameBoy Qt"),instance.error());return 1;}
    if(!initialSettings.languageChosen){
        instance.setHandler([&app](const QString&path){app.pendingFiles.append(path);});
        FirstLanguageDialog choice;
        if(choice.exec()!=QDialog::Accepted)return 0;
        initialSettings.language=choice.selection();initialSettings.languageChosen=true;
        const auto saveError=initialSettings.save();
        if(!saveError.isEmpty()){QMessageBox::warning(nullptr,"Language",saveError);return 1;}
        // The choice applies from the next launch; keep running in this session, including
        // any ROMs Finder or the OS delivered while the dialog was open.
        QMessageBox::information(nullptr,"Language / 语言",QString::fromUtf8(
            "Your language choice has been saved and will take effect the next time you launch the app.\n"
            "语言设置已保存，将在下次启动应用时生效。"));
    }
    NativeModalPump nativeModal([]{QList<QPointer<MainWindow>>games;for(auto*widget:QApplication::topLevelWidgets())if(auto*game=dynamic_cast<MainWindow*>(widget))games.append(game);for(auto game:games)if(game)game->refreshDuringNativeMove();});
    AiDebugServer ai;
    // Cocoa saves each document as the app terminates. Qt only asks to commit data before a session
    // ends (Windows: WM_QUERYENDSESSION) and may then exit without closing windows.
    QObject::connect(&app,&QGuiApplication::commitDataRequest,&app,[](QSessionManager&){MainWindow::flushAllBatteries();},Qt::DirectConnection);
    MainWindow window;
    auto enableDebugServer=[&]{if(!ai.setEnabled(true))window.statusBar()->showMessage(ai.error(),10000);};
    if(debugServerRequested)enableDebugServer();
    instance.setAiHandler(enableDebugServer);
    instance.setHandler([&window](const QString&path){auto*owner=window.isVisible()?&window:QApplication::activeWindow();
        if(!path.isEmpty()){MainWindow::openRequestedRom(path,owner);return;}
        auto*next=new MainWindow;next->setAttribute(Qt::WA_DeleteOnClose);next->show();placeNewWindow(next,owner);next->raise();next->activateWindow();});
    app.openFile=[&window](const QString&path){MainWindow::openRequestedRom(path,&window);};
    for(const auto&path:app.pendingFiles)QTimer::singleShot(0,&window,[&window,path]{MainWindow::openRequestedRom(path,&window);});
    app.pendingFiles.clear();
    if(!requested.isEmpty())QTimer::singleShot(0,&window,[&window,requested]{MainWindow::openRequestedRom(requested,&window);});
    // Finder delivers file-open events during startup. Let those select the
    // first window's content before presenting an otherwise empty homepage.
#ifdef Q_OS_MACOS
    QTimer::singleShot(100,&window,[&window]{if(!window.isVisible())window.show();});
#else
    QTimer::singleShot(0,&window,[&window]{if(!window.isVisible())window.show();});
#endif
    return app.exec();
}
