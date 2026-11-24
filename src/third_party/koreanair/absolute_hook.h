#pragma once
#include <windows.h>
#include <winternl.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdint>
namespace ac8hook {
struct Boundary {unsigned old_offset,new_offset;};
// Private absolute fallbacks have no MinHook entry to freeze/relocate for them.
// Enumerate before suspending, then verify the captured thread set while frozen.
// Every inaccessible/new thread or unrecognized IP makes installation fail
// closed. No heap allocation, logging or game calls occurs while suspended.
class FrozenThreads {
    using QuerySystem=NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS,PVOID,ULONG,PULONG);
    struct Thread {DWORD id=0;HANDLE handle=nullptr;bool suspended=false;};
    std::array<Thread,1024> threads{};
    unsigned count=0;
    void* query_buffer=nullptr;
    ULONG query_capacity=0,query_length=0;
    QuerySystem query=nullptr;
    SYSTEM_PROCESS_INFORMATION* process_record(DWORD process) {
        ULONG offset=0;
        while(offset<=query_length&&query_length-offset>=sizeof(SYSTEM_PROCESS_INFORMATION)) {
            auto* record=reinterpret_cast<SYSTEM_PROCESS_INFORMATION*>(static_cast<unsigned char*>(query_buffer)+offset);
            const ULONG bytes=record->NextEntryOffset?record->NextEntryOffset:query_length-offset;
            if(bytes<sizeof(*record)||bytes>query_length-offset||
               record->NumberOfThreads>(bytes-sizeof(*record))/sizeof(SYSTEM_THREAD_INFORMATION))break;
            if(reinterpret_cast<uintptr_t>(record->UniqueProcessId)==process)return record;
            if(!record->NextEntryOffset)break;
            offset+=record->NextEntryOffset;
        }
        SetLastError(ERROR_INVALID_DATA);return nullptr;
    }
public:
    ~FrozenThreads() {
        const auto error=GetLastError();
        for(unsigned i=0;i<count;++i) {
            if(threads[i].suspended)ResumeThread(threads[i].handle);
            if(threads[i].handle)CloseHandle(threads[i].handle);
        }
        // Release the query allocation only after every thread has resumed.
        if(query_buffer)VirtualFree(query_buffer,0,MEM_RELEASE);
        SetLastError(error);
    }
    bool freeze() {
        const DWORD process=GetCurrentProcessId(),self=GetCurrentThreadId();
        // Resolve and allocate before suspension. SystemProcessInformation's
        // documented SDK layouts describe the thread array immediately after
        // each process record. The native kernel entry writes our supplied
        // buffer; Toolhelp snapshot wrappers are deliberately absent here.
        query=reinterpret_cast<QuerySystem>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySystemInformation"));
        if(!query){SetLastError(ERROR_PROC_NOT_FOUND);return false;}
        ULONG required=0;query(SystemProcessInformation,nullptr,0,&required);
        bool prepared=false;
        for(unsigned attempt=0;attempt<3;++attempt) {
            constexpr ULONG maximum=64*1024*1024;
            const uint64_t requested=std::max<uint64_t>(1024*1024,uint64_t(required)+std::max<ULONG>(64*1024,required/4));
            if(requested>maximum){SetLastError(ERROR_INSUFFICIENT_BUFFER);return false;}
            query_capacity=ULONG(requested);
            query_buffer=VirtualAlloc(nullptr,query_capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!query_buffer)return false;
            query_length=0;
            const auto status=query(SystemProcessInformation,query_buffer,query_capacity,&query_length);
            if(status>=0&&query_length&&query_length<=query_capacity){prepared=true;break;}
            // Resizing is permitted only here, while all threads still run.
            VirtualFree(query_buffer,0,MEM_RELEASE);query_buffer=nullptr;
            if(query_length<=query_capacity){SetLastError(ERROR_INVALID_DATA);return false;}
            required=query_length;
        }
        if(!prepared){SetLastError(ERROR_INSUFFICIENT_BUFFER);return false;}
        auto* initial=process_record(process);if(!initial)return false;
        const auto* initial_threads=reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>(initial+1);
        for(ULONG i=0;i<initial->NumberOfThreads;++i) {
            const auto id=reinterpret_cast<uintptr_t>(initial_threads[i].ClientId.UniqueThread);
            if(id==self)continue;
            if(id>MAXDWORD||count==threads.size()){SetLastError(ERROR_INSUFFICIENT_BUFFER);return false;}
            auto& thread=threads[count++];thread.id=DWORD(id);
            thread.handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,thread.id);
            if(!thread.handle)return false;
        }
        for(unsigned i=0;i<count;++i) {
            if(SuspendThread(threads[i].handle)==DWORD(-1))return false;
            threads[i].suspended=true;
        }
        // No allocation, loader lookup or resize after suspension. A changed
        // system size is a refusal, never a request for a larger buffer here.
        query_length=0;
        if(query(SystemProcessInformation,query_buffer,query_capacity,&query_length)<0||
           !query_length||query_length>query_capacity){SetLastError(ERROR_INSUFFICIENT_BUFFER);return false;}
        auto* final=process_record(process);if(!final)return false;
        if(final->NumberOfThreads!=count+1){SetLastError(ERROR_RETRY);return false;}
        const auto* final_threads=reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>(final+1);
        for(ULONG i=0;i<final->NumberOfThreads;++i) {
            const auto id=reinterpret_cast<uintptr_t>(final_threads[i].ClientId.UniqueThread);
            if(id==self)continue;
            bool found=false;
            for(unsigned j=0;j<count;++j)found=found||threads[j].id==id;
            if(!found){SetLastError(ERROR_RETRY);return false;}
        }
        return true;
    }
    bool relocate(uintptr_t target,unsigned size,uintptr_t continuation,
                  const Boundary* boundaries,unsigned boundary_count,bool commit,bool& reachable) {
        for(unsigned i=0;i<count;++i) {
            CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(threads[i].handle,&context))return false;
            if(context.Rip<target||context.Rip>=target+size)continue;
            bool found=false;
            for(unsigned j=0;j<boundary_count;++j)if(context.Rip==target+boundaries[j].old_offset) {
                context.Rip=continuation+boundaries[j].new_offset;found=true;break;
            }
            if(!found){SetLastError(ERROR_INVALID_ADDRESS);return false;}
            if(commit) {
                if(!SetThreadContext(threads[i].handle,&context))return false;
                reachable=true;
            }
        }
        return true;
    }
};
inline void absolute_jump(unsigned char* destination,const void* address) {
    const unsigned char opcode[]={0xff,0x25,0,0,0,0};
    memcpy(destination,opcode,sizeof(opcode));
    const auto pointer=reinterpret_cast<uintptr_t>(address);memcpy(destination+6,&pointer,8);
}
inline bool install_once(void* target,void* detour,void* continuation,
                    const unsigned char* expected,unsigned size,
                    const Boundary* boundaries,unsigned boundary_count,
                    void** original_slot,bool& reachable) {
    reachable=false;
    if(size<14||size>64){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    unsigned char patch[64];memset(patch,0x90,size);absolute_jump(patch,detour);
    FrozenThreads frozen;
    if(!frozen.freeze())return false;
    if(memcmp(target,expected,size)){SetLastError(ERROR_INVALID_DATA);return false;}
    if(!frozen.relocate(reinterpret_cast<uintptr_t>(target),size,reinterpret_cast<uintptr_t>(continuation),boundaries,boundary_count,false,reachable))return false;
    DWORD old=0;
    if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old))return false;
    // Publish the callable original before any resumed thread can enter the
    // detour. If a later context operation fails, retain this continuation:
    // already relocated threads may finish in it even though target is native.
    if(original_slot){InterlockedExchangePointer(original_slot,continuation);reachable=true;}
    bool ok=frozen.relocate(reinterpret_cast<uintptr_t>(target),size,reinterpret_cast<uintptr_t>(continuation),boundaries,boundary_count,true,reachable);
    if(ok) {
        memcpy(target,patch,size);reachable=true;
        ok=FlushInstructionCache(GetCurrentProcess(),target,size)!=FALSE;
        if(!ok){memcpy(target,expected,size);FlushInstructionCache(GetCurrentProcess(),target,size);}
    }
    const auto error=GetLastError();DWORD ignored=0;
    VirtualProtect(target,size,old,&ignored);SetLastError(error);
    return ok;
}
inline bool install(void* target,void* detour,void* continuation,
                    const unsigned char* expected,unsigned size,
                    const Boundary* boundaries,unsigned boundary_count,
                    void** original_slot,bool& reachable) {
    for(unsigned attempt=0;attempt<8;++attempt) {
        if(install_once(target,detour,continuation,expected,size,boundaries,boundary_count,original_slot,reachable))return true;
        const auto error=GetLastError();
        // Startup workers can appear between the two thread snapshots. That
        // is a safe refusal before patching, not a permanent incompatibility.
        // install_once has already resumed every thread before this wait.
        // Never retry a published continuation or a signature/access failure.
        if(error!=ERROR_RETRY||reachable||attempt==7)return false;
        Sleep(1);
    }
    return false;
}
} // namespace ac8hook
