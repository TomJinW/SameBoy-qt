#include "screenwidget.h"
#include "gpuscreen.h"
#include "filterrenderer.h"
#include <QPainter>
#include <QLabel>
#include <QTimer>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

ScreenWidget::ScreenWidget(QWidget *parent) : QWidget(parent), gpu_(new GpuScreen(this)), overlay_(new QLabel(this))
{
    setFocusPolicy(Qt::StrongFocus);gpu_->setFocusPolicy(Qt::NoFocus);
    setMinimumSize(160,144); fpsTimer_.start(); gpu_->hide(); overlay_->hide();
    overlay_->setStyleSheet("QLabel { color: white; background: rgba(0,0,0,180); padding: 4px 8px; }");
    overlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    gpu_->failed=[this](QString error) {
        QTimer::singleShot(0,this,[this,error] {gpuFailed_=true;gpuError_=error;gpu_->hide();updateGpu();update();});
    };
}
void ScreenWidget::setSettings(const AppSettings &settings) {settings_=settings;updateGpu();update();}
void ScreenWidget::setFrame(QImage frame,bool odd,bool sgb,QImage previous)
{
    previous_=previous.isNull()?frame_:std::move(previous);frame_=std::move(frame);odd_=odd;sgb_=sgb;
    ++frames_;if(fpsTimer_.elapsed()>=1000){fps_=frames_*1000.0/fpsTimer_.restart();frames_=0;}
    updateGpu();update();
}
int ScreenWidget::blendingMode() const {return settings_.blending==2?(sgb_?1:odd_?3:2):settings_.blending;}
QRect ScreenWidget::frameRect() const
{
    if(frame_.isNull())return rect();QSize size=this->size();
    if(settings_.integerScale){const int scale=std::max(1,std::min(width()/frame_.width(),height()/frame_.height()));size=frame_.size()*scale;}
    else if(settings_.aspect)size=frame_.size().scaled(size,Qt::KeepAspectRatio);
    return QRect(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
}
void ScreenWidget::updateGpu()
{
    const bool visible=!frame_.isNull()&&!gpuFailed_&&!qEnvironmentVariableIsSet("SAMEBOY_SOFTWARE_PREVIEW");
    gpu_->setGeometry(frameRect());gpu_->setVisible(visible);
    if(visible)gpu_->setFrames(frame_,previous_,settings_.scaling,blendingMode());
    overlay_->setFont(settings_.monospace);
    overlay_->setText(gpuFailed_?gpuError_:tr("%1 fps").arg(fps_,0,'f',1));overlay_->adjustSize();overlay_->move(8,8);
    overlay_->setVisible(!frame_.isNull()&&(settings_.osd||gpuFailed_));overlay_->raise();
}
void ScreenWidget::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);updateGpu();}
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
    if(frame_.isNull()||!settings_.filterScreenshots)return frame_;
    return renderFilteredImage(frame_,previous_,frame_.size()*4,settings_.scaling,blendingMode());
}
void ScreenWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.fillRect(rect(),frame_.isNull()?palette().color(QPalette::Window):QColor(20,22,24));
    if(frame_.isNull()){p.setPen(palette().color(QPalette::WindowText));p.drawText(rect(),Qt::AlignCenter,tr("Drop a Game Boy / Game Boy Color ROM here\nor open with Ctrl+O"));return;}
    if(gpu_->isHidden())drawFrame(p,frameRect());
}
