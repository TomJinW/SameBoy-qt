#pragma once
#include <Core/gb.h>
#include <QDialog>
#include <QVector>
#include <functional>

class QTableWidget;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
using CoreExecutor = std::function<QString(std::function<QString(GB_gameboy_t *)>)>;

struct CheatEntry {
    QString description;
    uint16_t address = 0, bank = GB_CHEAT_ANY_BANK;
    uint8_t value = 0, oldValue = 0;
    bool conditional = false, enabled = true;
};
QVector<CheatEntry> cheatEntries(GB_gameboy_t *gb);
QString storeCheats(GB_gameboy_t *gb, const QString &base);
QString restoreCheats(GB_gameboy_t *gb, const QString &base);

class CheatsDialog final : public QDialog {
public:
    CheatsDialog(CoreExecutor execute, QString base, QWidget *parent = nullptr);
    void reload();
    void setCartridge(QString base);
private:
    CoreExecutor execute_;
    QString base_;
    QVector<CheatEntry> entries_;
    QTableWidget *table_;
    QLineEdit *description_, *address_, *value_, *old_, *code_, *importDescription_;
    QCheckBox *conditional_;
    void selected();
    void applyEditor();
    void commit(const QVector<CheatEntry> &entries);
};

class CheatSearchDialog final : public QDialog {
public:
    CheatSearchDialog(CoreExecutor execute, QString base, std::function<void()> showCheats, QWidget *parent = nullptr);
    ~CheatSearchDialog() override;
    void setCartridge(QString base);
private:
    CoreExecutor execute_;
    QString base_;
    std::function<void()> showCheats_;
    QTableWidget *table_;
    QComboBox *type_, *condition_;
    QLineEdit *operand_, *expression_;
    QLabel *count_;
    QPushButton *add_;
    QVector<GB_cheat_search_result_t> results_;
    void updateExpression();
    void search();
    void reset();
    void refreshValues();
    void addCheat();
    void writeValue(int row,const QString&text);
    bool refreshing_=false;
};
