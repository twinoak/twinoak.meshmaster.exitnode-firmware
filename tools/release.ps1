#Requires -Version 5.1
<#
.SYNOPSIS
    The release, in the parts a workflow runs and a test can run (task p1.11-52).

.DESCRIPTION
    A release of this firmware is one press of *Run workflow* on .github/workflows/release.yml.
    Nothing is typed there, so everything the press has to decide is decided here, in a script
    with a self-test, rather than in shell inside a workflow nobody can run twice:

      check       the rails, on every pull request: VERSION is the one place the version lives,
                  the three manifests that carry the library's version agree with it, CHANGELOG.md's
                  top section is headed with it, and every waiting release note parses.
      plan        what the press is about to do, and the refusals that stop it before anything is
                  built: nothing to release, the version's tag already exists, a ref that is not
                  master. One sentence each.
      notes       the waiting lines, as the bullets of the release's *What's new*, in the order the
                  tasks merged.
      move        after the release is out: the lines into release-notes/vX.Y.Z.md as published,
                  release-notes/unreleased/ emptied, the version moved to the next patch in every
                  place that carries it, and CHANGELOG.md's next section opened.
      ci-commit   which commit the *ci is green* gate must look at. The pull request the workflow
                  merges after a release gets no ci run of its own - GitHub starts no workflow from
                  an event raised with the job's own token - so the release after it would wait for
                  a run that never comes. This walks master's head back over commits that are the
                  github-actions bot's AND touch nothing but the release's own files, and names the
                  commit ci really ran on.

    The version is in VERSION at the root, as one line, and it names the release that comes NEXT.
    That is what lets the whole thing be one press: the workflow reads the number instead of being
    told it, and the pull request after a release moves it on. A bigger step than a patch is one
    line edited beforehand, in a pull request of its own.

    docs/release.md is the page. tasks/RUNBOOK.md is where a task is told to leave its line.

.PARAMETER Action
    check, plan, notes, move or ci-commit. See above.

.PARAMETER Repository
    The working tree to act in. The repository root by default.

.PARAMETER Ref
    plan: the ref the press named, empty for the workflow's own. Only a dry run may name one that
    is not master.

.PARAMETER Sha
    move: the commit the release was made from, for the stamp of the archived notes.
    ci-commit: where to start walking. HEAD by default.

.PARAMETER Out
    notes: the file to write the bullets to. Standard output as well, either way.

.PARAMETER DryRun
    plan: a rehearsal. The version is 0.0.0-dryrun+<sha7>, nothing is refused, and what a real
    release WOULD refuse is printed as a warning so that the rehearsal rehearses that too.

.PARAMETER At
    The moment to stamp the archived notes with. Now, unless a caller (the self-test) pins it.

.PARAMETER SelfTest
    Build a scratch repository in a temporary directory and run every case above against it. CI
    runs this, like tools/check-runners.py --self-test and its cousins.

.OUTPUTS
    Lines for the run's log. The machine-readable ones are `key=value`, and are appended to
    $env:GITHUB_OUTPUT when a workflow set it.
