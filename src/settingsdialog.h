#pragma once
#include "settings.h"
#include <QDialog>

class SettingsDialog final : public QDialog {
public:
    SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr,int initialTab=0);
    AppSettings settings() const { return settings_; }
    bool restartRequested() const {return restartRequested_;}
private:
    AppSettings settings_;
    bool restartRequested_=false;
};
