#include "../src/cheatsdialog.h"
#include "test_rom.h"
#include "../src/storagepaths.h"
#include <QFile>
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QCheckBox>
#include <QTimer>
#include <QComboBox>
#include <QScrollBar>
#include <QMessageBox>
#include <cstdio>

int main(int argc,char **argv){QApplication app(argc,argv);QTemporaryDir directory(QDir::currentPath()+"/cheats-smoke-XXXXXX");if(!directory.isValid())return 1;
    GB_gameboy_t *gb=GB_init(GB_alloc(),GB_MODEL_DMG_B);GB_debugger_set_disabled(gb,true);uint8_t rom[32768];make_test_rom(rom,false);GB_load_rom_from_buffer(gb,rom,sizeof(rom));
    const auto base=directory.filePath(QString::fromUtf8("测试作弊"));CoreExecutor execute=[gb](auto command){return command(gb);};
    auto expectWriteError=[&](auto action){bool warned=false;QTimer response;QObject::connect(&response,&QTimer::timeout,&app,[&]{for(auto*widget:app.topLevelWidgets())if(auto*box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()&&box->text().startsWith("Cannot save cheats:")){warned=true;box->accept();}});response.start(10);action();return warned;};
    auto hasAddress=[gb](uint16_t address){QVector<GB_cheat_search_result_t> results(GB_cheat_search_result_count(gb));if(results.isEmpty())return false;GB_cheat_search_get_results(gb,results.data());for(const auto &r:results)if(r.addr==address)return true;return false;};
    GB_write_memory(gb,0xc000,0x34);GB_write_memory(gb,0xc001,0x12);
    if(!GB_cheat_search_filter(gb,"new == $34",GB_CHEAT_SEARCH_DATA_TYPE_8BIT)||!hasAddress(0xc000))return 1;
    GB_write_memory(gb,0xc000,0x35);
    if(!GB_cheat_search_filter(gb,"new > old",GB_CHEAT_SEARCH_DATA_TYPE_8BIT)||!hasAddress(0xc000))return 1;
    GB_cheat_search_reset(gb);if(!GB_cheat_search_filter(gb,"new == $1235",GB_CHEAT_SEARCH_DATA_TYPE_16BIT)||!hasAddress(0xc000))return 1;
    GB_cheat_search_reset(gb);if(!GB_cheat_search_filter(gb,"new == $3512",GB_CHEAT_SEARCH_DATA_TYPE_16BIT_BE)||!hasAddress(0xc000))return 1;
    {
        CheatsDialog dialog(execute,base);dialog.setAttribute(Qt::WA_DontShowOnScreen);dialog.show();app.processEvents();
        auto button=[&](const QString &name){for(auto *b:dialog.findChildren<QPushButton *>())if(b->text()==name)return b;return static_cast<QPushButton *>(nullptr);};
        if(!button("Add Cheat…")||!button("Apply"))return 1;button("Add Cheat…")->click();
        auto *table=dialog.findChild<QTableWidget *>();if(table->rowCount()!=1)return 1;
        dialog.findChild<QLineEdit *>("cheatDescription")->setText("Health");dialog.findChild<QLineEdit *>("cheatAddress")->setText("$00:$C000");dialog.findChild<QLineEdit *>("cheatValue")->setText("$77");button("Apply")->click();
        auto entries=cheatEntries(gb);if(entries.size()!=1||entries[0].description!="Health"||entries[0].address!=0xc000||entries[0].value!=0x77)return 1;
        // Cocoa updateCheat/debugger literals: '$' (or 0x) marks hex, a plain number is decimal.
        for(const auto&[text,expected]:std::initializer_list<std::pair<const char*,int>>{{"10",10},{"0x2A",42},{"$77",0x77}}){
            dialog.findChild<QLineEdit *>("cheatValue")->setText(text);button("Apply")->click();
            if(cheatEntries(gb).first().value!=expected){std::fprintf(stderr,"Cheat value \"%s\" was stored as %d, Cocoa stores %d\n",text,cheatEntries(gb).first().value,expected);return 1;}}
        table->item(0,0)->setCheckState(Qt::Unchecked);if(cheatEntries(gb).first().enabled)return 1;
        const auto stored=cheatSavePath(base),backup=stored+".backup";if(!QFile::rename(stored,backup)||!QDir().mkdir(stored))return 1;
        const bool warned=expectWriteError([&]{table->item(0,0)->setCheckState(Qt::Checked);});
        const bool unchanged=!cheatEntries(gb).first().enabled&&table->item(0,0)->checkState()==Qt::Unchecked;
        if(!QDir().rmdir(stored)||!QFile::rename(backup,stored))return 1;
        if(!warned||!unchanged){std::fputs("Failed cheat toggle left the UI inconsistent with the core\n",stderr);return 1;}

        GB_remove_all_cheats(gb);if(!restoreCheats(gb,base).isEmpty()||cheatEntries(gb).size()!=1||cheatEntries(gb).first().enabled)return 1;
        if(argc>1&&!dialog.grab().save(QString::fromLocal8Bit(argv[1])+"/cheats.png"))return 1;
    }
    {
        int reads=0;CoreExecutor tracked=[&](auto command){++reads;return execute(std::move(command));};
        bool added=false;CheatSearchDialog dialog(tracked,base,[&]{added=true;});dialog.setAttribute(Qt::WA_DontShowOnScreen);dialog.show();app.processEvents();
        auto *operand=dialog.findChild<QLineEdit *>("searchOperand");operand->setText("$35");
        QPushButton *search=nullptr,*add=nullptr;for(auto *b:dialog.findChildren<QPushButton *>()){if(b->text()=="Search")search=b;if(b->text()=="Add Cheat")add=b;}
        if(!search||!add)return 1;search->click();auto *table=dialog.findChild<QTableWidget *>();int found=-1;
        for(int row=0;row<table->rowCount();++row)if(table->item(row,0)->text().endsWith("$C000"))found=row;
        if(found<0)return 1;table->selectRow(found);
        const auto stored=cheatSavePath(base),backup=stored+".backup";if(!QFile::rename(stored,backup)||!QDir().mkdir(stored))return 1;
        const auto beforeCount=cheatEntries(gb).size();const bool warned=expectWriteError([&]{add->click();});
        const bool unchanged=cheatEntries(gb).size()==beforeCount&&!added;
        if(!QDir().rmdir(stored)||!QFile::rename(backup,stored))return 1;
        if(!warned||!unchanged){std::fputs("Failed cheat save still modified the core or reported success\n",stderr);return 1;}
        add->click();if(!added||cheatEntries(gb).size()!=2)return 1;
        // Cocoa GBCheatSearchController: editing the current value writes memory, evaluated as a debugger expression.
        {const auto*wram=static_cast<const uint8_t*>(GB_get_direct_access(gb,GB_DIRECT_ACCESS_RAM,nullptr,nullptr));
         table->item(found,2)->setText("$40+2");app.processEvents();
         if(wram[0]!=0x42||table->item(found,2)->text()!="$42"){std::fprintf(stderr,"Editing a search result did not write memory: $%02X, shown %s\n",wram[0],qPrintable(table->item(found,2)->text()));return 1;}
         QString warning;QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,&app,[&]{for(auto*widget:app.topLevelWidgets())if(auto*box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()){warning=box->text();box->accept();}});dismiss.start(10);
         table->item(found,2)->setText("not an expression");dismiss.stop();const bool rejected=warning.contains("Invalid value or expression");
         if(!rejected||wram[0]!=0x42){std::fputs("An invalid search value was not rejected\n",stderr);return 1;}}
        if(argc>1&&!dialog.grab().save(QString::fromLocal8Bit(argv[1])+"/cheat-search.png"))return 1;
        dialog.hide();const int before=reads;for(auto*timer:dialog.findChildren<QTimer*>())QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection);
        if(reads!=before){std::fputs("Hidden cheat search still reads the core\n",stderr);return 1;}
        dialog.show();for(auto*b:dialog.findChildren<QPushButton*>())if(b->text()=="Reset")b->click();
        for(auto*box:dialog.findChildren<QComboBox*>())if(box->findText("Any")>=0)box->setCurrentIndex(box->findText("Any"));
        search->click();app.processEvents();if(table->rowCount()!=10000)return 1;
        size_t size=0;auto*ram=static_cast<uint8_t*>(GB_get_direct_access(gb,GB_DIRECT_ACCESS_CART_RAM,&size,nullptr));std::memset(ram,0x33,size);
        for(auto kind:{GB_DIRECT_ACCESS_RAM,GB_DIRECT_ACCESS_HRAM}){auto*data=static_cast<uint8_t*>(GB_get_direct_access(gb,kind,&size,nullptr));std::memset(data,0x33,size);}
        int changed=0;QObject::connect(table,&QTableWidget::itemChanged,&dialog,[&](QTableWidgetItem*item){if(item->column()==2)++changed;});
        for(auto*timer:dialog.findChildren<QTimer*>())QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection);
        if(changed<1||changed>64){std::fprintf(stderr,"Cheat refresh changed %d rows for one viewport\n",changed);return 1;}
        const int visibleUpdated=changed;table->verticalScrollBar()->setValue(100);app.processEvents();
        const auto current=table->rowAt(0);if(current<0||table->item(current,2)->text()!="$33")return 1;
        std::printf("Cheat search: 10000 rows, %d cells updated per visible refresh; hidden core requests=0\n",visibleUpdated);

    }
    const auto native=cheatSavePath(base);QFile nativeFile(native);if(!nativeFile.open(QIODevice::ReadOnly))return 1;const auto bytes=nativeFile.readAll();nativeFile.close();
    const auto coreFile=directory.filePath("core.cht");if(GB_save_cheats(gb,coreFile.toUtf8().constData()))return 1;QFile original(coreFile);if(!original.open(QIODevice::ReadOnly)||original.readAll()!=bytes)return 1;original.close();
    GB_remove_all_cheats(gb);if(GB_load_cheats(gb,coreFile.toUtf8().constData(),true)||cheatEntries(gb).size()!=2)return 1;
    GB_remove_all_cheats(gb);if(!storeCheats(gb,base).isEmpty()||!restoreCheats(gb,base).isEmpty()||!cheatEntries(gb).isEmpty())return 1;
    if(QFile::exists(base+".cheats.json"))return 1;
    if(cheatSavePath(directory.filePath("game.gbc"))!=directory.filePath("game.cht")||cheatSavePath(directory.filePath("game.gb"))!=directory.filePath("game.cht"))return 1;
    if(cheatSavePath(directory.filePath("game.gbcart/cartridge"))!=directory.filePath("game.gbcart/cheats.cht"))return 1;
    GB_add_cheat(gb,"Keep",0xc000,0,7,0,false,true);QFile broken(native);if(!broken.open(QIODevice::WriteOnly)||broken.write("broken")!=6)return 1;broken.close();
    if(restoreCheats(gb,base).isEmpty()||cheatEntries(gb).size()!=1)return 1;
    GB_free(gb);GB_dealloc(gb);std::puts("Cheat editing/toggling/persistence, custom expressions, 8/16-bit endian search and adding search results passed.");return 0;
}
