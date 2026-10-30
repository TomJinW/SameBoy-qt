#pragma once
#include <QAbstractNativeEventFilter>
#include <QSet>
#include <functional>
class NativeModalPump final:public QAbstractNativeEventFilter{
public:
 explicit NativeModalPump(std::function<void()>tick);
 ~NativeModalPump()override;
 void tickNativeModal();
 bool nativeEventFilter(const QByteArray&,void*,qintptr*)override;
private:
 std::function<void()>tick_;QSet<quintptr>windows_;bool ticking_=false;
};
