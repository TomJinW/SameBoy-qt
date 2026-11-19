#pragma once
#include "settings.h"
#include <QDialog>

class SettingsDialog final : public QDialog {
public:
    SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr,int initialTab=0);
    AppSettings settings() const { return settings_; }
private:
    AppSettings settings_;
};
