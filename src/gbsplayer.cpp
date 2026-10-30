#include "gbsplayer.h"
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <cstring>
namespace {
class Wave:public QWidget{
public:QByteArray pcm;using QWidget::QWidget;
protected:void paintEvent(QPaintEvent*)override{
    QPainter p(this);p.fillRect(rect(),QColor(61,47,96));p.setRenderHint(QPainter::Antialiasing);
    const int count=pcm.size()/4;if(!count)return;
    for(int channel=0;channel<2;++channel){QPainterPath path;const float center=height()*(channel?.65:.35);path.moveTo(0,center);
        const int points=qMin(width(),1024);for(int x=0;x<points;++x){qint16 value;const int index=x*(count-1)/qMax(1,points-1);std::memcpy(&value,pcm.constData()+index*4+channel*2,2);
            path.lineTo(float(x)*width()/qMax(1,points-1),center-value/32768.f*height()*.28);}
        p.setPen(QPen(channel?QColor(240,150,120):QColor(200,170,250),1.5));p.drawPath(path);
    }
}};
}
GbsPlayer::GbsPlayer(QWidget*parent):QWidget(parent){
    auto*layout=new QVBoxLayout(this);layout->setContentsMargins(24,24,24,12);
    title_=new QLabel;auto font=title_->font();font.setPointSize(20);font.setBold(true);title_->setFont(font);title_->setAlignment(Qt::AlignCenter);title_->setWordWrap(true);layout->addWidget(title_);
    author_=new QLabel;author_->setAlignment(Qt::AlignCenter);author_->setWordWrap(true);layout->addWidget(author_);
    auto*buttons=new QHBoxLayout;auto*restart=new QPushButton(tr("Restart"));pause_=new QPushButton(tr("Pause"));tracks_=new QComboBox;tracks_->setObjectName("gbsTracks");
    auto*previous=new QPushButton(tr("Previous"));auto*next=new QPushButton(tr("Next"));buttons->addWidget(restart);buttons->addWidget(pause_);buttons->addWidget(tracks_,1);buttons->addWidget(previous);buttons->addWidget(next);layout->addLayout(buttons);
    wave_=new Wave;wave_->setMinimumHeight(140);layout->addWidget(wave_,1);
    copyright_=new QLabel;copyright_->setAlignment(Qt::AlignCenter);copyright_->setWordWrap(true);layout->addWidget(copyright_);
    connect(tracks_,&QComboBox::currentIndexChanged,this,[this](int track){if(track>=0&&trackRequested)trackRequested(track);});
    connect(restart,&QPushButton::clicked,this,[this]{if(trackRequested)trackRequested(tracks_->currentIndex());});
    connect(pause_,&QPushButton::clicked,this,[this]{if(pauseRequested)pauseRequested();});
    connect(previous,&QPushButton::clicked,this,[this]{if(tracks_->count())tracks_->setCurrentIndex((tracks_->currentIndex()+tracks_->count()-1)%tracks_->count());});
    connect(next,&QPushButton::clicked,this,[this]{if(tracks_->count())tracks_->setCurrentIndex((tracks_->currentIndex()+1)%tracks_->count());});
}
void GbsPlayer::setInfo(const GB_gbs_info_t&info){QSignalBlocker block(tracks_);title_->setText(QString::fromLatin1(info.title));author_->setText(QString::fromLatin1(info.author));copyright_->setText(QString::fromUtf8("©")+QString::fromLatin1(info.copyright));tracks_->clear();for(int i=0;i<info.track_count;++i)tracks_->addItem(tr("Track %1").arg(i+1));tracks_->setCurrentIndex(info.first_track);static_cast<Wave*>(wave_)->pcm.clear();wave_->update();}
void GbsPlayer::setPaused(bool paused){pause_->setText(paused?tr("Play"):tr("Pause"));}
void GbsPlayer::setSamples(const QByteArray&pcm){if(pcm.isEmpty())return;static_cast<Wave*>(wave_)->pcm=pcm.right(8192);wave_->update();}

void GbsPlayer::selectTrack(int track){if(tracks_->count())tracks_->setCurrentIndex(qBound(0,track,tracks_->count()-1));}
