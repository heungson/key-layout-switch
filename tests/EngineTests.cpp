// Exercise the actual application worker without opening its UI or enabling
// remapping. No synthetic keyboard events or input-mode changes are made.
#include "../src/App.cpp"

namespace {
HANDLE probeEvent = nullptr;
constexpr UINT ProbeMessage = WM_APP+91;
LRESULT CALLBACK ObserveWorker(int code,WPARAM wParam,LPARAM lParam) {
    if (code>=0 && wParam==PM_REMOVE && reinterpret_cast<MSG*>(lParam)->message==ProbeMessage)
        SetEvent(probeEvent);
    return CallNextHookEx(nullptr,code,wParam,lParam);
}
}

int main() {
    instance=GetModuleHandleW(nullptr);
    enabled=false;
    if (!StartKeyboardThread()) { printf("FAIL: start dedicated keyboard thread\n"); StopKeyboardThread(); return 1; }
    bool passed=hookThreadId!=GetCurrentThreadId();
    probeEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HHOOK observer=SetWindowsHookExW(WH_GETMESSAGE,ObserveWorker,nullptr,hookThreadId);
    if (!probeEvent || !observer) passed=false;
    if (passed) {
        PostThreadMessageW(hookThreadId,ProbeMessage,0,0);
        // Deliberately do not pump the main thread. A UI-thread hook cannot
        // service this probe until this wait has ended.
        passed=WaitForSingleObject(probeEvent,1000)==WAIT_OBJECT_0;
    }
    if (observer) UnhookWindowsHookEx(observer);
    StopKeyboardThread();
    if (probeEvent) CloseHandle(probeEvent);
    passed=passed && hookThread==nullptr && hook==nullptr;
    printf("%s: keyboard worker processes messages while main thread is blocked; clean shutdown. No keys sent.\n",passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
