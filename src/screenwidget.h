#pragma once
#include <QWidget>
#include <QImage>
#include <QElapsedTimer>
#include "settings.h"
class GpuScreen;
class QLabel;

class ScreenWidget final : public QWidget {
public:
    explicit ScreenWidget(QWidget *parent = nullptr);
    void setFrame(QImage frame,bool odd=false,bool sgb=false,QImage previous={});
    void setSettings(const AppSettings &settings);
    QImage screenshot() const;
    QRect gameRect()const{return frameRect();}
    bool hasBorder()const{return !frame_.isNull()&&frame_.width()!=160;}
    QSize sizeHint() const override { return {640, 576}; }
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    QImage frame_;
    QImage previous_;
    AppSettings settings_;
    QElapsedTimer fpsTimer_;
    int frames_ = 0;
    double fps_ = 0;
    GpuScreen *gpu_;
    QLabel *overlay_;
    bool odd_=false,sgb_=false,gpuFailed_=false;
    QString gpuError_;
    int blendingMode() const;
    QRect frameRect() const;
    void updateGpu();
    void drawFrame(QPainter &p, const QRect &target) const;
};
