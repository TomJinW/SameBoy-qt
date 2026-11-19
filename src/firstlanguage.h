#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// This dialog is deliberately independent of the selected application translator.
class FirstLanguageDialog final : public QDialog {
public:
    explicit FirstLanguageDialog(QWidget *parent=nullptr):QDialog(parent){
        setObjectName("firstLanguageDialog");
        setWindowTitle("Welcome to SameBoy Qt");
        setWindowFlag(Qt::WindowContextHelpButtonHint,false);
        auto *layout=new QVBoxLayout(this);
        layout->addWidget(new QLabel("Choose your language",this));
        auto *note=new QLabel(QString::fromUtf8(
            "Your choice will take effect the next time you launch the app.\n"
            "Change it later in the app's settings.\n"
            "所选语言将在下次启动应用时生效。\n以后可在应用设置中更改语言。"),this);
        note->setWordWrap(true);
        layout->addWidget(note);
        for(const auto &language:{QStringLiteral("en"),QStringLiteral("zh_CN")}){
            auto *button=new QPushButton(language=="en"?QStringLiteral("English"):QString::fromUtf8("简体中文"),this);
            button->setObjectName(language=="en"?"chooseEnglish":"chooseChinese");
            button->setDefault(language=="en");
            layout->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,language]{selection_=language;accept();});
        }
        setFixedWidth(360);
    }
    QString selection() const{return selection_;}
private:
    QString selection_="en";
};
