# Reads the log of an Unreal build made with -StaticAnalyzer=Default -StaticAnalyzerOutputType=Text (MSVC /analyze) and fails when it
# has a compile error or an analyzer warning in the product code. Warnings in the test files are listed but do not fail the check: the
# analyzer does not know that TestNotNull(...) returning false ends the test, so it reports "dereferencing NULL" after every such check.
# Usage: powershell -File scripts/check-analysis.ps1 -Log analysis.log
param([Parameter(Mandatory = $true)][string]$Log)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Log)) { Write-Error "No analysis log at $Log"; exit 1 }
$lines = Get-Content -Encoding UTF8 $Log
# "...\InNavi\Source\InNavi\File.cpp(123,4) : warning C6011: ..." (a backslash or a slash between the folders)
$pattern = 'InNavi[\\/]Source[\\/]InNavi[\\/](?<file>[^\\/(]+)\((?<line>[0-9]+)[^)]*\)\s*:\s*(?<kind>warning|error) (?<code>C[0-9]+)'
$found = foreach ($line in $lines) {
    $m = [regex]::Match($line, $pattern)
    if ($m.Success) { [pscustomobject]@{ File = $m.Groups['file'].Value; Line = $m.Groups['line'].Value; Kind = $m.Groups['kind'].Value; Code = $m.Groups['code'].Value; Text = $line.Trim() } }
}
$errors = @($found | Where-Object { $_.Kind -eq 'error' })
$product = @($found | Where-Object { $_.Kind -eq 'warning' -and $_.File -notmatch 'Tests\.cpp$' })
$tests = @($found | Where-Object { $_.Kind -eq 'warning' -and $_.File -match 'Tests\.cpp$' })
Write-Host "Static analysis: $($errors.Count) errors, $($product.Count) warnings in the product code, $($tests.Count) in the tests (not counted)."
foreach ($finding in $errors + $product) { Write-Host "  $($finding.Text)" }
$failedBuild = $lines | Where-Object { $_ -match '^Result: Failed' }
if ($errors.Count -gt 0 -or $product.Count -gt 0) { exit 1 }
if ($failedBuild) { Write-Error 'The analysis build failed without a finding to show; see the log.'; exit 1 }
exit 0
