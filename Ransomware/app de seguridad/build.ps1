# Compila las dos aplicaciones en C++.
# Ejecutar desde PowerShell:  .\build.ps1

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsPath) {
    $candidates = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio','C:\Program Files\Microsoft Visual Studio' -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName '*\BuildTools' }
    $vsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools'
}
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "No se encontro vcvars64.bat. Instala 'Visual Studio Build Tools' con C++."
}

New-Item -ItemType Directory -Force -Path build, dist | Out-Null

# Si existe app.ico.png, se convierte automaticamente en src\app.ico.
if (Test-Path 'app.ico.png') {
    $iconScript = Join-Path $root 'tools\make_icon.ps1'
    if (Test-Path $iconScript) {
        & $iconScript -Source 'app.ico.png' -Destination 'src\app.ico'
    }
}

# Genera el archivo de recursos (icono e imagen son opcionales).
# En RC las barras invertidas se escapan, por eso se duplican.
$manifest = (Join-Path $root 'src\app.manifest').Replace('\', '\\')
$rc = @('#include <windows.h>', '#include "resource.h"', "1 24 `"$manifest`"")
if (Test-Path 'src\app.ico') {
    $icon = (Join-Path $root 'src\app.ico').Replace('\', '\\')
    $rc += "IDI_APP_ICON ICON `"$icon`""
}
if (Test-Path 'src\imagen.png') {
    $image = (Join-Path $root 'src\imagen.png').Replace('\', '\\')
    $rc += "IDR_APP_IMAGE RCDATA `"$image`""
}
Set-Content -Path 'build\app.rc' -Value $rc -Encoding ASCII

if (-not (Test-Path 'src\keys.h')) {
    Write-Host 'No existen claves todavia. Compilando build\keygen.exe...' -ForegroundColor Yellow
    $keygenBatch = @(
        '@echo off',
        "call `"$vcvars`" >nul",
        'if errorlevel 1 exit /b 1',
        'cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\keygen_crypto.obj" "src\crypto.cpp"',
        'if errorlevel 1 exit /b 1',
        'cl /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src" /Fo:"build\keygen_main.obj" "tools\keygen.cpp"',
        'if errorlevel 1 exit /b 1',
        'link /nologo /SUBSYSTEM:CONSOLE /OUT:"build\keygen.exe" build\keygen_crypto.obj build\keygen_main.obj bcrypt.lib',
        'if errorlevel 1 exit /b 1',
        'exit /b 0'
    )
    Set-Content -Path 'build\keygen_only.bat' -Value $keygenBatch -Encoding ASCII
    & cmd.exe /c 'build\keygen_only.bat'
    if ($LASTEXITCODE -ne 0) { throw "No se pudo compilar keygen.exe." }
    Write-Host ''
    Write-Host 'Ahora ejecuta:  build\keygen.exe   (escribe tu contrasena y pulsa Enter)' -ForegroundColor Yellow
    Write-Host 'Despues vuelve a ejecutar:  .\build.ps1' -ForegroundColor Yellow
    exit 0
}
if (-not (Test-Path 'keys\private_wrapped.bin')) {
    throw "Falta keys\private_wrapped.bin (la llave privada). Sin ella Descifrar.exe no puede descifrar."
}

# La llave privada solo se incrusta en Descifrar.exe, nunca en Cifrar.exe.
$privateKey = (Join-Path $root 'keys\private_wrapped.bin').Replace('\', '\\')
$privateRc = @('#include <windows.h>', '#include "resource.h"', "IDR_PRIVATE_KEY RCDATA `"$privateKey`"")
Set-Content -Path 'build\private.rc' -Value $privateRc -Encoding ASCII

$common = '/nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I "src"'
$libs = 'bcrypt.lib comctl32.lib gdiplus.lib shlwapi.lib shell32.lib ole32.lib user32.lib gdi32.lib'

$lines = @(
    '@echo off',
    "call `"$vcvars`" >nul",
    'if errorlevel 1 exit /b 1',
    'rc /nologo /I "src" /fo "build\app.res" "build\app.rc"',
    'if errorlevel 1 exit /b 1',
    'rc /nologo /I "src" /fo "build\private.res" "build\private.rc"',
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\crypto.obj`" `"src\crypto.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\vault.obj`" `"src\vault.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\ui.obj`" `"src\ui.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\encrypt_main.obj`" `"src\encrypt_main.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\decrypt_main.obj`" `"src\decrypt_main.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\keygen_crypto.obj`" `"src\crypto.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\keygen_main.obj`" `"tools\keygen.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\test_crypto.obj`" `"src\crypto.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\test_vault.obj`" `"src\vault.cpp`"",
    'if errorlevel 1 exit /b 1',
    "cl $common /Fo:`"build\test_main.obj`" `"tools\test_vault.cpp`"",
    'if errorlevel 1 exit /b 1',
    "link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:`"dist\Cifrar.exe`" build\app.res build\crypto.obj build\vault.obj build\ui.obj build\encrypt_main.obj $libs",
    'if errorlevel 1 exit /b 1',
    "link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:`"dist\Descifrar.exe`" build\app.res build\private.res build\crypto.obj build\vault.obj build\ui.obj build\decrypt_main.obj $libs",
    'if errorlevel 1 exit /b 1',
    "link /nologo /SUBSYSTEM:CONSOLE /OUT:`"build\keygen.exe`" build\keygen_crypto.obj build\keygen_main.obj bcrypt.lib",
    'if errorlevel 1 exit /b 1',
    "link /nologo /SUBSYSTEM:CONSOLE /OUT:`"build\test_vault.exe`" build\private.res build\test_crypto.obj build\test_vault.obj build\test_main.obj $libs",
    'if errorlevel 1 exit /b 1',
    'exit /b 0'
)
Set-Content -Path 'build\build_cmds.bat' -Value $lines -Encoding ASCII

& cmd.exe /c 'build\build_cmds.bat'
if ($LASTEXITCODE -ne 0) {
    throw "La compilacion fallo (codigo $LASTEXITCODE). Revisa los mensajes de arriba."
}

Write-Host ''
Write-Host 'Compilacion correcta:' -ForegroundColor Green
Get-ChildItem dist\*.exe | ForEach-Object {
    '{0}  ({1:N0} KB)' -f $_.Name, ($_.Length / 1KB)
}

