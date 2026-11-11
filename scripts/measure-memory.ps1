# Samples an already-running Windows desktop app without changing or stopping it.
# Prefer the actual InNavi-Win64-Shipping process over the package's bootstrap InNavi.exe.
# Example: powershell -ExecutionPolicy Bypass -File scripts\measure-memory.ps1 -ProcessId 1234 -DurationSeconds 60 -Scenario 'Campus idle'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$ProcessId,
    [ValidateRange(1, 3600)][int]$DurationSeconds = 60,
    [ValidateRange(100, 60000)][int]$IntervalMilliseconds = 500,
    [string]$Scenario = 'Desktop app',
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\InNavi\Saved\Performance\desktop-process-memory.json'),
    [ValidateRange(0, 1048576)][double]$MaxPrivateMiB = 0
)
$ErrorActionPreference = 'Stop'
$targetProcess = Get-Process -Id $ProcessId -ErrorAction Stop
$startedUtc = $targetProcess.StartTime.ToUniversalTime().ToString('o')
$processName = $targetProcess.ProcessName
$samples = [System.Collections.Generic.List[object]]::new()
$timer = [System.Diagnostics.Stopwatch]::StartNew()
$completed = $false
try {
    while ($true) {
        $targetProcess.Refresh()
        if ($targetProcess.HasExited) { break }
        $samples.Add([pscustomobject]@{
            elapsed_seconds = $timer.Elapsed.TotalSeconds
            working_set_bytes = $targetProcess.WorkingSet64
            private_bytes = $targetProcess.PrivateMemorySize64
            process_lifetime_peak_working_set_bytes = $targetProcess.PeakWorkingSet64
        })
        if ($timer.Elapsed.TotalSeconds -ge $DurationSeconds) { $completed = $true; break }
        $remainingMs = ($DurationSeconds - $timer.Elapsed.TotalSeconds) * 1000
        Start-Sleep -Milliseconds ([int][Math]::Max(1, [Math]::Min($IntervalMilliseconds, $remainingMs)))
    }
}
finally {
    $timer.Stop()
    $targetProcess.Dispose()
}
if ($samples.Count -eq 0) { throw "Process $ProcessId exited before any memory sample could be collected." }
$workingSet = $samples | Measure-Object -Property working_set_bytes -Minimum -Maximum -Average
$private = $samples | Measure-Object -Property private_bytes -Minimum -Maximum -Average
$lifetimePeak = ($samples | Measure-Object -Property process_lifetime_peak_working_set_bytes -Maximum).Maximum
$budgetPassed = $MaxPrivateMiB -eq 0 -or $private.Maximum / 1MB -le $MaxPrivateMiB
$report = [ordered]@{
    schema_version = 1
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
    scenario = $Scenario
    process_id = $ProcessId
    process_name = $processName
    process_started_at_utc = $startedUtc
    requested_duration_seconds = $DurationSeconds
    elapsed_seconds = $timer.Elapsed.TotalSeconds
    interval_milliseconds = $IntervalMilliseconds
    completed = $completed
    sample_count = $samples.Count
    scope = 'CPU process memory only; excludes dedicated GPU memory. Working set includes shared resident pages; private bytes are committed private memory. The lifetime peak can predate this sampling window.'
    sampled_working_set_bytes = @{ minimum = $workingSet.Minimum; maximum = $workingSet.Maximum; mean = $workingSet.Average }
    sampled_private_bytes = @{ minimum = $private.Minimum; maximum = $private.Maximum; mean = $private.Average }
    process_lifetime_peak_working_set_bytes = $lifetimePeak
    max_private_budget_mib = $MaxPrivateMiB
    budget_passed = $budgetPassed
    samples = $samples.ToArray()
}
$fullOutput = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($fullOutput)) | Out-Null
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $fullOutput -Encoding UTF8
Write-Host ("{0} (PID {1}), {2} samples: working set max {3:N2} MiB, private max {4:N2} MiB, lifetime peak working set {5:N2} MiB." -f $processName, $ProcessId, $samples.Count, ($workingSet.Maximum / 1MB), ($private.Maximum / 1MB), ($lifetimePeak / 1MB))
Write-Host "Report: $fullOutput"
if (-not $completed) { throw "Process $ProcessId exited before the requested duration; the report contains the partial measurements." }
if (-not $budgetPassed) { throw "Sampled private memory exceeded the $MaxPrivateMiB MiB budget." }
