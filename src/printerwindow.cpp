#include "printerwindow.h"
#include <QFileDialog>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QtMath>
#include <QScreen>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
PrinterWindow::PrinterWindow(QWidget*parent):QDialog(parent){
    setWindowTitle(tr("Printer"));resize(420,620);setMinimumSize(260,260);
    auto*layout=new QVBoxLayout(this);auto*toolbar=new QHBoxLayout;
    status_=new QLabel(tr("Waiting for printer data…"),this);toolbar->addWidget(status_,1);
    save_=new QPushButton(tr("Save…"),this);print_=new QPushButton(tr("Print…"),this);clear_=new QPushButton(tr("Clear"),this);
    toolbar->addWidget(save_);toolbar->addWidget(print_);toolbar->addWidget(clear_);layout->addLayout(toolbar);
    scroll_=new QScrollArea(this);scroll_->setAlignment(Qt::AlignHCenter|Qt::AlignTop);image_=new QLabel(scroll_->viewport());image_->setAlignment(Qt::AlignTop|Qt::AlignHCenter);scroll_->setWidget(image_);layout->addWidget(scroll_,1);
    connect(save_,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getSaveFileName(this,tr("Save printer feed"),"printer.png",tr("PNG images (*.png)"));
        if(!path.isEmpty()){if(QFileInfo(path).suffix().isEmpty())path+=".png";if(!saveFeed(path))QMessageBox::warning(this,tr("Save printer feed"),tr("Could not save the printer image."));}});
    connect(print_,&QPushButton::clicked,this,[this]{printFeed();});connect(clear_,&QPushButton::clicked,this,[this]{clearFeed();});refresh();
}
void PrinterWindow::refresh(){
    const bool available=!feed_.isNull();save_->setEnabled(available);print_->setEnabled(available);clear_->setEnabled(available);
    if(!available){image_->setPixmap({});image_->setText(tr("Printed images will appear here."));image_->adjustSize();return;}
    // Nearest-neighbor 2x preview; PNG export keeps the native 160-pixel width.
    image_->setPixmap(QPixmap::fromImage(feed_.scaled(feed_.size()*2,Qt::IgnoreAspectRatio,Qt::FastTransformation)));image_->adjustSize();
    scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
}
bool PrinterWindow::append(const QImage&strip){
    if(strip.isNull()||strip.width()!=160)return false;
    // Cocoa begins a fresh roll if its printer window was closed before the next job.
    const bool opening=!isVisible();
    if(opening)feed_={};
    if(strip.height()>65536-feed_.height())return false;
    QImage next(160,feed_.height()+strip.height(),QImage::Format_RGB32);if(next.isNull())return false;next.fill(Qt::white);
    {QPainter painter(&next);if(!feed_.isNull())painter.drawImage(0,0,feed_);painter.drawImage(0,feed_.height(),strip);}
    feed_=std::move(next);refresh();setPrinting(true);
    if(opening){
        const int maximum=screen()?qMax(220,screen()->availableGeometry().height()-100):800;
        resize(width(),qBound(220,feed_.height()*2+90,maximum));
        show();raise();
    }
    return true;
}
void PrinterWindow::setPrinting(bool printing){printing_=printing;status_->setText(printing?tr("Printing…"):feed_.isNull()?tr("Waiting for printer data…"):tr("Printing complete"));}
void PrinterWindow::clearFeed(){feed_={};refresh();setPrinting(false);}
bool PrinterWindow::saveFeed(const QString&path)const{if(feed_.isNull())return false;QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&feed_.save(&file,"PNG")&&file.commit();}
void PrinterWindow::printFeed(){
    if(feed_.isNull())return;QPrinter printer(QPrinter::HighResolution);QPrintDialog dialog(&printer,this);if(dialog.exec()!=QDialog::Accepted)return;
    if(!printTo(printer))QMessageBox::warning(this,tr("Print"),tr("Could not print the complete paper roll."));
}
bool PrinterWindow::printTo(QPrinter&printer)const{
    const auto image=feed_;if(image.isNull())return false;
    QPainter painter;if(!painter.begin(&printer))return false;
    const QRect rect(QPoint(0,0),printer.pageLayout().paintRectPixels(printer.resolution()).size());
    if(rect.isEmpty())return false;
    const int pageRows=qMax(1,qFloor(double(rect.height())*image.width()/rect.width()));
    for(int y=0;y<image.height();y+=pageRows){if(y&&!printer.newPage())return false;const int rows=qMin(pageRows,image.height()-y);
        painter.drawImage(QRectF(0,0,rect.width(),double(rows)*rect.width()/image.width()),image,QRectF(0,y,image.width(),rows));}
    return painter.end();
}