#>
[CmdletBinding()]
param(
    [ValidateSet('check', 'plan', 'notes', 'move', 'ci-commit')]
    [string]$Action = '',
    [string]$Repository = '',
    [string]$Ref = '',
    [string]$Sha = '',
    [string]$Out = '',
    [switch]$DryRun,
    [datetime]$At = [datetime]::UtcNow,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# A release note is a sentence or two about what a person who runs an exit node will notice. The
# bound is generous where meshmaster.dk's is 120 characters, because that repository's notes are
# shown as a list on a page and these are read inside a GitHub release beside the flashing table.
$script:MaxNoteLength = 400

# What the pull request after a release is allowed to touch, and nothing else (deliverable 6).
# ci-commit will only walk back over a commit whose every file is one of these.
$script:ReleaseOwnFiles = @(
    'VERSION',
    'CHANGELOG.md',
    'lib/agent-core/library.json',
    'lib/agent-core/idf_component.yml',
    'lib/agent-core/include/agent/Version.h'
)
$script:ReleaseOwnPrefixes = @('release-notes/')

# The identity the workflow commits as. ci-commit insists on it: a commit touching only the files
# above, made by anybody else, is a commit ci did run on and must be waited for.
$script:BotEmail = '41898282+github-actions[bot]@users.noreply.github.com'
$script:BotName = 'github-actions[bot]'

# ---- the pieces ---------------------------------------------------------------------------------

function Resolve-Root {
    param([string]$Path)

    if (-not $Path) { $Path = Join-Path $PSScriptRoot '..' }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Read-Text {
    param([string]$Path)

    return ([System.IO.File]::ReadAllText($Path) -replace "`r`n", "`n")
}

function Write-Text {
    param([string]$Path, [string]$Content)

    [System.IO.File]::WriteAllText($Path, ($Content -replace "`r`n", "`n"))
}

function Invoke-Git {
    param([string]$Root, [string[]]$Arguments)

    # Windows PowerShell wraps a native command's stderr in an ErrorRecord when it is redirected,
    # and $ErrorActionPreference = 'Stop' then turns git's every warning into a terminating error.
    # Local to this function, so that a real failure of the script still stops it.
    $ErrorActionPreference = 'Continue'
    $output = & git -C $Root @Arguments 2>$null
    # `$null | ForEach-Object` runs the body ONCE in Windows PowerShell, so a git command that
    # printed nothing would come back as one empty line rather than as nothing. Hence the foreach.
    # The answer unrolls on the way out, as every PowerShell function's does, so EVERY caller here
    # wraps the call in @() before asking it for .Count or an element.
    $lines = @()
    foreach ($line in @($output)) { if ($null -ne $line) { $lines += "$line" } }
    return $lines
}

function Get-DeclaredVersion {
    param([string]$Root)

    $file = Join-Path $Root 'VERSION'
    if (-not (Test-Path -LiteralPath $file)) {
        throw "There is no VERSION at the root of $Root; it is the one place the version lives (docs/release.md)."
    }
    $value = (Read-Text $file).Trim()
    if ($value -notmatch '^\d+\.\d+\.\d+$') {
        throw "VERSION says '$value'; it holds one line, X.Y.Z, and it names the release that comes next."
    }
    return $value
}

function Get-NextPatch {
    param([string]$Version)

    $parts = $Version -split '\.'
    return "$($parts[0]).$($parts[1]).$([int]$parts[2] + 1)"
}

# The three manifests that carry the library's version, each with the one line that carries it.
# The pattern is the whole line, so a version inside a comment or a dependency's pin is not it.
function Get-VersionCarriers {
    param([string]$Root)

    return @(
        [ordered]@{
            path    = 'lib/agent-core/include/agent/Version.h'
            pattern = '(?m)^(#define AGENT_CORE_VERSION ")(\d+\.\d+\.\d+)(")$'
        },
        [ordered]@{
            path    = 'lib/agent-core/library.json'
            pattern = '(?m)^(\s*"version": ")(\d+\.\d+\.\d+)(",)$'
        },
        [ordered]@{
            path    = 'lib/agent-core/idf_component.yml'
            pattern = '(?m)^(version: ")(\d+\.\d+\.\d+)(")$'
        }
    )
}

function Get-CarriedVersion {
    param([string]$Root, $Carrier)

    $file = Join-Path $Root $Carrier.path
    if (-not (Test-Path -LiteralPath $file)) { throw "no $($Carrier.path)" }
    $text = Read-Text $file
    $found = [regex]::Matches($text, $Carrier.pattern)
    if ($found.Count -ne 1) {
        throw "$($Carrier.path) states the library's version on $($found.Count) line(s); exactly one is expected."
    }
    return $found[0].Groups[2].Value
}

function Set-CarriedVersion {
    param([string]$Root, $Carrier, [string]$Version)

    $file = Join-Path $Root $Carrier.path
    $text = Read-Text $file
    $replaced = [regex]::Replace($text, $Carrier.pattern, "`${1}$Version`${3}")
    Write-Text $file $replaced
}

# CHANGELOG.md's top section is headed with the version the repository names, so the file reads
# right AT THE TAG for a consumer that pins it: at v0.7.1 the first heading says v0.7.1, where it
# used to say "Unreleased". The Walter's upstream watch takes exactly that first section
# (twinoak.meshcore.walter-firmware, .github/workflows/upstream.yml: `git show <tag>:CHANGELOG.md |
# sed -n "/^## /,/^## /p"`), so this is what keeps that quotation honest.
function Get-ChangelogTop {
    param([string]$Root)

    $file = Join-Path $Root 'CHANGELOG.md'
    if (-not (Test-Path -LiteralPath $file)) { throw 'no CHANGELOG.md' }
    foreach ($line in (Read-Text $file) -split "`n") {
        if ($line -match '^## v(\d+\.\d+\.\d+)( |$)') { return $Matches[1] }
        if ($line -match '^## ') {
            throw ("CHANGELOG.md's first section is '$line'; " + 'the top section is headed with the version the repository names, as "## vX.Y.Z" or "## vX.Y.Z - a title" (docs/release.md).')
        }
    }
    throw 'CHANGELOG.md has no `## vX.Y.Z` section.'
}

function Open-ChangelogSection {
    param([string]$Root, [string]$Version)

    $file = Join-Path $Root 'CHANGELOG.md'
    $lines = @((Read-Text $file) -split "`n")
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## v\d+\.\d+\.\d+( |$)') {
            $before = @()
            if ($i -gt 0) { $before = @($lines[0..($i - 1)]) }
            $opened = $before + @("## v$Version", '', 'Nothing yet.', '') + @($lines[$i..($lines.Count - 1)])
            Write-Text $file ($opened -join "`n")
            return
        }
    }
    throw 'CHANGELOG.md has no `## vX.Y.Z` section to open the next one above.'
}

function Get-UnreleasedDirectory {
    param([string]$Root)

    return (Join-Path $Root 'release-notes/unreleased')
}

