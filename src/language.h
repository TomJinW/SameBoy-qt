#pragma once
#include <QTranslator>
inline QString resolvedLanguage(const QString&setting){
 return setting=="zh_CN"?QStringLiteral("zh_CN"):QStringLiteral("en");
}
// All frontend strings share a source-text catalog, including inherited QWidget::tr calls.
class AppTranslator final:public QTranslator{
public:using QTranslator::QTranslator;
 QString translate(const char*context,const char*source,const char*disambiguation=nullptr,int n=-1)const override{
  auto value=QTranslator::translate(context,source,disambiguation,n);return value.isNull()?QTranslator::translate("SameBoyQt",source,disambiguation,n):value;
 }
};
