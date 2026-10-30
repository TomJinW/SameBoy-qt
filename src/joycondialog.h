#pragma once
#include "settings.h"
#include <QDialog>
#include <memory>
class Controllers;
class JoyConDialog final:public QDialog {
public:
    JoyConDialog(const AppSettings&,std::shared_ptr<Controllers>,QWidget*parent=nullptr);
    const AppSettings&settings()const{return settings_;}
private:
    AppSettings settings_;
};
