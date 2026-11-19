#pragma once
#include <QMainWindow>
#include <QByteArray>
#include <memory>
#include <functional>
#include <QPointer>
#include "settings.h"
#include "inputlease.h"
#include "controllers.h"
#include <QElapsedTimer>
#include <QAudioFormat>
#include <QMap>
#include "mousemotion.h"

class Emulator;
class ScreenWidget;
class AudioOutput;
class QIODevice;
class QAction;
class QTimer;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QSaveFile;
class CheatsDialog;
class CheatSearchDialog;
class AudioBuffer;
class Controllers;
class PrinterWindow;
class LocalLink;
class CameraInput;
class CameraSource;
class MemoryDialog;
class VramDialog;
class QStackedWidget;
class GbsPlayer;
class QLabel;
class DebugConsole;
class AiDebugServer;
class VideoRecorder;

class MainWindow final : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;
    bool openRom(const QString &path);
    static MainWindow* openRequestedRom(const QString&path,QWidget*owner=nullptr);
    QByteArray currentRomHash()const{return romHash_;}
    void refreshMenus();
    void refreshDuringNativeMove(){poll();}
    QString currentRomPath()const{return romPath_;}
    void shutdownForRestart(){stop();hide();}
    // releaseLink: the caller disconnects the link afterwards anyway, so free the partner now
    // instead of leaving it stalled behind this paused window while saves are flushed.
    bool prepareToClose(bool releaseLink=false);
    // Logoff/shutdown may end the process without closeEvent; write every open battery now, silently.
    static void flushAllBatteries();
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    static bool separateRomStorage(const QString&path,QWidget*owner);
    bool focusStorageOwner(const QString &path);
    void chooseRom();
    void stop(bool keepTools=false);
    void refreshToolWindows();
    void setChromeVisible(QWidget*,bool);
    void poll();
    void pollPrinterOutput();
    static MainWindow* controllerInputOwner();
    void clearInput();
    void updateInput();
    void configureAudio();
    void rebuildAudio();
    void scheduleAudioRebuild(bool force=false);
    void showPreferences(int tab=0);
    bool applySettings(const AppSettings&);
    struct PreparedRom;
    bool prepareRom(const QString&,const AppSettings&,PreparedRom&);
    bool openPreparedRom(const PreparedRom&);
    bool adoptSettings(const AppSettings&,const PreparedRom* prepared=nullptr);
    void buildMenus();
    void showGameContextMenu(const QPoint&);
    void saveScreenshot(bool choosePath);
    void saveState(int slot, bool load);
    void loadStateFile(const QString&source);
    void resetGame(bool quick);
    void changeModel(int hardware, bool pocket = false);
    void hotSwap();
    bool saveRom(bool choosePath);
    void setRomModified(bool modified);
    void showCheats();
    void showPrinter();
    void showMemory();
    void showConsole();
    void clearConsole();
    void submitDebugCommand(const QString&);
    QString setDeveloperMode(bool,bool reportError=true);
    void loadConsoleSymbols();
    void pollConsole();
    friend class AiDebugServer;
    void showVram();
    void applyDebugOptions();
    void connectTo(MainWindow*);
    void disconnectLink();
    void showCheatSearch();
    void toggleRecording();
    void stopRecording();
    void toggleVideoRecording();
    void stopVideoRecording();
    void pollVideoRecording();
    void closeCartridge();
    void refreshRecent();
    void coreCommand(const std::function<QString(GB_gameboy_t *)> &command);
    bool closing_=false;
    int romColor_=0; // romModelFamily of the open ROM.
    QSize gameWindowSize_;
    AppSettings settings_ = AppSettings::load();
    std::shared_ptr<CameraInput> cameraInput_;
    std::unique_ptr<CameraSource> cameraSource_;
    std::unique_ptr<Emulator> emulator_;
    ScreenWidget *screen_;
    QStackedWidget*views_=nullptr;
    GbsPlayer*gbsPlayer_=nullptr;
    bool gbsMode_=false;
    bool initialViewportPending_=true;
    int gbsTrack_=0;
    QAction *pause_;
    QTimer *timer_;
    std::unique_ptr<AudioOutput> audio_;
    std::shared_ptr<AudioBuffer> audioBuffer_;
    unsigned audioSampleRate_=96000;
    std::unique_ptr<Controllers> controllers_;
    JoyConAutoPairing autoPairing_;
    std::array<uint8_t,4> keyboardKeys_{},keyboardActions_{};
    quint8 previousHotkeys_=0;
    MouseMotion mouseMotion_;
    QMap<QString,quint32> previousPadButtons_;
    QLabel *fpsLabel_=nullptr,*romPathLabel_=nullptr;
    QAction *showStatusBar_=nullptr,*showMenuBar_=nullptr;
    QElapsedTimer fpsClock_;
    unsigned displayedFrames_=0;
    quint64 emulatedAtClock_=0;
    uint8_t previousSpeedActions_=0;
    uint8_t keys_ = 0;
    uint8_t actions_ = 0;
    QString romPath_;
    QString romSource_;
    QByteArray romHash_;
    QString storageBase_;
    QString screenshotPath_;
    QByteArray activeAudioDevice_;
    QAudioFormat activeAudioFormat_;
    quint64 audioGeneration_=0;
    bool audioRebuildPending_=false,audioRebuilding_=false;
    bool romModified_=false,discardRomOnClose_=false;
    QAction *saveRomAction_=nullptr,*saveRomAsAction_=nullptr;
    QAction *openRomAction_=nullptr,*resetAction_=nullptr,*preferencesAction_=nullptr,*screenshotAction_=nullptr,*fullScreenAction_=nullptr;
    QMenu *saveStateMenu_=nullptr,*loadStateMenu_=nullptr,*modelMenu_=nullptr;
    QMenu *debugMenu_=nullptr,*cheatsMenu_=nullptr,*connectMenu_=nullptr;
    QMenu *cpuFrequencyMenu_=nullptr;
    QAction *currentCpuFrequency_=nullptr;
    QMenu *recent_ = nullptr;
    QMenu *linkMenu_=nullptr;
    QAction *noneAccessory_=nullptr;
    QPointer<MainWindow> linkPartner_;
    QList<QAction *> gameActions_;
    QList<QAction *> modelActions_;
    QAction *developerModeAction_=nullptr,*breakDebuggerAction_=nullptr,*clearConsoleAction_=nullptr;
    QPointer<DebugConsole>console_;
    quint64 consoleSequence_=0,consoleCpuSequence_=0;bool consoleSidePending_=false;
    QElapsedTimer consoleSideClock_;
    QString aiId_;quint64 generation_=0;
    InputLease aiInput_;
    bool aiAdvancing_=false;quint64 stepRequest_=0;
    QAction *debugBackground_=nullptr,*debugObjects_=nullptr;
    std::array<QAction*,3>cpuFrequencyActions_{};
    std::array<QAction*,4>debugChannels_{};
    QPointer<MemoryDialog> memoryDialog_;
    QPointer<VramDialog> vramDialog_;
    QAction *mute_ = nullptr;
    QAction *record_ = nullptr,*videoRecordAction_=nullptr;
    std::unique_ptr<VideoRecorder>videoRecorder_;bool videoFinishing_=false;
    QAction *printerAction_=nullptr;
    QPointer<PrinterWindow> printerWindow_;
    QAction *cheatsEnabled_ = nullptr;
    std::unique_ptr<QSaveFile> recording_;
    quint32 recordedBytes_ = 0;
    // Cocoa's audio formats: 0 WAV, 1 AIFF, 2 raw PCM (frontend-written); VGM is the core's register log.
    int recordingFormat_ = 0;
    QString vgmTarget_, vgmTemporary_;
    QPointer<CheatsDialog> cheatsDialog_;
    QPointer<CheatSearchDialog> cheatSearchDialog_;
};
