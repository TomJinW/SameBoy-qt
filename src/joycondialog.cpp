#include "joycondialog.h"
#include "controllers.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QMap>
#include <QCryptographicHash>
JoyConDialog::JoyConDialog(const AppSettings&s,std::shared_ptr<Controllers>pads,QWidget*parent):QDialog(parent),settings_(s){
    setWindowTitle(tr("Arrange Joy-Cons"));resize(850,440);setMinimumSize(620,320);
    auto *layout=new QVBoxLayout(this);auto*instructions=new QLabel(tr("Pair each Joy-Con in your system Bluetooth settings first. Hold L on the left and R on the right to combine them here, or use the Combine buttons. Single Joy-Cons can be held horizontally or vertically."));
    instructions->setWordWrap(true);layout->addWidget(instructions);
    auto*table=new QTableWidget(0,4);table->setObjectName("joyConDevices");table->setHorizontalHeaderLabels({tr("Controller"),tr("Grip"),tr("Partner"),tr("Action")});
    table->verticalHeader()->hide();table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(table,1);
    auto*empty=new QLabel(tr("No Joy-Cons connected. They will appear here after Bluetooth pairing."));empty->setWordWrap(true);layout->addWidget(empty);
    auto*autoPair=new QCheckBox(tr("Automatically combine available left and right Joy-Cons"));autoPair->setObjectName("joyConAutoPair");autoPair->setChecked(settings_.joyConAutoPair);layout->addWidget(autoPair);
    auto*horizontal=new QCheckBox(tr("Use horizontal grip by default for single Joy-Cons"));horizontal->setObjectName("joyConHorizontal");horizontal->setChecked(settings_.joyConHorizontal);layout->addWidget(horizontal);
    auto*note=new QLabel(tr("Combined Joy-Cons appear as one controller in Controls. Separate Joy-Cons can be assigned to different players. Changes apply when Preferences is saved."));note->setWordWrap(true);layout->addWidget(note);
    auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    auto signature=std::make_shared<QString>();auto previous=std::make_shared<QMap<QString,quint32>>();
    auto refresh=std::make_shared<std::function<void()>>();
    *refresh=[this,pads,table,empty,signature,previous]{
        pads->poll();autoPairJoyCons(pads->devices(),settings_);
        // Cocoa's L + R arrangement gesture, using edges so Separate never immediately rejoins held buttons.
        for(const auto&l:pads->devices())if(l.joyCon==JoyConType::Left&&(l.buttons&(1u<<9)))
            for(const auto&r:pads->devices())if(r.joyCon==JoyConType::Right&&(r.buttons&(1u<<10))&&
                (!(previous->value(l.id)&(1u<<9))||!(previous->value(r.id)&(1u<<10))))pairJoyCons(settings_,l,r);
        previous->clear();for(const auto&p:pads->devices())previous->insert(p.id,p.buttons);
        const auto logical=pads->logicalDevices(settings_);QVector<PadSnapshot> joycons;
        QString current;
        for(const auto&p:logical)if(p.joyCon!=JoyConType::None){joycons.append(p);current+=p.id+p.name+QString::number(settings_.joyConGrips.value(p.id,-1))+QString::number(p.horizontal);}
        empty->setVisible(joycons.isEmpty());
        if(current!=*signature){*signature=current;table->setRowCount(joycons.size());table->clearContents();
            auto label=[](const PadSnapshot&p){return p.joyCon==JoyConType::Left?tr("Joy-Con (L)"):p.joyCon==JoyConType::Right?tr("Joy-Con (R)"):p.name;};
            auto fingerprint=[](const QString&id){return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex().left(8));};
            for(int row=0;row<joycons.size();++row){const auto p=joycons[row];auto*name=new QTableWidgetItem(label(p)+"\n"+(p.sources.size()==2?tr("Combined controller"):fingerprint(p.id)));name->setToolTip(p.id);name->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);table->setItem(row,0,name);
                if(p.joyCon==JoyConType::Pair){table->setItem(row,1,new QTableWidgetItem(tr("Combined")));auto*split=new QPushButton(tr("Separate Joy-Cons"));table->setCellWidget(row,3,split);split->setEnabled(p.sources.size()==2);
                    connect(split,&QPushButton::clicked,this,[this,p]{separateJoyCons(settings_,p.id);});continue;
                }
                auto*grip=new QComboBox;grip->addItem(tr("Default"),-1);grip->addItem(tr("Vertical"),0);grip->addItem(tr("Horizontal"),1);grip->setCurrentIndex(grip->findData(settings_.joyConGrips.value(p.id,-1)));table->setCellWidget(row,1,grip);
                connect(grip,&QComboBox::currentIndexChanged,this,[this,p,grip]{const int value=grip->currentData().toInt();if(value<0)settings_.joyConGrips.remove(p.id);else settings_.joyConGrips[p.id]=value;});
                auto*partner=new QComboBox;for(const auto&candidate:joycons)if((p.joyCon==JoyConType::Left&&candidate.joyCon==JoyConType::Right)||(p.joyCon==JoyConType::Right&&candidate.joyCon==JoyConType::Left))partner->addItem(label(candidate)+" · "+fingerprint(candidate.id),candidate.id);
                table->setCellWidget(row,2,partner);auto*combine=new QPushButton(tr("Combine"));combine->setEnabled(partner->count());table->setCellWidget(row,3,combine);
                connect(combine,&QPushButton::clicked,this,[this,p,partner,pads]{for(const auto&r:pads->devices())if(r.id==partner->currentData().toString()){pairJoyCons(settings_,p,r);break;}});
            }
            table->resizeRowsToContents();
        }
        for(int row=0;row<joycons.size();++row)if(auto*item=table->item(row,0)){const QBrush brush=joycons[row].buttons?palette().highlight():QBrush();if(item->background()!=brush)item->setBackground(brush);}
    };
    connect(autoPair,&QCheckBox::toggled,this,[this](bool enabled){settings_.joyConAutoPair=enabled;if(enabled)settings_.joyConSingles.clear();});
    connect(horizontal,&QCheckBox::toggled,this,[this](bool enabled){settings_.joyConHorizontal=enabled;});
    auto*timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[refresh]{(*refresh)();});timer->start(50);(*refresh)();
}
