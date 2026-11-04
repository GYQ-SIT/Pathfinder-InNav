# Reads Unreal's automation report and fails when any test failed or did not finish.
# The editor exits with a failure code when a test fails, but its own log is not in the log of the job: this lists the failed tests and their errors there.
# Usage: powershell -File scripts/check-test-report.ps1 -Report <folder with index.json> [-MinimumTests 100]
param(
    [Parameter(Mandatory = $true)][string]$Report,
    [int]$MinimumTests = 1
)
$ErrorActionPreference = 'Stop'
$index = Join-Path $Report 'index.json'
if (-not (Test-Path $index)) {
    Write-Error "No test report at $index - the tests did not run."
    exit 1
}
$data = Get-Content -Raw -Encoding UTF8 $index | ConvertFrom-Json
$passed = [int]$data.succeeded + [int]$data.succeededWithWarnings
$failed = [int]$data.failed
$notRun = [int]$data.notRun
$inProcess = [int]$data.inProcess
Write-Host "Tests: $passed passed ($($data.succeededWithWarnings) with warnings), $failed failed, $notRun not run, $inProcess in progress."
# What the performance tests measured, so the log shows how fast the machine that ran them is.
foreach ($test in $data.tests | Where-Object { $_.fullTestPath -like "*Performance*" }) {
    foreach ($entry in $test.entries | Where-Object { $_.event.type -eq "Info" }) { Write-Host "$($test.fullTestPath): $($entry.event.message)" }
}
if ($failed -gt 0) {
    foreach ($test in $data.tests | Where-Object { $_.state -eq 'Fail' }) {
        Write-Host "FAILED: $($test.fullTestPath)"
        foreach ($entry in $test.entries | Where-Object { $_.event.type -eq 'Error' }) { Write-Host "    $($entry.event.message)" }
    }
    exit 1
}
if ($notRun -gt 0 -or $inProcess -gt 0) {
    Write-Error "The test run was incomplete ($notRun not run, $inProcess in progress)."
    exit 1
}
if ($passed -lt $MinimumTests) {
    Write-Error "Only $passed tests ran; at least $MinimumTests are expected."
    exit 1
}
exit 0
