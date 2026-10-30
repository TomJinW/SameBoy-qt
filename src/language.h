#pragma once
#include <QLocale>
#include <QTranslator>
#include <QStringList>
inline QString resolvedLanguage(const QString&setting,const QStringList&systemLanguages){
 if(setting=="zh_CN"||setting=="en")return setting;
 for(const auto&name:systemLanguages){const QLocale locale(name);if(locale.language()==QLocale::Chinese&&locale.script()==QLocale::SimplifiedChineseScript)return "zh_CN";}
 return "en";
}
// All frontend strings share a source-text catalog, including inherited QWidget::tr calls.
class AppTranslator final:public QTranslator{
public:using QTranslator::QTranslator;
 QString translate(const char*context,const char*source,const char*disambiguation=nullptr,int n=-1)const override{
  auto value=QTranslator::translate(context,source,disambiguation,n);return value.isNull()?QTranslator::translate("SameBoyQt",source,disambiguation,n):value;
 }
};
