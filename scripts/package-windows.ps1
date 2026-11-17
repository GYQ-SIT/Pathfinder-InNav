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
    [string]$Version = '',
    [ValidateSet('Development', 'Shipping')][string]$Configuration = 'Development'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repo 'InNavi\InNavi.uproject'
$runUat = Join-Path $UnrealRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path $runUat)) { throw "Unreal Engine not found at $UnrealRoot (set UE_ROOT)." }
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { throw 'Close the Unreal editor first: it cannot package while it is open.' }

if ([string]::IsNullOrWhiteSpace($Name) -or $Name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw 'Name must be a valid Windows file name.'
}
$output = (New-Item -ItemType Directory -Force $OutputDir).FullName
$nonce = [guid]::NewGuid().ToString('N')
$archive = Join-Path $output "archive-$nonce"
$temporaryZip = Join-Path $output "build-$nonce.zip"
$backup = Join-Path $output "previous-$nonce.zip"
$zip = Join-Path $output "$Name.zip"
$published = $false
try {
    Write-Host "== Packaging $Name (this cooks the project: it takes a while)"
    & $runUat BuildCookRun "-project=$project" -noP4 -platform=Win64 "-clientconfig=$Configuration" -build -cook -stage -pak -prereqs -archive "-archivedirectory=$archive" -utf8output
    if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)." }

    $root = Join-Path $archive 'Windows'
    foreach ($needed in 'InNavi.exe', 'InNavi\Data\buildings.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $root $needed) -PathType Leaf)) {
            throw "No $needed in $root. The package is not complete."
        }
    }

    Write-Host '== Cleaning the package'
    foreach ($pattern in 'Manifest_*.txt', 'NOTICES.txt', '*.pdb', 'vc_redist.arm64.exe') {
        Get-ChildItem -LiteralPath $root -Recurse -Force -Filter $pattern -ErrorAction SilentlyContinue | Remove-Item -Force
    }
    $rootPrefix = (Get-Item -LiteralPath $root).FullName + [IO.Path]::DirectorySeparatorChar
    foreach ($folder in 'InNavi\Saved', 'Engine\Saved', 'Engine\Binaries\ThirdParty\Vulkan', 'Engine\Extras\GPUDumpViewer') {
        $path = Join-Path $root $folder
        if (Test-Path -LiteralPath $path) {
            $resolved = (Get-Item -LiteralPath $path).FullName
            if (-not $resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe staging path $resolved." }
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
    # The default mobile catalogue lives in buildings.json. Keep it alongside the campus database.
    Get-ChildItem -LiteralPath (Join-Path $root 'InNavi\Data') -File -Filter '*.json' |
        Where-Object Name -ne 'buildings.json' | Remove-Item -Force
    $samples = Join-Path $root 'SampleProjects'
    New-Item -ItemType Directory -Force $samples | Out-Null
    Copy-Item (Join-Path $repo 'docs\test-data\*.json') $samples

    $commit = (& git -C $repo rev-parse --short HEAD).Trim()
    $sourceState = if (@(& git -C $repo status --porcelain).Count) { 'modified working tree' } else { 'clean' }
    @(
        "InNavi - Team Pathfinder"
        "Build:   $Name"
        "Version: $(if ($Version) { $Version } else { 'n/a' })"
        "Commit:  $commit"
        "Source:  $sourceState"
        "Date:    $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm')) UTC"
        "Engine:  Unreal Engine 5.8, Windows 11 (64-bit)"
        "Config:  $Configuration"
        ""
        "Run InNavi.exe. The campus loads from InNavi\Data\map_extended.db."
        "SampleProjects\ has projects to open with File > Load (three buildings with doors, and campus routes)."
        "Documentation: https://github.com/GYQ-SIT/Pathfinder-InNav (README.md, docs/)."
    ) | Set-Content -Encoding UTF8 (Join-Path $root 'BUILD-INFO.txt')

    Write-Host "== Zipping to $zip"
    # Windows tar chooses the ZIP format from the temporary file's extension. The top-level items are named one by one: with "-C $root ." every
    # entry is stored as "./InNavi.exe", and Windows Explorer shows a ZIP like that as empty (the release of m2-build-6 looked empty when it was
    # opened by double click, though the files were inside).
    $top = @(Get-ChildItem -LiteralPath $root -Force | ForEach-Object Name)
    & tar.exe -a -c -f $temporaryZip -C $root @top
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $temporaryZip -PathType Leaf)) { throw 'Zipping failed.' }
    $names = @(& tar.exe -t -f $temporaryZip)
    $entries = @($names | ForEach-Object { $_ -replace '^\./', '' })
    if ($LASTEXITCODE -ne 0 -or $entries -notcontains 'InNavi.exe' -or $entries -notcontains 'InNavi/Data/buildings.json') {
        throw 'The ZIP is incomplete.'
    }
    if (@($names | Where-Object { $_ -eq '.' -or $_ -eq './' -or $_.StartsWith('./') }).Count -gt 0) {
        throw 'The ZIP has entries that start with "./": Windows Explorer would show it as empty.'
    }

    if ([IO.File]::Exists($zip)) { [IO.File]::Replace($temporaryZip, $zip, $backup) }
    else { [IO.File]::Move($temporaryZip, $zip) }
    $published = $true
} finally {
    if ([IO.File]::Exists($backup)) {
        if ($published) { Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue }
        elseif (-not [IO.File]::Exists($zip)) { [IO.File]::Move($backup, $zip) }
        else { Write-Warning "The previous ZIP was retained at $backup." }
    }
    if (Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $archive) {
        $resolved = (Get-Item -LiteralPath $archive).FullName
        if ([IO.Path]::GetDirectoryName($resolved) -eq $output) {
            Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
Write-Host ("== Done: {0} ({1:N0} MB)" -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB))
