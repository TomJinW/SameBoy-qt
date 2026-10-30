#pragma once
#include <Core/gb.h>
#include <QWidget>
#include <functional>
class QComboBox;class QPushButton;
class GbsPlayer final:public QWidget {
public:
    explicit GbsPlayer(QWidget*parent=nullptr);
    void setInfo(const GB_gbs_info_t&);
    void setPaused(bool);
    void selectTrack(int);
    void setSamples(const QByteArray&);
    std::function<void(int)> trackRequested;
    std::function<void()> pauseRequested;
private:
    QComboBox*tracks_;QPushButton*pause_;class QLabel*title_,*author_,*copyright_;
    QWidget*wave_;
};
