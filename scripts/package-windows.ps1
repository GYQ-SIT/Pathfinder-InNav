# Packages the Windows build (cook, stage, pak) and zips it for hand-in or for a release: the recipe the Milestone 1 build was made with,
# so the same package comes out of a pull request's machine and out of the release workflow.
#
#   powershell -File scripts\package-windows.ps1 -Name InNavi-m2-build-1 -OutputDir C:\Temp\release
#
# Needs Unreal Engine 5.8 (UE_ROOT, or the default install folder), and the Unreal editor closed. The zip is OutputDir\<Name>.zip.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$UnrealRoot = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'InNavi\InNavi.uproject'
$runUat = Join-Path $UnrealRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path $runUat)) { throw "Unreal Engine not found at $UnrealRoot (set UE_ROOT)." }
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { throw 'Close the Unreal editor first: it cannot package while it is open.' }

$archive = Join-Path $OutputDir 'archive'
$zip = Join-Path $OutputDir "$Name.zip"
if (Test-Path $archive) { Remove-Item -Recurse -Force $archive }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force $OutputDir | Out-Null

Write-Host "== Packaging $Name (this cooks the project: it takes a while)"
& $runUat BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -prereqs -archive "-archivedirectory=$archive" -utf8output
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)." }

$root = Join-Path $archive 'Windows'
if (-not (Test-Path (Join-Path $root 'InNavi.exe'))) { throw "No InNavi.exe in ${root}: the package is not complete." }

Write-Host '== Cleaning the package'
# Nothing of the machine it was made on goes in: logs, saved layouts, symbols, tools the app does not use.
foreach ($pattern in 'Manifest_*.txt', 'NOTICES.txt', '*.pdb') {
    Get-ChildItem -Path $root -Recurse -Force -Filter $pattern -ErrorAction SilentlyContinue | Remove-Item -Force
}
foreach ($folder in 'InNavi\Saved', 'Engine\Saved', 'Engine\Binaries\ThirdParty\Vulkan', 'Engine\Extras\GPUDumpViewer') {
    $path = Join-Path $root $folder
    if (Test-Path $path) { Remove-Item -Recurse -Force $path }
}
Get-ChildItem -Path $root -Recurse -Force -Filter 'vc_redist.arm64.exe' -ErrorAction SilentlyContinue | Remove-Item -Force
# The data folder keeps the campus database, the click sounds and the default dock layouts; the hand-made test projects stay in the repository.
Get-ChildItem -Path (Join-Path $root 'InNavi\Data') -Filter '*.json' -ErrorAction SilentlyContinue | Remove-Item -Force
# Sample projects to try File > Load with (doors, link doors between buildings, campus routes).
$samples = Join-Path $root 'SampleProjects'
New-Item -ItemType Directory -Force $samples | Out-Null
Copy-Item (Join-Path $repo 'docs\test-data\*.json') $samples

$commit = (& git -C $repo rev-parse --short HEAD).Trim()
@(
    "InNavi - Team Pathfinder"
    "Build:   $Name"
    "Version: $(if ($Version) { $Version } else { 'n/a' })"
    "Commit:  $commit"
    "Date:    $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC"
    "Engine:  Unreal Engine 5.8, Windows 11 (64-bit)"
    ""
    "Run InNavi.exe. The campus loads from InNavi\Data\map_extended.db."
    "SampleProjects\ has projects to open with File > Load (three buildings with doors, and campus routes)."
    "Documentation: https://github.com/GYQ-SIT/Pathfinder-InNav (README.md, docs/)."
) | Set-Content -Encoding UTF8 (Join-Path $root 'BUILD-INFO.txt')

Write-Host "== Zipping to $zip"
# Windows' own tar writes a zip that Explorer opens (Git Bash's tar does not understand a drive letter).
Push-Location $root
try { & tar.exe -a -c -f $zip * } finally { Pop-Location }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $zip)) { throw 'Zipping failed.' }
Remove-Item -Recurse -Force $archive
Write-Host ("== Done: {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
