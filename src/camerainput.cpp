#include "camerainput.h"
#include <QMutexLocker>
#include <QPainter>
#include <algorithm>
#include <cmath>
QImage prepareCameraImage(const QImage&source,bool mirror){
    if(source.isNull())return {};
    // Cocoa asks for at least 130x114 then reads the center 128x112 pixels.
    const double scale=std::max(130.0/source.width(),114.0/source.height());
    QImage image;
    // A very narrow image can expand into millions of unused pixels. Keep the
    // existing smooth scaler for ordinary photos, with at most 1M scratch pixels.
    if(std::ceil(source.width()*scale)*std::ceil(source.height()*scale)<=1024.0*1024){
        auto scaled=source.scaled(QSize(130,114),Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
        image=scaled.copy((scaled.width()-128)/2,(scaled.height()-112)/2,128,112);
    }else{
        const double width=std::round(source.width()*scale),height=std::round(source.height()*scale);
        const QRectF center(std::floor((width-128)/2)*source.width()/width,
                            std::floor((height-112)/2)*source.height()/height,
                            128.0*source.width()/width,112.0*source.height()/height);
        image=QImage(128,112,QImage::Format_ARGB32);
        {QPainter painter(&image);painter.setCompositionMode(QPainter::CompositionMode_Source);
         painter.setRenderHint(QPainter::SmoothPixmapTransform);
         painter.drawImage(QRectF(0,0,128,112),source,center);}
    }
    image=image.convertToFormat(QImage::Format_Grayscale8);
    return mirror?image.mirrored(true,false):image;
}
CameraInput::Snapshot CameraInput::snapshot()const{QMutexLocker lock(&mutex_);return state_;}
void CameraInput::configure(int mode){QMutexLocker lock(&mutex_);state_.mode=mode;state_.image={};state_.failed=false;++state_.epoch;requested_.store(false); }
void CameraInput::publish(const QImage&prepared){if(prepared.size()!=QSize(128,112)||prepared.format()!=QImage::Format_Grayscale8)return;QMutexLocker lock(&mutex_);state_.image=prepared.copy();state_.failed=false;++state_.version;}
void CameraInput::fail(){QMutexLocker lock(&mutex_);state_.failed=true;}
