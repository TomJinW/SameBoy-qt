#include "gpuscreen.h"
#include "filterrenderer.h"
#include <rhi/qrhi.h>
GpuScreen::GpuScreen(QWidget *parent):QRhiWidget(parent){
#ifdef Q_OS_WIN
    setApi(Api::Direct3D11);
#elif defined(Q_OS_MACOS)
    setApi(Api::Metal);
#else
    setApi(Api::OpenGL);
#endif
    connect(this,&QRhiWidget::renderFailed,this,[this]{if(failed)failed(tr("GPU rendering is unavailable. Displaying an unfiltered image."));});
}
GpuScreen::~GpuScreen(){releaseResources();}
void GpuScreen::setFrames(QImage current,QImage previous,int filter,int blending){current_=std::move(current);previous_=std::move(previous);filter_=filter;blending_=blending;update();}
void GpuScreen::initialize(QRhiCommandBuffer *){renderer_=std::make_unique<FilterRenderer>(rhi(),renderTarget()->renderPassDescriptor(),sampleCount());}
void GpuScreen::render(QRhiCommandBuffer *cb){if(!renderer_||current_.isNull())return;if(!renderer_->render(cb,renderTarget(),current_,previous_,filter_,blending_)&&failed)failed(renderer_->error());}
void GpuScreen::releaseResources(){renderer_.reset();}
