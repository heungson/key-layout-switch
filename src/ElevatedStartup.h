#pragma once
#include <windows.h>
#include <taskschd.h>
#include <sddl.h>
#include <oleauto.h>
#include <stdio.h>
#include <wchar.h>

namespace startup {
template<class T> struct Ptr {
    T* value=nullptr;
    ~Ptr() { if(value) value->Release(); }
    T** out() { return &value; }
    T* operator->() const { return value; }
    T* detach() { T* result=value; value=nullptr; return result; }
};
struct String {
    BSTR value=nullptr;
    explicit String(const wchar_t* text=nullptr) { if(text) value=SysAllocString(text); }
    ~String() { SysFreeString(value); }
    BSTR* out() { return &value; }
    operator BSTR() const { return value; }
};
inline constexpr wchar_t LegacyKey[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

inline HRESULT Identity(wchar_t* sid,size_t sidCount,wchar_t* taskName,size_t taskCount) {
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return HRESULT_FROM_WIN32(GetLastError());
    DWORD size=0;
    GetTokenInformation(token,TokenUser,nullptr,0,&size);
    auto user=static_cast<TOKEN_USER*>(HeapAlloc(GetProcessHeap(),0,size));
    if(!user) { CloseHandle(token); return E_OUTOFMEMORY; }
    HRESULT result=S_OK;
    LPWSTR converted=nullptr;
    if(!GetTokenInformation(token,TokenUser,user,size,&size) || !ConvertSidToStringSidW(user->User.Sid,&converted))
        result=HRESULT_FROM_WIN32(GetLastError());
    else if(wcscpy_s(sid,sidCount,converted) || swprintf_s(taskName,taskCount,L"DualLayout390-%s",converted)<0)
        result=E_INVALIDARG;
    if(converted) LocalFree(converted);
    HeapFree(GetProcessHeap(),0,user); CloseHandle(token);
    return result;
}

struct Connection {
    Ptr<ITaskService> service;
    Ptr<ITaskFolder> root;
    HRESULT Open() {
        HRESULT hr=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,
            reinterpret_cast<void**>(service.out()));
        VARIANT empty; VariantInit(&empty);
        if(SUCCEEDED(hr)) hr=service->Connect(empty,empty,empty,empty);
        if(SUCCEEDED(hr)) hr=service->GetFolder(String(L"\\"),root.out());
        return hr;
    }
};

inline HRESULT Definition(ITaskService* service,const wchar_t* executable,const wchar_t* sid,ITaskDefinition** output) {
    Ptr<ITaskDefinition> task;
    HRESULT hr=service->NewTask(0,task.out());
    if(FAILED(hr)) return hr;
    Ptr<IRegistrationInfo> registration;
    Ptr<IPrincipal> principal;
    Ptr<ITaskSettings> settings;
    Ptr<ITriggerCollection> triggers;
    Ptr<ITrigger> trigger;
    Ptr<ILogonTrigger> logon;
    Ptr<IActionCollection> actions;
    Ptr<IAction> action;
    Ptr<IExecAction> execute;
    if(SUCCEEDED(hr)) hr=task->get_RegistrationInfo(registration.out());
    if(SUCCEEDED(hr)) hr=registration->put_Author(String(L"KeyLayoutSwitch"));
    if(SUCCEEDED(hr)) hr=registration->put_Description(String(L"Start KeyLayoutSwitch as administrator in the notification area when this user signs in."));
    if(SUCCEEDED(hr)) hr=task->get_Principal(principal.out());
    if(SUCCEEDED(hr)) hr=principal->put_UserId(String(sid));
    if(SUCCEEDED(hr)) hr=principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if(SUCCEEDED(hr)) hr=principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    if(SUCCEEDED(hr)) hr=task->get_Settings(settings.out());
    if(SUCCEEDED(hr)) hr=settings->put_Enabled(VARIANT_TRUE);
    if(SUCCEEDED(hr)) hr=settings->put_StartWhenAvailable(VARIANT_TRUE);
    if(SUCCEEDED(hr)) hr=settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    if(SUCCEEDED(hr)) hr=settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    if(SUCCEEDED(hr)) hr=settings->put_ExecutionTimeLimit(String(L"PT0S"));
    if(SUCCEEDED(hr)) hr=settings->put_AllowHardTerminate(VARIANT_FALSE);
    if(SUCCEEDED(hr)) hr=settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    if(SUCCEEDED(hr)) hr=task->get_Triggers(triggers.out());
    if(SUCCEEDED(hr)) hr=triggers->Create(TASK_TRIGGER_LOGON,trigger.out());
    if(SUCCEEDED(hr)) hr=trigger->QueryInterface(IID_ILogonTrigger,reinterpret_cast<void**>(logon.out()));
    if(SUCCEEDED(hr)) hr=logon->put_UserId(String(sid));
    if(SUCCEEDED(hr)) hr=logon->put_Delay(String(L"PT5S"));
    if(SUCCEEDED(hr)) hr=task->get_Actions(actions.out());
    if(SUCCEEDED(hr)) hr=actions->Create(TASK_ACTION_EXEC,action.out());
    if(SUCCEEDED(hr)) hr=action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(execute.out()));
    if(SUCCEEDED(hr)) hr=execute->put_Path(String(executable));
    if(SUCCEEDED(hr)) hr=execute->put_Arguments(String(L"--tray"));
    if(SUCCEEDED(hr)) *output=task.detach();
    return hr;
}

