param(
    [string]$CmakePath,
    [string]$BuildDir,
    [int]$Parallel
)

$start = Get-Date
& "$CmakePath" --build "$BuildDir" --target all --parallel $Parallel
$exitCode = $LASTEXITCODE
$elapsed = (Get-Date) - $start
$elapsedStr = '{0:D2}:{1:D2}.{2:D3}' -f $elapsed.Minutes, $elapsed.Seconds, $elapsed.Milliseconds
Write-Host "===== Build elapsed: $elapsedStr ====="
exit $exitCode
