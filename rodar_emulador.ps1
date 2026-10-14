# =============================================================================
# Script de Execucao Rapida - Emulador Desktop M5Stack CoreS3 (SDL2)
# =============================================================================
# Diagnóstico do Ouvido (opcional, defina antes de rodar):
#   $env:BICHO_VAD_DEBUG = "1"                 -> loga nível do microfone, piso e estado do VAD a 10 Hz
#   $env:BICHO_TEST_WAV = "C:\caminho\frase.wav" -> injeta um WAV 16 kHz mono 16 bit como se fosse o microfone
#   $env:BICHO_TEST_WAV_DELAY_MS = "30000"     -> atraso da injeção (padrão 20000 ms)
$ErrorActionPreference = "Stop"

Write-Host "[BICHO] Compilando e iniciando o Emulador M5Stack CoreS3 (SDL2 a 60 FPS)..." -ForegroundColor Cyan

# Garante o compilador MinGW e a DLL do SDL2 no PATH da sessao atual
$mingwBin  = Join-Path $env:USERPROFILE ".platformio\packages\toolchain-gccmingw32\bin"
$sdl2Dir   = if ($env:SDL2_DIR) { $env:SDL2_DIR } else { "C:\tools\SDL2-mingw\SDL2-2.30.10\i686-w64-mingw32" }
$sdl2Bin   = Join-Path $sdl2Dir "bin"
$pyScripts = Join-Path $env:LOCALAPPDATA "Programs\Python\Python311\Scripts"
if (-not (Test-Path $pyScripts)) {
    $pyCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($null -ne $pyCmd -and $null -ne $pyCmd.Source) {
        $candidateScripts = Join-Path (Split-Path $pyCmd.Source -Parent) "Scripts"
        if (Test-Path $candidateScripts) {
            $pyScripts = $candidateScripts
        }
    }
}

$env:PATH = "$mingwBin;$sdl2Bin;$pyScripts;$env:PATH"

# Inicia o rastreador facial da webcam em background (YuNet ONNX / OpenCV)
$trackerProc = $null
$trackerScript = Join-Path $PSScriptRoot "scripts\webcam_tracker.py"
if (Test-Path $trackerScript) {
    Write-Host "[BICHO] Iniciando rastreamento facial da Webcam em background (scripts/webcam_tracker.py)..." -ForegroundColor DarkCyan
    $trackerProc = Start-Process -FilePath "python" -ArgumentList "`"$trackerScript`"" -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
}

try {
    # Compila e executa o binario nativo
    python -m platformio run -e native_pc -t exec
} finally {
    if ($null -ne $trackerProc -and !$trackerProc.HasExited) {
        Stop-Process -Id $trackerProc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "[BICHO] Rastreador da Webcam encerrado." -ForegroundColor DarkGray
    }
}