inline bool SameUser(const wchar_t* actual,const wchar_t* expectedSid) {
    if(!actual || !expectedSid) return false;
    if(!_wcsicmp(actual,expectedSid)) return true;
    // Task Scheduler normalizes a supplied SID to an account name in its COM
    // properties. Compare resolved SIDs, rather than display-name spelling.
    PSID expected=nullptr;
    if(!ConvertStringSidToSidW(expectedSid,&expected)) return false;
    BYTE actualSid[SECURITY_MAX_SID_SIZE]; wchar_t domain[256];
    DWORD sidSize=sizeof(actualSid),domainSize=256; SID_NAME_USE use;
    const bool equal=LookupAccountNameW(nullptr,actual,actualSid,&sidSize,domain,&domainSize,&use) &&
        EqualSid(actualSid,expected);
    LocalFree(expected);
    return equal;
}

inline bool Matches(ITaskDefinition* task,const wchar_t* executable,const wchar_t* sid) {
    Ptr<IPrincipal> principal; Ptr<IActionCollection> actions; Ptr<IAction> action;
    Ptr<IExecAction> execute; Ptr<ITriggerCollection> triggers; Ptr<ITrigger> trigger; Ptr<ILogonTrigger> logon;
    TASK_RUNLEVEL_TYPE level=TASK_RUNLEVEL_LUA;
    TASK_LOGON_TYPE login=TASK_LOGON_NONE;
    String user,path,args,triggerUser;
    LONG count=0;
    return SUCCEEDED(task->get_Principal(principal.out())) &&
        SUCCEEDED(principal->get_RunLevel(&level)) && level==TASK_RUNLEVEL_HIGHEST &&
        SUCCEEDED(principal->get_LogonType(&login)) && login==TASK_LOGON_INTERACTIVE_TOKEN &&
        SUCCEEDED(principal->get_UserId(user.out())) && SameUser(user.value,sid) &&
        SUCCEEDED(task->get_Actions(actions.out())) && SUCCEEDED(actions->get_Count(&count)) && count==1 &&
        SUCCEEDED(actions->get_Item(1,action.out())) &&
        SUCCEEDED(action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(execute.out()))) &&
        SUCCEEDED(execute->get_Path(path.out())) && path.value && !_wcsicmp(path.value,executable) &&
        SUCCEEDED(execute->get_Arguments(args.out())) && args.value && !wcscmp(args.value,L"--tray") &&
        SUCCEEDED(task->get_Triggers(triggers.out())) && SUCCEEDED(triggers->get_Count(&count)) && count==1 &&
        SUCCEEDED(triggers->get_Item(1,trigger.out())) &&
        SUCCEEDED(trigger->QueryInterface(IID_ILogonTrigger,reinterpret_cast<void**>(logon.out()))) &&
        SUCCEEDED(logon->get_UserId(triggerUser.out())) && SameUser(triggerUser.value,sid);
}

inline bool Enabled(const wchar_t* executable) {
    wchar_t sid[192],name[256];
    if(FAILED(Identity(sid,192,name,256))) return false;
    Connection connection;
    if(FAILED(connection.Open())) return false;
    Ptr<IRegisteredTask> registered; Ptr<ITaskDefinition> task;
    VARIANT_BOOL enabled=VARIANT_FALSE;
    return SUCCEEDED(connection.root->GetTask(String(name),registered.out())) &&
        SUCCEEDED(registered->get_Enabled(&enabled)) && enabled==VARIANT_TRUE &&
        SUCCEEDED(registered->get_Definition(task.out())) && Matches(task.value,executable,sid);
}

inline HRESULT RemoveLegacy() {
    HKEY key;
    LSTATUS result=RegOpenKeyExW(HKEY_CURRENT_USER,LegacyKey,0,KEY_SET_VALUE,&key);
    if(result==ERROR_FILE_NOT_FOUND) return S_OK;
    if(result!=ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    result=RegDeleteValueW(key,L"DualLayout390"); RegCloseKey(key);
    return result==ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(result);
}

inline bool LegacyEnabled(const wchar_t* executable) {
    wchar_t value[32768]={},expected[MAX_PATH+16]; DWORD size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,LegacyKey,L"DualLayout390",RRF_RT_REG_SZ,nullptr,value,&size)!=ERROR_SUCCESS)
        return false;
    swprintf_s(expected,L"\"%s\" --tray",executable);
    return !_wcsicmp(value,expected);
}

inline HRESULT Set(bool enabled,const wchar_t* executable) {
    wchar_t sid[192],name[256];
    HRESULT hr=Identity(sid,192,name,256);
    Connection connection;
    if(SUCCEEDED(hr)) hr=connection.Open();
    if(FAILED(hr)) return hr;
    if(!enabled) {
        hr=connection.root->DeleteTask(String(name),0);
        if(hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) hr=S_OK;
    } else {
        Ptr<ITaskDefinition> task; Ptr<IRegisteredTask> registered;
        hr=Definition(connection.service.value,executable,sid,task.out());
        VARIANT empty,user; VariantInit(&empty); VariantInit(&user);
        user.vt=VT_BSTR; user.bstrVal=SysAllocString(sid);
        if(!user.bstrVal) hr=E_OUTOFMEMORY;
        if(SUCCEEDED(hr)) hr=connection.root->RegisterTaskDefinition(String(name),task.value,TASK_CREATE_OR_UPDATE,
            user,empty,TASK_LOGON_INTERACTIVE_TOKEN,empty,registered.out());
        VariantClear(&user);
        if(SUCCEEDED(hr) && !Enabled(executable)) hr=E_FAIL;
    }
    // Retain the previous startup entry if task registration failed.
    if(SUCCEEDED(hr)) hr=RemoveLegacy();
    return hr;
}
}
