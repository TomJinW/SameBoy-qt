#pragma once
#include <QWidget>
#include <QImage>
#include <QElapsedTimer>
#include <functional>
#include "settings.h"
class GpuScreen;
class QLabel;

class ScreenWidget final : public QWidget {
public:
    explicit ScreenWidget(QWidget *parent = nullptr);
    std::function<void()> openRomRequested;
    void beginLoading();
    void setFrame(QImage frame,bool odd=false,bool sgb=false,QImage previous={});
    void setSettings(const AppSettings &settings);
    QImage screenshot() const;
    QRect gameRect()const{return frameRect();}
    bool hasBorder()const{return !frame_.isNull()&&frame_.width()!=160;}
    QSize sizeHint() const override { return {640, 576}; }
    void showMessage(const QString&text);
    QString osdText() const;
protected:
    bool event(QEvent *) override;
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
    QWidget *emptyPage_;
    bool odd_=false,sgb_=false,gpuFailed_=false;
    QString gpuError_,message_;
    QElapsedTimer messageClock_;
    int messageDuration_=0;
    int blendingMode() const;
    QRect frameRect() const;
    void updateGpu();
    void drawFrame(QPainter &p, const QRect &target) const;
};
