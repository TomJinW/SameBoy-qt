#pragma once
#include <QDialog>
#include <QImage>
class QPrinter;
class QLabel;class QScrollArea;class QPushButton;
class PrinterWindow final:public QDialog {
public:
    explicit PrinterWindow(QWidget*parent=nullptr);
    bool append(const QImage&strip);
    void setPrinting(bool);
    QImage feed()const{return feed_;}
    bool printing()const{return printing_;}
    bool saveFeed(const QString&path)const;
    void clearFeed();
    bool printTo(QPrinter&)const;
private:
    void refresh();
    void printFeed();
    QImage feed_;
    bool printing_=false;
    QLabel *image_,*status_;
    QScrollArea *scroll_;
    QPushButton *save_,*print_,*clear_;
};
