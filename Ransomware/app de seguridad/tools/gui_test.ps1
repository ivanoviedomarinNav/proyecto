# Prueba de extremo a extremo de los dos .exe (interfaz incluida).
# Uso: powershell -ExecutionPolicy Bypass -File tools\gui_test.ps1

param([string]$Password = 'ivan123jjqq')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Native {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h, string text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder text, int max);
}
"@

function Wait-ClassWindow([string]$className, [string]$title, [int]$seconds = 15) {
    $limit = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $limit) {
        $window = [Native]::FindWindow($className, $title)
        if ($window -ne [IntPtr]::Zero) { return $window }
        Start-Sleep -Milliseconds 150
    }
    throw "No apareció la ventana de clase '$className'."
}

function Window-Title([IntPtr]$window) {
    $text = New-Object System.Text.StringBuilder 256
    [Native]::GetWindowText($window, $text, 256) | Out-Null
    return $text.ToString()
}

function Click-Button([IntPtr]$window, [int]$id) {
    $button = [Native]::GetDlgItem($window, $id)
    if ($button -eq [IntPtr]::Zero) { throw "No se encontró el botón $id." }
    [Native]::PostMessage($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # BM_CLICK
}

function Wait-Window([System.Diagnostics.Process]$process, [int]$seconds = 15) {
    $limit = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $limit) {
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { return $process.MainWindowHandle }
        Start-Sleep -Milliseconds 150
    }
    throw "La ventana no apareció."
}

function Wait-Dialog([string]$title, [int]$seconds = 60) {
    $limit = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $limit) {
        $dialog = [Native]::FindWindow("#32770", $title)
        if ($dialog -ne [IntPtr]::Zero) { return $dialog }
        Start-Sleep -Milliseconds 150
    }
    throw "No apareció el cuadro de resultado de '$title'."
}

function Close-Dialog([IntPtr]$dialog) {
    # Los cuadros de Windows 11 no exponen un botón hijo: se cierran con WM_CLOSE,
    # que equivale a pulsar Aceptar.
    [Native]::PostMessage($dialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $limit = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $limit -and [Native]::IsWindow($dialog)) { Start-Sleep -Milliseconds 100 }
}

function Click-Accept([IntPtr]$window) {
    $button = [Native]::GetDlgItem($window, 1001)
    if ($button -eq [IntPtr]::Zero) { throw "No se encontró el botón." }
    [Native]::PostMessage($button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # BM_CLICK
}

function Set-Field([IntPtr]$window, [string]$text) {
    $edit = [Native]::GetDlgItem($window, 1002)
    if ($edit -eq [IntPtr]::Zero) { throw "No se encontró la caja de texto." }
    # Escribir como un usuario real: WM_SETTEXT desde otro proceso no actualiza
    # el búfer interno de un campo de contraseña.
    [Native]::SetFocus($edit) | Out-Null
    foreach ($character in $text.ToCharArray()) {
        [Native]::SendMessage($edit, 0x0102, [IntPtr][int]$character, [IntPtr]1) | Out-Null
    }
    return $edit
}

function Magic([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return "<no existe>" }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 8) { return "<corto>" }
    return -join [char[]]$bytes[0..7]
}

$desktop = [Environment]::GetFolderPath('Desktop')
$samples = @(
    'documentos importantes\Contrato de alquiler.txt',
    'documentos importantes\2026\Certificado medico.txt',
    'proyectos personales\Presupuesto 2026.csv',
    'datos privados\Diario personal.txt'
)
$folderNames = 'documentos importantes', 'proyectos personales', 'datos privados'
$failures = 0

function Check([bool]$condition, [string]$name) {
    if ($condition) {
        Write-Host "  [OK]   $name" -ForegroundColor Green
    } else {
        Write-Host "  [FALLA] $name" -ForegroundColor Red
        $script:failures++
    }
}

