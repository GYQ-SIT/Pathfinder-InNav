# Unpacks a package made by package-windows.ps1 and starts InNavi.exe once, without a window, to see that it opens and closes: a package
# that is missing a file, or that crashes at start, is found here and not by whoever downloads it.
#
#   powershell -File scripts\smoke-test-package.ps1 -Zip C:\Temp\release\InNavi-m2-build-1.zip
param(
    [Parameter(Mandatory = $true)][string]$Zip,
    [int]$TimeoutSeconds = 240
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Zip)) { throw "No zip at $Zip." }
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("innavi-smoke-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    & tar.exe -x -f $Zip -C $work
    if ($LASTEXITCODE -ne 0) { throw 'The zip could not be unpacked.' }
    $exe = Join-Path $work 'InNavi.exe'
    if (-not (Test-Path $exe)) { throw 'The package has no InNavi.exe at its top.' }
    # What the app reads from its folder must be in the package.
    foreach ($needed in 'InNavi\Data\buildings.json', 'InNavi\Data\map_extended.db', 'InNavi\Data\Audio\click_button.wav', 'InNavi\Data\DefaultLayouts\DefaultExteriorLayout.json', 'InNavi\Data\Prototype\mobile-prototype.json', 'BUILD-INFO.txt', 'SampleProjects') {
        if (-not (Test-Path (Join-Path $work $needed))) { throw "The package is missing $needed." }
    }
    $log = Join-Path $work 'smoke.log'
    $marker = Join-Path $work 'smoke-ready.txt'
    $shipping = Test-Path (Join-Path $work 'InNavi\Binaries\Win64\InNavi-Win64-Shipping.exe')
    $exitArgument = if ($shipping) { '-InNaviSmokeTest' } else { '-ExecCmds="quit"' }
    $process = Start-Process -FilePath $exe -ArgumentList @('-nullrhi', '-unattended', '-nosplash', '-nosound', "-abslog=`"$log`"", $exitArgument, "-InNaviSmokeResult=`"$marker`"") -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "InNavi.exe did not close within $TimeoutSeconds s."
    }
    $text = if (Test-Path $log) { Get-Content -Raw $log } else { '' }
    if ($process.ExitCode -ne 0) { throw "InNavi.exe closed with exit code $($process.ExitCode)." }
    if ($shipping -and (-not (Test-Path $marker) -or (Get-Content $marker -Raw).Trim() -ne 'ready')) { throw 'Shipping app did not report a ready game world.' }
    if ($text -match 'Fatal error|Assertion failed|Unhandled Exception') { throw 'The log of the start has a crash in it.' }
    Write-Host "The package starts and closes cleanly (exit code 0, $((Get-Item $log -ErrorAction SilentlyContinue).Length) bytes of log)."
}
finally {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ([IO.Path]::GetDirectoryName($resolvedWork).Equals($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedWork).StartsWith('innavi-smoke-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction SilentlyContinue
    } else { throw "Unsafe smoke-test cleanup path: $resolvedWork" }
}
