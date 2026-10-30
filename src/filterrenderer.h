#pragma once
#include <QImage>
#include <QString>
#include <memory>
#include <map>
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderTarget;
class QRhiRenderPassDescriptor;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiGraphicsPipeline;
QImage renderFilteredImage(const QImage &current,const QImage &previous,QSize size,int filter,int blending,QString *error=nullptr);

class FilterRenderer {
public:
    FilterRenderer(QRhi *rhi,QRhiRenderPassDescriptor *pass,int samples=1);
    ~FilterRenderer();
    bool render(QRhiCommandBuffer *cb,QRhiRenderTarget *target,const QImage &current,const QImage &previous,int filter,int blending);
    QString error() const{return error_;}
private:
    QRhi *rhi_;
    QRhiRenderPassDescriptor *pass_;
    int samples_;
    QString error_;
    std::unique_ptr<QRhiBuffer> uniforms_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiTexture> current_,previous_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::map<int,std::unique_ptr<QRhiGraphicsPipeline>> pipelines_;
};

class OffscreenFilterRenderer{
public:
    OffscreenFilterRenderer();~OffscreenFilterRenderer();
    QImage render(const QImage&,const QImage&,QSize,int filter,int blending,QString*error=nullptr);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
