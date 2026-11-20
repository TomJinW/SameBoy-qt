#include "screenwidget.h"
#include "gpuscreen.h"
#include "filterrenderer.h"
#include <QPainter>
#include <QLabel>
#include <QTimer>
#include <QResizeEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QKeySequence>
#include <algorithm>
#include <cmath>

ScreenWidget::ScreenWidget(QWidget *parent) : QWidget(parent), gpu_(new GpuScreen(this)), overlay_(new QLabel(this))
{
    emptyPage_=new QWidget(this);
    auto *welcome=new QVBoxLayout(emptyPage_);welcome->setContentsMargins(20,20,20,20);welcome->setSpacing(14);
    welcome->addStretch();
    auto *hint=new QLabel(tr("Drop a Game Boy / Game Boy Color ROM here"));hint->setAlignment(Qt::AlignCenter);hint->setWordWrap(true);welcome->addWidget(hint);
    auto *open=new QPushButton(tr("Open ROM…"));open->setObjectName("openRomButton");open->setCursor(Qt::PointingHandCursor);open->setAutoDefault(false);
    welcome->addWidget(open,0,Qt::AlignHCenter);
    connect(open,&QPushButton::clicked,this,[this]{if(openRomRequested)openRomRequested();});
    auto *shortcut=new QLabel(tr("Open: %1").arg(QKeySequence(QKeySequence::Open).toString(QKeySequence::NativeText)));shortcut->setAlignment(Qt::AlignCenter);welcome->addWidget(shortcut);
    welcome->addStretch();
    setFocusPolicy(Qt::StrongFocus);gpu_->setFocusPolicy(Qt::NoFocus);
    setMinimumSize(160,144); fpsTimer_.start(); gpu_->hide(); overlay_->hide();
    overlay_->setStyleSheet("QLabel { color: white; background: rgba(0,0,0,180); padding: 4px 8px; }");
    overlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    gpu_->failed=[this](QString error) {
        QTimer::singleShot(0,this,[this,error] {// Render failures can be reported on every frame: only the first error is shown, so alternating messages cannot restart the timer.
            const bool repeated=gpuFailed_;
            gpuFailed_=true;gpu_->hide();
            if(!repeated){gpuError_=error;gpuErrorClock_.start();QTimer::singleShot(gpuErrorDuration_+10,this,[this]{updateGpu();});}
            updateGpu();update();});
    };
}
void ScreenWidget::setSettings(const AppSettings &settings) {settings_=settings;updateGpu();update();}
// Moving to a screen with another scale factor changes the integer-scale rectangle even while paused.
bool ScreenWidget::event(QEvent *event){if(event->type()==QEvent::DevicePixelRatioChange){updateGpu();update();}return QWidget::event(event);}
void ScreenWidget::beginLoading()
{
    frame_={};previous_={};emptyPage_->hide();updateGpu();update();
}
void ScreenWidget::setFrame(QImage frame,bool odd,bool sgb,QImage previous)
{
    previous_=previous.isNull()?frame_:std::move(previous);frame_=std::move(frame);odd_=odd;sgb_=sgb;
    emptyPage_->setVisible(frame_.isNull());
    ++frames_;if(fpsTimer_.elapsed()>=1000){fps_=frames_*1000.0/fpsTimer_.restart();frames_=0;}
    updateGpu();update();
}
int ScreenWidget::blendingMode() const {return settings_.blending==2?(sgb_?1:odd_?3:2):settings_.blending;}
QRect ScreenWidget::frameRect() const
{
    if(frame_.isNull())return rect();QSize size=this->size();
    if(settings_.integerScale){
        // Cocoa (GBView setFrame:) scales by whole backing pixels. With fractional Windows scaling
        // (125%, 150%) whole logical pixels are not whole physical ones, so use the largest physical
        // multiple whose logical size is exact; the GPU texture then maps 1:1 onto the screen.
        const qreal dpr=devicePixelRatioF();const int fw=frame_.width(),fh=frame_.height();
        auto exact=[dpr](int physical,int&logical){const qreal value=physical/dpr;logical=qRound(value);return std::abs(value-logical)<1e-6;};
        bool found=false;
        for(int scale=int(std::min(width()*dpr/fw,height()*dpr/fh));scale>=1&&!found;--scale){int w=0,h=0;if(exact(fw*scale,w)&&exact(fh*scale,h)&&w<=width()&&h<=height()){size=QSize(w,h);found=true;}}
        // No exact size fits (e.g. 125% in a small view): keep a whole physical multiple and let Qt's
        // integer geometry round it, which is off by under one physical pixel instead of a fractional scale.
        for(int scale=int(std::min(width()*dpr/fw,height()*dpr/fh));scale>=1&&!found;--scale){const int w=qRound(fw*scale/dpr),h=qRound(fh*scale/dpr);if(w<=width()&&h<=height()){size=QSize(w,h);found=true;}}
        if(!found){const int scale=std::max(1,std::min(width()/fw,height()/fh));size=frame_.size()*scale;}
    }
    else if(settings_.aspect)size=frame_.size().scaled(size,Qt::KeepAspectRatio);
    return QRect(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
}
void ScreenWidget::updateGpu()
{
    emptyPage_->setGeometry(rect());
    const bool visible=!frame_.isNull()&&!gpuFailed_&&!qEnvironmentVariableIsSet("SAMEBOY_SOFTWARE_PREVIEW");
    gpu_->setGeometry(frameRect());gpu_->setVisible(visible);
    if(visible)gpu_->setFrames(frame_,previous_,settings_.scaling,blendingMode());
    overlay_->setFont(settings_.monospace);
    const bool message=!message_.isEmpty()&&messageClock_.isValid()&&messageClock_.elapsed()<messageDuration_;
    // The GPU error is shown for a few seconds only; the software renderer keeps running afterwards.
    const bool gpuErrorShown=gpuFailed_&&gpuErrorClock_.isValid()&&gpuErrorClock_.elapsed()<gpuErrorDuration_;
    overlay_->setText(gpuErrorShown?gpuError_:message?message_:tr("%1 fps").arg(fps_,0,'f',1));overlay_->adjustSize();overlay_->move(8,8);
    overlay_->setVisible(!frame_.isNull()&&(settings_.osd||gpuErrorShown));overlay_->raise();
}
void ScreenWidget::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);updateGpu();}
// Cocoa GBOSDView displayText: shows an event for 2.5 s (4 s longer for multi-line text) when the OSD is enabled.
void ScreenWidget::showMessage(const QString&text)
{
    if(!settings_.osd)return;
    message_=text;messageDuration_=text.contains('\n')?6500:2500;messageClock_.start();updateGpu();
    QTimer::singleShot(messageDuration_+10,this,[this]{updateGpu();});
}
QString ScreenWidget::osdText() const{return overlay_->isVisible()?overlay_->text():QString();}
void ScreenWidget::drawFrame(QPainter &p,const QRect &target) const
{
    QImage image=frame_;
    if(settings_.blending&&previous_.size()==frame_.size()){
        image=frame_.copy();const int mode=blendingMode();
        for(int y=0;y<image.height();++y){auto *line=reinterpret_cast<QRgb *>(image.scanLine(y));const auto *old=reinterpret_cast<const QRgb *>(previous_.constScanLine(y));
            const double ratio=mode==1?0.5:((y&1)==(mode==3)?1.0/3.0:2.0/3.0);
            auto blend=[ratio](int a,int b){return qRound(std::pow(std::pow(a/255.0,2.2)*(1-ratio)+std::pow(b/255.0,2.2)*ratio,1/2.2)*255);};
            for(int x=0;x<image.width();++x)line[x]=qRgb(blend(qRed(line[x]),qRed(old[x])),blend(qGreen(line[x]),qGreen(old[x])),blend(qBlue(line[x]),qBlue(old[x])));
        }
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform,settings_.scaling==1);p.drawImage(target,image);
}
QImage ScreenWidget::screenshot() const
{
    if(frame_.isNull())return {};
    const QSize size=frame_.size()*settings_.screenshotScale;
    if(!settings_.filterScreenshots)return frame_.scaled(size,Qt::IgnoreAspectRatio,Qt::FastTransformation);
    return renderFilteredImage(frame_,previous_,size,settings_.scaling,blendingMode());
}
void ScreenWidget::paintEvent(QPaintEvent *)
{
    // Letterboxing around a game is black, like Cocoa's GBBorderView; the empty page follows the palette.
    QPainter p(this);p.fillRect(rect(),frame_.isNull()?palette().color(QPalette::Window):QColor(Qt::black));
    if(frame_.isNull())return;
    if(gpu_->isHidden())drawFrame(p,frameRect());
}
