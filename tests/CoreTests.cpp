#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/KeyboardCore.h"
#include "../src/InputContext.h"

namespace {
int checks = 0, failures = 0;
void Expect(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; printf("FAIL: %s\n", name); }
}
dual::Policy English() {
    dual::Policy policy;
    policy.enabled = true;
    policy.mode = dual::InputMode::English;
    return policy;
}

void LayoutTests() {
    // Independent reference: output of each physical US-QWERTY typing row.
    const char* rows[] = {"[]", "',.pyfgcrl/=", "aoeuidhtns-", ";qjkxbmwvz"};
    const uint16_t scans[][12] = {
        {0x0c,0x0d,0},
        {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b},
        {0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0},
        {0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0}
    };
    HKL us = LoadKeyboardLayoutW(L"00000409", 0); // Not activated; active input stays unchanged.
    Expect(us != nullptr, "US keyboard reference available");
    for (int row=0; row<4; ++row) for (size_t i=0; i<strlen(rows[row]); ++i) {
        const auto mapped = dual::DvorakScan(scans[row][i]);
        const auto result = mapped ? mapped : scans[row][i];
        const auto expected = static_cast<UINT>(VkKeyScanExA(rows[row][i], us) & 0xff);
        Expect(MapVirtualKeyExW(result, MAPVK_VSC_TO_VK_EX, us) == expected, "Dvorak physical row matches reference");
    }
    Expect(sizeof(dual::mappings)/sizeof(dual::mappings[0]) == 33, "All 33 changed positions present");
    for (const auto& mapping : dual::mappings) {
        Expect(mapping.source != mapping.target, "Only changed positions mapped");
        Expect(mapping.target > 0 && mapping.target < 128, "Target is a normal scan code");
        for (const auto& other : dual::mappings) if (&mapping != &other) {
            Expect(mapping.source != other.source, "Sources are unique");
            Expect(mapping.target != other.target, "Destinations are unique");
        }
    }
    const uint16_t unchanged[] = {0x01,0x02,0x0e,0x0f,0x1c,0x1d,0x1e,0x2a,0x32,0x36,0x38,0x39,0x3a,0x45,0x46,0x57,0x58};
    for (auto scan : unchanged)
        Expect(dual::DvorakScan(static_cast<uint16_t>(scan)) == 0, "Control and unchanged keys pass through");
}

void PolicyTests() {
    auto policy = English();
    Expect(dual::ShouldMap(policy), "Enabled English maps");
    const dual::InputMode passthrough[] = {dual::InputMode::Korean, dual::InputMode::Other, dual::InputMode::Unknown};
    for (const auto mode : passthrough) {
        policy.mode = mode;
        Expect(!dual::ShouldMap(policy), "Korean, other and unavailable modes pass through");
    }
    policy = English(); policy.enabled = false;
    Expect(!dual::ShouldMap(policy), "Paused passes through");
    policy = English(); policy.shortcutHeld = true;
    Expect(!dual::ShouldMap(policy), "Ctrl/Alt/Win shortcuts pass through");
    policy.preserveShortcuts = false;
    Expect(dual::ShouldMap(policy), "Optional Dvorak shortcuts supported");
    policy = English(); policy.scrollBypass = true;
    Expect(!dual::ShouldMap(policy), "Scroll Lock bypasses");
    policy = English(); policy.extended = true;
    Expect(!dual::ShouldMap(policy), "Extended navigation keys pass through");
}

void LifecycleTests() {
    for (const auto& mapping : dual::mappings) {
        dual::KeyRouter router;
        auto policy = English();
        Expect(router.Down(mapping.source, policy) == mapping.target, "First down maps");
        policy.enabled = false; policy.mode = dual::InputMode::Korean; policy.shortcutHeld = true;
        Expect(router.Down(mapping.source, policy) == mapping.target, "Repeat retains route after mode/pause/modifier change");
        Expect(router.Up(mapping.source, false) == mapping.target, "Up always releases translated target");
        Expect(router.Up(mapping.source, false) == 0, "Orphan up passes through");
        Expect(router.Down(mapping.source, policy) == 0, "Paused first down passes through");
        policy = English();
        Expect(router.Down(mapping.source, policy) == 0, "Passthrough hold stays passthrough after enable");
        Expect(router.Up(mapping.source, false) == 0, "Passthrough hold releases original key");
        Expect(router.Down(mapping.source, policy) == mapping.target, "Next fresh down uses new policy");
        router.Failed(mapping.source);
        Expect(router.Up(mapping.source, false) == 0, "Injection failure retains original key release");
    }
    dual::KeyRouter router;
    auto policy = English(); policy.extended = true;
    Expect(router.Down(0x35, policy) == 0, "Numpad divide isn't remapped");
    Expect(router.Up(0x35, true) == 0, "Extended key-up doesn't collide with slash");
    Expect(router.Down(0xffff, English()) == 0, "Out-of-range scan passes through");
    Expect(router.Up(0xffff, false) == 0, "Out-of-range release passes through");
}

