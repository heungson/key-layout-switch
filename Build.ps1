param(
    [string]$Zig = (Join-Path $PSScriptRoot '..\.tools\zig-x86_64-windows-0.15.2\zig.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build'),
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Zig)) {
    $installedZig = Get-Command zig -ErrorAction SilentlyContinue
    if (-not $installedZig) { throw 'Install Zig 0.15.2+ and supply -Zig <path>, or use CMake with Visual Studio. See README.md.' }
    $Zig = $installedZig.Source
}
$Zig = (Resolve-Path -LiteralPath $Zig).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$resourcePath = Join-Path $OutputDirectory 'app.res'
$applicationPath = Join-Path $OutputDirectory 'KeyLayoutSwitch.exe'
$testPath = Join-Path $OutputDirectory 'CoreTests.exe'
$engineTestPath = Join-Path $OutputDirectory 'EngineTests.exe'
$startupTestPath = Join-Path $OutputDirectory 'StartupTests.exe'
Push-Location $PSScriptRoot
try {
    & $Zig rc /c 65001 /fo $resourcePath app.rc
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    $compilerFlags = @('-std=c++17', '-O2', '-target', 'x86_64-windows-gnu', '-D_WIN32_WINNT=0x0a00', '-Wall', '-Wextra', '-Werror', '-Wno-unused-command-line-argument', '-fno-exceptions', '-fno-rtti', '-nostdlib++', '-static')
    $applicationLibraries = @('-lcomctl32','-limm32','-lshell32','-lole32','-loleaut32','-luuid','-ladvapi32','-lgdi32','-luser32','-lgdiplus','-ldwmapi')
    & $Zig c++ @compilerFlags -municode '-Wl,--subsystem,windows' src/App.cpp $resourcePath @applicationLibraries -o $applicationPath
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    # Check the produced PE image: a console subsystem creates an unwanted
    # terminal on elevated/manual and scheduled launches, even with a GUI entry.
    $imageStream = [IO.File]::OpenRead($applicationPath)
    $imageReader = [IO.BinaryReader]::new($imageStream)
    try {
        $imageStream.Position = 0x3c
        $peOffset = $imageReader.ReadInt32()
        $imageStream.Position = $peOffset + 24 + 68
        if ($imageReader.ReadUInt16() -ne 2) { throw 'Output is not a Windows GUI executable.' }
    }
    finally { $imageReader.Dispose() }
    Write-Output "Built: $applicationPath"
    if ($Test) {
        & $Zig c++ @compilerFlags tests/CoreTests.cpp -limm32 -luser32 -o $testPath
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        & $testPath
        if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
        & $Zig c++ @compilerFlags tests/EngineTests.cpp @applicationLibraries -o $engineTestPath
        if ($LASTEXITCODE -ne 0) { throw 'Engine test compilation failed.' }
        & $engineTestPath
        if ($LASTEXITCODE -ne 0) { throw 'Keyboard thread tests failed.' }
        & $Zig c++ @compilerFlags tests/StartupTests.cpp -lole32 -loleaut32 -luuid -ladvapi32 -o $startupTestPath
        if ($LASTEXITCODE -ne 0) { throw 'Startup test compilation failed.' }
        & $startupTestPath
        if ($LASTEXITCODE -ne 0) { throw 'Startup definition tests failed.' }
    }
}
finally { Pop-Location }
