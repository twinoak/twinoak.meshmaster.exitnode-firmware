<#
.SYNOPSIS
  Is there a MeshCore companion tag newer than the pin?

.DESCRIPTION
  The first tier of docs\upstream.md, and the only part of it worth having outside a workflow:
  .github\workflows\upstream.yml runs this weekly and opens a pull request when the answer is yes.
  Having it here means the answer can be checked by hand, and the workflow tested, without waiting
  a week or pushing a branch:

    .\tools\upstream-check.ps1                                  # against the pin in platformio.ini
    .\tools\upstream-check.ps1 -Tag companion-v1.16.0           # as if the pin were one tag back

  It reads and writes nothing. Moving the pin is the workflow's job, on a branch.

.PARAMETER Tag
  Pretend the repository is pinned at this tag. What the acceptance check of task 65 uses to see
  the "there is something newer" path without a real bump.

.PARAMETER GitHubOutput
  Also append newer/tag/sha/previous_tag to $env:GITHUB_OUTPUT, for the workflow.
#>
[CmdletBinding()]
param(
    [string]$Tag = '',
    [string]$Upstream = 'https://github.com/meshcore-dev/MeshCore.git',
    [switch]$GitHubOutput
)

$ErrorActionPreference = 'Continue'   # native commands write to stderr; exit codes are checked by hand
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ini = Join-Path $root 'platformio.ini'

$pin = ''
$currentTag = $Tag
foreach ($line in Get-Content $ini) {
    if ($line -match '^pin\s*=\s*([0-9a-f]{40})') { $pin = $Matches[1] }
    elseif (-not $Tag -and $line -match '^tag\s*=\s*(companion-v\d+\.\d+\.\d+)') { $currentTag = $Matches[1] }
}
if (-not $pin) { throw "could not read [meshcore].pin from $ini" }
if ($currentTag -notmatch '^companion-v(\d+\.\d+\.\d+)$') { throw "could not read [meshcore].tag from $ini" }
$currentVersion = [version]$Matches[1]

# Every companion tag upstream has, newest by version and not by the order git prints them.
$refs = & git ls-remote --tags $Upstream 'refs/tags/companion-v*' 2>$null
if ($LASTEXITCODE -ne 0) { throw "could not read the tags of $Upstream" }
$tags = @{}
foreach ($line in @($refs)) {
    # "<sha>\trefs/tags/companion-v1.17.1" and, for an annotated tag, "...^{}" with the commit.
    if ($line -notmatch '^([0-9a-f]{40})\s+refs/tags/(companion-v\d+\.\d+\.\d+)(\^\{\})?$') { continue }
    $sha = $Matches[1]; $name = $Matches[2]; $peeled = [bool]$Matches[3]
    # The peeled line is the commit and wins over the tag object.
    if ($peeled -or -not $tags.ContainsKey($name)) { $tags[$name] = $sha }
}
if ($tags.Count -eq 0) { throw "no companion-v* tags at $Upstream" }

$newest = $tags.Keys | Sort-Object { [version]($_ -replace '^companion-v', '') } | Select-Object -Last 1
$newestVersion = [version]($newest -replace '^companion-v', '')
$newer = $newestVersion -gt $currentVersion

Write-Host "pinned:  $currentTag  $pin"
Write-Host "newest:  $newest  $($tags[$newest])"
if ($newer) { Write-Host "newer:   yes - $currentTag -> $newest" }
else { Write-Host "newer:   no - nothing to bump" }

if ($GitHubOutput -and $env:GITHUB_OUTPUT) {
    @(
        "newer=$($newer.ToString().ToLowerInvariant())",
        "tag=$newest",
        "sha=$($tags[$newest])",
        "previous_tag=$currentTag",
        "previous_sha=$pin"
    ) | Out-File -Append -Encoding utf8 $env:GITHUB_OUTPUT
}