void WindowsTests() {
    HWND test = CreateWindowExW(0, L"EDIT", L"", WS_POPUP | ES_MULTILINE, 0,0,100,100,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Expect(test != nullptr, "Create isolated hidden Windows input field");
    const auto context = dual::ReadContext(test);
    Expect(context.foreground == test, "Context retains requested window");
    Expect(context.layout == GetKeyboardLayout(0), "Reads correct thread keyboard layout");
    Expect(context.focused == test, "Falls back to supplied input field when no focus");
    DestroyWindow(test);
    Expect(dual::ReadContext(test).mode == dual::InputMode::Unknown, "Destroyed window passes through safely");
    Expect(dual::ReadContext(nullptr).mode == dual::InputMode::Unknown, "Missing foreground passes through safely");
    const auto foreground = dual::ReadContext();
    printf("Foreground IME smoke check: mode=%d, keyboard=%p, legacyQuery=%d\n",
        static_cast<int>(foreground.mode), foreground.layout, foreground.queriedIme);
}

void ContinuityTests() {
    using Mode = dual::InputMode;
    dual::ModeContinuity memory;
    auto resolve = [&](Mode mode,uint64_t time,bool failed=true,uintptr_t foreground=1,uintptr_t focus=2,uintptr_t layout=3) {
        return memory.Resolve(mode,foreground,focus,layout,time,failed);
    };
    Expect(resolve(Mode::Unknown,100)==Mode::Unknown,"No mode is invented before a successful query");
    Expect(resolve(Mode::English,100)==Mode::English,"Remember confirmed English mode");
    Expect(resolve(Mode::Unknown,112)==Mode::English,"Transient IME failure retains English conversion");
    Expect(resolve(Mode::Unknown,350)==Mode::English,"Bounded continuity includes deadline");
    Expect(resolve(Mode::Unknown,351)==Mode::Unknown,"Failures do not refresh continuity deadline");
    resolve(Mode::English,400);
    Expect(resolve(Mode::Korean,410)==Mode::Korean,"Authoritative Korean switch takes effect immediately");
    Expect(resolve(Mode::Unknown,420)==Mode::Korean,"Transient failure does not remap Korean");
    memory.Invalidate();
    Expect(resolve(Mode::Unknown,421)==Mode::Unknown,"Language switch invalidation removes stale state");
    resolve(Mode::English,500);
    Expect(resolve(Mode::Unknown,501,true,9)==Mode::Unknown,"Foreground change cannot inherit English");
    resolve(Mode::English,600);
    Expect(resolve(Mode::Unknown,601,true,1,9)==Mode::Unknown,"Focus change cannot inherit English");
    resolve(Mode::English,700);
    Expect(resolve(Mode::Unknown,701,true,1,2,9)==Mode::Unknown,"Layout change cannot inherit English");
    resolve(Mode::English,800);
    Expect(resolve(Mode::Unknown,801,false)==Mode::Unknown,"Missing IME is not a transient timeout");
    resolve(Mode::English,900);
    Expect(resolve(Mode::Other,901)==Mode::Other,"Another installed layout overrides cached English");
    Expect(resolve(Mode::Unknown,902)==Mode::Unknown,"Other layout cannot seed continuity");

    // Reproduce intermittent original letters with the user's example words.
    // Exercise the complete decision + scan routing path, without injecting keys.
    const char* expected="sometimes it skips the conversion to dvorak. you can see in what i typed here.";
    HKL us=LoadKeyboardLayoutW(L"00000409",0);
    dual::KeyRouter router;
    dual::ModeContinuity continuity;
    char before[128]={},after[128]={};
    for (size_t i=0;i<strlen(expected);++i) {
        const auto vk=static_cast<UINT>(VkKeyScanExA(expected[i],us)&0xff);
        const auto destination=static_cast<uint16_t>(MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,us));
        uint16_t source=destination;
        for (const auto& mapping:dual::mappings) if (mapping.target==destination) source=mapping.source;
        const bool timeout=i%5==3;
        const auto observed=timeout ? Mode::Unknown : Mode::English;
        auto policy=English(); policy.mode=observed;
        const auto oldScan=dual::ShouldMap(policy) && dual::DvorakScan(source) ? dual::DvorakScan(source) : source;
        policy.mode=continuity.Resolve(observed,1,2,3,1000+i*30,timeout);
        const auto mapped=router.Down(source,policy);
        const auto newScan=mapped ? mapped : source;
        const auto toChar=[&](uint16_t scan) {
            const UINT key=MapVirtualKeyExW(scan,MAPVK_VSC_TO_VK_EX,us);
            UINT c=MapVirtualKeyExW(key,MAPVK_VK_TO_CHAR,us);
            if (c>='A' && c<='Z') c+='a'-'A';
            return static_cast<char>(c);
        };
        before[i]=toChar(oldScan); after[i]=toChar(newScan);
        Expect(after[i]==expected[i],"Intermittent IME query failures do not corrupt rapid English typing");
        Expect(router.Up(source,false)==mapped,"Timeout recovery retains matching key release");
    }
    Expect(strcmp(before,expected)!=0,"Regression simulation reproduces original conversion skips");
    Expect(strcmp(after,expected)==0,"Regression sentence is exact after timeout recovery");
    printf("IME timeout regression before: %s\nIME timeout regression after:  %s\n",before,after);
}
}

int main() {
    LayoutTests(); PolicyTests(); LifecycleTests(); WindowsTests(); ContinuityTests();
    printf("%s: %d checks, %d failures. No keys sent; no input mode changed.\n",
        failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
