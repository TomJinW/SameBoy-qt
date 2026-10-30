#include "boundedfile.h"
#include "cheatsdialog.h"
#include "coreallocation.h"
#include "storagepaths.h"
#include <QtEndian>
#include <cstring>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString hex(unsigned value, int digits) { return "$" + QString::number(value,16).rightJustified(digits,'0').toUpper(); }
bool parseHex(QString text, unsigned max, unsigned &result) {
    text = text.trimmed(); text.remove('$'); if (text.startsWith("0x",Qt::CaseInsensitive)) text.remove(0,2);
    bool ok; result = text.toUInt(&ok,16); return ok && result <= max;
}
QByteArray cheatDescription(const QString&text){
    auto bytes=text.toUtf8();bytes.truncate(127);
    while(!bytes.isEmpty()&&QString::fromUtf8(bytes).toUtf8()!=bytes)bytes.chop(1);
    return bytes;
}
void setEntries(GB_gameboy_t *gb, const QVector<CheatEntry> &entries) {
    GB_remove_all_cheats(gb);
    for (const auto &entry : entries) GB_add_cheat(gb,cheatDescription(entry.description).constData(),entry.address,entry.bank,
        entry.value,entry.oldValue,entry.conditional,entry.enabled);
}
// Same format as GB_save_cheats: native little-endian header followed by GB_cheat_t records.
QByteArray serialize(const QVector<CheatEntry>&entries){
    static_assert(sizeof(GB_cheat_t)==136&&offsetof(GB_cheat_t,description)==8);
    QByteArray bytes(8+entries.size()*136,0);auto*out=reinterpret_cast<uchar*>(bytes.data());
    qToLittleEndian<quint32>(0x53424368,out);qToLittleEndian<quint32>(136,out+4);
    for(int i=0;i<entries.size();++i){auto*r=out+8+i*136;const auto&e=entries[i];
        qToLittleEndian<quint16>(e.address,r);qToLittleEndian<quint16>(e.bank,r+2);r[4]=e.value;r[5]=e.oldValue;r[6]=e.conditional;r[7]=e.enabled;
        const auto description=cheatDescription(e.description);
        std::memcpy(r+8,description.constData(),description.size());
    }return bytes;
}
QString decodeCht(const QByteArray&bytes,QVector<CheatEntry>&entries){
    if(bytes.size()<8)return QObject::tr("Invalid SameBoy cheat database.");
    const auto*data=reinterpret_cast<const uchar*>(bytes.constData());
    const bool little=qFromLittleEndian<quint32>(data)==0x53424368;
    if(!little&&qFromBigEndian<quint32>(data)!=0x53424368)return QObject::tr("Not a SameBoy cheat database.");
    const auto recordSize=little?qFromLittleEndian<quint32>(data+4):qFromBigEndian<quint32>(data+4);
    if(recordSize!=136||(bytes.size()-8)%136)return QObject::tr("Incompatible or truncated SameBoy cheat database.");
    for(qsizetype offset=8;offset<bytes.size();offset+=136){const auto*r=data+offset;
        if(r[6]>1||r[7]>1)return QObject::tr("Invalid cheat flags.");
        QByteArray description(reinterpret_cast<const char*>(r+8),128);const int end=description.indexOf('\0');if(end>=0)description.truncate(end);
        entries.append({QString::fromUtf8(description),little?qFromLittleEndian<quint16>(r):qFromBigEndian<quint16>(r),little?qFromLittleEndian<quint16>(r+2):qFromBigEndian<quint16>(r+2),r[4],r[5],bool(r[6]),bool(r[7])});
    }return {};
}
QString persistEntries(const QString &base, const QVector<CheatEntry> &entries) {
    const auto bytes = serialize(entries); QSaveFile file(cheatSavePath(base));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return QObject::tr("Cannot save cheats: %1").arg(file.errorString());
    return {};
}
void warn(QWidget *parent, const QString &error) { if (!error.isEmpty()) QMessageBox::warning(parent,QObject::tr("Cheats"),error); }
}
QVector<CheatEntry> cheatEntries(GB_gameboy_t *gb) {
    size_t count; const auto cheats = GB_get_cheats(gb,&count); QVector<CheatEntry> result;
    for (size_t i = 0; i < count; ++i) { const auto &c = *cheats[i]; result.append({QString::fromUtf8(c.description),c.address,c.bank,c.value,c.old_value,c.use_old_value,c.enabled}); }
    return result;
}
QString storeCheats(GB_gameboy_t *gb, const QString &base) { return persistEntries(base,cheatEntries(gb)); }
QString restoreCheats(GB_gameboy_t *gb, const QString &base) {
    QFile native(cheatSavePath(base));
    if(native.exists()){
        QByteArray bytes;if(!readBoundedFile(native,bytes,16*1024*1024))return QObject::tr("Cannot read SameBoy cheat database.");
        QVector<CheatEntry> entries;const auto error=decodeCht(bytes,entries);if(!error.isEmpty())return error;
        setEntries(gb,entries);return {};
    }
    return {};
}

