#pragma once
#include <QString>
inline QString compactMenuStyle()
{
    return QStringLiteral("QMenu { padding: 2px 0px; }"
                          "QMenu::item { padding: 2px 20px 2px 18px; }"
                          "QMenu::item:selected:enabled { background-color: palette(highlight); color: palette(highlighted-text); }"
                          "QMenuBar::item:selected { background-color: palette(highlight); color: palette(highlighted-text); }"
                          "QMenuBar::item:pressed { background-color: palette(highlight); color: palette(highlighted-text); }"
                          "QMenu::separator { height: 1px; margin: 3px 8px; }"
                          "QMenuBar::item { padding: 3px 8px; }");
}

#include <QApplication>
#include <QStyleHints>
#include <QStyle>
#include <QEvent>
#include <QTimer>
inline void refreshInterfacePalette(){
    if(qApp->property("refreshingInterfacePalette").toBool())return;
    qApp->setProperty("refreshingInterfacePalette",true);
    const int theme=qApp->property("interfaceTheme").toInt();
    const bool dark=theme==2||(theme==0&&QGuiApplication::styleHints()->colorScheme()==Qt::ColorScheme::Dark);
    // Unpolish first: otherwise the stylesheet proxy can restore old widget palettes.
    const QString stylesheet=qApp->styleSheet();
    if(!stylesheet.isEmpty())qApp->setStyleSheet({});
    QPalette palette=QApplication::style()->standardPalette();
    const QColor text=dark?QColor(240,240,240):QColor(20,20,20);
    const QColor disabledText=dark?QColor(145,145,145):QColor(120,120,120);
    const QColor window=dark?QColor(30,30,30):QColor(245,245,245);
    const QColor base=dark?QColor(35,35,35):QColor(255,255,255);
    const QColor alternate=dark?QColor(45,45,45):QColor(240,240,240);
    const QColor button=dark?QColor(50,50,50):QColor(240,240,240);
    const QColor highlight=dark?QColor(0,120,200):QColor(0,105,190);
    const auto set=[&](QPalette::ColorGroup group,QPalette::ColorRole role,const QColor&color){palette.setColor(group,role,color);};
    // Set every role used by Widgets explicitly. A native theme notification can
    // arrive after setColorScheme() and otherwise leave disabled/placeholder and
    // bevel colors from the previous scheme in already-open windows.
    for(auto group:{QPalette::Active,QPalette::Inactive}){
        set(group,QPalette::Window,window);set(group,QPalette::WindowText,text);
        set(group,QPalette::Base,base);set(group,QPalette::AlternateBase,alternate);set(group,QPalette::Text,text);
        set(group,QPalette::Button,button);set(group,QPalette::ButtonText,text);
        set(group,QPalette::BrightText,dark?QColor(255,80,80):QColor(180,0,0));
        set(group,QPalette::Light,dark?QColor(85,85,85):QColor(255,255,255));
        set(group,QPalette::Midlight,dark?QColor(65,65,65):QColor(220,220,220));
        set(group,QPalette::Mid,dark?QColor(75,75,75):QColor(190,190,190));
        set(group,QPalette::Dark,dark?QColor(25,25,25):QColor(150,150,150));
        set(group,QPalette::Shadow,dark?QColor(15,15,15):QColor(80,80,80));
        set(group,QPalette::Highlight,highlight);set(group,QPalette::HighlightedText,Qt::white);
        set(group,QPalette::Link,dark?QColor(100,170,240):QColor(0,90,180));
        set(group,QPalette::LinkVisited,dark?QColor(190,140,230):QColor(110,50,150));
        set(group,QPalette::ToolTipBase,alternate);set(group,QPalette::ToolTipText,text);
        set(group,QPalette::PlaceholderText,disabledText);set(group,QPalette::Accent,highlight);
    }
    set(QPalette::Disabled,QPalette::Window,window);set(QPalette::Disabled,QPalette::WindowText,disabledText);
    set(QPalette::Disabled,QPalette::Base,alternate);set(QPalette::Disabled,QPalette::AlternateBase,alternate);set(QPalette::Disabled,QPalette::Text,disabledText);
    set(QPalette::Disabled,QPalette::Button,alternate);set(QPalette::Disabled,QPalette::ButtonText,disabledText);
    set(QPalette::Disabled,QPalette::BrightText,disabledText);set(QPalette::Disabled,QPalette::Light,dark?QColor(65,65,65):QColor(245,245,245));
    set(QPalette::Disabled,QPalette::Midlight,dark?QColor(55,55,55):QColor(225,225,225));set(QPalette::Disabled,QPalette::Mid,dark?QColor(55,55,55):QColor(200,200,200));
    set(QPalette::Disabled,QPalette::Dark,dark?QColor(25,25,25):QColor(170,170,170));set(QPalette::Disabled,QPalette::Shadow,dark?QColor(15,15,15):QColor(100,100,100));
    set(QPalette::Disabled,QPalette::Highlight,dark?QColor(55,75,90):QColor(190,210,230));set(QPalette::Disabled,QPalette::HighlightedText,disabledText);
    set(QPalette::Disabled,QPalette::Link,disabledText);set(QPalette::Disabled,QPalette::LinkVisited,disabledText);
    set(QPalette::Disabled,QPalette::ToolTipBase,alternate);set(QPalette::Disabled,QPalette::ToolTipText,disabledText);
    set(QPalette::Disabled,QPalette::PlaceholderText,disabledText);set(QPalette::Disabled,QPalette::Accent,disabledText);
    QApplication::setPalette(palette);
    if(!stylesheet.isEmpty())qApp->setStyleSheet(stylesheet);
    qApp->setProperty("refreshingInterfacePalette",false);
}
// Native theme notifications may update the application palette after colorSchemeChanged.
// Reapply once the notification has finished, without reacting to our own palette writes.
class InterfacePaletteObserver final:public QObject {
public:using QObject::QObject;
protected:bool eventFilter(QObject*object,QEvent*event)override{
    if(object==qApp&&(event->type()==QEvent::ApplicationPaletteChange||event->type()==QEvent::ThemeChange)
       &&!qApp->property("refreshingInterfacePalette").toBool()&&!pending_){
        scheduleRefresh();
    }
    return false;
}
private:
    void scheduleRefresh(){
        if(qApp->property("refreshingInterfacePalette").toBool()||pending_)return;
        pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;refreshInterfacePalette();});
    }
    bool pending_=false;
};
inline void applyInterfaceTheme(int theme){
    qApp->setProperty("interfaceTheme",theme);
    if(!qApp->property("interfacePaletteConnected").toBool()){
        qApp->setProperty("interfacePaletteConnected",true);
        qApp->installEventFilter(new InterfacePaletteObserver(qApp));
        QObject::connect(QGuiApplication::styleHints(),&QStyleHints::colorSchemeChanged,qApp,[]{
            // QPA may publish its new native palette after this signal returns.
            // Reapply once the platform notification has finished as well as
            // in applyInterfaceTheme(), which performs the immediate preview.
            QTimer::singleShot(0,qApp,[]{refreshInterfacePalette();});
        });
    }
    QGuiApplication::styleHints()->setColorScheme(theme==1?Qt::ColorScheme::Light:theme==2?Qt::ColorScheme::Dark:Qt::ColorScheme::Unknown);
    refreshInterfacePalette();
}
