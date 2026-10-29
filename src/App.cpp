#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>
#include <dwmapi.h>
#include "KeyboardCore.h"
#include "InputContext.h"
#include "ModernUi.h"
#include "ElevatedStartup.h"

namespace {
// Retain legacy identifiers so settings and single-instance behavior survive
// the rename from Dual Layout 390 to KeyLayoutSwitch.
constexpr wchar_t WindowClass[] = L"DualLayout390.Main.v1";
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT RefreshMessage = WM_APP + 2;
constexpr ULONG_PTR InjectionTag = 0x3344564b;
enum { Enable = 100, Shortcuts, ScrollBypass, Startup, WindowsSettings, Hide, Quit, TestPad,
       NavKeyboard = 120, NavTest, NavAppearance, NavSetup, ThemeSystem, ThemeLight, ThemeDark,
       MenuShow = 200, MenuPause, MenuExit };
HWND window, enabledBox, shortcutsBox, scrollBox, startupBox, statusTitle, statusText,
     contextText, testPad, setupText, heading, subtitle, koreanTitle, koreanCaption,
     englishTitle, englishCaption, preferencesHeading,
     testHint, settingsButton, hideButton, quitButton;
HWND children[48] = {}, navigation[4] = {}, themeChoices[3] = {};
int page = 0, themePreference = 0;
HFONT regularFont, titleFont, sectionFont, smallFont, statusFont;
HBRUSH background, cardBrush;
HICON appIcon;
HHOOK hook;
HANDLE hookThread = nullptr;
DWORD hookThreadId = 0;
HINSTANCE instance;
dual::KeyRouter router;
// Interlocked state keeps the portable executable independent of a C++ runtime.
struct SharedValue {
    volatile LONG value;
    operator LONG() const { return InterlockedCompareExchange(const_cast<volatile LONG*>(&value),0,0); }
    SharedValue& operator=(LONG next) { InterlockedExchange(&value,next); return *this; }
};
SharedValue enabled{0}, preserveShortcuts{1}, scrollBypass{1}, scrollLockOn{0};
bool conflict = false, quitting = false, trayAdded = false, preview = false, hotkeyRegistered = false;
SharedValue injectionError{0};
dual::ModeContinuity modeContinuity;
bool scrollHeld = false;

dual::InputMode ReadStableMode() {
    const auto context = dual::ReadContext();
    return modeContinuity.Resolve(context.mode, reinterpret_cast<uintptr_t>(context.foreground),
        reinterpret_cast<uintptr_t>(context.focused), reinterpret_cast<uintptr_t>(context.layout),
        GetTickCount64(), context.queryFailed);
}
int dpi = 96, childCount = 0;
dual::InputMode displayedMode = dual::InputMode::Unknown;
bool displayedActive = false;
ULONG_PTR graphicsToken = 0;
constexpr design::Button masterStyle = {L"Enable KeyLayoutSwitch", L"Use Dvorak for English and your Windows IME for Korean.", true, false, false, L"\xE765"};
constexpr design::Button shortcutsStyle = {L"Keep QWERTY shortcuts", L"Keep familiar Ctrl, Alt and Win key positions.", true, false, false, L"\xE765"};
constexpr design::Button scrollStyle = {L"Scroll Lock bypass", L"Turn on Scroll Lock to temporarily use original keys.", true, false, false, L"\xE72E"};
constexpr design::Button startupStyle = {L"Launch at sign-in", L"Start as administrator in the notification area.", true, false, false, L"\xE7E8"};
constexpr design::Button settingsStyle = {L"Open language settings"};
constexpr design::Button hideStyle = {L"Hide to tray", nullptr, false, false, false, L"\xE8A7", true};
constexpr design::Button quitStyle = {L"Quit", nullptr, false, false, false, L"\xE8AC", true};
constexpr design::Button navStyles[] = {
    {L"Keyboard", nullptr, false, false, false, L"\xE765", true},
    {L"Typing test", nullptr, false, false, false, L"\xE70F", true},
    {L"Appearance", nullptr, false, false, false, L"\xE790", true},
    {L"Setup guide", nullptr, false, false, false, L"\xE897", true}
};
constexpr design::Button themeStyles[] = {
    {L"System",nullptr,false,false,false,nullptr,false,true},
    {L"Light",nullptr,false,false,false,nullptr,false,true},
    {L"Dark",nullptr,false,false,false,nullptr,false,true}
};
wchar_t settingsPath[MAX_PATH] = {};
NOTIFYICONDATAW tray = {};

int S(int value) { return MulDiv(value, dpi, 96); }
bool IsChecked(HWND control) { return Button_GetCheck(control) == BST_CHECKED; }
void Check(HWND control, bool value) {
    if (IsChecked(control) != value) Button_SetCheck(control, value ? BST_CHECKED : BST_UNCHECKED);
}
void SetText(HWND control, const wchar_t* value) {
    wchar_t existing[512];
    GetWindowTextW(control, existing, 512);
    if (wcscmp(existing, value)) SetWindowTextW(control, value);
}

void InitSettings() {
    wchar_t directory[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, directory))) return;
    wcscat_s(directory, L"\\DualLayout390");
    CreateDirectoryW(directory, nullptr);
    swprintf_s(settingsPath, L"%s\\settings.ini", directory);
    enabled = GetPrivateProfileIntW(L"Keyboard", L"Enabled", 0, settingsPath) != 0;
    preserveShortcuts = GetPrivateProfileIntW(L"Keyboard", L"PreserveShortcuts", 1, settingsPath) != 0;
    scrollBypass = GetPrivateProfileIntW(L"Keyboard", L"ScrollLockBypass", 1, settingsPath) != 0;
    themePreference = GetPrivateProfileIntW(L"Appearance", L"Theme", 0, settingsPath);
    if (themePreference < 0 || themePreference > 2) themePreference = 0;
}

