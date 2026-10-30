#pragma once
#include <QDialog>
#include <functional>
#include "emulator.h"
class QPlainTextEdit;class QLineEdit;class QLabel;class CpuLoadGraph;class QPushButton;
class DebugConsole final:public QDialog{
public:
 DebugConsole(const QFont&,std::function<void(QString)>send,std::function<void()>clear,std::function<void()>loadSymbols,QWidget*parent=nullptr);
 void append(const QVector<DebugLog>&);
 void clearOutput();
 void resetCpuLoad();
 void updateCpuLoad(const QVector<CpuLoadSample>&,bool idle);
 void setState(bool enabled,bool stopped,const QString&side={},double cpu=-1,bool paused=false);
private:
 QPlainTextEdit *output_,*side_;QLineEdit*input_;QLabel*status_;CpuLoadGraph*cpu_;QVector<QPushButton*>stoppedButtons_;QPushButton*interrupt_;
};
