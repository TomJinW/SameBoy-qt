#include "../src/emulator.h"
#include "../src/printerwindow.h"
#include "test_rom.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QPrinter>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <cstdio>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Printer check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
extern "C" bool test_printer_packet(GB_gameboy_t*);
extern "C" bool test_printer_clean(GB_gameboy_t*);
int main(int argc,char**argv){
    QApplication app(argc,argv);QTemporaryDir temp(QDir::currentPath()+"/printer-XXXXXX");CHECK(temp.isValid());
    QByteArray rom(32768,0);make_test_rom(reinterpret_cast<uint8_t*>(rom.data()),false);
    QFile boot(BOOT_ROM_DIR "/dmg_boot.bin");CHECK(boot.open(QIODevice::ReadOnly));
    Emulator worker(rom,boot.readAll(),temp.filePath("battery.sav"),false);worker.setPrinterConnected(true);worker.setPaused(true);worker.start();QThread::msleep(40);
    CHECK(worker.execute([](GB_gameboy_t*gb){return GB_get_built_in_accessory(gb)==GB_ACCESSORY_PRINTER?QString():QString("Printer not attached");}).isEmpty());
    CHECK(worker.execute([](GB_gameboy_t*gb){
        return test_printer_packet(gb)?QString():QString("Printer packet/completion failed");
    }).isEmpty());
    auto output=worker.takePrinterOutput();CHECK(output.size()==2);CHECK(!output[0].done&&output[1].done);
    auto strip=output[0].image;CHECK(strip.size()==QSize(160,21));CHECK(strip.pixelColor(0,0)==Qt::white&&strip.pixelColor(0,20)==Qt::white);
    CHECK(strip.pixelColor(0,2)==Qt::black);CHECK(strip.pixelColor(1,2)==QColor(85,85,85));CHECK(strip.pixelColor(2,2)==QColor(170,170,170));CHECK(strip.pixelColor(3,2)==Qt::white);
    PrinterWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);CHECK(window.append(strip));CHECK(window.isVisible());app.processEvents();
    window.move(120,140);window.resize(480,400);app.processEvents();const auto geometry=window.geometry();
    CHECK(window.append(strip));app.processEvents();CHECK(window.geometry()==geometry);CHECK(window.feed().height()==42);window.setPrinting(false);
    const auto png=temp.filePath(QString::fromUtf8("打印纸带.png"));CHECK(window.saveFeed(png));CHECK(QImage(png)==window.feed());
    // Exercise both palette transitions while a printed roll is displayed.
    for(bool dark:{false,true}){QPalette palette=window.palette();palette.setColor(QPalette::Window,dark?QColor(32,32,32):QColor(245,245,245));palette.setColor(QPalette::WindowText,dark?Qt::white:Qt::black);palette.setColor(QPalette::Button,dark?QColor(55,55,55):QColor(235,235,235));palette.setColor(QPalette::ButtonText,dark?Qt::white:Qt::black);palette.setColor(QPalette::Base,dark?QColor(40,40,40):Qt::white);palette.setColor(QPalette::Text,dark?Qt::white:Qt::black);window.setPalette(palette);app.processEvents();
      for(auto*label:window.findChildren<QLabel*>())CHECK(label->palette().color(QPalette::WindowText)==palette.color(QPalette::WindowText));
      for(auto*button:window.findChildren<QPushButton*>())CHECK(button->palette().color(QPalette::ButtonText)==palette.color(QPalette::ButtonText));
      CHECK(window.findChild<QScrollArea*>()->viewport()->palette().color(QPalette::Base)==palette.color(QPalette::Base));CHECK(window.feed()==QImage(png));
      if(argc>1)CHECK(window.grab().save(QString::fromLocal8Bit(argv[1])+(dark?"/printer-dark.png":"/printer-light.png")));
    }
    if(argc>1)CHECK(window.grab().save(QString::fromLocal8Bit(argv[1])+"/printer.png"));
    QImage longStrip(160,900,QImage::Format_RGB32);longStrip.fill(Qt::gray);CHECK(window.append(longStrip));app.processEvents();CHECK(window.geometry()==geometry);
    QPrinter printer;printer.setOutputFormat(QPrinter::PdfFormat);const auto pdf=temp.filePath("printer.pdf");printer.setOutputFileName(pdf);CHECK(window.printTo(printer));QFile file(pdf);CHECK(file.open(QIODevice::ReadOnly));auto pdfData=file.readAll();CHECK(pdfData.startsWith("%PDF")&&pdfData.count("/Type /Page")>2);file.close();
    window.hide();CHECK(window.append(strip));CHECK(window.feed().height()==21);window.clearFeed();CHECK(window.feed().isNull());
    worker.setPrinterConnected(false);QThread::msleep(40);CHECK(worker.execute([](GB_gameboy_t*gb){return GB_get_built_in_accessory(gb)==GB_ACCESSORY_NONE?QString():QString("Disconnect failed");}).isEmpty());
    worker.setPrinterConnected(true);QThread::msleep(40);CHECK(worker.execute([](GB_gameboy_t*gb){return test_printer_clean(gb)?QString():QString("Reconnect failed");}).isEmpty());
    worker.requestInterruption();CHECK(worker.wait(2000));
    std::puts("Printer protocol, margins, four grey levels, completion, roll accumulation, native PNG, paginated PDF, clear, disconnect/reconnect passed.");
}
