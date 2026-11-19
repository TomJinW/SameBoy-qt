#pragma once
#include <QImage>
#include <QMutex>
#include <QThread>
#include <atomic>
#include <cstdint>
#include "settings.h"
#include "inputlease.h"
#include <optional>
#include <functional>
#include <future>
#include <deque>
class RequestSession;
class AudioBuffer;
class LocalLink;
class CameraInput;
struct DebugLog{quint64 sequence=0;QString text;int attributes=0;};
struct DebugLogBatch{QVector<DebugLog> entries;quint64 first=0,next=0;};
struct CpuLoadSample{quint64 sequence=0;double frame=0,second=0;};
struct VideoFramePacket{QImage image,previous;QByteArray pcm;unsigned sampleRate=48000;double frameRate=60;int filter=0,blending=0;};
struct PrinterOutput { QImage image; bool done=false; };
struct EmulatorInput {
    std::array<uint8_t,4> keys{},actions{};
    InputLease lease;
    std::array<float,4> x{},y{};
    std::array<bool,4> analog{};
    float motionX=0,motionY=0,speed=1;
};

// All core calls and core-owned memory stay on the emulation thread.
// The GUI consumes only bounded snapshots, never a queued signal per frame.
class Emulator final : public QThread {
public:
    Emulator(QByteArray rom, QByteArray boot, QString savePath, int color, AppSettings settings = {},unsigned sampleRate=48000,std::shared_ptr<AudioBuffer> output={},QString legacySavePath={},std::shared_ptr<CameraInput> cameraInput={});
    ~Emulator() override;
    void setKeys(uint8_t keys) { keys_.store(keys); }
    void setPaused(bool paused) { QMutexLocker lock(&mutex_);++stepControlEpoch_;stepCancelRequested_=true;paused_.store(paused); }
    void setActions(uint8_t actions) { actions_.store(actions); }
    void setInput(const EmulatorInput &input) { QMutexLocker lock(&mutex_); input_=input; }
    bool hasCameraCartridge()const{return cameraCartridge_.load();}
    bool hasMotion() const {return hasMotion_.load();}
    float rumble() const {return rumble_.load();}
    void setPrinterConnected(bool connected) {printerConnected_.store(connected);}
    QVector<PrinterOutput> takePrinterOutput();
    void setVideoCapture(bool);
    QVector<VideoFramePacket>takeVideoFrames(int limit=120);
    QString takeVideoError();
    void setCaptureAudio(bool capture) {captureAudio_.store(capture);}
    void updateSettings(const AppSettings &settings);
    QString execute(std::function<QString(GB_gameboy_t *)> command);
    void executeAsync(QObject *context, std::function<QString(GB_gameboy_t *)> command, std::function<void(QString)> completion);
    quint64 sendDebugCommand(const QString&,std::shared_ptr<RequestSession> session={});
    QVector<CpuLoadSample> cpuLoadSamples(quint64 after=0);
    DebugLogBatch debugLogs(quint64 after=0,int limit=1000);
    bool debuggerStopped()const{return debugStopped_.load();}
    bool paused()const{return paused_.load();}
    quint64 completedDebugCommand()const{return completedDebugCommand_.load();}
    QString captureDebugOutput(GB_gameboy_t*,const QString&);
    QString loadDebugSymbols(const QString&romPath,const QByteArray&registers,const QByteArray&symbols,const QString&symbolPath={});
    QImage currentFrame(){QMutexLocker lock(&mutex_);return lastCoreFrame_;}
    using StepCompletion=std::function<void(QString,QImage,int)>;
    void stepFrames(QObject*,int count,const std::array<uint8_t,4>&keys,StepCompletion,std::optional<InputLease> lease=std::nullopt);
    QString reconfigureAudio(unsigned rate,std::shared_ptr<AudioBuffer> output);
    QString selectGbsTrack(int track);
    QString setLocalLink(std::shared_ptr<LocalLink>,int side=0);
    QString swapCartridge(const QByteArray &rom, const QString &savePath, const QString &legacySavePath={});
    QImage takeFrame(bool *odd=nullptr,bool *sgb=nullptr,QImage *previous=nullptr);
    QByteArray takeAudio();
    QString takeError();
    QString flushBattery();
    void discardBatteryOnClose(bool discard=true);
    double cpuFrequencyHz()const{return cpuFrequencyHz_.load();}
    double recordingFrameRate()const{return recordingFrameRate_.load();}
    double emulationSpeed() const{return speed_.load();}
    // Frames the core has produced, whether or not the GUI drew each of them.
    quint64 emulatedFrames() const{return emulatedFrames_.load();}
protected:
    void run() override;
private:
    bool discardFinalBattery_=false;
    std::shared_ptr<CameraInput> cameraInput_;
    std::atomic<bool> cameraCartridge_{false};
    std::shared_ptr<LocalLink> link_;
    int linkSide_=0;
    std::atomic<bool> printerConnected_{false};
    QVector<PrinterOutput> printerOutput_;
    bool videoCapture_=false;std::deque<VideoFramePacket>videoFrames_;QString videoError_;
    struct DebugCommand {quint64 id;QByteArray bytes;std::shared_ptr<RequestSession> session;};
    std::deque<DebugCommand> debugCommands_;
    std::deque<DebugLog> debugLogs_;
    std::deque<CpuLoadSample>cpuLoad_;quint64 nextCpuSample_=1;
    quint64 nextDebugCommand_=1,nextDebugLog_=1,activeDebugCommand_=0;
    qsizetype debugLogCharacters_=0;
    std::atomic<bool> debugStopped_{false},debugPending_{false};
    std::atomic<quint64> completedDebugCommand_{0};
    struct FrameStep{int remaining=0,completed=0;std::array<uint8_t,4>keys{};StepCompletion done;std::optional<InputLease> lease;};
    std::optional<FrameStep> frameStep_;
    void finishFrameStep(GB_gameboy_t*,QString error={});
    std::atomic<bool>stepCancelRequested_{false};
    quint64 stepControlEpoch_=0;
    QString debugRomPath_,debugSymbolPath_;
    QByteArray registerSymbols_,romSymbols_;
    EmulatorInput input_;
    std::atomic<bool> hasMotion_{false},captureAudio_{true};
    std::atomic<float> rumble_{0};
    QByteArray rom_, boot_;
    QString savePath_,legacySavePath_;
    int color_; // romModelFamily: 0 DMG, 1 CGB, 2 SGB
    bool batteryReadFailed_=false;
    AppSettings settings_;
    unsigned sampleRate_;
    std::shared_ptr<AudioBuffer> audioOutput_;
    std::atomic<double> speed_{1},cpuFrequencyHz_{4194304},recordingFrameRate_{59.7275};
    std::atomic<quint64> emulatedFrames_{0};
    std::optional<AppSettings> pendingSettings_;
    std::atomic<uint8_t> actions_{0};
    std::atomic<uint8_t> keys_{0};
    std::atomic<bool> paused_{false};
    QMutex mutex_;
    QImage frame_;
    QImage lastCoreFrame_,previousFrame_;
    bool oddFrame_=false,sgbFrame_=false;
    QByteArray audio_;
    QString error_;
    struct Command {
        std::function<QString(GB_gameboy_t *)> function;
        std::promise<QString> result;
        std::function<void(QString)> canceled;
    };
    std::deque<Command> commands_;
    bool acceptingCommands_=true;
};
