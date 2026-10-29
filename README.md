# KeyLayoutSwitch

KeyLayoutSwitch is the renamed successor to Dual Layout 390. It is a general Windows keyboard layout utility; the current release supports Korean Sebeolsik 390 with English Dvorak. Other layout pairs are not configurable yet.

[Download the Windows x64 executable](https://github.com/heungson/key-layout-switch/raw/refs/heads/main/dist/KeyLayoutSwitch.exe)

The compiled executable and its SHA-256 checksum are included in `dist/`. Source code and build instructions are below.

The executable is now `KeyLayoutSwitch.exe`. Existing settings, single-instance identifiers, and the `DualLayout390-<your user SID>` scheduled task name are retained for upgrade compatibility. Quit the old app before using the renamed executable, and use `KeyLayoutSwitch.exe --install-startup` to update sign-in startup to its new path.

A native, portable **C++ / Win32** Windows desktop application that combines **세벌식 390 Korean** with **US Dvorak English**. No AutoHotkey, .NET, Python, installation, or driver is needed to run the executable.

Version 1.2 uses a PowerToys-style settings shell: persistent sidebar navigation, compact full-width settings rows, Windows icons, and separate Keyboard, Typing test, Appearance, and Setup guide pages. Appearance offers System, Light, and Dark themes, applied immediately to the content, controls, text field, and title bar. System is the default and responds to Windows theme changes. The native typing area includes a Dvorak key reference. The multi-resolution keyboard icon is embedded in the executable; its vector source is in `assets\KeyLayoutSwitch.svg` and can be regenerated with `tools\MakeIcon.ps1`.

Version 1.2.1 fixes intermittent original letters during English typing: a failed IME query can reuse a confirmed mode for at most 250 ms within the same foreground window, focused control, and keyboard layout. Language-switch keys clear that state. A dedicated keyboard thread keeps the hook responsive while the settings window draws, opens menus, or saves options.

Version 1.3 always requests administrator privileges on manual launch so it can remap keys in administrator terminals. Sign-in startup uses a per-user Windows scheduled task with highest privileges and an interactive logon trigger, starting `--tray` five seconds after sign-in. Once approved and registered, the task launches elevated without a UAC prompt at each sign-in. Existing Run startup is migrated only after the task is successfully registered and verified.

Version 1.3.1 corrects the executable subsystem to Windows GUI, preventing the empty console window on manual and elevated sign-in launches. The build script verifies the PE subsystem after linking and fails if the executable is marked as a console application.

## Start using it

1. Run `build\KeyLayoutSwitch.exe` and approve the Windows UAC prompt. The first launch is paused.
2. Exit your old **Dvorak.exe** using its tray menu. KeyLayoutSwitch detects that process and refuses to enable while it is running; it never stops that process itself.
3. In Windows Settings → Time & language → Language & region → Korean → Language options → Microsoft IME → Keyboard options → General, select **3 Beolsik 390 / 세벌식 390**. The exact navigation labels can vary by Windows version. The app's **Open language settings** button opens the starting page.
4. Select **Korean Microsoft IME** using Win + Space. Keep the IME's ordinary US/QWERTY English base; do not select Windows' separate Dvorak keyboard at the same time.
5. Turn on the main **On / Off** switch. Use your normal **한/영** key to alternate between Korean 390 and English Dvorak. Try both in the app's text box and in your usual applications.

The utility provides the English remapping; **Microsoft IME still provides Korean 390 composition and Hanja**. It does not install its own Korean IME or select 390 for you. Using the IME's composition preserves normal syllable editing, candidates, and application integration.

## Controls

| Control | Behavior |
| --- | --- |
| Main On / Off switch | Apply Dvorak only when the focused field reports English and the keyboard base is Korean IME or US QWERTY. |
| Keep QWERTY shortcuts | Ctrl, Alt and Win combinations retain their familiar physical keys. Turn off for Dvorak shortcut positions. Shift and Caps Lock always work normally. |
| Scroll Lock bypass | When on, Scroll Lock on temporarily restores original keys. Your existing Right Ctrl → Scroll Lock utility can remain in use. |
| Ctrl + Alt + F12 | Pause or resume, unless another program already owns this shortcut. The window reports if it is unavailable. |
| App theme (Appearance) | Choose System, Light, or Dark. Saved in the Appearance section of settings.ini; System follows Windows. |
| Launch at sign-in | Start as administrator through a scheduled task for the current user. Turn off before moving/deleting the executable. |
| Close / minimize / Hide to tray | Keep the utility running in the notification area. Click its icon to reopen, or right-click for controls. |
| Quit | Release translated keys, remove the hook, and exit. |

The other existing utilities for 한/영, Hanja and volume can remain running. Avoid other letter-remapping utilities, including the `.ahk` version of Dvorak: arbitrary scripts cannot all be detected by process name.

Options are saved to `%LOCALAPPDATA%\DualLayout390\settings.ini`. The checkbox for sign-in startup manages only the `DualLayout390-<your user SID>` scheduled task in the root Task Scheduler folder. It runs as your signed-in account with highest privileges, uses no stored password, allows battery operation, and has no execution time limit. The obsolete `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\DualLayout390` value is removed after successful setup. Run `KeyLayoutSwitch.exe --install-startup` to enable or repair elevated startup. No Windows language settings are written. There is no network communication, clipboard access, or typed-text logging. The test box is not saved.

## Compatibility and limits

- Windows 10/11, x64. The executable requests administrator privileges for both ordinary and administrator applications. Windows can still block replacement input into protected applications; the utility pauses when `SendInput` fails. The secure desktop and sign-in screen are outside its scope.
- The app uses a low-level keyboard hook and injected physical scan codes. Applications that consume raw input, some games, remote sessions and custom editors may behave differently.
- Automatic mode detection reads the focused input window's Korean IME conversion mode, with a bounded 12 ms query. Korean/native mode passes through, English mode maps to Dvorak. Unknown and other keyboard layouts pass through. A brief IME query failure can use the last confirmed mode for up to 250 ms in the exact same input context; repeated failures do not extend that deadline. Focus/layout changes and language-switch keys invalidate it. Some TSF-only applications do not expose reliable legacy IME state; check Korean/English switching in the applications you use. The displayed status describes what the API reports, and is not proof of every editor's actual state.
- The 390 selection cannot be reliably inferred from the per-window keyboard ID. The Korean status says “Windows IME” instead of claiming to have verified 390.
- Independently installed US-Dvorak and other non-US-base layouts pass through to avoid double conversion. Choose Korean Microsoft IME or English (United States) **US** for this utility.
- Keys injected by other software pass through. No keyboard driver or system-wide input-method registration is installed.

## Build and test

From PowerShell in this application folder, with Zig 0.15.2 or newer:

```powershell
.\Build.ps1 -Zig C:\path\to\zig.exe -Test
```

The script also finds Zig on PATH, or a sibling `.tools\zig-x86_64-windows-0.15.2\zig.exe`. It compiles resources and a statically linked x64 Windows GUI executable to `build`. The generated app needs only Windows system DLLs, and does not require the build toolchain.

Alternatively, with Visual Studio Build Tools, Windows SDK and CMake:

```powershell
cmake -S . -B build-msvc -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Tests check the complete Dvorak reference rows, unchanged keys, mode and modifier policies, extended-key separation, repeat and release handling across mode/focus/pause changes, injection failure recovery, and actual Windows input-context queries against a private hidden input field. Regression tests inject simulated IME query failures into a complete reference sentence and verify exact output and matching releases. A separate worker test confirms that keyboard processing continues while the main thread is blocked, then shuts down cleanly. The tests do not send keyboard input, change the active input mode, or stop existing utilities. Startup definition tests verify the current account, highest privileges, interactive logon, exact executable and tray arguments, battery behavior, and unlimited runtime without registering test tasks. Physical typing and application-specific IME compatibility require hands-on validation.

`--quit` requests a graceful exit of the existing elevated instance for updates. `--tray` starts in the tray. `--preview` opens a UI preview without installing a keyboard hook, registering hotkeys, adding a tray icon, or loading/saving preferences; keyboard and startup switches are disabled in that mode. Theme switching and navigation remain available for visual checks; preview theme choices are not saved. `Build.ps1 -OutputDirectory <path>` can compile a separate preview without replacing a running executable.

## Implementation references

- [Microsoft Korean IME](https://learn.microsoft.com/en-us/globalization/input/korean-ime)
- [LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
- [SendInput and privilege restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- [KEYBDINPUT scan codes](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput)
- [Default IME window and WM_IME_CONTROL](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetdefaultimewnd)
- [IME native conversion mode](https://learn.microsoft.com/en-us/windows/win32/intl/ime-conversion-mode-values)