void SaveSettings() {
    if (!settingsPath[0] || preview) return;
    WritePrivateProfileStringW(L"Keyboard", L"Enabled", enabled ? L"1" : L"0", settingsPath);
    WritePrivateProfileStringW(L"Keyboard", L"PreserveShortcuts", preserveShortcuts ? L"1" : L"0", settingsPath);
    WritePrivateProfileStringW(L"Keyboard", L"ScrollLockBypass", scrollBypass ? L"1" : L"0", settingsPath);
    WritePrivateProfileStringW(L"Appearance", L"Theme", themePreference==1 ? L"1" : themePreference==2 ? L"2" : L"0", settingsPath);
}

void ApplyTheme() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    design::Theme(themePreference==2 || (themePreference==0 && light==0));
    if (background) DeleteObject(background);
    if (cardBrush) DeleteObject(cardBrush);
    background = CreateSolidBrush(design::Canvas);
    cardBrush = CreateSolidBrush(design::Surface);
    if (!window) return;
    SetClassLongPtrW(window,GCLP_HBRBACKGROUND,reinterpret_cast<LONG_PTR>(background));
    const BOOL darkTitle = design::dark;
    DwmSetWindowAttribute(window,20,&darkTitle,sizeof(darkTitle));
    DwmSetWindowAttribute(window,35,&design::Canvas,sizeof(COLORREF));
    DwmSetWindowAttribute(window,36,&design::Ink,sizeof(COLORREF));
    for (int i=0;i<3;++i) Check(themeChoices[i],themePreference==i);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}

bool HasConflict() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true; // Don't risk double mapping if enumeration fails.
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (!_wcsicmp(entry.szExeFile, L"Dvorak.exe")) { found = true; break; }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found;
}

bool IsStartupEnabled() {
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    return startup::Enabled(executable);
}

HRESULT startupError=S_OK;
bool SetStartup(bool value) {
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    startupError=startup::Set(value,executable);
    return SUCCEEDED(startupError);
}
bool SendScan(WORD scan, bool up) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scan;
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    input.ki.dwExtraInfo = InjectionTag;
    SetLastError(0);
    if (SendInput(1, &input, sizeof(input)) == 1) return true;
    injectionError = GetLastError();
    // UIPI can reject injection without setting an error code.
    if (!injectionError) injectionError = ERROR_ACCESS_DENIED;
    enabled = false;
    PostMessageW(window, RefreshMessage, 0, 0);
    return false;
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM message, LPARAM parameter) {
    if (code != HC_ACTION) return CallNextHookEx(hook, code, message, parameter);
    const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(parameter);
    if (key.vkCode==VK_SCROLL) {
        const bool down=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
        if (down && !scrollHeld) scrollLockOn=!scrollLockOn;
        scrollHeld=down;
    }
    // Invalidate on both edges, including switches produced by other utilities.
    // The IME applies a switch only after the hook returns.
    if (key.vkCode == VK_HANGUL || key.vkCode == VK_KANJI || key.vkCode == VK_MODECHANGE ||
        key.vkCode == VK_IME_ON || key.vkCode == VK_IME_OFF ||
        (key.vkCode == VK_SPACE && ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
            (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))) ||
        ((key.vkCode == VK_LSHIFT || key.vkCode == VK_RSHIFT || key.vkCode == VK_SHIFT) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000)) ||
        ((key.vkCode == VK_LMENU || key.vkCode == VK_RMENU || key.vkCode == VK_MENU) &&
            (GetAsyncKeyState(VK_SHIFT) & 0x8000)))
        modeContinuity.Invalidate();
    if (key.flags & LLKHF_INJECTED) return CallNextHookEx(hook, code, message, parameter);
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool extended = (key.flags & LLKHF_EXTENDED) != 0;
    const auto source = static_cast<uint16_t>(key.scanCode);
    if ((!up && !down) || extended || source >= 128 || !dual::DvorakScan(source))
        return CallNextHookEx(hook, code, message, parameter);

    uint16_t target = 0;
    if (up) {
        target = router.Up(source, extended);
    } else {
        dual::Policy policy;
        policy.enabled = enabled;
        policy.preserveShortcuts = preserveShortcuts;
        policy.shortcutHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
            (GetAsyncKeyState(VK_MENU) & 0x8000) || (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
            (GetAsyncKeyState(VK_RWIN) & 0x8000);
        policy.scrollBypass = scrollBypass && scrollLockOn;
        // Don't query another process for bypassed keys or repeat events.
        if (enabled && !policy.scrollBypass && !(preserveShortcuts && policy.shortcutHeld) && !router.held[source].held)
            policy.mode = ReadStableMode();
        target = router.Down(source, policy);
    }
    if (target) {
        if (SendScan(target, up)) return 1;
        router.Failed(source);
    }
    return CallNextHookEx(hook, code, message, parameter);
}

