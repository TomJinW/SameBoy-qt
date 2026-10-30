#include "filterrenderer.h"
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <rhi/qrhi_platform.h>
#include <QOffscreenSurface>
#include <QFile>
#include <QMatrix4x4>
#include <cstring>

namespace {
const char *keys[]={"NearestNeighbor","Bilinear","LCD","Pixelated","SmoothBilinear","MonoLCD","CRT","FlatCRT","Scale2x","Scale4x","AAScale2x","AAScale4x","HQ2x","OmniScale","OmniScaleLegacy","AAOmniScaleLegacy"};
QShader shader(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QShader::fromSerialized(file.readAll());}
}
FilterRenderer::FilterRenderer(QRhi *rhi,QRhiRenderPassDescriptor *pass,int samples):rhi_(rhi),pass_(pass),samples_(samples)
{
    uniforms_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,80));
    sampler_.reset(rhi_->newSampler(QRhiSampler::Nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
    if(!uniforms_->create()||!sampler_->create())error_="Could not create GPU filter resources.";
}
FilterRenderer::~FilterRenderer()=default;
bool FilterRenderer::render(QRhiCommandBuffer *cb,QRhiRenderTarget *target,const QImage &frame,const QImage &old,int filter,int blending)
{
    if(frame.isNull()||!error_.isEmpty())return false;
    if(!current_||current_->pixelSize()!=frame.size()){
        pipelines_.clear();bindings_.reset();current_.reset(rhi_->newTexture(QRhiTexture::RGBA8,frame.size()));previous_.reset(rhi_->newTexture(QRhiTexture::RGBA8,frame.size()));
        if(!current_->create()||!previous_->create()){error_="Could not allocate GPU frame textures.";return false;}
        bindings_.reset(rhi_->newShaderResourceBindings());bindings_->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::VertexStage|QRhiShaderResourceBinding::FragmentStage,uniforms_.get()),
            QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,current_.get(),sampler_.get()),
            QRhiShaderResourceBinding::sampledTexture(2,QRhiShaderResourceBinding::FragmentStage,previous_.get(),sampler_.get())});
        if(!bindings_->create()){error_="Could not bind GPU filter textures.";return false;}
    }
    filter=qBound(0,filter,15);
    auto &pipeline=pipelines_[filter];
    if(!pipeline){
        const auto vertex=shader(":/filters/quad.vert.qsb"),fragment=shader(QString(":/filters/%1.frag.qsb").arg(keys[filter]));
        if(!vertex.isValid()||!fragment.isValid()){error_="A required SameBoy filter shader is missing.";return false;}
        pipeline.reset(rhi_->newGraphicsPipeline());pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pipeline->setShaderStages({{QRhiShaderStage::Vertex,vertex},{QRhiShaderStage::Fragment,fragment}});
        pipeline->setShaderResourceBindings(bindings_.get());pipeline->setRenderPassDescriptor(pass_);pipeline->setSampleCount(samples_);
        if(!pipeline->create()){error_=QString("Could not initialize filter %1.").arg(keys[filter]);return false;}
    }
    auto *updates=rhi_->nextResourceUpdateBatch();
    updates->uploadTexture(current_.get(),frame.convertToFormat(QImage::Format_RGBA8888));
    updates->uploadTexture(previous_.get(),(old.size()==frame.size()?old:frame).convertToFormat(QImage::Format_RGBA8888));
    QByteArray bytes(80,'\0');const auto matrix=rhi_->clipSpaceCorrMatrix();std::memcpy(bytes.data(),matrix.constData(),64);
    const float resolution[]={float(target->pixelSize().width()),float(target->pixelSize().height())};std::memcpy(bytes.data()+64,resolution,8);
    const qint32 mode=old.size()==frame.size()?blending:0;std::memcpy(bytes.data()+72,&mode,4);updates->updateDynamicBuffer(uniforms_.get(),0,80,bytes.constData());
    cb->beginPass(target,Qt::black,{1,0},updates);cb->setGraphicsPipeline(pipeline.get());cb->setViewport(QRhiViewport(0,0,resolution[0],resolution[1]));cb->setShaderResources(bindings_.get());cb->draw(4);cb->endPass();return true;
}

struct OffscreenFilterRenderer::Impl{
 std::unique_ptr<QOffscreenSurface>surface;std::unique_ptr<QRhi>rhi;std::unique_ptr<QRhiTexture>output;std::unique_ptr<QRhiRenderPassDescriptor>pass;std::unique_ptr<QRhiTextureRenderTarget>target;std::unique_ptr<FilterRenderer>renderer;
 Impl(){
#ifdef Q_OS_WIN
    QRhiD3D11InitParams params;
    rhi.reset(QRhi::create(QRhi::D3D11,&params));
    if(!rhi)rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
    QRhiMetalInitParams params;
    rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
    QRhiGles2InitParams params;
    surface.reset(QRhiGles2InitParams::newFallbackSurface());params.fallbackSurface=surface.get();
    rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
}
 QImage render(const QImage&frame,const QImage&old,QSize size,int filter,int blending,QString*error){
 if(frame.isNull()||size.isEmpty())return {};
    auto fail=[&](const QString &message)->QImage{if(error)*error=message;renderer.reset();target.reset();pass.reset();output.reset();return {};};
    if(!rhi)return fail("No GPU or software graphics device is available.");
    if(!output||output->pixelSize()!=size){
    renderer.reset();target.reset();pass.reset();output.reset(rhi->newTexture(QRhiTexture::RGBA8,size,1,QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
    if(!output->create())return fail("Could not create filter output.");
    target.reset(rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(QRhiColorAttachment(output.get()))));
    pass.reset(target->newCompatibleRenderPassDescriptor());target->setRenderPassDescriptor(pass.get());if(!target->create())return fail("Could not create filter target.");
    renderer=std::make_unique<FilterRenderer>(rhi.get(),pass.get());}
    QRhiCommandBuffer *cb;
    if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return fail("Could not begin filter rendering.");
    if(!renderer->render(cb,target.get(),frame,old,filter,blending)){rhi->endOffscreenFrame();return fail(renderer->error());}
    QRhiReadbackResult result;bool done=false;result.completed=[&]{done=true;};
    auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(output.get()),&result);cb->resourceUpdate(updates);
    rhi->endOffscreenFrame();rhi->finish();if(!done||result.data.isEmpty())return fail("Could not read back filtered pixels.");
    QImage image(reinterpret_cast<const uchar *>(result.data.constData()),result.pixelSize.width(),result.pixelSize.height(),QImage::Format_RGBA8888);
    return rhi->isYUpInFramebuffer()?image.mirrored():image.copy();
}
};
OffscreenFilterRenderer::OffscreenFilterRenderer():impl_(std::make_unique<Impl>()){}
OffscreenFilterRenderer::~OffscreenFilterRenderer()=default;
QImage OffscreenFilterRenderer::render(const QImage&frame,const QImage&old,QSize size,int filter,int blending,QString*error){return impl_->render(frame,old,size,filter,blending,error);}
QImage renderFilteredImage(const QImage&frame,const QImage&old,QSize size,int filter,int blending,QString*error){OffscreenFilterRenderer renderer;return renderer.render(frame,old,size,filter,blending,error);}
