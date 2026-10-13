param([string]$OutputPath, [switch]$Diagnostics)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & python tools/pack_shaders.py
    if ($LASTEXITCODE -ne 0) { throw 'Shader packaging failed.' }
    $candidate = Join-Path $PSScriptRoot 'build/candidate.exe'
    $extra = @()
    if ($Diagnostics) { $extra = @('-DNULLPOINT_TESTS') }
    if (-not $OutputPath) { $OutputPath = Join-Path $PSScriptRoot $(if ($Diagnostics) { 'build/NULLPOINT-tests.exe' } else { 'NULLPOINT.exe' }) }
    & gcc nullpoint.c -o $candidate @extra -std=c99 -Oz -s -mwindows -ffunction-sections -fno-asynchronous-unwind-tables -fno-ident -fsingle-precision-constant -static-libgcc '-Wl,--gc-sections,--no-insert-timestamp' -lopengl32 -lgdi32 -lwinmm -lcabinet -lm
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    $game = Get-Item -LiteralPath $candidate
    if (-not $Diagnostics -and $game.Length -ge 96000) { throw "Size limit exceeded: $($game.Length) bytes" }
    Copy-Item -LiteralPath $candidate -Destination $OutputPath -Force
    Write-Output "$(if ($Diagnostics) { 'Developer checks' } else { 'NULLPOINT.exe' }): $($game.Length) bytes"
    Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
} finally { Pop-Location }