# The waiting lines, in the order the tasks MERGED: the commit that added each file, read along
# master's first parent so that a `--no-ff` merge of a task branch is the commit, and its date is
# the moment that task landed. A file git does not know yet - a line written in the working tree,
# which is every line while its own pull request is open - sorts last, by name.
function Get-Notes {
    param([string]$Root)

    $directory = Get-UnreleasedDirectory $Root
    if (-not (Test-Path -LiteralPath $directory)) { return @() }

    $notes = @()
    foreach ($file in @(Get-ChildItem -LiteralPath $directory -Filter '*.md' -File | Sort-Object -Property Name)) {
        $relative = "release-notes/unreleased/$($file.Name)"
        $stamp = [long]::MaxValue
        $merged = @(Invoke-Git $Root @('log', '--first-parent', '--diff-filter=A', '--format=%ct', '-1', '--', $relative))
        if ($merged.Count -gt 0 -and $merged[0] -match '^\d+$') { $stamp = [long]$merged[0] }
        $notes += [ordered]@{
            name   = $file.Name
            path   = $relative
            text   = Read-Note -Path $file.FullName -Relative $relative
            merged = $stamp
        }
    }

    return @($notes | Sort-Object -Property @{ Expression = { $_.merged } }, @{ Expression = { $_.name } })
}

# One paragraph, plain words, about what a person who runs an exit node will notice. No heading, no
# list marker, no blank line: the release renders it as one bullet and nothing else.
function Read-Note {
    param([string]$Path, [string]$Relative)

    $lines = @((Read-Text $Path) -split "`n")
    while ($lines.Count -gt 0 -and $lines[0].Trim().Length -eq 0) { $lines = @($lines[1..($lines.Count - 1)]) }
    while ($lines.Count -gt 0 -and $lines[-1].Trim().Length -eq 0) { $lines = @($lines[0..($lines.Count - 2)]) }

    if ($lines.Count -eq 0) { throw "$Relative is empty; a release note is one or two sentences about what a person will notice (docs/release.md)." }

    foreach ($line in $lines) {
        if ($line.Trim().Length -eq 0) { throw "$Relative has a blank line in it; a release note is one paragraph (docs/release.md)." }
        if ($line -match '^\s*(#|[-*+] |\||>)') { throw "$Relative begins a line with '$($line.Trim())'; a release note carries no heading, no list marker and no quote - the release makes the bullet (docs/release.md)." }
    }

    $text = (($lines | ForEach-Object { $_.Trim() }) -join ' ').Trim()
    if ($text.Length -gt $script:MaxNoteLength) {
        throw "$Relative is $($text.Length) characters; the bound is $script:MaxNoteLength (docs/release.md)."
    }
    return $text
}

function Format-Bullets {
    param($Notes)

    return (@($Notes | ForEach-Object { "- $($_.text)" }) -join "`n")
}

function Write-Pair {
    param([string]$Key, [string]$Value)

    Write-Output "$Key=$Value"
    if ($env:GITHUB_OUTPUT) { Add-Content -LiteralPath $env:GITHUB_OUTPUT -Value "$Key=$Value" }
}

# ---- check ---------------------------------------------------------------------------------------

function Invoke-Check {
    param([string]$Root, [switch]$Quiet)

    $version = Get-DeclaredVersion $Root
    $wrong = @()

    foreach ($carrier in Get-VersionCarriers $Root) {
        $carried = Get-CarriedVersion -Root $Root -Carrier $carrier
        if ($carried -ne $version) {
            $wrong += "$($carrier.path) says $carried where VERSION says $version."
        }
        else {
            Write-Output "$($carrier.path) -> $carried"
        }
    }

    $top = Get-ChangelogTop $Root
    if ($top -ne $version) {
        $wrong += "CHANGELOG.md's top section is headed v$top where VERSION says $version."
    }
    else {
        Write-Output "CHANGELOG.md -> v$top"
    }

    $gitkeep = Join-Path (Get-UnreleasedDirectory $Root) '.gitkeep'
    if (-not (Test-Path -LiteralPath $gitkeep)) {
        $wrong += 'release-notes/unreleased/.gitkeep is gone; it is what keeps the directory through a release that empties it.'
    }

    $notes = @(Get-Notes $Root)
    foreach ($note in $notes) { Write-Output "$($note.path) -> $($note.text.Length) characters" }

    if ($wrong.Count -gt 0) {
        throw ("The version is in one place, VERSION, and these do not agree with it:`n  " + ($wrong -join "`n  ") + "`nMove them together, or let the release's own pull request do it (docs/release.md).")
    }

    Write-Output "The release rails hold: VERSION $version, $($notes.Count) line(s) waiting."
    if (-not $Quiet) {
        Write-Pair 'version' $version
        Write-Pair 'lines' "$($notes.Count)"
    }
}

# ---- plan ----------------------------------------------------------------------------------------

