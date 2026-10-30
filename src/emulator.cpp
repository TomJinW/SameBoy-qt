#include "boundedfile.h"
#include "coreallocation.h"
#include "requestsession.h"
#include "emulator.h"
#include "storagepaths.h"
#include "audiobuffer.h"
#include "locallink.h"
#include "camerainput.h"
#include "debuggerbridge.h"
#include <Core/gb.h>
#include <QFile>
#include <QFileInfo>
#include <cstdlib>
#include <QCoreApplication>
#include <QPointer>
#include <QMutexLocker>
#include <QSaveFile>
#include <chrono>
#include <algorithm>
#include <thread>
#include <utility>
#include <cstring>

namespace {
QString readBatteryFile(QFile&file,QByteArray&bytes){
    constexpr qint64 maximum=16*1024*1024;
    if(!file.open(QIODevice::ReadOnly))return file.errorString();
    const auto expected=file.size();
    if(expected<0||expected>maximum)return "Battery save exceeds the 16 MiB limit.";
    // One extra byte detects growth without allowing readAll to allocate unbounded memory.
    bytes=file.read(expected+1);
    if(file.error()!=QFileDevice::NoError||bytes.size()!=expected||file.size()!=expected)
        return "Battery save could not be read completely.";
    return {};
}
struct Output {
    QImage pixels{160, 144, QImage::Format_RGB32};
    QByteArray samples;
    QString*debugCapture=nullptr;
    std::function<void(QString,int)> log;
    std::function<char*()> debugInput;
    std::function<void()> reload;
    bool frameReady = false;double frameCpu=0,secondCpu=0;
    LocalLink*link=nullptr;int side=0;uint64_t linkCycles=0,reportedCycles=0;
    std::atomic<float>*rumble=nullptr;
    std::function<void(PrinterOutput)> printer;
    std::shared_ptr<CameraInput> cameraInput;
    CameraInput::Snapshot cameraTicket;
    QImage cameraImage;
    bool cameraPending=false;
    std::chrono::steady_clock::time_point cameraDeadline;

};
void cameraRequest(GB_gameboy_t*gb){
    auto*out=static_cast<Output*>(GB_get_user_data(gb));out->cameraTicket=out->cameraInput->snapshot();
    out->cameraInput->request();out->cameraPending=true;out->cameraDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
}
uint8_t cameraPixel(GB_gameboy_t*gb,uint8_t x,uint8_t y){
    const auto&image=static_cast<Output*>(GB_get_user_data(gb))->cameraImage;
    return image.isNull()||x>=128||y>=112?0:image.constScanLine(y)[x];
}
void vblank(GB_gameboy_t *gb, GB_vblank_type_t)
{ auto*out=static_cast<Output *>(GB_get_user_data(gb));out->frameReady=true;out->frameCpu=GB_debugger_get_frame_cpu_usage(gb);out->secondCpu=GB_debugger_get_second_cpu_usage(gb); }
uint32_t encode(GB_gameboy_t *, uint8_t r, uint8_t g, uint8_t b) { return qRgb(r, g, b); }
void sample(GB_gameboy_t *gb, GB_sample_t *s)
{
    auto *out = static_cast<Output *>(GB_get_user_data(gb));
    static_assert(sizeof(GB_sample_t)==4);
    out->samples.append(reinterpret_cast<const char *>(s), sizeof(*s));
}
}

Emulator::Emulator(QByteArray rom, QByteArray boot, QString savePath, bool color, AppSettings settings,unsigned sampleRate,std::shared_ptr<AudioBuffer> output,QString legacySavePath,std::shared_ptr<CameraInput> cameraInput)
    : cameraInput_(std::move(cameraInput)),rom_(std::move(rom)), boot_(std::move(boot)), savePath_(std::move(savePath)),legacySavePath_(std::move(legacySavePath)), color_(color), settings_(std::move(settings)),sampleRate_(sampleRate),audioOutput_(std::move(output)) {}
