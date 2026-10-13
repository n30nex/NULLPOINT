param([string]$OutputPath = (Join-Path $PSScriptRoot 'NULLPOINT.exe'))
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & gcc nullpoint.c -o $OutputPath -std=c99 -Os -s -mwindows -ffunction-sections -fno-asynchronous-unwind-tables -fno-ident -static-libgcc '-Wl,--gc-sections,--no-insert-timestamp' -lopengl32 -lgdi32 -lwinmm -lm
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    $game = Get-Item -LiteralPath $OutputPath
    if ($game.Length -ge 96000) { throw "Size limit exceeded: $($game.Length) bytes" }
    Write-Output "NULLPOINT.exe: $($game.Length) bytes / 96,000 byte limit"
    Get-FileHash -LiteralPath $game.FullName -Algorithm SHA256
} finally { Pop-Location }
