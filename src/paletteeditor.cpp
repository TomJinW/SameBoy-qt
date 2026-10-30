#include "paletteeditor.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSlider>
#include <QVBoxLayout>
#include <QFile>
#include <QStylePainter>
#include <QStyleOptionSlider>

namespace {
class HueSlider final:public QSlider {
public:
    HueSlider():QSlider(Qt::Horizontal){}
protected:
    void paintEvent(QPaintEvent *event) override {
        if(!isEnabled()){QSlider::paintEvent(event);return;}
        QStylePainter painter(this);QStyleOptionSlider option;initStyleOption(&option);
        auto groove=style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderGroove,this);
        QRectF track(groove.left(),height()/2.0-2,groove.width(),4);QLinearGradient gradient(track.topLeft(),track.topRight());
        for(int i=0;i<=6;++i)gradient.setColorAt(i/6.0,QColor::fromHsvF((i%6)/6.0,1,1));
        painter.setPen(Qt::NoPen);painter.setBrush(gradient);painter.drawRoundedRect(track,2,2);
        option.subControls=QStyle::SC_SliderHandle;painter.drawComplexControl(QStyle::CC_Slider,option);
    }
};
}

PaletteEditor::PaletteEditor(QVector<UserPalette> palettes,int selected,QWidget *parent):QDialog(parent),palettes_(std::move(palettes))
{
    setWindowTitle(tr("Custom Palettes"));resize(780,440);auto *layout=new QHBoxLayout(this);auto *left=new QVBoxLayout;list_=new QListWidget;list_->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);left->addWidget(list_,1);
    auto *buttons=new QHBoxLayout;auto *add=new QPushButton("+");auto *remove=new QPushButton("−");auto *more=new QPushButton(tr("More…"));buttons->addWidget(add);buttons->addWidget(remove);buttons->addWidget(more);left->addLayout(buttons);layout->addLayout(left,1);
    auto *right=new QVBoxLayout;auto *wells=new QHBoxLayout;
    for(int i=0;i<5;++i){auto *button=new QPushButton;button->setMinimumSize(62,62);button->setToolTip(i==4?tr("Disabled LCD color"):tr("Shade %1").arg(i));colors_[i]=button;wells->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,i]{const int row=selectedIndex();if(row<0)return;auto color=QColorDialog::getColor(palettes_[row].colors[i],this,tr("Palette color"));if(color.isValid()){palettes_[row].colors[i]=color;changed();}});}
    right->addLayout(wells);distinct_=new QCheckBox(tr("Distinct disabled LCD color"));manual_=new QCheckBox(tr("Manual mode"));right->addWidget(distinct_);right->addWidget(manual_);
    auto *form=new QFormLayout;auto slider=[&](const QString &label,int max){auto *s=new QSlider(Qt::Horizontal);s->setRange(0,max);form->addRow(label,s);connect(s,&QSlider::valueChanged,this,&PaletteEditor::changed);return s;};
    brightness_=slider(tr("Brightness bias:"),256);hue_=new HueSlider;hue_->setRange(0,360);form->addRow(tr("Hue bias:"),hue_);connect(hue_,&QSlider::valueChanged,this,&PaletteEditor::changed);
    strength_=slider(tr("Hue bias strength:"),256);right->addLayout(form);right->addStretch();
    auto *close=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);right->addWidget(close);connect(close,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(close,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addLayout(right,3);
    connect(list_,&QListWidget::currentRowChanged,this,&PaletteEditor::refresh);
    connect(list_,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){const int row=list_->row(item);if(updating_||row<0)return;const auto text=item->text().trimmed();bool duplicate=false;for(int i=0;i<palettes_.size();++i)if(i!=row&&palettes_[i].name==text)duplicate=true;
        if(text.isEmpty()||duplicate){refill(row);return;}palettes_[row].name=text;});
    connect(manual_,&QCheckBox::toggled,this,&PaletteEditor::changed);connect(distinct_,&QCheckBox::toggled,this,&PaletteEditor::changed);
    connect(add,&QPushButton::clicked,this,[this]{UserPalette palette;palette.name=uniqueName(tr("Untitled Palette"));palette.recalculate();palettes_.append(palette);refill(palettes_.size()-1);});
    connect(remove,&QPushButton::clicked,this,[this]{const int row=selectedIndex();if(row<0)return;palettes_.removeAt(row);if(palettes_.isEmpty()){UserPalette p;p.name=tr("Untitled Palette");p.recalculate();palettes_.append(p);}refill(qMin(row,int(palettes_.size()-1)));});
    auto *menu=new QMenu(more);more->setMenu(menu);auto *restore=menu->addAction(tr("Restore Default Palettes"));menu->addSeparator();auto *import=menu->addAction(tr("Import Palette…"));auto *exportAction=menu->addAction(tr("Export Palette…"));
    connect(restore,&QAction::triggered,this,[this]{if(QMessageBox::question(this,tr("Restore Palettes"),tr("Replace the current palette library with the defaults?"))==QMessageBox::Yes){palettes_=defaultPalettes();refill(0);}});
    connect(import,&QAction::triggered,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("Import Palette"),{},tr("SameBoy palette (*.sbp)"));if(path.isEmpty())return;QFile file(path);UserPalette p;
        if(!file.open(QIODevice::ReadOnly)){QMessageBox::warning(this,tr("Palette"),tr("Invalid SameBoy palette file."));return;}const auto bytes=file.read(32);
        if(file.error()!=QFileDevice::NoError||!UserPalette::importSbp(bytes,uniqueName(QFileInfo(path).completeBaseName()),p)){QMessageBox::warning(this,tr("Palette"),tr("Invalid SameBoy palette file."));return;}palettes_.append(p);refill(palettes_.size()-1);});
    connect(exportAction,&QAction::triggered,this,[this]{const int row=selectedIndex();if(row<0)return;const auto path=QFileDialog::getSaveFileName(this,tr("Export Palette"),palettes_[row].name+".sbp",tr("SameBoy palette (*.sbp)"));if(path.isEmpty())return;QSaveFile file(path);const auto bytes=palettes_[row].exportSbp();
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())QMessageBox::warning(this,tr("Palette"),tr("Cannot export palette."));});
    if(palettes_.isEmpty()){UserPalette p;p.name=tr("Untitled Palette");p.recalculate();palettes_.append(p);}refill(qBound(0,selected,int(palettes_.size()-1)));
}
int PaletteEditor::selectedIndex() const{return list_->currentRow();}
QString PaletteEditor::uniqueName(QString base) const{QString name=base;int suffix=2;auto exists=[&]{for(const auto &p:palettes_)if(p.name==name)return true;return false;};while(exists())name=base+QString(" %1").arg(suffix++);return name;}
void PaletteEditor::refill(int selected){updating_=true;list_->clear();for(const auto &p:palettes_){auto *item=new QListWidgetItem(p.name,list_);item->setFlags(item->flags()|Qt::ItemIsEditable);}list_->setCurrentRow(selected);updating_=false;refresh();}
void PaletteEditor::refresh(){const int row=selectedIndex();if(row<0||updating_)return;updating_=true;const auto &p=palettes_[row];manual_->setChecked(p.manual);distinct_->setChecked(p.distinctLcd);brightness_->setValue(p.brightness);hue_->setValue(p.hue);strength_->setValue(p.strength);
    for(int i=0;i<5;++i){colors_[i]->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid palette(mid); border-radius: 28px; }").arg(p.colors[i].name()));colors_[i]->setEnabled(p.manual?(i<4||p.distinctLcd):(i==0||i==4));}
    brightness_->setEnabled(!p.manual);hue_->setEnabled(!p.manual);strength_->setEnabled(!p.manual);updating_=false;}
void PaletteEditor::changed(){const int row=selectedIndex();if(updating_||row<0)return;auto &p=palettes_[row];p.manual=manual_->isChecked();p.distinctLcd=distinct_->isChecked();p.brightness=brightness_->value();p.hue=hue_->value();p.strength=strength_->value();p.recalculate();refresh();}
