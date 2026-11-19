#pragma once
// Test diagnostics: print every thread's call stack when a smoke test stalls, so an intermittent hang
// can be located without a debugger. Windows only; module+export names, no PDB required.
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <cstdio>
inline void dumpAllThreadStacks()
{
    const HANDLE process=GetCurrentProcess();const DWORD self=GetCurrentThreadId();
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);SymInitialize(process,nullptr,TRUE);
    const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)return;
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry)){
        if(entry.th32OwnerProcessID!=GetCurrentProcessId()||entry.th32ThreadID==self)continue;
        const HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);if(!thread)continue;
        if(SuspendThread(thread)!=DWORD(-1)){
            CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
            if(GetThreadContext(thread,&context)){
                STACKFRAME64 frame{};DWORD machine;
#if defined(_M_ARM64)||defined(__aarch64__)
                machine=IMAGE_FILE_MACHINE_ARM64;frame.AddrPC.Offset=context.Pc;frame.AddrStack.Offset=context.Sp;frame.AddrFrame.Offset=context.Fp;
#else
                machine=IMAGE_FILE_MACHINE_AMD64;frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;frame.AddrFrame.Offset=context.Rbp;
#endif
                frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
                std::fprintf(stderr,"--- thread %lu\n",entry.th32ThreadID);
                for(int depth=0;depth<40&&StackWalk64(machine,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)&&frame.AddrPC.Offset;++depth){
                    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+256]{};auto*symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=255;
                    IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);DWORD64 displacement=0;
                    const bool named=SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol);const bool known=SymGetModuleInfo64(process,frame.AddrPC.Offset,&module);
                    std::fprintf(stderr,"  %s!%s+0x%llx\n",known?module.ModuleName:"?",named?symbol->Name:"?",static_cast<unsigned long long>(displacement));
                }
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);SymCleanup(process);
}
#else
inline void dumpAllThreadStacks(){}
#endif
