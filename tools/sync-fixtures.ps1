<#
.SYNOPSIS
  Copies the contract fixtures from twinoak/meshmaster.dk into tests/fixtures/contracts, or
  verifies the copy that is here.

.DESCRIPTION
  The firmware keeps the .NET agent's contract to the byte, and the fixtures are how that is
  checked: the JSON files MeshMaster.Contracts' round-trip tests generate (tests/fixtures/contracts
  in twinoak/meshmaster.dk, written by the test itself so they cannot drift from the records).
  A difference between the two sets is a bug in the firmware, not a second dialect.

  This script is the only way that directory changes here. It writes MANIFEST.txt beside the
  fixtures: the meshmaster.dk commit the copy came from, and the SHA-256 and size of every file.

    .\tools\sync-fixtures.ps1                      # fetch again from the commit in MANIFEST.txt
    .\tools\sync-fixtures.ps1 -Commit <sha|master> # fetch from another commit and record it
    .\tools\sync-fixtures.ps1 -Verify              # no network: do the files still match MANIFEST.txt?

  -Verify is what CI runs (.github/workflows/ci.yml) and what fails when somebody edits a fixture
  here instead of in meshmaster.dk. `pio test -e native` checks the same set from the other side -
  every file present, listed and well formed - so a plain test run notices too.

  Needs gh, logged in: meshmaster.dk is private, so the fixtures are read through the GitHub API
  with gh's own credentials and no token is ever written down here.

.PARAMETER Commit
  The meshmaster.dk commit (or branch, or tag) to fetch from. Default: the one MANIFEST.txt
  records, or master when there is no manifest yet. A branch name is resolved to its commit and
  the commit is what gets recorded.

.PARAMETER AllowEmpty
  Do not fail when the source directory does not exist at that commit. Task 66 of pile 4.2 is
  what creates it in meshmaster.dk; until that has landed, a sync is legitimately empty.
#>
[CmdletBinding()]
param(
    [string]$Commit = '',
    [string]$Repo = 'twinoak/meshmaster.dk',
    [string]$SourcePath = 'tests/fixtures/contracts',
    [switch]$Verify,
    [switch]$AllowEmpty
)

# Native commands (gh) write to stderr and are checked by their exit code, as run-all.ps1 does.
$ErrorActionPreference = 'Continue'
# PowerShell 5.1 decodes what a native command prints with the console's codepage, which would
# turn a non-ASCII character in a fixture into mojibake on the way in. The fixtures are the .NET
# agent's bytes; they are read as UTF-8 or not at all.
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dir = Join-Path $root 'tests\fixtures\contracts'
$manifestPath = Join-Path $dir 'MANIFEST.txt'

function Read-Manifest {
    if (-not (Test-Path $manifestPath)) { return $null }
    $lines = Get-Content $manifestPath
    $source = $lines | Where-Object { $_ -match '^# source: .* at commit ([0-9a-f]{40})$' } | Select-Object -First 1
    $commit = if ($source -match 'commit ([0-9a-f]{40})') { $Matches[1] } else { '' }
    $entries = @()
    foreach ($line in $lines) {
        if ($line -match '^([0-9a-f]{64})\s+(\d+)\s+(\S.*)$') {
            $entries += [pscustomobject]@{ Sha256 = $Matches[1]; Size = [int]$Matches[2]; Name = $Matches[3] }
        }
    }
    return [pscustomobject]@{ Commit = $commit; Entries = $entries }
}

function Get-Sha256([string]$path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
}

# ---------------------------------------------------------------- verify (no network)
if ($Verify) {
    $manifest = Read-Manifest
    if (-not $manifest) { throw "no $manifestPath - run this script without -Verify first" }
    $problems = @()
    foreach ($e in $manifest.Entries) {
        $file = Join-Path $dir $e.Name
        if (-not (Test-Path $file)) { $problems += "$($e.Name): in MANIFEST.txt, not on disk"; continue }
        $size = (Get-Item -LiteralPath $file).Length
        if ($size -ne $e.Size) { $problems += "$($e.Name): $size bytes, MANIFEST.txt says $($e.Size)" }
        $sha = Get-Sha256 $file
        if ($sha -ne $e.Sha256) { $problems += "$($e.Name): SHA-256 $sha, MANIFEST.txt says $($e.Sha256)" }
    }
    $listed = @($manifest.Entries | ForEach-Object { $_.Name })
    foreach ($file in Get-ChildItem $dir -Filter *.json -File -ErrorAction SilentlyContinue) {
        if ($listed -notcontains $file.Name) { $problems += "$($file.Name): on disk, not in MANIFEST.txt" }
    }
    if ($problems.Count -gt 0) {
        Write-Host "The contract fixtures no longer match MANIFEST.txt:"
        $problems | ForEach-Object { Write-Host "  $_" }
        Write-Host ""
        Write-Host "They are a copy of $Repo/$SourcePath at commit $($manifest.Commit); change them there"
        Write-Host "and run .\tools\sync-fixtures.ps1 -Commit <new sha>, never by hand here."
        exit 1
    }
    Write-Host "$($manifest.Entries.Count) contract fixture(s) match MANIFEST.txt ($Repo at $($manifest.Commit))."
    exit 0
}

