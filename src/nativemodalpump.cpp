#include "nativemodalpump.h"
#include <QCoreApplication>
#include <QEvent>
#include <QScopedValueRollback>
#ifdef Q_OS_WIN
#include <windows.h>
namespace {
void CALLBACK modalTimer(HWND,UINT,UINT_PTR id,DWORD){
 reinterpret_cast<NativeModalPump*>(id)->tickNativeModal();
}
}
#endif
NativeModalPump::NativeModalPump(std::function<void()>tick):tick_(std::move(tick)){QCoreApplication::instance()->installNativeEventFilter(this);}
NativeModalPump::~NativeModalPump(){
 QCoreApplication::instance()->removeNativeEventFilter(this);
#ifdef Q_OS_WIN
 for(auto window:windows_)KillTimer(reinterpret_cast<HWND>(window),reinterpret_cast<UINT_PTR>(this));
#endif
}
bool NativeModalPump::nativeEventFilter(const QByteArray&,void*message,qintptr*result){
#ifdef Q_OS_WIN
 const auto*event=static_cast<MSG*>(message);const auto window=reinterpret_cast<quintptr>(event->hwnd);const auto timer=reinterpret_cast<UINT_PTR>(this);
 if(event->message==WM_ENTERSIZEMOVE){if(SetTimer(event->hwnd,timer,16,modalTimer))windows_.insert(window);}
 else if(event->message==WM_EXITSIZEMOVE||event->message==WM_NCDESTROY){if(windows_.remove(window))KillTimer(event->hwnd,timer);}
 else if(event->message==WM_TIMER&&event->wParam==timer&&windows_.contains(window)&&!ticking_){
  // Windows dispatches native timers inside its MoveSize modal loop. Qt's
  // normal posted update/queued completion delivery may otherwise be delayed.
  tickNativeModal();if(result)*result=0;return true;
 }
#else
 (void)message;(void)result;
#endif
 return false;
}

void NativeModalPump::tickNativeModal(){
 if(ticking_)return;
 QScopedValueRollback guard(ticking_,true);
 tick_();
 QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
 QCoreApplication::sendPostedEvents(nullptr,QEvent::UpdateRequest);
}
