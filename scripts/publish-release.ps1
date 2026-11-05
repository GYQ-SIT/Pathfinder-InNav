# Creates (or reuses) the GitHub release of a tag and attaches the zip of the build to it. Run by release-windows.yml; it can also be run
# by hand with a token that may write to the repository's releases.
#
#   powershell -File scripts\publish-release.ps1 -Tag m2-build-1 -Zip C:\Temp\release\InNavi-m2-build-1.zip -Repo GYQ-SIT/Pathfinder-InNav -Token <token>
#
# A tag that is built again (the workflow is run again) keeps its release: the zip of the same name is replaced, not added twice.
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [Parameter(Mandatory = $true)][string]$Zip,
    [Parameter(Mandatory = $true)][string]$Repo,
    [Parameter(Mandatory = $true)][string]$Token,
    [string]$Notes = ''
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Zip)) { throw "No zip at $Zip." }
$headers = @{ Authorization = "Bearer $Token"; Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' }
$api = "https://api.github.com/repos/$Repo"

$release = $null
try { $release = Invoke-RestMethod -Headers $headers -Uri "$api/releases/tags/$Tag" } catch {
    if ($_.Exception.Response.StatusCode.value__ -ne 404) { throw }
}
if (-not $release) {
    $body = @{
        tag_name = $Tag
        name = "InNavi $Tag"
        # A test build of a milestone, not a final version.
        prerelease = $true
        generate_release_notes = $true
        body = $Notes
    } | ConvertTo-Json
    $release = Invoke-RestMethod -Method Post -Headers $headers -Uri "$api/releases" -Body $body -ContentType 'application/json'
    Write-Host "Created release $($release.html_url)"
} else {
    Write-Host "Release of $Tag exists: $($release.html_url)"
}

$name = Split-Path -Leaf $Zip
$existing = $release.assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
$suffix = [guid]::NewGuid().ToString('N')
$uploadName = if ($existing) { "replacement-$suffix.zip" } else { $name }
# curl streams the file; Invoke-RestMethod would read a package of hundreds of MB into memory first.
$upload = "https://uploads.github.com/repos/$Repo/releases/$($release.id)/assets?name=$([uri]::EscapeDataString($uploadName))"
$size = (Get-Item $Zip).Length
Write-Host ("Uploading {0} ({1:N0} MB)" -f $name, ($size / 1MB))
$response = & curl.exe --silent --show-error --fail --retry 3 -X POST -H "Authorization: Bearer $Token" -H 'Content-Type: application/zip' -H 'Accept: application/vnd.github+json' --data-binary "@$Zip" $upload
if ($LASTEXITCODE -ne 0) { throw "Upload failed ($LASTEXITCODE)." }
$replacement = ($response -join "`n") | ConvertFrom-Json
if (!$replacement.id -or $replacement.state -ne 'uploaded' -or [long]$replacement.size -ne $size) {
    throw 'GitHub did not confirm a complete upload.'
}

function Rename-ReleaseAsset($Asset, [string]$NewName) {
    $body = @{ name = $NewName } | ConvertTo-Json
    Invoke-RestMethod -Method Patch -Headers $headers -Uri "$api/releases/assets/$($Asset.id)" -Body $body -ContentType 'application/json' | Out-Null
}

if ($existing) {
    # Keep the original download until a complete replacement exists. Keep a downloadable backup
    # during the two renames so a failure at either step can be rolled back without re-uploading.
    try {
        Rename-ReleaseAsset $existing "backup-$suffix.zip"
        Rename-ReleaseAsset $replacement $name
    } catch {
        $renameError = $_
        try {
            Rename-ReleaseAsset $existing $name
            Invoke-RestMethod -Method Delete -Headers $headers -Uri "$api/releases/assets/$($replacement.id)" | Out-Null
        } catch {
            Write-Warning "Could not finish rollback. Inspect the replacement and backup assets on the release before retrying."
        }
        throw $renameError
    }
    try {
        Invoke-RestMethod -Method Delete -Headers $headers -Uri "$api/releases/assets/$($existing.id)" | Out-Null
    } catch {
        Write-Warning "The replacement is published. The backup asset could not be removed."
    }
}
Write-Host "Done: $($release.html_url)"
