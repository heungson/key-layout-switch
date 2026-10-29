#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include "../src/ElevatedStartup.h"

namespace {
int failures=0,checks=0;
void Expect(bool value,const char* text) {
    ++checks;
    if(!value) { ++failures; printf("FAIL: %s\n",text); }
}
}
int main() {
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
    {
        wchar_t sid[192],name[256];
        Expect(SUCCEEDED(startup::Identity(sid,192,name,256)),"Resolve current user's SID and task name");
        Expect(wcsstr(name,sid)!=nullptr,"Startup task is scoped to this user");
        startup::Connection connection;
        HRESULT hr=connection.Open();
        Expect(SUCCEEDED(hr),"Connect to Windows Task Scheduler");
        if(SUCCEEDED(hr)) {
            startup::Ptr<ITaskDefinition> task;
            const wchar_t* executable=L"D:\\Settings with spaces\\세벌식\\DualLayout390.exe";
            hr=startup::Definition(connection.service.value,executable,sid,task.out());
            Expect(SUCCEEDED(hr),"Create native elevated logon task definition without registering it");
            if(SUCCEEDED(hr)) {
                Expect(startup::Matches(task.value,executable,sid),"Correct account, highest privileges, interactive logon, tray arguments and executable");
                Expect(!startup::Matches(task.value,L"D:\\another.exe",sid),"Different executable is not accepted as enabled startup");
                Expect(!startup::Matches(task.value,executable,L"S-1-5-18"),"System account is not accepted as user's startup");
                startup::Ptr<ITaskSettings> settings;
                task->get_Settings(settings.out());
                VARIANT_BOOL battery=VARIANT_TRUE,stop=VARIANT_TRUE,enabled=VARIANT_FALSE;
                settings->get_DisallowStartIfOnBatteries(&battery); settings->get_StopIfGoingOnBatteries(&stop);
                settings->get_Enabled(&enabled);
                Expect(!battery && !stop,"Startup and remapping continue on battery power");
                Expect(enabled==VARIANT_TRUE,"Sign-in task is enabled");
                startup::String limit;
                settings->get_ExecutionTimeLimit(limit.out());
                Expect(limit.value && !wcscmp(limit.value,L"PT0S"),"Background utility has no execution time limit");
                TASK_INSTANCES_POLICY policy;
                settings->get_MultipleInstances(&policy);
                Expect(policy==TASK_INSTANCES_IGNORE_NEW,"Scheduled startup does not create parallel instances");
                startup::Ptr<IPrincipal> principal;
                task->get_Principal(principal.out()); principal->put_RunLevel(TASK_RUNLEVEL_LUA);
                Expect(!startup::Matches(task.value,executable,sid),"Non-elevated task is not accepted as administrator startup");
            }
        }
    }
    CoUninitialize();
    printf("%s: %d startup definition checks. No tasks or registry entries changed.\n",failures ? "FAIL" : "PASS",checks);
    return failures ? 1 : 0;
}