void Emulator::updateSettings(const AppSettings &settings) { QMutexLocker lock(&mutex_); pendingSettings_ = settings; }
QString Emulator::execute(std::function<QString(GB_gameboy_t *)> command)
{
    if (QThread::currentThread()==this) return "Nested core execution is not allowed.";
    Command request{std::move(command), {}};
    auto result = request.result.get_future();
    { QMutexLocker lock(&mutex_);if(!acceptingCommands_||!isRunning()||isInterruptionRequested())return "Emulation is not running.";if(commands_.size()>=256)return "Core command queue is full.";commands_.push_back(std::move(request)); }
    return result.get();
}
void Emulator::executeAsync(QObject *context, std::function<QString(GB_gameboy_t *)> command, std::function<void(QString)> completion)
{
    QPointer<QObject> receiver(context);
    auto deliver=[receiver,completion=std::move(completion)](QString error){
        QMetaObject::invokeMethod(QCoreApplication::instance(),[receiver,completion,error]{if(receiver)completion(error);},Qt::QueuedConnection);
    };
    if(QThread::currentThread()==this){deliver("Nested core execution is not allowed.");return;}
    Command request{[command=std::move(command),deliver](GB_gameboy_t*g){auto error=command(g);deliver(error);return error;},{},deliver};
    QMutexLocker lock(&mutex_);if(!acceptingCommands_||!isRunning()||isInterruptionRequested()){lock.unlock();deliver("Emulation is not running.");return;}if(commands_.size()>=256){lock.unlock();deliver("Core command queue is full.");return;}commands_.push_back(std::move(request));
}
void Emulator::stepFrames(QObject*context,int count,const std::array<uint8_t,4>&keys,StepCompletion completion,std::optional<InputLease> lease){
    QPointer<QObject> receiver(context);
    auto deliver=[receiver,completion=std::move(completion)](QString error,QImage image,int frames){QMetaObject::invokeMethod(QCoreApplication::instance(),[receiver,completion,error,image,frames]{if(receiver)completion(error,image,frames);},Qt::QueuedConnection);};
    quint64 epoch;{QMutexLocker lock(&mutex_);epoch=stepControlEpoch_;}
    executeAsync(context,[this,count,keys,deliver,epoch,lease](GB_gameboy_t*g){
        QMutexLocker lock(&mutex_);
        if(epoch!=stepControlEpoch_)return QString("Frame step canceled before execution");
        if(count<1||count>600)return QString("frames must be 1..600");
        if(link_||GB_debugger_is_stopped(g)||frameStep_)return QString("Frame step requires an unlinked core outside the debugger with no pending step");
        frameStep_=FrameStep{count,0,keys,deliver,lease};stepCancelRequested_=false;paused_=false;return QString();
    },[deliver](QString error){if(!error.isEmpty())deliver(error,{},0);});
}
void Emulator::finishFrameStep(GB_gameboy_t*g,QString error){
    if(!frameStep_)return;auto step=std::move(*frameStep_);frameStep_.reset();
    {QMutexLocker lock(&mutex_);if(stepCancelRequested_.exchange(false)){if(error.isEmpty())error="Frame step interrupted";}else paused_=true;}
    for(unsigned player=0;player<4;++player)GB_set_key_mask_for_player(g,static_cast<GB_key_mask_t>(0),player);
    QImage image;{QMutexLocker lock(&mutex_);image=lastCoreFrame_;}
    if(!error.isEmpty())error+=QString(" (%1 completed frames)").arg(step.completed);
    step.done(error,image,step.completed);
}
quint64 Emulator::sendDebugCommand(const QString&line,std::shared_ptr<RequestSession> session){
    if(line.isEmpty()||line.toUtf8().size()>8192||line.contains('\n')||line.contains('\r')||line.contains(QChar(0)))return 0;QMutexLocker lock(&mutex_);if(!acceptingCommands_||!isRunning()||isInterruptionRequested()||debugCommands_.size()>=128)return 0;
    const auto id=nextDebugCommand_++;debugCommands_.push_back({id,line.toUtf8(),std::move(session)});debugPending_=true;return id;
}
QVector<CpuLoadSample> Emulator::cpuLoadSamples(quint64 after){QMutexLocker lock(&mutex_);QVector<CpuLoadSample>result;for(const auto&sample:cpuLoad_)if(sample.sequence>after)result.append(sample);return result;}
DebugLogBatch Emulator::debugLogs(quint64 after,int limit){QMutexLocker lock(&mutex_);DebugLogBatch result;result.first=debugLogs_.empty()?nextDebugLog_:debugLogs_.front().sequence;result.next=nextDebugLog_-1;
    for(const auto&entry:debugLogs_)if(entry.sequence>after){result.entries.append(entry);if(result.entries.size()>=limit)break;}return result;
}
QString Emulator::captureDebugOutput(GB_gameboy_t*g,const QString&line){auto*out=static_cast<Output*>(GB_get_user_data(g));QString result;auto*previous=out->debugCapture;out->debugCapture=&result;auto bytes=line.toUtf8();GB_debugger_execute_command(g,bytes.data());out->debugCapture=previous;return result;}
QString Emulator::loadDebugSymbols(const QString&path,const QByteArray&registers,const QByteArray&symbols,const QString&symbolPath){return execute([this,path,registers,symbols,symbolPath](GB_gameboy_t*g){debugRomPath_=path;debugSymbolPath_=symbolPath.isEmpty()?QFileInfo(path).path()+"/"+QFileInfo(path).completeBaseName()+".sym":symbolPath;registerSymbols_=registers;romSymbols_=symbols;GB_debugger_clear_symbols(g);sameboy_debug_load_symbols(g,registers.constData(),registers.size());sameboy_debug_load_symbols(g,symbols.constData(),symbols.size());return QString();});}
QString Emulator::reconfigureAudio(unsigned rate,std::shared_ptr<AudioBuffer> output){
 if(rate!=44100&&rate!=48000&&rate!=96000)return "Unsupported sample rate.";
 return execute([this,rate,output](GB_gameboy_t*g){sampleRate_=rate;audioOutput_=output;GB_set_sample_rate(g,rate);auto*out=static_cast<Output*>(GB_get_user_data(g));out->samples.clear();out->samples.reserve(rate*4/50);QMutexLocker lock(&mutex_);audio_.clear();return QString();});
}
QString Emulator::selectGbsTrack(int track){return execute([this,track](GB_gameboy_t*gb){
    if(!rom_.startsWith(QByteArray("GBS\1",4))||track<0||track>=uint8_t(rom_[4]))return QString("Invalid GBS track");
    GB_gbs_switch_track(gb,uint8_t(track));if(audioOutput_)audioOutput_->clear();{QMutexLocker lock(&mutex_);audio_.clear();}return QString();
});}
QString Emulator::setLocalLink(std::shared_ptr<LocalLink> link,int side){
    return execute([this,link,side](GB_gameboy_t*gb){
        auto*out=static_cast<Output*>(GB_get_user_data(gb));if(link_==link)return QString();
        if(link_)link_->detachLocked(linkSide_);
        GB_disconnect_serial(gb);GB_set_infrared_input(gb,false);GB_set_infrared_callback(gb,nullptr);
        link_=link;linkSide_=side;out->link=link.get();out->side=side;out->linkCycles=out->reportedCycles=0;out->frameReady=false;out->samples.resize(0);
        if(audioOutput_)audioOutput_->clear();
        if(link){auto guard=link->lock();link->attachLocked(side,gb,&out->frameReady,&out->linkCycles,&paused_,&debugStopped_,&debugPending_);
            GB_set_serial_transfer_bit_start_callback(gb,[](GB_gameboy_t*g,bool bit){auto*o=static_cast<Output*>(GB_get_user_data(g));o->link->startBitLocked(o->side,bit);});
            GB_set_serial_transfer_bit_end_callback(gb,[](GB_gameboy_t*g){auto*o=static_cast<Output*>(GB_get_user_data(g));return o->link->endBitLocked(o->side);});
            GB_set_infrared_callback(gb,[](GB_gameboy_t*g,bool on){auto*o=static_cast<Output*>(GB_get_user_data(g));o->link->infraredLocked(o->side,on);});
        }return QString();
    });
}
QString Emulator::swapCartridge(const QByteArray &rom, const QString &savePath,const QString &legacySavePath)
{
    return execute([this, rom, savePath,legacySavePath](GB_gameboy_t *gb) {
        if(rom_.startsWith(QByteArray("GBS\1",4)))return QString("Reopen a game ROM to leave GBS playback.");
        if(batteryReadFailed_)return QString("The previous battery could not be read; reopen it before changing cartridges.");
        if(rom.size()<0x150||rom.size()>8*1024*1024||rom.startsWith(QByteArray("GBS\1",4)))
            return QString("Invalid cartridge ROM.");
        const int size = GB_save_battery_size(gb);
        QByteArray current(size, '\0');
        if(size&&GB_save_battery_to_buffer(gb,reinterpret_cast<uint8_t*>(current.data()),current.size()))
            return QString("Could not serialize the previous cartridge battery.");
        const QString target=QFile::exists(savePath)||legacySavePath.isEmpty()?savePath:legacySavePath;
        QByteArray battery;
        if(storageIdentity(target)==storageIdentity(savePath_))battery=current;
        else {
            QFile nextSave(target);
            if(nextSave.exists()){
                const auto error=readBatteryFile(nextSave,battery);
                if(!error.isEmpty())return QString("Could not read the new cartridge battery: %1\n%2").arg(target,error);
            }
        }
        if(size){
            QSaveFile save(savePath_);
            if(!save.open(QIODevice::WriteOnly)||save.write(current)!=current.size()||!save.commit())
                return QString("Could not save the previous cartridge battery.");
        }
        GB_load_rom_from_buffer(gb, reinterpret_cast<const uint8_t *>(rom.constData()), rom.size());
        rom_ = rom;cameraCartridge_.store(rom.size()>0x147&&uint8_t(rom[0x147])==0xfc); savePath_ = savePath;legacySavePath_=legacySavePath;
        if (!battery.isEmpty()) GB_load_battery_from_buffer(gb, reinterpret_cast<const uint8_t *>(battery.constData()), battery.size());
        GB_remove_all_cheats(gb);
        GB_rewind_reset(gb);
        GB_cheat_search_reset(gb);
        return QString();
    });
}
QString Emulator::flushBattery(){return execute([this](GB_gameboy_t*g){
    const auto size=GB_save_battery_size(g);if(!size)return QString();
    if(batteryReadFailed_)return QString("The original battery could not be read; automatic saving is disabled.");
    QByteArray bytes(size,0);QSaveFile file(savePath_);
    if(GB_save_battery_to_buffer(g,reinterpret_cast<uint8_t*>(bytes.data()),bytes.size())||
       !file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
        return QString("Cannot write battery save: %1\n%2").arg(savePath_,file.errorString());
    GB_clear_battery_dirty(g);discardFinalBattery_=false;return QString();
});}
void Emulator::discardBatteryOnClose(bool discard){execute([this,discard](GB_gameboy_t*){discardFinalBattery_=discard;return QString();});}
void Emulator::setVideoCapture(bool enabled){QMutexLocker lock(&mutex_);if(enabled){videoFrames_.clear();videoError_.clear();}videoCapture_=enabled;}
QVector<VideoFramePacket>Emulator::takeVideoFrames(int limit){QMutexLocker lock(&mutex_);QVector<VideoFramePacket>result;while(limit-->0&&!videoFrames_.empty()){result.append(std::move(videoFrames_.front()));videoFrames_.pop_front();}return result;}
QString Emulator::takeVideoError(){QMutexLocker lock(&mutex_);return std::exchange(videoError_,{});}
QVector<PrinterOutput> Emulator::takePrinterOutput(){QMutexLocker lock(&mutex_);return std::exchange(printerOutput_,{});}
Emulator::~Emulator() { requestInterruption(); wait(); }
QImage Emulator::takeFrame(bool *odd,bool *sgb,QImage *previous) { QMutexLocker lock(&mutex_); if(odd)*odd=oddFrame_;if(sgb)*sgb=sgbFrame_;if(previous)*previous=previousFrame_;return std::exchange(frame_, {}); }
QByteArray Emulator::takeAudio() { QMutexLocker lock(&mutex_); return std::exchange(audio_, {}); }
QString Emulator::takeError() { QMutexLocker lock(&mutex_); return std::exchange(error_, {}); }

void Emulator::run()
{
    GB_gameboy_t *gb = allocateGameBoy(settings_.model(color_));
    if (!gb) {
        std::deque<Command> abandoned;
        {
            QMutexLocker lock(&mutex_);
            acceptingCommands_ = false;
            abandoned.swap(commands_);
            debugCommands_.clear();
            debugPending_ = false;
            error_ = QObject::tr("Could not allocate the emulator core.");
        }
        for (auto &command : abandoned) {
            const QString reason = QObject::tr("Emulator core initialization failed.");
            command.result.set_value(reason);
            if (command.canceled) command.canceled(reason);
        }
        return;
    }
    sameboy_debug_enable(gb,settings_.developerMode);
    Output out;out.cameraInput=cameraInput_;
    cameraCartridge_.store(rom_.size()>0x147&&!rom_.startsWith(QByteArray("GBS\1",4))&&uint8_t(rom_[0x147])==0xfc);
    out.printer=[this](PrinterOutput output){QMutexLocker lock(&mutex_);
        if(printerOutput_.size()>=64){error_="Printer output queue is full. Resume the interface before printing more.";return;}
        printerOutput_.append(std::move(output));
    };
    out.rumble=&rumble_;
    out.samples.reserve(sampleRate_*4/50);
    out.pixels.fill(Qt::black);
    out.log=[this](QString text,int attributes){QMutexLocker lock(&mutex_);if(text.size()>65536)text=text.left(65536);debugLogCharacters_+=text.size();debugLogs_.push_back({nextDebugLog_++,std::move(text),attributes});while(debugLogs_.size()>10000||debugLogCharacters_>1024*1024){debugLogCharacters_-=debugLogs_.front().text.size();debugLogs_.pop_front();}};
    out.debugInput=[this,&out]()->char*{QMutexLocker lock(&mutex_);if(activeDebugCommand_){completedDebugCommand_=activeDebugCommand_;activeDebugCommand_=0;}while(!debugCommands_.empty()&&debugCommands_.front().session&&!debugCommands_.front().session->active())debugCommands_.pop_front();if(debugCommands_.empty()){debugPending_=false;return nullptr;}
        auto entry=std::move(debugCommands_.front());debugCommands_.pop_front();debugPending_=true;activeDebugCommand_=entry.id;const auto bytes=entry.bytes;
        const QString echo=">"+QString::fromUtf8(bytes)+"\n";debugLogCharacters_+=echo.size();debugLogs_.push_back({nextDebugLog_++,echo,GB_LOG_BOLD});auto*result=static_cast<char*>(std::malloc(bytes.size()+1));if(result)std::memcpy(result,bytes.constData(),bytes.size()+1);return result;};
    GB_set_user_data(gb, &out);
    GB_set_log_callback(gb,[](GB_gameboy_t*g,const char*text,GB_log_attributes_t attributes){auto*out=static_cast<Output*>(GB_get_user_data(g));if(out->debugCapture)*out->debugCapture+=QString::fromUtf8(text);else out->log(QString::fromUtf8(text),attributes);});
    sameboy_debug_attach(gb);GB_set_async_input_callback(gb,[](GB_gameboy_t*g){return static_cast<Output*>(GB_get_user_data(g))->debugInput();});
    GB_debugger_set_reload_callback(gb,[](GB_gameboy_t*g){auto*out=static_cast<Output*>(GB_get_user_data(g));if(out->reload)out->reload();});
    GB_set_rgb_encode_callback(gb, encode);
    GB_set_vblank_callback(gb, vblank);
    GB_set_rumble_callback(gb,[](GB_gameboy_t*gb,double amplitude){static_cast<Output*>(GB_get_user_data(gb))->rumble->store(float(amplitude));});
    GB_set_turbo_mode(gb, true, true); // Frontend owns pacing; never skip core frames.
    int rewindLength = -1;
    GB_palette_t activePalette{};
    auto applySettings = [&] {
        sameboy_debug_enable(gb,settings_.developerMode);
        GB_set_rumble_mode(gb,static_cast<GB_rumble_mode_t>(settings_.rumbleMode));
        GB_set_rtc_mode(gb, static_cast<GB_rtc_mode_t>(settings_.rtc));
        if (rewindLength != settings_.rewindSeconds) {
            GB_set_rewind_length(gb, settings_.rewindSeconds); rewindLength = settings_.rewindSeconds;
        }
        GB_set_color_correction_mode(gb, static_cast<GB_color_correction_mode_t>(settings_.colorCorrection));
        GB_set_light_temperature(gb, settings_.temperature / 100.0);
        const GB_palette_t *palettes[] = {&GB_PALETTE_GREY, &GB_PALETTE_DMG, &GB_PALETTE_MGB, &GB_PALETTE_GBL};
        activePalette = settings_.palette < 4 ? *palettes[settings_.palette] : settings_.palettes.value(settings_.palette-4).corePalette();
        GB_set_palette(gb, &activePalette);
        GB_set_highpass_filter_mode(gb, static_cast<GB_highpass_mode_t>(settings_.highpass));
        GB_set_interference_volume(gb, settings_.interference / 100.0);
        GB_set_border_mode(gb, static_cast<GB_border_mode_t>(settings_.border));
        const QSize size(GB_get_screen_width(gb), GB_get_screen_height(gb));
        if (out.pixels.size() != size) { out.pixels = QImage(size, QImage::Format_RGB32); out.pixels.fill(Qt::black); }
        GB_set_pixels_output(gb, reinterpret_cast<uint32_t *>(out.pixels.bits()));
    };
    applySettings();
    if(!boot_.isEmpty())GB_load_boot_rom_from_buffer(gb, reinterpret_cast<const uint8_t *>(boot_.constData()), boot_.size());
    if(rom_.startsWith(QByteArray("GBS\1",4)))GB_load_gbs_from_buffer(gb,reinterpret_cast<const uint8_t*>(rom_.constData()),rom_.size(),nullptr);
    else GB_load_rom_from_buffer(gb, reinterpret_cast<const uint8_t *>(rom_.constData()), rom_.size());
    GB_set_sample_rate(gb, sampleRate_);
    GB_apu_set_sample_callback(gb, sample);
    QFile save(QFile::exists(savePath_)||legacySavePath_.isEmpty()?savePath_:legacySavePath_);
    if(save.exists()){
        QByteArray bytes;const auto error=readBatteryFile(save,bytes);
        if(!error.isEmpty()){
            batteryReadFailed_=true;QMutexLocker lock(&mutex_);
            error_="Cannot read battery save; automatic saving is disabled for this session: "+save.fileName()+"\n"+error;
        }else GB_load_battery_from_buffer(gb,reinterpret_cast<const uint8_t*>(bytes.constData()),bytes.size());
        save.close();
    }
    auto persist = [&] {
        const int size = GB_save_battery_size(gb);
        if (!size) return true;
        if(batteryReadFailed_){QMutexLocker lock(&mutex_);error_="Battery was not saved because the original save could not be read: "+savePath_;return false;}
        QByteArray bytes(size, '\0');
        QSaveFile file(savePath_);
        if (GB_save_battery_to_buffer(gb, reinterpret_cast<uint8_t *>(bytes.data()), bytes.size()) == 0
            && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
            GB_clear_battery_dirty(gb);return true;
        } else {
            QMutexLocker lock(&mutex_);
            error_ = "Cannot write battery save: " + savePath_ + "\n" + file.errorString();return false;
        }
    };
    out.reload=[&,this]{
        QFile file(debugRomPath_);
        QByteArray replacement;if(!readBoundedFile(file,replacement,8*1024*1024,112)){GB_log(gb,"Could not reload ROM.\n");return;}
        const bool music=replacement.startsWith(QByteArray("GBS\1",4));
        if(music!=rom_.startsWith(QByteArray("GBS\1",4))||(!music&&replacement.size()<0x150)){
            GB_log(gb,"Could not reload an incomplete or incompatible ROM. Open it from File instead.\n");return;
        }
        if(music){
            auto*probe=allocateGameBoy(GB_MODEL_CGB_E);if(!probe){const auto message=QObject::tr("Could not allocate a temporary emulator core.").toUtf8();GB_log(gb,"%s\n",message.constData());return;}GB_gbs_info_t info{};
            const auto invalid=GB_load_gbs_from_buffer(probe,reinterpret_cast<const uint8_t*>(replacement.constData()),replacement.size(),&info);
            GB_free(probe);GB_dealloc(probe);
            if(invalid||!info.track_count||info.first_track>=info.track_count||uint8_t(replacement[5])==0){GB_log(gb,"Could not reload invalid GBS music.\n");return;}
        }
        QByteArray battery(GB_save_battery_size(gb),'\0');
        if(!battery.isEmpty()&&GB_save_battery_to_buffer(gb,reinterpret_cast<uint8_t*>(battery.data()),battery.size())){GB_log(gb,"Could not serialize battery for reload.\n");return;}
        if(!persist()){GB_log(gb,"Could not reload ROM while the battery save is pending.\n");return;}
        rom_=replacement;GB_reset(gb);
        if(music)GB_load_gbs_from_buffer(gb,reinterpret_cast<const uint8_t*>(rom_.constData()),rom_.size(),nullptr);
        else GB_load_rom_from_buffer(gb,reinterpret_cast<const uint8_t*>(rom_.constData()),rom_.size());
        if(!battery.isEmpty())GB_load_battery_from_buffer(gb,reinterpret_cast<const uint8_t*>(battery.constData()),battery.size());
        cameraCartridge_.store(!music&&uint8_t(rom_[0x147])==0xfc);
        out.samples.clear();if(audioOutput_)audioOutput_->clear();
        GB_debugger_clear_symbols(gb);sameboy_debug_load_symbols(gb,registerSymbols_.constData(),registerSymbols_.size());
        QFile symbols(debugSymbolPath_);QByteArray local;readBoundedFile(symbols,local,4*1024*1024);romSymbols_=std::move(local);
        sameboy_debug_load_symbols(gb,romSymbols_.constData(),romSymbols_.size());applySettings();
    };
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    auto lastSave = next;
    auto measureStart=next;uint64_t emulatedNanoseconds=0;
    unsigned frameNumber = 0;
    bool printerConnected=false;int cameraMode=-1;
    while (!isInterruptionRequested()) {
        auto connection=link_;
        auto coreGuard=connection?connection->lock():std::unique_lock<std::mutex>();
        std::deque<Command> commands;
        { QMutexLocker lock(&mutex_); commands.swap(commands_); }
        for(auto&command:commands){
            if(isInterruptionRequested()){
                const QString reason="Emulation stopped before the command executed.";
                command.result.set_value(reason);if(command.canceled)command.canceled(reason);continue;
            }
            command.result.set_value(command.function(gb));
        }
        if(connection!=link_)continue;
        if(frameStep_&&(connection||stepCancelRequested_.load()||paused_.load()||GB_debugger_is_stopped(gb)))finishFrameStep(gb,"Frame step interrupted");
        {
            std::optional<AppSettings> pending;
            { QMutexLocker lock(&mutex_); pending.swap(pendingSettings_); }
            if (pending) { settings_ = *pending; applySettings(); }
        }
        if(!GB_debugger_is_stopped(gb)&&debugPending_.load())sameboy_debug_async(gb);
        if(settings_.developerMode&&GB_debugger_is_stopped(gb)&&debugStopped_.load()&&!debugPending_.load()){
            if(coreGuard.owns_lock())coreGuard.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(5));next=Clock::now();continue;
        }
        if(cameraInput_){
            const auto camera=cameraInput_->snapshot();
            if(cameraMode!=camera.mode){
                cameraMode=camera.mode;
                GB_set_camera_get_pixel_callback(gb,cameraMode==CameraInput::Noise?nullptr:cameraPixel);
                GB_set_camera_update_request_callback(gb,cameraMode==CameraInput::Noise?nullptr:cameraRequest);
            }
            if(out.cameraPending){
                if(camera.epoch!=out.cameraTicket.epoch){out.cameraTicket=camera;cameraInput_->request();out.cameraDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);}
                const bool timeout=std::chrono::steady_clock::now()>=out.cameraDeadline;
                const bool available=!camera.image.isNull()&&(camera.mode==CameraInput::Image||camera.version>out.cameraTicket.version);
                if(camera.mode==CameraInput::Noise||camera.failed||timeout||available){
                    out.cameraImage=(available&&!camera.failed)?camera.image:QImage();
                    GB_camera_updated(gb);out.cameraPending=false;
                    if(timeout){QMutexLocker lock(&mutex_);error_="Camera timed out. Check the device/permission, or select an image source in Preferences.";}
                }
            }
        }
        const bool desiredPrinter=!connection&&printerConnected_.load();
        if(connection)printerConnected=false;
        else if(printerConnected!=desiredPrinter||(GB_get_built_in_accessory(gb)==GB_ACCESSORY_PRINTER)!=desiredPrinter){
            printerConnected=desiredPrinter;
            if(printerConnected)GB_connect_printer(gb,[](GB_gameboy_t*g,uint32_t*image,uint8_t height,uint8_t top,uint8_t bottom,uint8_t){
                if(!height&&!top&&!bottom)return;
                QImage page(160,top+height+bottom,QImage::Format_RGB32);page.fill(Qt::white);
                // Cocoa uses palette-mapped core pixels and white margins, without reapplying exposure.
                if(height)std::memcpy(page.scanLine(top),image,160*height*sizeof(uint32_t));
                static_cast<Output*>(GB_get_user_data(g))->printer({std::move(page),false});
            },[](GB_gameboy_t*g){static_cast<Output*>(GB_get_user_data(g))->printer({{},true});});
            else {GB_disconnect_serial(gb);out.printer({{},true});}
        }
        if (paused_.load()) {
            if(connection)connection->setSpeedLocked(linkSide_,1);
            if(coreGuard.owns_lock())coreGuard.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            next = Clock::now();
            measureStart=next;emulatedNanoseconds=0;
            continue;
        }
        EmulatorInput input;
        {QMutexLocker lock(&mutex_);input=input_;}
        input.keys[0]|=keys_.load();input.actions[0]|=actions_.load();
        const auto leased=input.lease.active();for(unsigned p=0;p<4;++p)input.keys[p]|=leased[p];
        if(frameStep_){input={};input.keys=frameStep_->lease?frameStep_->lease->active():frameStep_->keys;}
        uint8_t actions=0;for(auto value:input.actions)actions|=value;if(connection)actions&=7;
        if(connection){
            double requested=std::max(1.f,input.speed);
            if(settings_.capTurbo)requested=std::min(requested,settings_.turboPercent/100.0);
            if(actions&4)requested=settings_.capTurbo?settings_.turboPercent/100.0:0;
            connection->setSpeedLocked(linkSide_,requested);
        }
        // Match Cocoa: underclock the core so it generates a full-duration audio stream.
        // Pacing/statistics below use the unmultiplied rate to avoid slowing twice.
        const double slowClock=connection||(actions&4)?1.0:(actions&64)?1.0/32:(actions&32)?1.0/8:(actions&16)?0.5:std::clamp(double(input.speed),1.0/3,1.0);
        constexpr double gameBoyClockHz=4194304.0;
        const double nominalClock=settings_.cpuFrequency==1?gameBoyClockHz:settings_.cpuFrequency==2?2.0*gameBoyClockHz:double(GB_get_unmultiplied_clock_rate(gb));
        const double forcedClock=nominalClock/GB_get_unmultiplied_clock_rate(gb);
        if(audioOutput_){const double nativeFps=GB_get_usual_frame_rate(gb)*GB_get_unmultiplied_clock_rate(gb)/GB_get_clock_rate(gb);audioOutput_->setLatencyBudget(slowClock<0.5?unsigned(2000/nativeFps/slowClock+40):50);}
        GB_set_clock_multiplier(gb,forcedClock*slowClock);
        const bool motion=GB_has_accelerometer(gb);hasMotion_.store(motion);
        if(motion)GB_set_accelerometer_values(gb,input.motionX,input.motionY);
        for(unsigned p=0;p<4;++p){
            auto keys=input.keys[p];
            if(frameNumber&2){if(input.actions[p]&1)keys|=GB_KEY_A_MASK;if(input.actions[p]&2)keys|=GB_KEY_B_MASK;}
            const bool stickDirections=!(p==0&&motion);
            const bool analog=stickDirections&&input.analog[p];
            if(stickDirections&&!analog){if(input.x[p]>.45)keys|=1;if(input.x[p]<-.45)keys|=2;if(input.y[p]<-.45)keys|=4;if(input.y[p]>.45)keys|=8;}
            GB_set_use_faux_analog_inputs(gb,p,analog);
            GB_set_key_mask_for_player(gb,static_cast<GB_key_mask_t>(keys),p);
            if(analog)GB_set_faux_analog_inputs(gb,p,input.x[p],input.y[p]);
        }
        ++frameNumber;
        const bool rewind = (actions & 8) && settings_.rewindSeconds > 0;
        if (rewind) { GB_rewind_pop(gb); GB_rewind_pop(gb); }
        uint64_t ticks = 0;
        if(connection){
            coreGuard.unlock();const bool ready=connection->runFrame(linkSide_);coreGuard=connection->lock();
            if(!ready){debugStopped_=settings_.developerMode&&GB_debugger_is_stopped(gb);coreGuard.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(2));next=Clock::now();continue;}
            ticks=out.linkCycles-out.reportedCycles;out.reportedCycles=out.linkCycles;
        }else{out.frameReady=false;while(!out.frameReady&&!isInterruptionRequested()){const auto cycles=sameboy_debug_run(gb);ticks+=cycles;if(!cycles&&GB_debugger_is_stopped(gb))break;}}
        debugStopped_=settings_.developerMode&&GB_debugger_is_stopped(gb);
        if(debugStopped_&&!out.frameReady){finishFrameStep(gb,"Frame step interrupted by debugger");out.samples.clear();if(audioOutput_)audioOutput_->clear();{QMutexLocker lock(&mutex_);audio_.clear();}if(coreGuard.owns_lock())coreGuard.unlock();next=Clock::now();std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
        const auto duration = ticks * 1000000000ULL / (2ULL * GB_get_unmultiplied_clock_rate(gb));
        emulatedNanoseconds+=duration;
        if (rewind) {
            // Reverse stereo frames, keeping each left/right sample pair together.
            constexpr qsizetype stride = 2 * sizeof(int16_t);
            for (qsizetype left = 0, right = out.samples.size() - stride; left < right; left += stride, right -= stride)
                for (qsizetype byte = 0; byte < stride; ++byte) std::swap(out.samples[left + byte], out.samples[right + byte]);
        }
        {
            QMutexLocker lock(&mutex_);
            cpuLoad_.push_back({nextCpuSample_++,out.frameCpu,out.secondCpu});if(cpuLoad_.size()>256)cpuLoad_.pop_front();
            previousFrame_=lastCoreFrame_;lastCoreFrame_=out.pixels.copy();frame_=lastCoreFrame_;
            oddFrame_=GB_is_odd_frame(gb);sgbFrame_=GB_is_sgb(gb);
            if(videoCapture_){if(videoFrames_.size()>=120){videoCapture_=false;videoError_="Video encoding could not keep up; recording stopped without silently dropping frames.";}else{
                const int blend=settings_.blending==2?(sgbFrame_?1:oddFrame_?3:2):settings_.blending;
                const double fps=GB_get_usual_frame_rate(gb)*GB_get_unmultiplied_clock_rate(gb)/GB_get_clock_rate(gb);
                videoFrames_.push_back({lastCoreFrame_,previousFrame_,out.samples,sampleRate_,fps,settings_.scaling,blend});
            }}
            // At most 100 ms of pending PCM, even if the UI is busy.
            const auto limit=qMax<qsizetype>(sampleRate_*4/10,out.samples.size()*2);
            if (audio_.size() + out.samples.size() > limit) audio_.clear();
            if (captureAudio_.load()) audio_.append(out.samples);
        }
        if(audioOutput_)audioOutput_->append(out.samples);
        if(connection)connection->consumeFrameLocked(linkSide_);
        out.samples.resize(0); // Retain allocated PCM capacity between frames.
        if(frameStep_){++frameStep_->completed;if(--frameStep_->remaining==0)finishFrameStep(gb);}
        const auto now = Clock::now();
        if(now-measureStart>=std::chrono::milliseconds(250)){
            speed_.store(double(emulatedNanoseconds)/std::chrono::duration_cast<std::chrono::nanoseconds>(now-measureStart).count());
            measureStart=now;emulatedNanoseconds=0;
        }
        if (now - lastSave > std::chrono::seconds(5)) {
            if (GB_get_battery_dirty(gb)) persist();
            lastSave = now;
        }
        double speed;
        if(connection)speed=connection->speedLocked();
        else{
            speed=input.speed;
            if(settings_.capTurbo&&speed>1)speed=std::min(speed,settings_.turboPercent/100.0);
            if(actions&4)speed=settings_.capTurbo?settings_.turboPercent/100.0:0;
            else if(actions&64)speed=1.0/32;
            else if(actions&32)speed=1.0/8;
            else if(actions&16)speed=0.5;
        }
        if(frameStep_)speed=0;
        next += std::chrono::nanoseconds(speed > 0 ? static_cast<uint64_t>(duration / speed) : 0);
        if (speed == 0) next = now;
        if (now - next > std::chrono::milliseconds(100)) next = now;
        if(coreGuard.owns_lock())coreGuard.unlock();
        // Underclocked frames can be hundreds of milliseconds apart. Service requests
        // while waiting, without advancing another frame or holding the link mutex.
        while(Clock::now()<next&&!isInterruptionRequested()){
            if(paused_.load()||frameStep_||debugPending_.load())break;
            {QMutexLocker lock(&mutex_);uint8_t current=actions_.load();for(auto value:input_.actions)current|=value;if(connection)current&=7;if(((current^actions)&0x7c)||input_.speed!=input.speed){next=Clock::now();break;}}

            auto waitingLink=link_;auto waitingGuard=waitingLink?waitingLink->lock():std::unique_lock<std::mutex>();
            std::deque<Command> waiting;{QMutexLocker lock(&mutex_);waiting.swap(commands_);}
            for(auto&command:waiting){if(isInterruptionRequested()){
                const QString reason="Emulation stopped before the command executed.";
                command.result.set_value(reason);if(command.canceled)command.canceled(reason);continue;
            }command.result.set_value(command.function(gb));}
            if(waitingLink!=link_)break;if(waitingGuard.owns_lock())waitingGuard.unlock();
            const auto remaining=next-Clock::now();if(remaining>Clock::duration::zero())std::this_thread::sleep_for(std::min(remaining,std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(5))));
        }
    }
    finishFrameStep(gb,"Emulation stopped during frame step");
    // A request can race with interruption after it has passed the enqueue gate.
    // Complete/cancel every accepted promise before destroying the core.
    std::deque<Command> abandoned;
    {QMutexLocker lock(&mutex_);acceptingCommands_=false;abandoned.swap(commands_);debugCommands_.clear();debugPending_=false;activeDebugCommand_=0;}
    for(auto&command:abandoned){const QString reason="Emulation stopped before the command executed.";command.result.set_value(reason);if(command.canceled)command.canceled(reason);}
    auto finalGuard=link_?link_->lock():std::unique_lock<std::mutex>();
    if(link_)link_->detachLocked(linkSide_);
    if(!discardFinalBattery_)persist();
    rumble_.store(0);hasMotion_.store(false);
    GB_free(gb);
    GB_dealloc(gb);
}