function Invoke-Plan {
    param([string]$Root, [string]$Ref, [bool]$IsDryRun)

    $version = Get-DeclaredVersion $Root
    $tag = "v$version"
    $head = @(Invoke-Git $Root @('rev-parse', 'HEAD'))[0]
    $short = $head.Substring(0, 7)

    $refusals = @()

    # From master's head and no other commit. `ref` is what the press named; empty is the
    # workflow's own ref, which is master.
    $named = $Ref.Trim()
    if ($named -and $named -ne 'master' -and $named -ne 'refs/heads/master') {
        $refusals += "A release is made from master and from nothing else, and '$named' is not master - only a dry run may name another ref."
    }
    else {
        $masterHead = @(Invoke-Git $Root @('rev-parse', 'refs/remotes/origin/master'))
        if ($masterHead.Count -gt 0 -and $masterHead[0] -match '^[0-9a-f]{40}$' -and $masterHead[0] -ne $head) {
            $refusals += "A release is made from master's head, and $short is not it ($($masterHead[0].Substring(0, 7)) is) - pull and press again."
        }
    }

    $notes = @(Get-Notes $Root)
    if ($notes.Count -eq 0) {
        $refusals += 'There is nothing to release: no task has left a line in release-notes/unreleased/ since the last release.'
    }

    if (@(Invoke-Git $Root @('tag', '--list', $tag)).Count -gt 0) {
        $refusals += "The tag $tag already exists, so the version was not moved on after the last release: merge the open release pull request, or move VERSION and the three manifests to $(Get-NextPatch $version) by hand, and press again."
    }

    # The rails of `check` are a refusal too: a release must not publish a version the manifests
    # inside the image disagree with.
    try { Invoke-Check -Root $Root -Quiet | Out-Null }
    catch {
        $first = @($_.Exception.Message -split "`n")[0]
        $refusals += $first
    }

    if ($IsDryRun) {
        $full = "0.0.0-dryrun+$short"
        Write-Pair 'release' 'false'
    }
    else {
        $full = "$version+$short"
        Write-Pair 'release' 'true'
    }

    Write-Pair 'sha' $head
    Write-Pair 'version' $version
    Write-Pair 'tag' $tag
    Write-Pair 'full' $full
    Write-Pair 'lines' "$($notes.Count)"
    Write-Pair 'refusals' "$($refusals.Count)"

    foreach ($refusal in $refusals) {
        if ($IsDryRun) { Write-Output "would refuse a release: $refusal" }
        else { Write-Output "refused: $refusal" }
    }

    if ($refusals.Count -gt 0 -and -not $IsDryRun) { throw $refusals[0] }

    if ($IsDryRun) { Write-Output "Dry run of $tag from $short as $full; nothing will be published." }
    else { Write-Output "Releasing $tag from $short as $full, with $($notes.Count) line(s)." }
}

# ---- notes ---------------------------------------------------------------------------------------

function Invoke-Notes {
    param([string]$Root, [string]$Out)

    $notes = @(Get-Notes $Root)
    if ($notes.Count -eq 0) { throw 'There is nothing to release: no line is waiting in release-notes/unreleased/.' }

    $bullets = Format-Bullets $notes
    if ($Out) {
        # .NET's current directory is not PowerShell's, and Write-Text is [System.IO.File]'s.
        if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location).Path $Out }
        Write-Text $Out ($bullets + "`n")
    }
    Write-Output $bullets
    Write-Pair 'lines' "$($notes.Count)"
}

# ---- move ----------------------------------------------------------------------------------------

function Invoke-Move {
    param([string]$Root, [string]$Sha, [datetime]$At)

    $version = Get-DeclaredVersion $Root
    $notes = @(Get-Notes $Root)
    if ($notes.Count -eq 0) { throw "There is nothing to move: release-notes/unreleased/ is empty, so v$version published no line." }

    $bullets = Format-Bullets $notes
    $stamp = $At.ToUniversalTime().ToString('yyyy-MM-dd HH:mm')
    $from = 'an unnamed commit'
    if ($Sha) { $from = "``$($Sha.Substring(0, [Math]::Min(7, $Sha.Length)))``" }

    $archive = "release-notes/v$version.md"
    $published = @"
# v$version

Released $stamp UTC from $from. These are the bullets of that release's *What's new*, as published.

$bullets
"@
    Write-Text (Join-Path $Root $archive) ($published + "`n")

    foreach ($note in $notes) { Remove-Item -LiteralPath (Join-Path $Root $note.path) -Force }

    $next = Get-NextPatch $version
    Write-Text (Join-Path $Root 'VERSION') "$next`n"
    foreach ($carrier in Get-VersionCarriers $Root) { Set-CarriedVersion -Root $Root -Carrier $carrier -Version $next }
    Open-ChangelogSection -Root $Root -Version $next

    Write-Output "v${version}: $($notes.Count) line(s) into $archive, release-notes/unreleased/ emptied, the version moved to $next."
    Write-Pair 'moved' "$($notes.Count)"
    Write-Pair 'archive' $archive
    Write-Pair 'next' $next
}

# ---- ci-commit -----------------------------------------------------------------------------------

