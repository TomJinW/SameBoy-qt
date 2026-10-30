#pragma once
#include "camerainput.h"
#include "settings.h"
#include <QObject>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoSink>
#include <memory>
#include <functional>
class QCamera;
class CameraSource final:public QObject {
public:
    CameraSource(std::shared_ptr<CameraInput>,QObject*parent=nullptr);
    ~CameraSource()override;
    void configure(const AppSettings&);
    void poll(bool allowed);
    void stop();
    std::function<void(const QString&)> message;
private:
    void start();
    void createSink();
    void fail(const QString&);
    std::shared_ptr<CameraInput> input_;
    QMediaCaptureSession session_;std::unique_ptr<QVideoSink>sink_;QMediaDevices devices_;
    std::unique_ptr<QCamera> camera_;
    AppSettings settings_;quint64 captureEpoch_=0;
    bool wanted_=false,allowed_=false,permissionPending_=false,failed_=false;
};
