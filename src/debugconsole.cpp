#include "debugconsole.h"
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextCursor>
#include <QScrollBar>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QUrl>
namespace {
class CommandLine:public QLineEdit{
 QStringList history_;int cursor_=0;std::function<void(QString)>send_;
public:CommandLine(std::function<void(QString)>send):send_(std::move(send)){
 connect(this,&QLineEdit::returnPressed,this,[this]{auto command=text();if(command.isEmpty()&&!history_.isEmpty())command=history_.last();if(command.isEmpty())return;if(history_.isEmpty()||history_.last()!=command)history_.append(command);if(history_.size()>200)history_.removeFirst();cursor_=history_.size();clear();send_(command);});}
protected:void keyPressEvent(QKeyEvent*event)override{
 if(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down){cursor_=qBound(0,cursor_+(event->key()==Qt::Key_Up?-1:1),history_.size());setText(cursor_<history_.size()?history_[cursor_]:QString());return;}
 if(event->key()==Qt::Key_C&&(event->modifiers()&Qt::ControlModifier)&&!hasSelectedText()){send_("interrupt");return;}QLineEdit::keyPressEvent(event);
}
};
}
class CpuLoadGraph final:public QWidget{
 QVector<double>samples_;double usage_=0;quint64 count_=0;
public:CpuLoadGraph(){setMinimumHeight(100);setObjectName("debugCpuLoad");}
 void clear(){samples_.clear();usage_=0;count_=0;setProperty("sampleCount",0);setProperty("secondUsage",0);update();}
 void add(double sample,double second){samples_.append(std::clamp(sample,0.0,1.0));if(samples_.size()>256)samples_.removeFirst();usage_=std::clamp(second,0.0,1.0);setProperty("sampleCount",double(++count_));setProperty("secondUsage",usage_);update();}
protected:void paintEvent(QPaintEvent*)override{
  QPainter p(this);p.fillRect(rect(),palette().brush(QPalette::Window));auto bold=font();bold.setBold(true);p.setFont(bold);p.setPen(palette().color(QPalette::WindowText));p.drawText(QRect(6,2,width()-12,20),Qt::AlignLeft|Qt::AlignVCenter,tr("CPU Load"));
  const QRectF plot(0,24,width(),qMax(1,height()-48));const QColor green(48,190,80),red(240,65,55);p.setRenderHint(QPainter::Antialiasing);QPointF previous(0,plot.bottom());
  for(int i=0;i<256;++i){const int index=i-(256-samples_.size());const double value=index<0?0:samples_[index];QPointF point(plot.left()+plot.width()*i/255,plot.bottom()-value*plot.height());if(i){auto color=value>=1.0?red:green;QPainterPath area;area.moveTo(previous.x(),plot.bottom());area.lineTo(previous);area.lineTo(point);area.lineTo(point.x(),plot.bottom());area.closeSubpath();auto fill=color;fill.setAlpha(85);p.fillPath(area,fill);p.setPen(QPen(color,1));p.drawLine(previous,point);}previous=point;}
  p.setPen(palette().color(QPalette::WindowText));p.drawText(QRect(6,height()-24,width()-12,22),Qt::AlignLeft|Qt::AlignVCenter,QString::number(usage_*100,'f',2)+"%");
 }
};
DebugConsole::DebugConsole(const QFont&font,std::function<void(QString)>send,std::function<void()>clear,std::function<void()>symbols,QWidget*parent):QDialog(parent,Qt::Window){
 setWindowTitle(tr("Debug Console"));setAttribute(Qt::WA_DeleteOnClose);resize(1040,680);auto*layout=new QVBoxLayout(this);auto*split=new QSplitter;layout->addWidget(split,1);
 output_=new QPlainTextEdit;output_->setObjectName("debugOutput");output_->setReadOnly(true);output_->setLineWrapMode(QPlainTextEdit::NoWrap);output_->setFont(font);output_->document()->setMaximumBlockCount(6000);split->addWidget(output_);
 auto*right=new QWidget;auto*rightLayout=new QVBoxLayout(right);rightLayout->setContentsMargins(0,0,0,0);rightLayout->addWidget(new QLabel(tr("Registers and backtrace")));side_=new QPlainTextEdit;side_->setReadOnly(true);side_->setFont(font);side_->setObjectName("debugSide");rightLayout->addWidget(side_,1);cpu_=new CpuLoadGraph;rightLayout->addWidget(cpu_);split->addWidget(right);split->setSizes({680,360});
 auto*controls=new QHBoxLayout;for(const auto&entry:QVector<QPair<QString,QString>>{{tr("Continue"),"continue"},{tr("Step Into"),"step"},{tr("Step Over"),"next"},{tr("Step Out"),"finish"},{tr("Backstep"),"backstep"}}){auto*button=new QPushButton(entry.first);button->setObjectName("debug_"+entry.second);controls->addWidget(button);stoppedButtons_.append(button);connect(button,&QPushButton::clicked,this,[send,command=entry.second]{send(command);});}
 interrupt_=new QPushButton(tr("Break"));controls->addWidget(interrupt_);connect(interrupt_,&QPushButton::clicked,this,[send]{send("interrupt");});auto*clearButton=new QPushButton(tr("Clear Console"));controls->addWidget(clearButton);connect(clearButton,&QPushButton::clicked,this,std::move(clear));layout->addLayout(controls);
 auto*row=new QHBoxLayout;row->addWidget(new QLabel(">"));input_=new CommandLine(send);input_->setFont(font);input_->setObjectName("debugCommand");input_->setPlaceholderText(tr("Debugger command (help lists commands)"));row->addWidget(input_,1);auto*load=new QPushButton(tr("Load Symbols…"));row->addWidget(load);connect(load,&QPushButton::clicked,this,std::move(symbols));auto*help=new QPushButton(tr("Help"));row->addWidget(help);connect(help,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://sameboy.github.io/debugger/"));});layout->addLayout(row);status_=new QLabel;layout->addWidget(status_);setState(false,false);
}
void DebugConsole::append(const QVector<DebugLog>&entries){auto*bar=output_->verticalScrollBar();const bool bottom=bar->value()>=bar->maximum()-2;QTextCursor cursor(output_->document());cursor.movePosition(QTextCursor::End);
 for(const auto&entry:entries){QTextCharFormat format;if(entry.attributes&GB_LOG_BOLD)format.setFontWeight(QFont::Bold);if(entry.attributes&GB_LOG_UNDERLINE_MASK)format.setFontUnderline(true);if(entry.attributes&GB_LOG_ERROR)format.setForeground(QColor(220,80,80));cursor.insertText(entry.text,format);}if(bottom)bar->setValue(bar->maximum());
}
void DebugConsole::clearOutput(){output_->clear();side_->clear();}
void DebugConsole::setState(bool enabled,bool stopped,const QString&side,double cpu,bool paused){input_->setEnabled(enabled);interrupt_->setEnabled(enabled&&!stopped);for(auto*button:stoppedButtons_)button->setEnabled(enabled&&stopped);status_->setText(!enabled?tr("Enable Developer Mode and open a ROM to send commands."):stopped?tr("Debugger stopped"):paused?tr("Paused"):tr("Running — Ctrl+C breaks execution"));if(!side.isNull()&&side_->toPlainText()!=side)side_->setPlainText(side);(void)cpu;}
void DebugConsole::updateCpuLoad(const QVector<CpuLoadSample>&samples,bool idle){for(const auto&sample:samples)cpu_->add(sample.frame,sample.second);if(idle&&samples.isEmpty())cpu_->add(0,0);}

void DebugConsole::resetCpuLoad(){cpu_->clear();}