CheatsDialog::CheatsDialog(CoreExecutor execute, QString base, QWidget *parent)
    : QDialog(parent,Qt::Window), execute_(std::move(execute)), base_(std::move(base))
{
    setWindowTitle(tr("Cheats")); resize(900,600); setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    table_ = new QTableWidget(0,3); table_->setHorizontalHeaderLabels({tr("Enabled"),tr("Description"),tr("Action")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch); table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    table_->verticalHeader()->hide(); layout->addWidget(table_,1);
    auto *buttons = new QHBoxLayout; auto *add = new QPushButton(tr("Add Cheat…")); auto *remove = new QPushButton(tr("Remove"));
    buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch(); layout->addLayout(buttons);
    auto *editors = new QHBoxLayout;
    auto *manual = new QGroupBox(tr("Edit Cheat")); auto *form = new QFormLayout(manual);
    description_ = new QLineEdit; address_ = new QLineEdit; value_ = new QLineEdit; old_ = new QLineEdit;
    description_->setObjectName("cheatDescription");address_->setObjectName("cheatAddress");value_->setObjectName("cheatValue");old_->setObjectName("cheatOldValue");
    conditional_ = new QCheckBox(tr("Only if old value was:"));
    form->addRow(tr("Description:"),description_); form->addRow(tr("Change byte at address:"),address_);
    form->addRow(tr("To value:"),value_); form->addRow(conditional_,old_);
    auto *apply = new QPushButton(tr("Apply")); form->addRow(apply);
    auto *import = new QGroupBox(tr("Import GameShark or Game Genie cheat")); auto *importForm = new QFormLayout(import);
    code_ = new QLineEdit; code_->setPlaceholderText(tr("Code")); importDescription_ = new QLineEdit; importDescription_->setPlaceholderText(tr("Description"));
    auto *importButton = new QPushButton(tr("Import")); importForm->addRow(code_); importForm->addRow(importDescription_); importForm->addRow(importButton);
    editors->addWidget(manual,1); editors->addWidget(import,1); layout->addLayout(editors);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(close); connect(close,&QDialogButtonBox::rejected,this,&QDialog::close);
    connect(add,&QPushButton::clicked,this,[this] { auto entries=entries_; entries.append({tr("New Cheat"),0,0,0,0,false,true}); commit(entries); table_->selectRow(entries_.size()-1); });
    connect(remove,&QPushButton::clicked,this,[this] { const int row=table_->currentRow(); if(row<0)return; auto entries=entries_; entries.removeAt(row); commit(entries); });
    connect(table_,&QTableWidget::itemSelectionChanged,this,&CheatsDialog::selected);
    connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem *item) {
        if(item->row()>=entries_.size())return; auto entries=entries_;
        if(item->column()==0) entries[item->row()].enabled=item->checkState()==Qt::Checked;
        else if(item->column()==1) entries[item->row()].description=item->text(); else return;
        commit(entries);
    });
    connect(apply,&QPushButton::clicked,this,&CheatsDialog::applyEditor);
    connect(conditional_,&QCheckBox::toggled,old_,&QWidget::setEnabled);
    connect(importButton,&QPushButton::clicked,this,[this] {
        GB_gameboy_t *validator=allocateGameBoy(GB_MODEL_DMG_B);
        if(!validator){warn(this,tr("Could not allocate a temporary emulator core."));return;}
        const auto *entry=GB_import_cheat(validator,code_->text().trimmed().toUtf8().constData(),importDescription_->text().toUtf8().constData(),true);
        if(!entry){GB_free(validator);GB_dealloc(validator);warn(this,tr("Invalid GameShark or Game Genie code."));return;}
        auto entries=entries_; entries.append(cheatEntries(validator).first()); GB_free(validator);GB_dealloc(validator);
        commit(entries); table_->selectRow(entries_.size()-1); code_->clear(); importDescription_->clear();
    });
    reload();
}
void CheatsDialog::setCartridge(QString base){base_=std::move(base);code_->clear();importDescription_->clear();table_->clearSelection();reload();}
void CheatsDialog::reload() {
    const int selectedRow=table_->currentRow();
    warn(this,execute_([this](GB_gameboy_t *gb){entries_=cheatEntries(gb);return QString();}));
    QSignalBlocker blocker(table_); table_->setRowCount(entries_.size());
    for(int row=0;row<entries_.size();++row){const auto &e=entries_[row];
        auto *enabled=new QTableWidgetItem; enabled->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable); enabled->setCheckState(e.enabled?Qt::Checked:Qt::Unchecked);
        auto *description=new QTableWidgetItem(e.description);
        auto *action=new QTableWidgetItem(QString("%1:%2 → %3%4").arg(e.bank==GB_CHEAT_ANY_BANK?"*":hex(e.bank,2),hex(e.address,4),hex(e.value,2),e.conditional?tr(" if old = %1").arg(hex(e.oldValue,2)):QString()));
        action->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
        table_->setItem(row,0,enabled);table_->setItem(row,1,description);table_->setItem(row,2,action);
    }
    if(!entries_.isEmpty())table_->selectRow(qBound(0,selectedRow,entries_.size()-1));
    selected();
}
void CheatsDialog::selected(){const int row=table_->currentRow();const bool valid=row>=0&&row<entries_.size();
    for(auto *widget:{description_,address_,value_})widget->setEnabled(valid);conditional_->setEnabled(valid);old_->setEnabled(valid&&entries_[row].conditional);
    if(!valid){description_->clear();address_->clear();value_->clear();old_->clear();return;}
    const auto &e=entries_[row];description_->setText(e.description);address_->setText((e.bank==GB_CHEAT_ANY_BANK?"*":hex(e.bank,2))+":"+hex(e.address,4));value_->setText(hex(e.value,2));conditional_->setChecked(e.conditional);old_->setText(hex(e.oldValue,2));
}
void CheatsDialog::commit(const QVector<CheatEntry> &entries){const auto error=persistEntries(base_,entries);if(!error.isEmpty()){warn(this,error);reload();return;}
    warn(this,execute_([entries](GB_gameboy_t *gb){setEntries(gb,entries);return QString();}));reload();}