void ReleaseKeys() {
    for (auto& route : router.held) {
        if (route.held && route.target) SendScan(route.target, true);
        route = {};
    }
}

DWORD WINAPI KeyboardThread(void* ready) {
    MSG message;
    PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    hook = SetWindowsHookExW(WH_KEYBOARD_LL,KeyboardHook,instance,0);
    const UINT_PTR timer = hook ? SetTimer(nullptr,0,40,nullptr) : 0;
    SetEvent(static_cast<HANDLE>(ready));
    if (!hook) return 1;
    while (GetMessageW(&message,nullptr,0,0)>0) {
        if (message.message==WM_TIMER && message.wParam==timer) {
            if (enabled) ReadStableMode();
            else modeContinuity.Invalidate();
        } else {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    if (timer) KillTimer(nullptr,timer);
    ReleaseKeys();
    UnhookWindowsHookEx(hook);
    hook=nullptr;
    return 0;
}

bool StartKeyboardThread() {
    scrollLockOn=(GetKeyState(VK_SCROLL)&1)!=0;
    scrollHeld=(GetAsyncKeyState(VK_SCROLL)&0x8000)!=0;
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!ready) return false;
    hookThread=CreateThread(nullptr,0,KeyboardThread,ready,0,&hookThreadId);
    if (hookThread) WaitForSingleObject(ready,INFINITE);
    CloseHandle(ready);
    return hookThread && hook;
}

void StopKeyboardThread() {
    if (!hookThread) return;
    PostThreadMessageW(hookThreadId,WM_QUIT,0,0);
    WaitForSingleObject(hookThread,INFINITE);
    CloseHandle(hookThread);
    hookThread=nullptr;
}

HICON MakeIcon() {
    HICON icon = reinterpret_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    if (icon) return icon;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 32, 32), mask = CreateBitmap(32, 32, 1, 1, nullptr);
    auto old = SelectObject(dc, color);
    RECT bounds = {0,0,32,32};
    HBRUSH brush = CreateSolidBrush(design::Accent);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
    HFONT font = CreateFontW(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255,255,255));
    DrawTextW(dc, L"D", -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, mask);
    PatBlt(dc, 0, 0, 32, 32, BLACKNESS);
    SelectObject(dc, old);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON result = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return result;
}

void AddTray() {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = appIcon;
    wcscpy_s(tray.szTip, L"KeyLayoutSwitch — paused");
    trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    // Keep the original callback protocol (wParam=id, lParam=event).
}

void ShowWindowFromTray() {
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
}

void Refresh() {
    Check(enabledBox, enabled);
    const auto context = dual::ReadContext();
    const wchar_t* title;
    const wchar_t* detail;
    if (conflict) {
        title = L"Another remapper is running";
        detail = L"Exit Dvorak.exe from its tray menu, then turn on KeyLayoutSwitch.";
    } else if (injectionError) {
        title = L"Windows blocked a replacement key";
        detail = L"Check the target app's administrator privileges, then enable again.";
    } else if (!enabled) {
        title = L"Ready when you are";
        detail = L"Turn on KeyLayoutSwitch to use Dvorak whenever you switch to English.";
    } else if (scrollBypass && scrollLockOn) {
        title = L"Original keys · Scroll Lock is on";
        detail = L"Turn off Scroll Lock to resume automatic English Dvorak.";
    } else if (context.mode == dual::InputMode::Korean) {
        title = L"한글 · Korean input";
        detail = L"Your Windows IME handles Korean. Use 한/영 to switch to Dvorak.";
    } else if (context.mode == dual::InputMode::English) {
        title = L"English · Dvorak is active";
        detail = L"Type in Dvorak. Use your usual 한/영 key to switch to Korean.";
    } else if (context.mode == dual::InputMode::Other) {
        title = L"Using another keyboard layout";
        detail = L"Choose Korean Microsoft IME or English (US) with Win + Space.";
    } else {
        title = L"Input mode unavailable";
        detail = L"This field's IME status cannot be read. Original keys are available.";
    }
    SetText(statusTitle, title);
    SetText(statusText, detail);
    SetText(contextText, preview ? L"Preview · keyboard remapping is not running" :
        conflict ? L"Your existing Dvorak.exe has been left running." : L"Changes are saved automatically.");
    const bool active = enabled && !conflict && !injectionError &&
        !(scrollBypass && scrollLockOn);
    if (displayedMode != context.mode || displayedActive != active) {
        displayedMode = context.mode;
        displayedActive = active;
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(koreanTitle, nullptr, TRUE);
        InvalidateRect(koreanCaption, nullptr, TRUE);
        InvalidateRect(englishTitle, nullptr, TRUE);
        InvalidateRect(englishCaption, nullptr, TRUE);
    }
    if (trayAdded) {
        swprintf_s(tray.szTip, L"KeyLayoutSwitch — %s", title);
        Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
}

void ToggleEnabled() {
    conflict = HasConflict();
    if (conflict) {
        enabled = false;
        MessageBoxW(window, L"Dvorak.exe is still running. Exit it from its tray menu first, then enable KeyLayoutSwitch. Your existing process has not been stopped.",
            L"Avoid applying Dvorak twice", MB_OK | MB_ICONINFORMATION);
    } else {
        enabled = !enabled;
        injectionError = 0;
    }
    SaveSettings();
    Refresh();
}

HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id, HFONT font = nullptr, DWORD ex = 0) {
    if (!wcscmp(type,L"STATIC")) style |= SS_NOPREFIX;
    HWND control = CreateWindowExW(ex, type, text, WS_CHILD | WS_VISIBLE | style,
        0,0,0,0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : regularFont), TRUE);
    if (childCount < 48) children[childCount++] = control;
    return control;
}