function Test-ReleaseOwnCommit {
    param([string]$Root, [string]$Commit)

    # A merge is never one of ours: the workflow squashes.
    $parents = @((@(Invoke-Git $Root @('rev-list', '--parents', '-n', '1', $Commit))[0]) -split ' ')
    if ($parents.Count -ne 2) { return $false }

    $author = @(Invoke-Git $Root @('log', '-1', '--format=%ae', $Commit))
    if ($author.Count -eq 0 -or $author[0] -ne $script:BotEmail) { return $false }

    $files = @(Invoke-Git $Root @('diff-tree', '--no-commit-id', '--name-only', '-r', $Commit))
    if ($files.Count -eq 0) { return $false }
    foreach ($file in $files) {
        if ($script:ReleaseOwnFiles -contains $file) { continue }
        $under = $false
        foreach ($prefix in $script:ReleaseOwnPrefixes) { if ($file.StartsWith($prefix)) { $under = $true } }
        if (-not $under) { return $false }
    }
    return $true
}

function Invoke-CiCommit {
    param([string]$Root, [string]$Sha)

    $commit = $Sha
    if (-not $commit) { $commit = 'HEAD' }
    $commit = @(Invoke-Git $Root @('rev-parse', $commit))[0]

    $skipped = @()
    # A cap, because a walk with no end is a worse failure than a wait: five is more release
    # pull requests in a row than can land without a release between them.
    for ($i = 0; $i -lt 5; $i++) {
        if (-not (Test-ReleaseOwnCommit -Root $Root -Commit $commit)) { break }
        $skipped += $commit
        $commit = @(Invoke-Git $Root @('rev-parse', "$commit^"))[0]
    }

    foreach ($one in $skipped) {
        Write-Output "$($one.Substring(0, 7)) is the release's own commit - the github-actions bot's, touching nothing but the release's own files - and has no ci run of its own."
    }
    Write-Output "ci is read from $($commit.Substring(0, 7))."
    Write-Pair 'ci_sha' $commit
    Write-Pair 'skipped' "$($skipped.Count)"
}

# ---- the self-test --------------------------------------------------------------------------------