void CheatsDialog::applyEditor(){const int row=table_->currentRow();if(row<0)return;auto entries=entries_;auto &e=entries[row];
    auto parts=address_->text().split(':');unsigned address,bank=GB_CHEAT_ANY_BANK,value,old=0;
    bool valid=parts.size()<=2&&parseHex(parts.last(),65535,address)&&parseHex(value_->text(),255,value);
    if(parts.size()==2&&parts.first().trimmed()!="*")valid=valid&&parseHex(parts.first(),65535,bank);
    if(conditional_->isChecked())valid=valid&&parseHex(old_->text(),255,old);
    if(!valid){warn(this,tr("Use hexadecimal addresses and byte values, for example $01:$C123 and $FF."));return;}
    e.description=description_->text();e.address=address;e.bank=bank;e.value=value;e.oldValue=old;e.conditional=conditional_->isChecked();commit(entries);
}

CheatSearchDialog::CheatSearchDialog(CoreExecutor execute,QString base,std::function<void()> showCheats,QWidget *parent)
    :QDialog(parent,Qt::Window),execute_(std::move(execute)),base_(std::move(base)),showCheats_(std::move(showCheats))
{
    setWindowTitle(tr("Cheat Search"));resize(780,560);setAttribute(Qt::WA_DeleteOnClose);
    auto *layout=new QVBoxLayout(this);table_=new QTableWidget(0,3);table_->setHorizontalHeaderLabels({tr("Address"),tr("Previous Value"),tr("Current Value")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setAlternatingRowColors(true);table_->verticalHeader()->hide();layout->addWidget(table_,1);
    auto *form=new QFormLayout;type_=new QComboBox;type_->addItem(tr("8-Bit"),0);type_->addItem(tr("16-Bit"),1);type_->addItem(tr("16-Bit (Big Endian)"),3);
    condition_=new QComboBox;condition_->addItems({tr("Any"),tr("Is Equal To…"),tr("Is Different From…"),tr("Is Greater Than…"),tr("Is Equal or Greater Than…"),tr("Is Less Than…"),tr("Is Equal or Less Than…"),tr("Did Change"),tr("Did Not Change"),tr("Did Increase"),tr("Did Decrease"),tr("Custom…")});condition_->setCurrentIndex(1);
    operand_=new QLineEdit("$0");operand_->setObjectName("searchOperand");expression_=new QLineEdit;expression_->setObjectName("searchExpression");auto *row=new QHBoxLayout;row->addWidget(condition_,1);row->addWidget(operand_);
    form->addRow(tr("Data Type:"),type_);form->addRow(tr("Search Condition:"),row);form->addRow(tr("Expression:"),expression_);layout->addLayout(form);
    auto *buttons=new QHBoxLayout;auto *resetButton=new QPushButton(tr("Reset"));add_=new QPushButton(tr("Add Cheat"));auto *searchButton=new QPushButton(tr("Search"));count_=new QLabel;
    buttons->addWidget(resetButton);buttons->addWidget(count_,1);buttons->addWidget(add_);buttons->addWidget(searchButton);layout->addLayout(buttons);add_->setEnabled(false);
    connect(condition_,&QComboBox::currentIndexChanged,this,&CheatSearchDialog::updateExpression);connect(operand_,&QLineEdit::textChanged,this,&CheatSearchDialog::updateExpression);
    connect(resetButton,&QPushButton::clicked,this,&CheatSearchDialog::reset);connect(searchButton,&QPushButton::clicked,this,&CheatSearchDialog::search);connect(add_,&QPushButton::clicked,this,&CheatSearchDialog::addCheat);
    connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{add_->setEnabled(table_->currentRow()>=0);});
    connect(table_->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{refreshValues();});
    auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&CheatSearchDialog::refreshValues);timer->start(250);updateExpression();reset();
}
CheatSearchDialog::~CheatSearchDialog()=default;
void CheatSearchDialog::setCartridge(QString base){base_=std::move(base);reset();}
void CheatSearchDialog::updateExpression(){const int index=condition_->currentIndex();expression_->setEnabled(index==11);operand_->setEnabled(index>=1&&index<=6);
    const QStringList expressions{"1","new == (%1)","new != (%1)","new > (%1)","new >= (%1)","new < (%1)","new <= (%1)","new != old","new == old","new > old","new < old"};
    if(index<expressions.size())expression_->setText(index>=1&&index<=6?expressions[index].arg(operand_->text()):expressions[index]);
}
void CheatSearchDialog::reset(){warn(this,execute_([](GB_gameboy_t *gb){GB_cheat_search_reset(gb);return QString();}));results_.clear();table_->setRowCount(0);type_->setEnabled(true);count_->clear();add_->setEnabled(false);}
void CheatSearchDialog::search(){QVector<GB_cheat_search_result_t> results;
    const auto expression=expression_->text().toUtf8();const auto dataType=static_cast<GB_cheat_search_data_type_t>(type_->currentData().toInt());
    const auto error=execute_([&](GB_gameboy_t *gb){if(!GB_cheat_search_filter(gb,expression.constData(),dataType))return tr("Invalid search expression.");
        results.resize(GB_cheat_search_result_count(gb));if(!results.isEmpty())GB_cheat_search_get_results(gb,results.data());return QString();});
    if(!error.isEmpty()){warn(this,error);return;}results_=std::move(results);type_->setEnabled(results_.isEmpty());
    const int shown=qMin<qsizetype>(results_.size(),10000);table_->setRowCount(shown);const int digits=type_->currentData().toInt()&1?4:2;
    for(int row=0;row<shown;++row){const auto &r=results_[row];table_->setItem(row,0,new QTableWidgetItem(hex(r.bank,2)+":"+hex(r.addr,4)));table_->setItem(row,1,new QTableWidgetItem(hex(r.value,digits)));table_->setItem(row,2,new QTableWidgetItem);}
    count_->setText(results_.size()>shown?tr("%1 results (first %2 shown)").arg(results_.size()).arg(shown):tr("%1 results").arg(results_.size()));refreshValues();
}
void CheatSearchDialog::refreshValues(){if(results_.isEmpty()||!isVisible()||isMinimized())return;
    const int first=qMax(0,table_->rowAt(0));int last=table_->rowAt(table_->viewport()->height()-1);
    if(last<first)last=qMin(table_->rowCount()-1,first+table_->viewport()->height()/qMax(1,table_->verticalHeader()->defaultSectionSize())+1);
    QVector<int> rows;for(int row=first;row<=last;++row)rows.append(row);
    if(table_->currentRow()>=0&&!rows.contains(table_->currentRow()))rows.append(table_->currentRow());
    QVector<uint16_t> values;values.reserve(rows.size());
    const int type=type_->currentData().toInt();const auto selected=results_;
    const auto error=execute_([&](GB_gameboy_t *gb){for(int row:rows){if(row>=selected.size())return QString();const auto &r=selected[row];size_t size;const auto kind=r.addr<0xC000?GB_DIRECT_ACCESS_CART_RAM:r.addr<0xE000?GB_DIRECT_ACCESS_RAM:GB_DIRECT_ACCESS_HRAM;
        const auto *data=static_cast<const uint8_t *>(GB_get_direct_access(gb,kind,&size,nullptr));size_t offset=r.addr<0xC000?(r.addr&0x1fff)+r.bank*0x2000:r.addr<0xE000?(r.addr&0xfff)+r.bank*0x1000:r.addr&0x7f;
        uint16_t value=offset<size?data[offset]:0;if((type&1)&&offset+1<size)value=type&2?(value<<8)|data[offset+1]:value|(data[offset+1]<<8);values.append(value);}return QString();});
    if(!error.isEmpty())return;for(int i=0;i<values.size();++i)if(auto*item=table_->item(rows[i],2))item->setText(hex(values[i],type&1?4:2));
}
void CheatSearchDialog::addCheat(){refreshValues();const int row=table_->currentRow();if(row<0)return;const auto result=results_[row];const int type=type_->currentData().toInt();
    unsigned value;if(!parseHex(table_->item(row,2)->text(),65535,value)){warn(this,tr("Invalid value."));return;}
    const auto error=execute_([&](GB_gameboy_t *gb){
        auto entries=cheatEntries(gb);
        entries.append({tr("Search result"),result.addr,result.bank,uint8_t(type&2?value>>8:value),0,false,true});
        if(type&1)entries.append({tr("Search result (second byte)"),uint16_t(result.addr+1),result.bank,uint8_t(type&2?value:value>>8),0,false,true});
        const auto error=persistEntries(base_,entries);if(!error.isEmpty())return error;
        setEntries(gb,entries);return QString();
    });
    if(!error.isEmpty()){warn(this,error);return;}showCheats_();
}
