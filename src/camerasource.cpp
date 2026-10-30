#include "camerasource.h"
#include <QCamera>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QPermissions>
#include <QVideoFrame>
#include <QImageReader>
CameraSource::CameraSource(std::shared_ptr<CameraInput>input,QObject*parent):QObject(parent),input_(std::move(input)){
    connect(&devices_,&QMediaDevices::videoInputsChanged,this,[this]{
        if(camera_){bool found=false;for(const auto&device:QMediaDevices::videoInputs())if(device.id()==camera_->cameraDevice().id())found=true;
            if(!found){stop();fail(tr("Camera disconnected. Select another device in Preferences → Camera."));}}
        else if(failed_){failed_=false;wanted_=true;}
    });
}
CameraSource::~CameraSource(){stop();}
void CameraSource::createSink(){
    if(sink_)return;
    sink_=std::make_unique<QVideoSink>(this);session_.setVideoSink(sink_.get());
    const auto capture=captureEpoch_,epoch=input_->snapshot().epoch;
    connect(sink_.get(),&QVideoSink::videoFrameChanged,this,[this,capture,epoch](const QVideoFrame&frame){
        if(capture!=captureEpoch_||epoch!=input_->snapshot().epoch||!allowed_||settings_.cameraMode!=CameraInput::Device)return;
        const auto image=prepareCameraImage(frame.toImage(),settings_.cameraMirror);if(!image.isNull())input_->publish(image);
    });
}
void CameraSource::stop(){
    ++captureEpoch_;allowed_=false;wanted_=false;
    if(camera_)camera_->stop();session_.setCamera(nullptr);camera_.reset();
    session_.setVideoSink(nullptr);sink_.reset();
}
void CameraSource::fail(const QString&text){failed_=true;input_->fail();if(message)message(text);}
void CameraSource::configure(const AppSettings&s){
    stop();settings_=s;failed_=false;input_->configure(s.cameraMode);
    if(s.cameraMode==CameraInput::Device)createSink();
    if(s.cameraMode==CameraInput::Image){
        QImageReader reader(s.cameraImage);reader.setAutoTransform(true);const auto size=reader.size();
        if(size.isValid()&&(size.width()>640||size.height()>640)){
            auto scaled=size.scaled(QSize(640,640),Qt::KeepAspectRatio);
            // Integer aspect scaling can round the narrow dimension to zero.
            scaled.setWidth(qMax(1,scaled.width()));scaled.setHeight(qMax(1,scaled.height()));
            reader.setScaledSize(scaled);
        }
        const auto image=prepareCameraImage(reader.read(),s.cameraMirror);
        if(image.isNull())fail(tr("Cannot read camera image. Select an image in Preferences → Camera."));else input_->publish(image);
    }
}
void CameraSource::poll(bool allowed){
    wanted_|=input_->takeRequest();allowed_=allowed;
    if(!allowed){
        if(sink_||camera_){const bool resume=wanted_||bool(camera_);stop();wanted_=resume;}
        return;
    }
    if(settings_.cameraMode==CameraInput::Device&&wanted_&&!failed_){start();}
}
void CameraSource::start(){
    if(!allowed_||permissionPending_)return;
#if QT_CONFIG(permissions)
    const QCameraPermission permission;
    const auto status=qApp->checkPermission(permission);
    if(status==Qt::PermissionStatus::Undetermined){permissionPending_=true;const auto epoch=input_->snapshot().epoch,capture=captureEpoch_;
        qApp->requestPermission(permission,this,[this,epoch,capture](const QPermission&result){permissionPending_=false;if(captureEpoch_!=capture||input_->snapshot().epoch!=epoch||settings_.cameraMode!=CameraInput::Device)return;if(result.status()==Qt::PermissionStatus::Granted){if(allowed_)start();}else fail(tr("Camera permission denied. Enable access in system settings, or choose an image source."));});return;
    }
    if(status==Qt::PermissionStatus::Denied){fail(tr("Camera permission denied. Enable access in system settings, or choose an image source."));return;}
#endif
    createSink();
    if(!camera_){
        auto device=QMediaDevices::defaultVideoInput();
        if(!settings_.cameraDevice.isEmpty()){device={};for(const auto&candidate:QMediaDevices::videoInputs())if(candidate.id()==settings_.cameraDevice){device=candidate;break;}}
        if(device.isNull()){fail(tr("No camera is available. Select another camera or an image in Preferences → Camera."));return;}
        camera_=std::make_unique<QCamera>(device);session_.setCamera(camera_.get());
        connect(camera_.get(),&QCamera::errorOccurred,this,[this,capture=captureEpoch_](QCamera::Error,const QString&text){if(capture!=captureEpoch_)return;fail(tr("Camera error: %1").arg(text));});
    }
    wanted_=false;camera_->start();
}
