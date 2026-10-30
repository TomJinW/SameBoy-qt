#pragma once
#include "palettes.h"
#include <QDialog>
class QListWidget;
class QPushButton;
class QCheckBox;
class QSlider;
class PaletteEditor final : public QDialog {
public:
    PaletteEditor(QVector<UserPalette> palettes,int selected,QWidget *parent=nullptr);
    QVector<UserPalette> palettes() const{return palettes_;}
    int selectedIndex() const;
private:
    QVector<UserPalette> palettes_;
    QListWidget *list_;
    std::array<QPushButton *,5> colors_;
    QCheckBox *manual_,*distinct_;
    QSlider *brightness_,*hue_,*strength_;
    bool updating_=false;
    void refresh();
    void changed();
    void refill(int selected);
    QString uniqueName(QString base) const;
};
