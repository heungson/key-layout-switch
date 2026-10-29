#pragma once
#include <windows.h>
#include <imm.h>
#include "KeyboardCore.h"

namespace dual {
// WM_IME_CONTROL command from the Windows IMM protocol; some SDK headers
// expose this only in immdev.h rather than the public imm.h.
inline constexpr WPARAM GetConversionMode = 0x0001;
struct Context {
    HWND foreground = nullptr;
    HWND focused = nullptr;
    HKL layout = nullptr;
    InputMode mode = InputMode::Unknown;
    bool queriedIme = false;
    bool queryFailed = false;
};

inline Context ReadContext(HWND foreground = GetForegroundWindow()) {
    Context context;
    context.foreground = foreground;
    DWORD thread = GetWindowThreadProcessId(foreground, nullptr);
    if (!thread) return context;
    GUITHREADINFO info = {};
    info.cbSize = sizeof(info);
    context.focused = GetGUIThreadInfo(thread, &info) && info.hwndFocus ? info.hwndFocus : foreground;
    // A focused child can belong to a different GUI thread from its frame.
    const DWORD focusedThread = GetWindowThreadProcessId(context.focused, nullptr);
    if (focusedThread) thread = focusedThread;
    context.layout = GetKeyboardLayout(thread);
    const auto layout = reinterpret_cast<UINT_PTR>(context.layout);
    const WORD language = LOWORD(layout);
    if (!context.layout) return context;

    // Only translate known US-QWERTY bases. An installed US-Dvorak layout
    // (F0020409/00010409) must never be transformed a second time.
    if (language == 0x0409 && (HIWORD(layout) == 0x0409 || HIWORD(layout) == 0)) {
        context.mode = InputMode::English;
        return context;
    }
    if (language != 0x0412) {
        context.mode = InputMode::Other;
        return context;
    }

    HWND ime = ImmGetDefaultIMEWnd(context.focused);
    if (!ime) return context;
    DWORD_PTR conversion = 0;
    // Korean native conversion is the distinction that matters, not merely
    // whether an IME is open. Bound cross-process work on the hook thread.
    if (!SendMessageTimeoutW(ime, WM_IME_CONTROL, GetConversionMode, 0,
            SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 12, &conversion)) {
        context.queryFailed = true;
        return context;
    }
    context.queriedIme = true;
    context.mode = (conversion & IME_CMODE_NATIVE) ? InputMode::Korean : InputMode::English;
    return context;
}
}