$encryptProcess = $null
$cancelProcess = $null
$decryptProcess = $null
try {
    Write-Host "`n1) Abriendo Cifrar.exe y revisando la ventanita previa..." -ForegroundColor Cyan
    $encryptProcess = Start-Process -FilePath ".\dist\Cifrar.exe" -PassThru
    $popup = Wait-ClassWindow 'SeguroDestinationWindow' 'Cifrar mis carpetas'
    Check ($popup -ne [IntPtr]::Zero) "apareció la ventanita Destination Folder"
    Check ((Window-Title $popup) -eq 'Cifrar mis carpetas') "la ventanita tiene el título de la aplicación"
    Check ([Native]::GetDlgItem($popup, 2001) -ne [IntPtr]::Zero) "la ventanita tiene el botón OK"
    Check ([Native]::GetDlgItem($popup, 2002) -ne [IntPtr]::Zero) "la ventanita tiene el botón Cancelar"
    Write-Host "   Pulsando OK..." -ForegroundColor DarkGray
    Click-Button $popup 2001

    $encryptWindow = Wait-ClassWindow 'SeguroPersonalWindow' 'Cifrar mis carpetas'
    Check ($encryptWindow -ne [IntPtr]::Zero) "después de la ventanita apareció la ventana principal"
    Check ((Window-Title $encryptWindow) -eq 'Cifrar mis carpetas') "la ventana de cifrado abrió"
    Check ([Native]::FindWindowEx($encryptWindow, [IntPtr]::Zero, "EDIT", $null) -ne [IntPtr]::Zero) "la caja de texto existe"

    Write-Host "   Pulsando Cifrar ahora..." -ForegroundColor DarkGray
    Click-Accept $encryptWindow
    $dialog = Wait-Dialog 'Cifrar mis carpetas'
    Check ($dialog -ne [IntPtr]::Zero) "apareció el mensaje de resultado"
    Close-Dialog $dialog

    Start-Sleep -Milliseconds 500

    Write-Host "`n2) Verificando el cifrado en el Escritorio..." -ForegroundColor Cyan
    foreach ($relative in $samples) {
        $full = Join-Path $desktop $relative
        Check ((Magic $full) -eq 'SGENC001') "$relative quedó cifrado"
    }
    foreach ($name in $folderNames) {
        Check (Test-Path -LiteralPath (Join-Path $desktop "$name\.seguro")) "$name conserva su registro"
        Check (Test-Path -LiteralPath (Join-Path $desktop $name)) "$name sigue existiendo como carpeta"
    }

    # Texto visible al abrir con el Bloc de notas: debe ser basura, no el texto original.
    $plain = Get-Content -LiteralPath (Join-Path $desktop $samples[0]) -Raw -ErrorAction SilentlyContinue
    Check ($plain -notmatch 'CONTRATO') "el contenido ya no es legible como texto"

    [Native]::PostMessage($encryptWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # WM_CLOSE
    $encryptProcess.WaitForExit(5000) | Out-Null

    Write-Host "`n3) Abriendo Descifrar.exe con la contraseña..." -ForegroundColor Cyan
    $decryptProcess = Start-Process -FilePath ".\dist\Descifrar.exe" -PassThru
    $decryptWindow = Wait-Window $decryptProcess
    Check ($decryptProcess.MainWindowTitle -eq 'Descifrar mis carpetas') "la ventana de descifrado abrió"

    Set-Field $decryptWindow $Password | Out-Null
    Click-Accept $decryptWindow
    $dialog = Wait-Dialog 'Descifrar mis carpetas'
    Check ($dialog -ne [IntPtr]::Zero) "apareció el mensaje de resultado"
    Close-Dialog $dialog

    Start-Sleep -Milliseconds 500

    Write-Host "`n4) Verificando el descifrado..." -ForegroundColor Cyan
    $first = Get-Content -LiteralPath (Join-Path $desktop $samples[0]) -Raw
    Check ($first -match 'CONTRATO DE ALQUILER') "el documento original volvió intacto"
    $nested = Get-Content -LiteralPath (Join-Path $desktop $samples[1]) -Raw
    Check ($nested -match 'CERTIFICADO MEDICO') "el documento anidado volvió intacto"
    foreach ($name in $folderNames) {
        Check (-not (Test-Path -LiteralPath (Join-Path $desktop "$name\.seguro"))) "$name ya no tiene registro"
        $temps = Get-ChildItem -LiteralPath (Join-Path $desktop $name) -Recurse -Filter *.sgtmp -ErrorAction SilentlyContinue
        Check ($temps.Count -eq 0) "$name no dejó archivos temporales"
    }

    [Native]::PostMessage($decryptWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $decryptProcess.WaitForExit(5000) | Out-Null

    Write-Host "`n5) Comprobando que Cancelar también abre la ventana principal..." -ForegroundColor Cyan
    $cancelProcess = Start-Process -FilePath ".\dist\Cifrar.exe" -PassThru
    $popup = Wait-ClassWindow 'SeguroDestinationWindow' 'Cifrar mis carpetas'
    Click-Button $popup 2002
    $cancelWindow = Wait-ClassWindow 'SeguroPersonalWindow' 'Cifrar mis carpetas'
    Check ($cancelWindow -ne [IntPtr]::Zero) "Cancelar también abrió la ventana principal"
    Check ((Window-Title $cancelWindow) -eq 'Cifrar mis carpetas') "la ventana principal de Cancelar es la correcta"
    Check ([Native]::GetDlgItem($cancelWindow, 1001) -ne [IntPtr]::Zero) "la ventana principal conserva el botón de cifrar"
    Check ([Native]::GetDlgItem($cancelWindow, 1002) -ne [IntPtr]::Zero) "la ventana principal conserva la caja de texto"
    [Native]::PostMessage($cancelWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $cancelProcess.WaitForExit(5000) | Out-Null
}
finally {
    if ($encryptProcess -and -not $encryptProcess.HasExited) { Stop-Process -Id $encryptProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($cancelProcess -and -not $cancelProcess.HasExited) { Stop-Process -Id $cancelProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($decryptProcess -and -not $decryptProcess.HasExited) { Stop-Process -Id $decryptProcess.Id -Force -ErrorAction SilentlyContinue }
}

Write-Host ""
if ($failures -eq 0) {
    Write-Host "RESULTADO: TODO CORRECTO" -ForegroundColor Green
    exit 0
}
Write-Host "RESULTADO: $failures FALLO(S)" -ForegroundColor Red
exit 1