function Invoke-SelfTest {
    function Confirm {
        param([string]$What, [scriptblock]$Body)

        try {
            & $Body
            $script:selfPasses++
            Write-Host "  ok    $What"
        }
        catch {
            $script:selfFailures += "$What : $($_.Exception.Message)"
            Write-Host "  FAIL  $What : $($_.Exception.Message)"
        }
    }

    $script:selfFailures = @()
    $script:selfPasses = 0

    $scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("release-selftest-" + [guid]::NewGuid().ToString('n').Substring(0, 8))
    New-Item -ItemType Directory -Path $scratch | Out-Null
    $me = $PSCommandPath

    # Each case is this same script, in a child process, so that what is tested is the exit code
    # and the words a workflow would see and not a function call. The child must not inherit
    # $GITHUB_OUTPUT, or every scratch repository's key=value would land in the real run's outputs.
    $host_ = 'powershell'
    if ($PSVersionTable.PSEdition -eq 'Core') { $host_ = 'pwsh' }
    $realOutput = $env:GITHUB_OUTPUT
    $env:GITHUB_OUTPUT = ''

    function Run {
        param([string]$Root, [string[]]$Arguments)

        # 2>&1, because what a case asserts about a refusal is the sentence, and a refusal writes
        # it to stderr. The preference is local: a child that fails is the point here, not an error.
        $ErrorActionPreference = 'Continue'
        $output = & $host_ -NoProfile -NonInteractive -File $me -Repository $Root @Arguments 2>&1
        return [pscustomobject]@{ code = $LASTEXITCODE; text = (@($output | ForEach-Object { "$_" }) -join "`n") }
    }

    function Seed {
        param([string]$Root, [string]$Version = '1.2.3')

        New-Item -ItemType Directory -Path (Join-Path $Root 'release-notes/unreleased') -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $Root 'lib/agent-core/include/agent') -Force | Out-Null
        Write-Text (Join-Path $Root 'VERSION') "$Version`n"
        Write-Text (Join-Path $Root 'release-notes/unreleased/.gitkeep') ''
        Write-Text (Join-Path $Root 'lib/agent-core/include/agent/Version.h') "#pragma once`n`n#define AGENT_CORE_VERSION `"$Version`"`n"
        Write-Text (Join-Path $Root 'lib/agent-core/library.json') "{`n  `"name`": `"agent-core`",`n  `"version`": `"$Version`",`n  `"license`": `"MIT`"`n}`n"
        Write-Text (Join-Path $Root 'lib/agent-core/idf_component.yml') "## a comment with version: v0.0.1 in it`nversion: `"$Version`"`ndescription: scratch`n"
        Write-Text (Join-Path $Root 'CHANGELOG.md') "# Changelog`n`nThe preamble.`n`n## v$Version`n`nNothing yet.`n`n## v1.2.2 - the one before`n`nSomething.`n"

        $ErrorActionPreference = 'Continue'
        & git -C $Root init --quiet --initial-branch=master 2>$null | Out-Null
        & git -C $Root config user.name 'Scratch' | Out-Null
        & git -C $Root config user.email 'scratch@example.invalid' | Out-Null
        # The scratch repository's files are written with LF here; without this git says so, five
        # times per commit, and the self-test's output is then mostly warnings.
        & git -C $Root config core.autocrlf false | Out-Null
        & git -C $Root add -A | Out-Null
        & git -C $Root commit --quiet -m 'the scratch repository' | Out-Null
        & git -C $Root update-ref refs/remotes/origin/master HEAD | Out-Null
    }

    # A note landed by a merge commit at a given moment, the way a task's pull request lands it.
    function Land {
        param([string]$Root, [string]$Name, [string]$Text, [string]$When)

        $ErrorActionPreference = 'Continue'
        $branch = "task/$($Name -replace '\.md$', '')"
        & git -C $Root checkout --quiet -b $branch | Out-Null
        Write-Text (Join-Path $Root "release-notes/unreleased/$Name") "$Text`n"
        & git -C $Root add -A | Out-Null
        $env:GIT_AUTHOR_DATE = $When
        $env:GIT_COMMITTER_DATE = $When
        & git -C $Root commit --quiet -m "the line of $Name" | Out-Null
        & git -C $Root checkout --quiet master | Out-Null
        & git -C $Root merge --quiet --no-ff -m "Merge $branch" $branch | Out-Null
        Remove-Item Env:GIT_AUTHOR_DATE, Env:GIT_COMMITTER_DATE
        & git -C $Root update-ref refs/remotes/origin/master HEAD | Out-Null
    }

    function New-Scratch {
        param([string]$Name)

        $root = Join-Path $scratch $Name
        New-Item -ItemType Directory -Path $root | Out-Null
        Seed -Root $root
        return $root
    }

    Write-Host 'tools/release.ps1 --self-test, against a scratch repository:'

    # 1. The rails hold on a tree that is in order.
    $a = New-Scratch 'in-order'
    Confirm 'check passes on a tree whose version is in one place' {
        $r = Run -Root $a -Arguments @('-Action', 'check')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch 'VERSION 1\.2\.3') { throw "did not name the version: $($r.text)" }
    }

    # 2. A manifest that disagrees is the rail of deliverable 1.
    $b = New-Scratch 'manifest-adrift'
    Write-Text (Join-Path $b 'lib/agent-core/library.json') "{`n  `"name`": `"agent-core`",`n  `"version`": `"1.2.2`",`n  `"license`": `"MIT`"`n}`n"
    Confirm 'check fails when a manifest does not say what VERSION says' {
        $r = Run -Root $b -Arguments @('-Action', 'check')
        if ($r.code -eq 0) { throw 'it passed' }
        if ($r.text -notmatch 'library\.json says 1\.2\.2 where VERSION says 1\.2\.3') { throw "did not name the file: $($r.text)" }
    }

    # 3. And the changelog's top section is headed with it.
    $c = New-Scratch 'changelog-adrift'
    Write-Text (Join-Path $c 'CHANGELOG.md') "# Changelog`n`n## Unreleased`n`nNothing yet.`n"
    Confirm 'check fails when the changelog still says Unreleased' {
        $r = Run -Root $c -Arguments @('-Action', 'check')
        if ($r.code -eq 0) { throw 'it passed' }
        if ($r.text -notmatch 'Unreleased') { throw "did not quote the heading: $($r.text)" }
    }

    # 4. The format of a line.
    $d = New-Scratch 'bad-lines'
    Write-Text (Join-Path $d 'release-notes/unreleased/90.md') "- a bullet marker`n"
    Confirm 'a line that carries a list marker is refused' {
        $r = Run -Root $d -Arguments @('-Action', 'check')
        if ($r.code -eq 0) { throw 'it passed' }
        if ($r.text -notmatch 'list marker') { throw "did not say why: $($r.text)" }
    }
    Write-Text (Join-Path $d 'release-notes/unreleased/90.md') ("x" * ($script:MaxNoteLength + 1) + "`n")
    Confirm 'a line past the bound is refused' {
        $r = Run -Root $d -Arguments @('-Action', 'check')
        if ($r.code -eq 0) { throw 'it passed' }
        if ($r.text -notmatch "the bound is $script:MaxNoteLength") { throw "did not say the bound: $($r.text)" }
    }
    Write-Text (Join-Path $d 'release-notes/unreleased/90.md') "one paragraph`n`nand a second`n"
    Confirm 'a line that is two paragraphs is refused' {
        $r = Run -Root $d -Arguments @('-Action', 'check')
        if ($r.code -eq 0) { throw 'it passed' }
        if ($r.text -notmatch 'one paragraph') { throw "did not say why: $($r.text)" }
    }

    # 5. Nothing to release.
    $e = New-Scratch 'nothing-waiting'
    Confirm 'a release with no waiting line is refused, in one sentence' {
        $r = Run -Root $e -Arguments @('-Action', 'plan')
        if ($r.code -eq 0) { throw 'it published' }
        if ($r.text -notmatch 'There is nothing to release') { throw "did not say why: $($r.text)" }
    }

    # 6. The tag of the version already exists: the version was not moved on.
    $f = New-Scratch 'tag-already'
    Land -Root $f -Name '52.md' -Text 'The button publishes.' -When '2026-11-13T09:00:00Z'
    & git -C $f tag v1.2.3 | Out-Null
    Confirm 'a release whose tag exists is refused, and the sentence says how to finish it' {
        $r = Run -Root $f -Arguments @('-Action', 'plan')
        if ($r.code -eq 0) { throw 'it published' }
        if ($r.text -notmatch 'The tag v1\.2\.3 already exists') { throw "did not say why: $($r.text)" }
        if ($r.text -notmatch 'move VERSION and the three manifests to 1\.2\.4') { throw "did not say how to finish it: $($r.text)" }
    }

    # 7. A ref that is not master.
    $g = New-Scratch 'another-ref'
    Land -Root $g -Name '52.md' -Text 'The button publishes.' -When '2026-11-13T09:00:00Z'
    Confirm 'a release from a ref that is not master is refused' {
        $r = Run -Root $g -Arguments @('-Action', 'plan', '-Ref', 'task/p1.11-52')
        if ($r.code -eq 0) { throw 'it published' }
        if ($r.text -notmatch "'task/p1\.11-52' is not master") { throw "did not say why: $($r.text)" }
    }
    Confirm 'and a dry run may name one, and refuses nothing' {
        $r = Run -Root $g -Arguments @('-Action', 'plan', '-Ref', 'task/p1.11-52', '-DryRun')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch 'release=false') { throw "did not say it releases nothing: $($r.text)" }
        if ($r.text -notmatch 'full=0\.0\.0-dryrun\+') { throw "did not name the dry run's version: $($r.text)" }
        if ($r.text -notmatch 'would refuse a release') { throw "did not rehearse the refusal: $($r.text)" }
    }

    # 8. Two lines compile into What's new in the order the tasks MERGED, not the order of names.
    $h = New-Scratch 'two-lines'
    Land -Root $h -Name '90.md' -Text 'The second one by name, merged first.' -When '2026-11-13T09:00:00Z'
    Land -Root $h -Name '10.md' -Text 'The first one by name, merged second.' -When '2026-11-13T10:00:00Z'
    Confirm 'two lines compile into What''s new in merge order' {
        $r = Run -Root $h -Arguments @('-Action', 'notes')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        $bullets = @(($r.text -split "`n") | Where-Object { $_ -like '- *' })
        if ($bullets.Count -ne 2) { throw "got $($bullets.Count) bullet(s): $($r.text)" }
        if ($bullets[0] -notmatch 'merged first') { throw "the order is not the merge order: $($r.text)" }
        if ($bullets[1] -notmatch 'merged second') { throw "the order is not the merge order: $($r.text)" }
    }
    Confirm 'and the release itself is planned, from master''s head' {
        $r = Run -Root $h -Arguments @('-Action', 'plan')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch 'release=true') { throw "not a release: $($r.text)" }
        if ($r.text -notmatch 'tag=v1\.2\.3') { throw "wrong tag: $($r.text)" }
        if ($r.text -notmatch 'lines=2') { throw "wrong count: $($r.text)" }
    }

    # 9. A release asked for from a commit that is not master's head.
    Confirm 'a release from a commit behind master''s head is refused' {
        & git -C $h update-ref refs/remotes/origin/master HEAD | Out-Null
        & git -C $h checkout --quiet 'HEAD~1' | Out-Null
        $r = Run -Root $h -Arguments @('-Action', 'plan')
        & git -C $h checkout --quiet master | Out-Null
        if ($r.code -eq 0) { throw 'it published' }
        if ($r.text -notmatch "is not it") { throw "did not say why: $($r.text)" }
    }

    # 10. The move after a release.
    $stamp = [datetime]::new(2026, 11, 13, 12, 34, 0, [System.DateTimeKind]::Utc)
    Confirm 'after a release the lines are archived, the version moves on, the changelog opens the next section' {
        $sha = (& git -C $h rev-parse HEAD).Trim()
        $r = Run -Root $h -Arguments @('-Action', 'move', '-Sha', $sha, '-At', '2026-11-13T12:34:00Z')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }

        $archive = Join-Path $h 'release-notes/v1.2.3.md'
        if (-not (Test-Path -LiteralPath $archive)) { throw 'no release-notes/v1.2.3.md' }
        $published = Read-Text $archive
        if ($published -notmatch '(?m)^# v1\.2\.3$') { throw "the archive is not headed with the version: $published" }
        if ($published -notmatch 'merged first') { throw "the archive lost a line: $published" }
        if ($published -notmatch '2026-11-13 12:34 UTC') { throw "the archive is not stamped: $published" }
        $order = $published.IndexOf('merged first'), $published.IndexOf('merged second')
        if ($order[0] -gt $order[1]) { throw 'the archive is not in merge order' }

        $waiting = @(Get-ChildItem -LiteralPath (Join-Path $h 'release-notes/unreleased') -Filter '*.md' -File)
        if ($waiting.Count -ne 0) { throw "release-notes/unreleased/ still holds $($waiting.Count) file(s)" }
        if (-not (Test-Path -LiteralPath (Join-Path $h 'release-notes/unreleased/.gitkeep'))) { throw 'the .gitkeep went with them' }

        if ((Read-Text (Join-Path $h 'VERSION')).Trim() -ne '1.2.4') { throw 'VERSION did not move on' }
        foreach ($carrier in Get-VersionCarriers $h) {
            $carried = Get-CarriedVersion -Root $h -Carrier $carrier
            if ($carried -ne '1.2.4') { throw "$($carrier.path) says $carried and not 1.2.4" }
        }

        $changelog = Read-Text (Join-Path $h 'CHANGELOG.md')
        $headings = @(($changelog -split "`n") | Where-Object { $_ -like '## *' })
        if ($headings[0] -ne '## v1.2.4') { throw "the changelog's top section is '$($headings[0])'" }
        if ($headings[1] -ne '## v1.2.3') { throw "the released section is not second: '$($headings[1])'" }
    }

    Confirm 'and the rails hold again on what the move left' {
        $r = Run -Root $h -Arguments @('-Action', 'check')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch 'VERSION 1\.2\.4') { throw "did not name the new version: $($r.text)" }
    }

    Confirm 'and the next release refuses, because nothing is waiting again' {
        $r = Run -Root $h -Arguments @('-Action', 'plan')
        if ($r.code -eq 0) { throw 'it published' }
        if ($r.text -notmatch 'There is nothing to release') { throw "did not say why: $($r.text)" }
    }

    # 11. The ci is green gate at the release after: the bot's commit has no ci run of its own.
    Confirm 'the ci gate reads the commit below the release''s own pull request' {
        & git -C $h add -A | Out-Null
        & git -C $h -c "user.name=$script:BotName" -c "user.email=$script:BotEmail" commit --quiet -m 'Release notes: v1.2.3' | Out-Null
        $below = (& git -C $h rev-parse 'HEAD~1').Trim()
        $r = Run -Root $h -Arguments @('-Action', 'ci-commit')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch "ci_sha=$below") { throw "did not walk back to $below : $($r.text)" }
        if ($r.text -notmatch 'skipped=1') { throw "did not count the commit it skipped: $($r.text)" }
    }

    Confirm 'and stops at a commit that is somebody else''s, or touches anything else' {
        Write-Text (Join-Path $h 'README.md') "a change of a file that is not the release's own`n"
        & git -C $h add -A | Out-Null
        & git -C $h -c "user.name=$script:BotName" -c "user.email=$script:BotEmail" commit --quiet -m 'something else' | Out-Null
        $head = (& git -C $h rev-parse HEAD).Trim()
        $r = Run -Root $h -Arguments @('-Action', 'ci-commit')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch "ci_sha=$head") { throw "it walked past a commit of its own: $($r.text)" }
        if ($r.text -notmatch 'skipped=0') { throw "it skipped something: $($r.text)" }
    }

    Confirm 'and at a release-shaped commit that is a person''s, because ci ran on that one' {
        Write-Text (Join-Path $h 'VERSION') "1.2.5`n"
        foreach ($carrier in Get-VersionCarriers $h) { Set-CarriedVersion -Root $h -Carrier $carrier -Version '1.2.5' }
        & git -C $h add -A | Out-Null
        & git -C $h commit --quiet -m 'a bigger step, by hand' | Out-Null
        $head = (& git -C $h rev-parse HEAD).Trim()
        $r = Run -Root $h -Arguments @('-Action', 'ci-commit')
        if ($r.code -ne 0) { throw "exit $($r.code): $($r.text)" }
        if ($r.text -notmatch "ci_sha=$head") { throw "it walked past a person's commit: $($r.text)" }
    }

    if (-not $env:RELEASE_SELFTEST_KEEP) { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }
    else { Write-Host "the scratch repositories are in $scratch" }
    $env:GITHUB_OUTPUT = $realOutput

    Write-Host ''
    if ($script:selfFailures.Count -gt 0) {
        Write-Host "$($script:selfPasses) passed, $($script:selfFailures.Count) FAILED."
        throw ($script:selfFailures -join "`n")
    }
    Write-Host "$($script:selfPasses) cases, all green."
}

# ---- the door --------------------------------------------------------------------------------------

if ($SelfTest) {
    Invoke-SelfTest
    return
}

if (-not $Action) { throw 'Name an -Action (check, plan, notes, move, ci-commit) or -SelfTest. Get-Help .\tools\release.ps1 -Detailed.' }

$root = Resolve-Root $Repository

switch ($Action) {
    'check' { Invoke-Check -Root $root }
    'plan' { Invoke-Plan -Root $root -Ref $Ref -IsDryRun ([bool]$DryRun) }
    'notes' { Invoke-Notes -Root $root -Out $Out }
    'move' { Invoke-Move -Root $root -Sha $Sha -At $At }
    'ci-commit' { Invoke-CiCommit -Root $root -Sha $Sha }
}
