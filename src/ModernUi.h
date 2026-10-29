#pragma once
#include <gdiplus.h>

// Lightweight Fluent-inspired presentation over real Win32 controls. Buttons
// keep their native keyboard handling and UI Automation checkbox semantics.
namespace design {
inline COLORREF Canvas, Surface, Ink, Muted, Border, Accent, AccentSoft, Hover, ControlFill, OnAccent;
inline bool dark = false;
inline void Theme(bool useDark) {
    dark = useDark;
    Canvas = dark ? RGB(32,32,32) : RGB(243,243,243);
    Surface = dark ? RGB(43,43,43) : RGB(251,251,251);
    Ink = dark ? RGB(245,245,245) : RGB(27,27,27);
    Muted = dark ? RGB(190,190,190) : RGB(96,96,96);
    Border = dark ? RGB(49,49,49) : RGB(225,225,225);
    Accent = dark ? RGB(96,205,255) : RGB(0,103,192);
    AccentSoft = dark ? RGB(38,57,66) : RGB(229,241,251);
    Hover = dark ? RGB(52,52,52) : RGB(246,246,246);
    ControlFill = dark ? RGB(57,57,57) : RGB(255,255,255);
    OnAccent = dark ? RGB(0,47,65) : RGB(255,255,255);
}
inline HFONT captionFont = nullptr;
inline HFONT iconFont = nullptr;

inline Gdiplus::Color Color(COLORREF value) {
    return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
}

class Buffer {
    HDC destination;
    HBITMAP bitmap;
    HGDIOBJ previous;
    int width, height;
public:
    HDC dc;
    Buffer(HDC target, int w, int h) : destination(target), width(w), height(h) {
        dc = CreateCompatibleDC(target);
        bitmap = CreateCompatibleBitmap(target, w > 0 ? w : 1, h > 0 ? h : 1);
        previous = SelectObject(dc, bitmap);
    }
    ~Buffer() {
        BitBlt(destination, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
};

inline void Fill(HDC dc, RECT bounds, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

inline void Round(HDC dc, float x, float y, float width, float height, float radius,
                  COLORREF fill, COLORREF outline, float stroke = 1.0f) {
    if (width <= 1 || height <= 1) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float diameter = radius * 2;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, diameter, diameter, 180, 90);
    path.AddArc(x + width-diameter, y, diameter, diameter, 270, 90);
    path.AddArc(x + width-diameter, y + height-diameter, diameter, diameter, 0, 90);
    path.AddArc(x, y + height-diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Color(fill));
    graphics.FillPath(&brush, &path);
    if (stroke > 0) {
        Gdiplus::Pen pen(Color(outline), stroke);
        graphics.DrawPath(&pen, &path);
    }
}

inline void Text(HDC dc, const wchar_t* text, RECT bounds, HFONT font, COLORREF color,
                 UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) {
    auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &bounds, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

inline void Keyboard(HDC dc, int x, int y, int size, COLORREF color) {
    const float unit = size / 48.0f;
    Round(dc, x, y, size, size, 12*unit, AccentSoft, AccentSoft, 0);
    Round(dc, x+8*unit, y+14*unit, 32*unit, 22*unit, 3*unit, Surface, color, 1.6f*unit);
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Color(color));
    for (int row=0; row<2; ++row) for (int column=0; column<5; ++column)
        graphics.FillRectangle(&brush, x+(12+column*5)*unit, y+(18+row*5)*unit, 2.5f*unit, 2.5f*unit);
    graphics.FillRectangle(&brush, x+18*unit, y+29*unit, 13*unit, 2.0f*unit);
}

struct Button {
    const wchar_t* title;
    const wchar_t* description = nullptr;
    bool toggle = false;
    bool compact = false;
    bool primary = false;
    const wchar_t* icon = nullptr;
    bool navigation = false;
    bool choice = false;
};

inline void PaintButton(HWND hwnd, HDC target, const Button& spec) {
    RECT bounds;
    GetClientRect(hwnd, &bounds);
    Buffer buffer(target, bounds.right, bounds.bottom);
    HDC dc = buffer.dc;
    const int dpi = GetDpiForWindow(hwnd);
    auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool hovering = GetPropW(hwnd, L"DualLayout390.Hover") != nullptr;
    const bool pressed = (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    const bool disabled = !IsWindowEnabled(hwnd);
    const bool focus = GetFocus() == hwnd && !(SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    Fill(dc, bounds, spec.choice || (!spec.toggle && !spec.navigation) ? Surface : Canvas);
    if (spec.navigation) {
        if (checked || hovering || pressed)
            Round(dc, 0, 0, bounds.right, bounds.bottom, scale(4),
                checked || pressed ? Surface : Hover, Border, 0);
        if (checked) Round(dc, scale(2), scale(12), scale(3), bounds.bottom-scale(24), scale(1), Accent, Accent, 0);
        RECT glyph = {scale(17),0,scale(41),bounds.bottom};
        if (spec.icon) Text(dc, spec.icon, glyph, iconFont, checked ? Accent : Ink);
        RECT label = {scale(53),0,bounds.right-scale(10),bounds.bottom};
        Text(dc, spec.title, label, font, disabled ? Muted : Ink);
        if (focus) { RECT r = bounds; InflateRect(&r,-scale(3),-scale(3)); DrawFocusRect(dc,&r); }
    } else if (spec.toggle) {
        if (!spec.compact)
            Round(dc, 0.5f, 0.5f, bounds.right-1.0f, bounds.bottom-1.0f, scale(4),
                hovering ? Hover : Surface, focus ? Accent : Border, focus ? 1.5f : 1.0f);
        const int switchWidth = scale(42), switchHeight = scale(22);
        const int switchX = bounds.right-scale(spec.compact ? 3 : 20)-switchWidth;
        const int switchY = (bounds.bottom-switchHeight)/2;
        if (!spec.compact) {
            if (spec.icon) {
                RECT glyph = {scale(20),0,scale(44),bounds.bottom};
                Text(dc,spec.icon,glyph,iconFont,disabled ? Muted : Ink);
            }
            RECT title = {scale(spec.icon ? 60 : 20),scale(12),switchX-scale(60),scale(34)};
            RECT description = {title.left,scale(34),title.right,bounds.bottom-scale(10)};
            Text(dc, spec.title, title, font, disabled ? Muted : Ink);
            if (spec.description) Text(dc, spec.description, description, captionFont, Muted);
            RECT state = {switchX-scale(51),0,switchX-scale(12),bounds.bottom};
            Text(dc,checked ? L"On" : L"Off",state,font,Muted,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        } else {
            RECT label = {0,0,switchX-scale(10),bounds.bottom};
            Text(dc, checked ? L"On" : L"Off", label, font, disabled ? Muted : Ink, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        COLORREF track = disabled ? ControlFill : checked ? Accent : ControlFill;
        Round(dc, static_cast<float>(switchX), static_cast<float>(switchY), static_cast<float>(switchWidth),
            static_cast<float>(switchHeight), switchHeight/2.0f, track, checked ? track : RGB(132,138,148), 1.0f);
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush thumb(Color(checked && !disabled ? OnAccent : Muted));
        const int thumbSize = scale(12);
        const int thumbX = switchX + (checked ? switchWidth-scale(5)-thumbSize : scale(5));
        graphics.FillEllipse(&thumb, thumbX, switchY+(switchHeight-thumbSize)/2, thumbSize, thumbSize);
        if (focus && spec.compact) {
            RECT focusRect = bounds;
            InflateRect(&focusRect, -1, -1);
            DrawFocusRect(dc, &focusRect);
        }
    } else {
        COLORREF fill = spec.primary ? Accent : spec.choice && checked ? AccentSoft :
            pressed || hovering ? Hover : ControlFill;
        Round(dc, 0.5f, 0.5f, bounds.right-1.0f, bounds.bottom-1.0f, scale(6), fill,
            focus || (spec.choice && checked) ? Accent : spec.primary ? fill : Border, focus ? 2.0f : 1.0f);
        RECT label = bounds;
        InflateRect(&label, -scale(10), 0);
        Text(dc, spec.title, label, font, disabled ? Muted : spec.primary ? OnAccent : Ink,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

inline LRESULT CALLBACK ButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR id, DWORD_PTR data) {
    const auto& spec = *reinterpret_cast<const Button*>(data);
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        PaintButton(hwnd, dc, spec);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_PRINTCLIENT: PaintButton(hwnd, reinterpret_cast<HDC>(wParam), spec); return 0;
    case WM_MOUSEMOVE:
        if (!GetPropW(hwnd, L"DualLayout390.Hover")) {
            SetPropW(hwnd, L"DualLayout390.Hover", reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(hwnd, L"DualLayout390.Hover");
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case BM_SETCHECK: case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_KEYDOWN: case WM_KEYUP: case WM_UPDATEUISTATE: {
        const auto result = DefSubclassProc(hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_NCDESTROY:
        RemovePropW(hwnd, L"DualLayout390.Hover");
        RemoveWindowSubclass(hwnd, ButtonProc, id);
        break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

inline void Style(HWND hwnd, const Button* spec) {
    SetWindowSubclass(hwnd, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(spec));
}
}