# ---------------------------------------------------------------- fetch
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'gh is not on PATH; it is how a private repository is read' }

if (-not $Commit) {
    $manifest = Read-Manifest
    $Commit = if ($manifest -and $manifest.Commit) { $manifest.Commit } else { 'master' }
}

# A branch or tag is resolved to the commit, because that is what gets written down.
$sha = (& gh api "repos/$Repo/commits/$Commit" --jq '.sha' 2>$null | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or -not ($sha -match '^[0-9a-f]{40}$')) { throw "could not resolve $Repo@$Commit to a commit" }
Write-Host "$Repo/$SourcePath at $sha"

$listing = & gh api "repos/$Repo/contents/$SourcePath`?ref=$sha" 2>$null
if ($LASTEXITCODE -ne 0) {
    if ($AllowEmpty) {
        Write-Host "  $SourcePath does not exist at that commit - nothing to copy (-AllowEmpty)."
        $files = @()
    }
    else {
        throw "$SourcePath does not exist in $Repo at $sha. Pass -AllowEmpty if that is expected (task 66 of pile 4.2 creates it)."
    }
}
else {
    # One string, not the array of lines gh printed: ConvertFrom-Json on an array parses each
    # line on its own in PowerShell 5.1.
    # Only the fixtures: this directory's own README.md is ours, and meshmaster.dk has one of
    # its own in the same place.
    $files = @((($listing -join "`n") | ConvertFrom-Json) | Where-Object { $_.type -eq 'file' -and $_.name -like '*.json' })
}

New-Item -ItemType Directory -Force -Path $dir | Out-Null

# Everything the previous sync left, so a file deleted in meshmaster.dk disappears here too.
$manifest = Read-Manifest
if ($manifest) {
    foreach ($e in $manifest.Entries) {
        $stale = Join-Path $dir $e.Name
        if (Test-Path $stale) { Remove-Item -LiteralPath $stale -Force }
    }
}

$entries = @()
foreach ($f in $files | Sort-Object name) {
    # Through gh, because meshmaster.dk is private and gh already holds the credentials. The file
    # is written as UTF-8 without a BOM, LF, one trailing newline - a redirection on Windows would
    # add a BOM and CRLF, and then no two machines would agree on a fixture's SHA-256.
    $target = Join-Path $dir $f.name
    $body = & gh api "repos/$Repo/contents/$SourcePath/$($f.name)`?ref=$sha" -H 'Accept: application/vnd.github.raw'
    if ($LASTEXITCODE -ne 0) { throw "could not fetch $($f.name)" }
    $text = ($body -join "`n")
    if (-not $text.EndsWith("`n")) { $text += "`n" }
    [IO.File]::WriteAllText($target, $text, (New-Object Text.UTF8Encoding($false)))
    $entries += [pscustomobject]@{
        Sha256 = Get-Sha256 $target
        Size   = (Get-Item -LiteralPath $target).Length
        Name   = $f.name
    }
    Write-Host "  $($f.name) ($($entries[-1].Size) bytes)"
}

$lines = @(
    '# The contract fixtures of the .NET agent, copied - never edited here.',
    "# source: $Repo/$SourcePath at commit $sha",
    '# written by tools/sync-fixtures.ps1; see README.md in this directory',
    '#',
    '# sha256                                                           bytes  name'
)
foreach ($e in $entries) { $lines += ('{0}  {1,6}  {2}' -f $e.Sha256, $e.Size, $e.Name) }
[IO.File]::WriteAllText($manifestPath, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))

Write-Host "MANIFEST.txt: $($entries.Count) file(s) from $sha"
