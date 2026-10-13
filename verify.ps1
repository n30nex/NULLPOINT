param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$shipping = Join-Path $PSScriptRoot 'build/NULLPOINT.exe'
$diagnostic = Join-Path $PSScriptRoot 'build/NULLPOINT-tests.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('build/qa-' + [Guid]::NewGuid().ToString('N').Substring(0,8)) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
& (Join-Path $PSScriptRoot 'build.ps1') -OutputPath $shipping
& (Join-Path $PSScriptRoot 'build.ps1') -Diagnostics -OutputPath $diagnostic
function Run-Check([string]$Exe, [string[]]$Arguments) {
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $OutputDirectory -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw "Check timed out: $Arguments" }
    if ($process.ExitCode -ne 0) { throw "Check failed: $Arguments ($($process.ExitCode))" }
}
Run-Check $diagnostic @('--selftest')
foreach ($level in 'recruit','operative','veteran') {
    Run-Check $diagnostic @('--playtest',"--$level")
    Copy-Item -LiteralPath (Join-Path $OutputDirectory 'campaign.txt') -Destination (Join-Path $OutputDirectory "campaign-$level.txt") -Force
}
Run-Check $shipping @('--verify')
& python (Join-Path $PSScriptRoot 'tools/check_captures.py') $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Native capture check failed.' }
$game = Get-Item -LiteralPath $shipping
if ($game.Length -ge 96000) { throw 'Shipping executable exceeds the challenge limit.' }
Write-Output "PASS: shipping game $($game.Length) bytes; diagnostics, all campaigns, native rendering and captures passed."
Write-Output "Evidence: $OutputDirectory"
Get-FileHash -LiteralPath $shipping -Algorithm SHA256
