#pragma once
#include <QRhiWidget>
#include <QImage>
#include <functional>
#include <memory>
class FilterRenderer;
class GpuScreen final:public QRhiWidget {
public:
    explicit GpuScreen(QWidget *parent=nullptr);
    ~GpuScreen() override;
    void setFrames(QImage current,QImage previous,int filter,int blending);
    std::function<void(QString)> failed;
protected:
    void initialize(QRhiCommandBuffer *) override;
    void render(QRhiCommandBuffer *) override;
    void releaseResources() override;
private:
    QImage current_,previous_;
    int filter_=0,blending_=0;
    std::unique_ptr<FilterRenderer> renderer_;
};