HFONT Font(int points, int weight) {
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

int ContentX(int width) { return 224 + (width-224-((width-304)>840 ? 840 : width-304))/2; }
int ContentWidth(int width) { return (width-304)>840 ? 840 : width-304; }

void Layout() {
    if (!heading) return;
    RECT bounds; GetClientRect(window,&bounds);
    const int width=MulDiv(bounds.right,96,dpi), height=MulDiv(bounds.bottom,96,dpi);
    const int x=ContentX(width), w=ContentWidth(width);
    auto move = [&](HWND c,int a,int b,int cw,int ch) { MoveWindow(c,S(a),S(b),S(cw),S(ch),TRUE); };
    auto visible = [](HWND c,bool value) { ShowWindow(c,value ? SW_SHOW : SW_HIDE); };
    for (int i=0;i<childCount;++i) visible(children[i],false);
    HWND common[] = {heading,subtitle,contextText,hideButton,quitButton};
    for (auto c:common) visible(c,true);
    for (int i=0;i<4;++i) {
        visible(navigation[i],true); Check(navigation[i],page==i);
        move(navigation[i],8,100+i*44,208,40);
    }
    const wchar_t* titles[] = {L"Keyboard",L"Typing test",L"Appearance",L"Setup guide"};
    const wchar_t* descriptions[] = {
        L"Two layouts. One familiar way to switch.",
        L"Try English Dvorak, then switch to Korean with your usual 한/영 key.",
        L"Make KeyLayoutSwitch feel at home on your desktop.",
        L"Connect 세벌식 390 and Dvorak in a few steps."
    };
    SetText(heading,titles[page]); SetText(subtitle,descriptions[page]);
    move(heading,x,26,w,40); move(subtitle,x,75,w,30);
    move(contextText,x,height-35,w,23);
    move(hideButton,8,height-128,208,40); move(quitButton,8,height-84,208,40);
    if (page==0) {
        HWND group[] = {statusTitle,statusText,enabledBox,koreanTitle,koreanCaption,englishTitle,englishCaption,
            preferencesHeading,shortcutsBox,scrollBox};
        for (auto c:group) visible(c,true);
        move(statusTitle,x+150,127,w-150,28); move(statusText,x+150,163,w-150,54);
        move(enabledBox,x,242,w,68);
        move(koreanTitle,x+60,363,w-230,23); move(koreanCaption,x+60,387,w-230,22);
        move(englishTitle,x+60,435,w-230,23); move(englishCaption,x+60,459,w-230,22);
        SetText(preferencesHeading,L"Shortcuts & behavior"); move(preferencesHeading,x,514,w,26);
        move(shortcutsBox,x,548,w,68); move(scrollBox,x,620,w,68);
    } else if (page==1) {
        visible(testPad,true); visible(testHint,true);
        SetText(testHint,L"Your typing stays here only while this app is open. Nothing is saved.");
        move(testHint,x,123,w,26);
        move(testPad,x+17,171,w-34,206);
        RECT editRect={S(3),S(4),S(w-43),S(198)};
        SendMessageW(testPad,EM_SETRECT,0,reinterpret_cast<LPARAM>(&editRect));
    } else if (page==2) {
        visible(startupBox,true); move(startupBox,x,336,w,68);
        for (int i=0;i<3;++i) {
            visible(themeChoices[i],true); Check(themeChoices[i],themePreference==i);
            move(themeChoices[i],x+w-288+i*86,190,80,34);
        }
    } else {
        visible(setupText,true); visible(settingsButton,true);
        move(setupText,x+60,166,w-88,56);
        move(settingsButton,x+60,235,210,34);
    }
    InvalidateRect(window,nullptr,FALSE);
}

void CreateControls() {
    regularFont=Font(10,FW_NORMAL); titleFont=Font(22,FW_SEMIBOLD);
    sectionFont=Font(11,FW_SEMIBOLD); smallFont=Font(10,FW_NORMAL); statusFont=Font(13,FW_SEMIBOLD);
    design::captionFont=smallFont;
    design::iconFont=CreateFontW(-S(19),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe MDL2 Assets");
    for (int i=0;i<4;++i) {
        navigation[i]=Control(L"BUTTON",navStyles[i].title,BS_RADIOBUTTON|WS_TABSTOP|(i==0 ? WS_GROUP : 0),NavKeyboard+i);
        design::Style(navigation[i],&navStyles[i]);
    }
    heading=Control(L"STATIC",L"Keyboard",SS_LEFT,-1,titleFont);
    subtitle=Control(L"STATIC",L"",SS_LEFT,-1);
    statusTitle=Control(L"STATIC",L"",SS_LEFT,-1,statusFont);
    statusText=Control(L"STATIC",L"",SS_LEFT,-1);
    contextText=Control(L"STATIC",L"",SS_LEFT,-1,smallFont);
    enabledBox=Control(L"BUTTON",masterStyle.title,BS_AUTOCHECKBOX|WS_TABSTOP|WS_GROUP,Enable);
    koreanTitle=Control(L"STATIC",L"세벌식 390",SS_LEFT,-1);
    koreanCaption=Control(L"STATIC",L"Korean · Microsoft IME",SS_LEFT,-1,smallFont);
    englishTitle=Control(L"STATIC",L"Dvorak",SS_LEFT,-1);
    englishCaption=Control(L"STATIC",L"English · US keyboard base",SS_LEFT,-1,smallFont);
    preferencesHeading=Control(L"STATIC",L"Shortcuts & behavior",SS_LEFT,-1,sectionFont);
    shortcutsBox=Control(L"BUTTON",shortcutsStyle.title,BS_AUTOCHECKBOX|WS_TABSTOP,Shortcuts);
    scrollBox=Control(L"BUTTON",scrollStyle.title,BS_AUTOCHECKBOX|WS_TABSTOP,ScrollBypass);
    startupBox=Control(L"BUTTON",startupStyle.title,BS_AUTOCHECKBOX|WS_TABSTOP,Startup);
    for (int i=0;i<3;++i) {
        themeChoices[i]=Control(L"BUTTON",themeStyles[i].title,BS_RADIOBUTTON|WS_TABSTOP|(i==0 ? WS_GROUP : 0),ThemeSystem+i);
        design::Style(themeChoices[i],&themeStyles[i]);
    }
    setupText=Control(L"STATIC",L"In Windows Settings, open Korean > Language options > Microsoft IME > Keyboard options > General.\r\nSelect 3 Beolsik 390 (세벌식 390).",SS_LEFT,-1);
    testHint=Control(L"STATIC",L"",SS_LEFT,-1,smallFont);
    testPad=Control(L"EDIT",L"",WS_TABSTOP|WS_GROUP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,TestPad);
    settingsButton=Control(L"BUTTON",settingsStyle.title,BS_PUSHBUTTON|WS_TABSTOP,WindowsSettings);
    hideButton=Control(L"BUTTON",hideStyle.title,BS_PUSHBUTTON|WS_TABSTOP,Hide);
    quitButton=Control(L"BUTTON",quitStyle.title,BS_PUSHBUTTON|WS_TABSTOP,Quit);
    design::Style(enabledBox,&masterStyle); design::Style(shortcutsBox,&shortcutsStyle);
    design::Style(scrollBox,&scrollStyle); design::Style(startupBox,&startupStyle);
    design::Style(settingsButton,&settingsStyle); design::Style(hideButton,&hideStyle); design::Style(quitButton,&quitStyle);
    Check(enabledBox,enabled); Check(shortcutsBox,preserveShortcuts); Check(scrollBox,scrollBypass);
    Check(startupBox,IsStartupEnabled());
    if (preview) {
        EnableWindow(enabledBox,FALSE); EnableWindow(shortcutsBox,FALSE);
        EnableWindow(scrollBox,FALSE); EnableWindow(startupBox,FALSE); EnableWindow(hideButton,FALSE);
    }
    Layout();
}

void PaintSurface(HDC target) {
    RECT bounds; GetClientRect(window,&bounds);
    design::Buffer buffer(target,bounds.right,bounds.bottom); HDC dc=buffer.dc;
    design::Fill(dc,bounds,design::Canvas);
    const int width=MulDiv(bounds.right,96,dpi),height=MulDiv(bounds.bottom,96,dpi);
    const int x=ContentX(width),w=ContentWidth(width);
    auto rect=[](int a,int b,int c,int d) { return RECT{S(a),S(b),S(a+c),S(b+d)}; };
    auto text=[&](const wchar_t* t,int a,int b,int c,int d,HFONT f,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
        design::Text(dc,t,rect(a,b,c,d),f,color,flags);
    };
    auto card=[&](int a,int b,int c,int d) { design::Round(dc,S(a)+0.5f,S(b)+0.5f,S(c)-1.0f,S(d)-1.0f,S(4),design::Surface,design::Border); };
    auto icon=[&](const wchar_t* glyph,int y) { text(glyph,x+20,y,25,68,design::iconFont,design::Ink); };
    design::Fill(dc,rect(223,0,1,height),design::Border);
    DrawIconEx(dc,S(20),S(26),appIcon,S(28),S(28),0,nullptr,DI_NORMAL);
    text(L"KeyLayoutSwitch",60,25,150,28,sectionFont,design::Ink);
    text(L"KEYBOARD UTILITY",20,70,185,20,smallFont,design::Muted);
    text(L"Version 1.4.0",24,height-33,180,20,smallFont,design::Muted);
    if (page==0) {
        design::Keyboard(dc,S(x),S(120),S(112),design::Accent);
        text(L"Layouts",x,326,w,24,sectionFont,design::Ink);
        card(x,352,w,68); card(x,424,w,68);
        text(L"한",x+19,352,28,68,statusFont,design::Ink);
        text(L"D",x+21,424,25,68,statusFont,design::Ink);
        const bool ko=displayedActive && displayedMode==dual::InputMode::Korean;
        const bool en=displayedActive && displayedMode==dual::InputMode::English;
        text(ko ? L"●  Active" : L"Windows IME",x+w-150,352,130,68,smallFont,ko ? design::Accent : design::Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        text(en ? L"●  Active" : L"English layout",x+w-150,424,130,68,smallFont,en ? design::Accent : design::Muted,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        card(x,692,w,58); text(L"\xE765",x+20,692,25,58,design::iconFont,design::Ink);
        text(L"Pause or resume",x+60,698,w-300,23,regularFont,design::Ink);
        text(preview ? L"Preview only" : hotkeyRegistered ? L"Available in any app" : L"Shortcut already in use",x+60,721,w-300,20,smallFont,design::Muted);
        const wchar_t* keys[]={L"Ctrl",L"Alt",L"F12"};
        for (int i=0;i<3;++i) {
            const int kx=x+w-180+i*52;
            design::Round(dc,S(kx),S(706),S(44),S(30),S(4),design::ControlFill,design::Border);
            text(keys[i],kx,706,44,30,smallFont,design::Ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    } else if (page==1) {
        card(x,154,w,240);
        if (GetFocus()==testPad) design::Fill(dc,rect(x+1,392,w-2,2),design::Accent);
        text(L"Dvorak reference",x,426,w,26,sectionFont,design::Ink);
        text(L"Letter and punctuation keys, in physical keyboard order",x,457,w,23,smallFont,design::Muted);
        const wchar_t* rows[]={L"' , . P Y F G C R L / =",L"A O E U I D H T N S -",L"; Q J K X B M W V Z"};
        const int key=42,gap=5;
        for (int r=0;r<3;++r) {
            int col=0;
            for (const wchar_t* p=rows[r];*p;++p) {
                if (*p==L' ') continue;
                const int kx=x+20+r*15+col++*(key+gap),ky=505+r*48;
                design::Round(dc,S(kx),S(ky),S(key),S(40),S(5),design::Surface,design::Border);
                wchar_t label[]={*p,0}; text(label,kx,ky,key,40,regularFont,design::Ink,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
        }
        text(L"Korean composition is provided by Microsoft IME.",x,678,w,26,smallFont,design::Muted);
    } else if (page==2) {
        text(L"Personalization",x,126,w,26,sectionFont,design::Ink);
        card(x,164,w,86); icon(L"\xE790",173);
        text(L"App theme",x+60,179,w-380,24,regularFont,design::Ink);
        text(L"System follows your Windows appearance.",x+60,206,w-380,24,smallFont,design::Muted);
        text(L"Startup",x,298,w,26,sectionFont,design::Ink);
        text(L"About KeyLayoutSwitch",x,452,w,26,sectionFont,design::Ink);
        card(x,490,w,100); icon(L"\xE946",506);
        text(L"KeyLayoutSwitch",x+60,508,w-85,24,sectionFont,design::Ink);
        text(L"Version 1.4.0 · Running as administrator",x+60,540,w-85,24,smallFont,design::Muted);
        text(L"Appearance changes apply immediately and are saved automatically.",x,620,w,30,smallFont,design::Muted);
    } else {
        card(x,126,w,164);
        text(L"1",x+21,139,25,28,sectionFont,design::Accent);
        text(L"Choose Korean 390 in Windows",x+60,137,w-90,26,sectionFont,design::Ink);
        card(x,302,w,110);
        text(L"2",x+21,316,25,28,sectionFont,design::Accent);
        text(L"Select your keyboard base",x+60,315,w-90,26,sectionFont,design::Ink);
        text(L"Use Win + Space to select Korean Microsoft IME or English (US) — US.\nKeep the ordinary US/QWERTY base so Dvorak is applied once.",x+60,350,w-90,50,regularFont,design::Muted,DT_LEFT|DT_WORDBREAK);
        card(x,424,w,110);
        text(L"3",x+21,438,25,28,sectionFont,design::Accent);
        text(L"Enable KeyLayoutSwitch and start typing",x+60,437,w-90,26,sectionFont,design::Ink);
        text(L"Exit Dvorak.exe, then enable KeyLayoutSwitch on the Keyboard page.\nUse 한/영 to switch languages and check both in Typing test.",x+60,472,w-90,50,regularFont,design::Muted,DT_LEFT|DT_WORDBREAK);
        text(L"Works with your existing Windows IME",x,574,w,26,sectionFont,design::Ink);
        text(L"Microsoft IME handles Korean composition and Hanja. KeyLayoutSwitch remaps English keys.\nThe app cannot verify your 390 selection; check it in Windows settings.",x,613,w,66,regularFont,design::Muted,DT_LEFT|DT_WORDBREAK);
    }
}
void TrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MenuShow, L"Open KeyLayoutSwitch");
    AppendMenuW(menu, MF_STRING | (enabled ? MF_CHECKED : 0), MenuPause, L"Enable Dvorak");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuExit, L"Quit");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        window = hwnd;
        CreateControls();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && trayAdded) ShowWindow(hwnd, SW_HIDE);
        else Layout();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        PaintSurface(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {S(1080), S(840)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi = HIWORD(wParam);
        HFONT fonts[] = {regularFont, titleFont, sectionFont, smallFont, statusFont};
        regularFont = Font(10, FW_NORMAL); titleFont = Font(22, FW_SEMIBOLD);
        sectionFont = Font(11, FW_SEMIBOLD); smallFont = Font(10, FW_NORMAL); statusFont = Font(13, FW_SEMIBOLD);
        design::captionFont = smallFont;
        DeleteObject(design::iconFont);
        design::iconFont=CreateFontW(-S(19),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe MDL2 Assets");
        for (int i=0; i<childCount; ++i) {
            const auto oldFont = reinterpret_cast<HFONT>(SendMessageW(children[i], WM_GETFONT, 0, 0));
            HFONT next = oldFont == fonts[1] ? titleFont : oldFont == fonts[2] ? sectionFont :
                oldFont == fonts[3] ? smallFont : oldFont == fonts[4] ? statusFont : regularFont;
            SendMessageW(children[i], WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        }
        for (auto font : fonts) DeleteObject(font);
        const auto bounds = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, bounds->left, bounds->top, bounds->right-bounds->left,
            bounds->bottom-bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        const bool language = control==koreanTitle || control==koreanCaption || control==englishTitle || control==englishCaption;
        const bool inCard = language || control==setupText;
        const COLORREF fill = inCard ? design::Surface : design::Canvas;
        const bool caption = control==subtitle || control==statusText || control==contextText ||
            control==testHint || control==koreanCaption || control==englishCaption;
        SetTextColor(dc,caption ? design::Muted : design::Ink);
        SetBkColor(dc,fill);
        SetDCBrushColor(dc,fill);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, design::Ink);
        SetBkColor(dc, design::Surface);
        return reinterpret_cast<LRESULT>(cardBrush);
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case NavKeyboard: case NavTest: case NavAppearance: case NavSetup:
            page=LOWORD(wParam)-NavKeyboard; Layout(); SetFocus(navigation[page]); return 0;
        case ThemeSystem: case ThemeLight: case ThemeDark:
            themePreference=LOWORD(wParam)-ThemeSystem; SaveSettings(); ApplyTheme(); return 0;
        case Enable: case MenuPause: ToggleEnabled(); return 0;
        case Shortcuts: preserveShortcuts = IsChecked(shortcutsBox); SaveSettings(); return 0;
        case ScrollBypass: scrollBypass = IsChecked(scrollBox); SaveSettings(); Refresh(); return 0;
        case Startup:
            if (!SetStartup(IsChecked(startupBox)))
                MessageBoxW(hwnd, L"Windows could not update the sign-in setting.", L"Startup setting", MB_OK | MB_ICONERROR);
            Check(startupBox, IsStartupEnabled());
            return 0;
        case WindowsSettings:
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", L"ms-settings:regionlanguage", nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                MessageBoxW(hwnd, L"Open Windows Settings > Time & language > Language & region.", L"Language settings", MB_OK);
            return 0;
        case Hide:
            if (trayAdded) ShowWindow(hwnd, SW_HIDE);
            return 0;
        case MenuShow: ShowWindowFromTray(); return 0;
        case Quit: case MenuExit: quitting = true; DestroyWindow(hwnd); return 0;
        case TestPad:
            if (HIWORD(wParam)==EN_SETFOCUS || HIWORD(wParam)==EN_KILLFOCUS)
                InvalidateRect(hwnd,nullptr,FALSE);
            return 0;
        }
        break;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED:
        ApplyTheme(); return 0;
    case WM_HOTKEY:
        if (wParam == 1) ToggleEnabled();
        return 0;
    case RefreshMessage:
        SaveSettings(); Refresh(); return 0;
    case WM_TIMER: {
        static int tick = 0;
        if (++tick % 12 == 0) {
            conflict = HasConflict();
            if (conflict && enabled) { enabled = false; SaveSettings(); }
        }
        Refresh();
        return 0;
    }
    case TrayMessage:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) ShowWindowFromTray();
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) TrayMenu();
        return 0;
    case WM_CLOSE:
        if (trayAdded && !quitting) ShowWindow(hwnd, SW_HIDE);
        else DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        StopKeyboardThread();
        if (hotkeyRegistered) UnregisterHotKey(hwnd, 1);
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    default:
        if (message == RegisterWindowMessageW(L"TaskbarCreated") && !preview) { AddTray(); Refresh(); return 0; }
        if (message == RegisterWindowMessageW(L"DualLayout390.Show")) { ShowWindowFromTray(); return 0; }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE appInstance, HINSTANCE, PWSTR command, int show) {
    instance = appInstance;
    // Management command for graceful updates of an elevated instance. This
    // executable's administrator manifest supplies the matching privilege.
    if(!wcscmp(command,L"--quit")) {
        HWND existing=FindWindowW(WindowClass,nullptr);
        if(!existing) return 0;
        DWORD processId=0;
        GetWindowThreadProcessId(existing,&processId);
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);
        if(!process) return 9;
        wchar_t executable[MAX_PATH]; DWORD length=MAX_PATH;
        const bool known=QueryFullProcessImageNameW(process,0,executable,&length)!=FALSE;
        const wchar_t* filename=known ? wcsrchr(executable,L'\\') : nullptr;
        if(!filename || (_wcsicmp(filename+1,L"DualLayout390.exe") &&
            _wcsicmp(filename+1,L"KeyLayoutSwitch.exe"))) { CloseHandle(process); return 9; }
        const bool requested=PostMessageW(existing,WM_COMMAND,Quit,0)!=FALSE;
        const bool stopped=requested && WaitForSingleObject(process,5000)==WAIT_OBJECT_0;
        CloseHandle(process);
        return stopped ? 0 : 9;
    }
    preview = wcsstr(command, L"--preview") != nullptr;
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 7;
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,0,nullptr);
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    if(!preview && (wcsstr(command,L"--install-startup") || startup::LegacyEnabled(executable))) {
        const HRESULT result=startup::Set(true,executable);
        if(FAILED(result)) {
            wchar_t message[256];
            swprintf_s(message,L"Windows could not configure administrator sign-in startup (0x%08X).",static_cast<unsigned>(result));
            MessageBoxW(nullptr,message,L"KeyLayoutSwitch startup",MB_OK|MB_ICONERROR);
            CoUninitialize(); return 8;
        }
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, preview ? L"Local\\DualLayout390.Preview" : L"Local\\DualLayout390.Singleton");
    if (!mutex) { CoUninitialize(); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(WindowClass, nullptr);
        if (existing) PostMessageW(existing, RegisterWindowMessageW(L"DualLayout390.Show"), 0, 0);
        CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    dpi = GetDpiForSystem();
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput graphicsInput;
    if (Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr) != Gdiplus::Ok) {
        CloseHandle(mutex);
        CoUninitialize();
        return 6;
    }
    if (!preview) InitSettings();
    conflict = HasConflict();
    if (conflict) enabled = false;
    ApplyTheme();
    appIcon = MakeIcon();
    WNDCLASSEXW type = {};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hIcon = type.hIconSm = appIcon;
    type.hbrBackground = background;
    type.lpszClassName = WindowClass;
    if (!RegisterClassExW(&type)) return 2;
    RECT rect = {0, 0, S(1160), S(810)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    window = CreateWindowExW(0, WindowClass, L"KeyLayoutSwitch", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right-rect.left, rect.bottom-rect.top,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 3;
    // Windows 11 rounds the system-managed frame; Windows 10 ignores this.
    const DWORD cornerPreference = 2;
    DwmSetWindowAttribute(window,33,&cornerPreference,sizeof(cornerPreference));
    ApplyTheme();
    if (!preview) {
        if (!StartKeyboardThread()) {
            MessageBoxW(window, L"Windows could not install the keyboard hook. The app will close without changing input.", L"KeyLayoutSwitch", MB_OK | MB_ICONERROR);
            DestroyWindow(window);
            CloseHandle(mutex);
            return 4;
        }
        hotkeyRegistered = RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12) != FALSE;
        AddTray();
    }
    Refresh();
    SetTimer(window, 1, 300, nullptr);
    if (!wcsstr(command, L"--tray") || !trayAdded) {
        ShowWindow(window, show);
        UpdateWindow(window);
    }
    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DeleteObject(regularFont); DeleteObject(titleFont); DeleteObject(sectionFont);
    DeleteObject(smallFont); DeleteObject(statusFont); DeleteObject(background);
    DeleteObject(cardBrush);
    DeleteObject(design::iconFont);
    DestroyIcon(appIcon);
    CloseHandle(mutex);
    CoUninitialize();
    Gdiplus::GdiplusShutdown(graphicsToken);
    return result < 0 ? 5 : static_cast<int>(message.wParam);
}
