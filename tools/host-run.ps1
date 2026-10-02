<#
.SYNOPSIS
  The host harness against meshmaster.dk's compose stack: the desk twin of the bench walk.

.DESCRIPTION
  This is the other half of tools/host-stub.py. The stub replays a recorded server so that CI can
  run one cycle with no stack at all; this script runs the same host program against a *real*
  meshmaster.dk on the workstation, so that what the firmware says and what the server stores are
  held against each other rather than against a fixture.

  It is the desk twin of the walk in docs/console.md, and it says so: everything here is the
  protocol and none of it is the radio. The bench list of every task's report is still the bench
  list.

  What it does, in order:

    1. The stack. The clone of twinoak/meshmaster.dk beside this repository (or -Stack), at
       -Commit if one is named, and "docker compose up -d" unless -SkipCompose. Waits for it.
    2. The token. A personal access token with `read` is all the assertions below need, and the
       seeded admin's is the default (-AdminToken), so a run needs nothing typed. A token with more
       than `read` changes nothing here: the actions that need more are gated, and a token is always
       refused one (see the note below).
    3. The register. Finds the exit node and the repeater whose public key the scenario uses, so
       that the fake repeater of src/host/HostMesh.h and the row on the server are the same thing.
    4. The run. "pio run -e host", then the program against the stack with the scenario and the
       journal, for one cycle of whatever the server has queued.
    5. The assertions. On the server's side: the exit node's record (transport onboard, the board,
       the companion key, the thirteen job types), the jobs page, the per-key change rows, the
       association's state. On the fake's side: the journal - the timestamps rising, and no
       password anywhere in it.
    6. -Record writes what it saw into tests/fixtures/server/one-cycle.json, which CI replays.

.NOTES
  WHAT THIS SCRIPT CANNOT DO, AND WHY. meshmaster.dk closes four doors to a script on purpose. By
  its own spec/API.md section 2 and decision D-26:

    - POST /api/v1/me/tokens is 501: a token is issued in the web UI and nowhere else. A script
      *can* drive that page - sign in with the seed admin's password and a TOTP code, then post the
      new-token form - and task p1.1-08 proved it by hand with curl. This script does not: the
      application's cookies are `Secure` unconditionally (meshmaster.dk docs/dev/local.md) and
      .NET's CookieContainer will not send a Secure cookie over plain HTTP, so Windows PowerShell
      cannot hold that session at all. The token is therefore a parameter, and the seeded one is
      the default.
    - A personal access token CANNOT re-authenticate, so every gated action answers
      reauth-required to one, for ever. The gated ones this walk needs are
      POST /repeaters/{id}/associations (and /advance, /retry), POST /repeaters/{id}/apply,
      POST /repeaters/{id}/cli and POST /repeaters/{id}/credentials/rotate.
    - Session cookies are NOT accepted on /api/v1 at all: a browser session is a stranger there.

  So the association, the apply, the CLI job and the rotation have exactly one door between them -
  a Razor page, posted with a session behind a fresh second factor. This script does not drive
  pages: it prints the walk and the assertion for each, the way a report's "Not verified here" list
  does, and exits 3. That is not a gap in the harness; it is the server's trust boundary working,
  and it is better said than gone round.

  Everything that is not behind that boundary is scripted, asserted and recorded here.

  Windows PowerShell 5.1: no pipeline chain operators, no -SkipHttpErrorCheck, and a 302 from
  Invoke-WebRequest with -MaximumRedirection 0 is an exception rather than an answer. Every call
  below goes through Invoke-Http, which is the one place that knows it.

.EXAMPLE
  ./tools/host-run.ps1
.EXAMPLE
  ./tools/host-run.ps1 -Record -Scenario tests/fixtures/host/compose-stack.json
#>
[CmdletBinding()]
param(
    # The clone of twinoak/meshmaster.dk. The default is beside this repository, which is where
    # "git clone" puts it.
    [string]$Stack,
    # Check the stack out at this commit first. Without it, whatever the clone is at is used and
    # the script says which commit that was.
    [string]$Commit,
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    # The exit node's agent token. The default is the seeded one of docs/dev/local.md.
    [string]$AgentToken = 'mma_meshmaster-dev-seed-agent-00000000000000001',
    # A personal access token with at least `read`. The default is the seeded admin's, which
    # meshmaster.dk's docs/dev/local.md publishes; it is a development value and nobody's secret.
    # `read` is all this script asserts with - everything that needs more is gated (see the header).
    [string]$AdminToken = 'mm_meshmaster-dev-seed-token-00000000000000001',
    [string]$Scenario = 'tests/fixtures/host/compose-stack.json',
    [string]$Journal = 'out/host-run-journal.json',
    [string]$Recording = 'tests/fixtures/server/one-cycle.json',
    [int]$MaxSeconds = 2700,
    [switch]$Record,
    [switch]$SkipCompose,
    [switch]$SkipBuild,
    # Build and run the host program in a container. On Windows this is the only way: there is no
    # g++ and .pio/build/host/program is a Linux binary (tasks/RUNBOOK.md section 2,
    # docs/host.md section 1). The container reaches the stack at host.docker.internal, whose
    # address the client checks like any other - it has to be a private one.
    [switch]$Docker,
    [string]$Image = 'python:3.11-slim',
    # The name of a container that already has g++ and PlatformIO, so a run is seconds rather than
    # a minute of apt-get. Created if it is not there.
    [string]$Container = 'mmhost'
)

$ErrorActionPreference = 'Stop'

# A local whose name matches a parameter's is the same variable: `$journal = @(...)` would assign an
# array to the [string]$Journal above and `.Count` would be 1 for ever. Both of this script's
# collisions (that one and $record against -Record) cost a debugging round; the locals below are
# named so that there are none.
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Stack) { $Stack = Join-Path (Split-Path -Parent $repo) 'meshmaster.dk' }
# Windows has no g++, so the program is built and run in a container unless somebody says otherwise.
if (-not $PSBoundParameters.ContainsKey('Docker') -and ($env:OS -eq 'Windows_NT')) { $Docker = $true }
$script:failures = @()
$script:passed = 0

function Say([string]$text) { Write-Host $text }
function Step([string]$text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }

# `$ok` is deliberately untyped. PowerShell's comparison operators answer a *filtered array* when
# their left side is one, and a [bool] parameter then throws something that says nothing about which
# assertion it was - so the coercion happens here, named, and an answer that is not a plain true or
# false is reported as the fault it is rather than crashing the run.
function Check($ok, [string]$what, [string]$detail) {
    if ($ok -isnot [bool]) {
        $kind = 'nothing'
        if ($null -ne $ok) { $kind = $ok.GetType().Name }
        Write-Host "  (the assertion '$what' was handed a $kind, not a true or false)" -ForegroundColor DarkYellow
        $ok = [bool]@($ok).Count -and ([bool](@($ok)[0]))
    }
    if ($ok) {
        $script:passed++
        Write-Host "  ok      $what" -ForegroundColor DarkGreen
        return
    }
    $line = $what
    if ($detail) { $line = "$what : $detail" }
    $script:failures += $line
    Write-Host "  FAILED  $line" -ForegroundColor Red
}

# One HTTP call, with the answer whatever its status.
#
# Windows PowerShell 5.1 makes this awkward in two different ways, and both have to be handled or a
# redirect and a 404 look the same as a stack that is not there:
#   - a 4xx or a 5xx is a System.Net.WebException, whose Response carries the answer;
#   - `-MaximumRedirection 0` is an *InvalidOperationException* that is written as an error while the
#     302 is returned all the same.
# So every call is made with -ErrorAction SilentlyContinue and an error variable, and the answer is
# taken from whichever of the two has it.
function Invoke-Http {
    param(
        [string]$Uri,
        [string]$Method = 'GET',
        $Body,
        [hashtable]$Headers,
        $WebSession,
        [string]$ContentType,
        [int]$MaximumRedirection = 5
    )
    $request = @{ Uri = $Uri; Method = $Method; UseBasicParsing = $true
                  MaximumRedirection = $MaximumRedirection }
    if ($Body) { $request.Body = $Body }
    if ($Headers) { $request.Headers = $Headers }
    if ($WebSession) { $request.WebSession = $WebSession }
    if ($ContentType) { $request.ContentType = $ContentType }

    # Quiet in this scope: -ErrorAction alone does not stop 5.1 printing a WebException's body, and
    # a 404 this script expects must not look like a crash.
    $ErrorActionPreference = 'SilentlyContinue'
    $problems = $null
    $answer = Invoke-WebRequest @request -ErrorAction SilentlyContinue -ErrorVariable problems
    if ($answer) {
        $location = $null
        try { $location = $answer.Headers['Location'] } catch { }
        return [pscustomobject]@{ Status = [int]$answer.StatusCode; Content = $answer.Content
                                  Location = $location }
    }
    foreach ($problem in @($problems)) {
        $response = $null
        try { $response = $problem.Exception.Response } catch { }
        if (-not $response) { continue }
        $content = ''
        try {
            $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
            $content = $reader.ReadToEnd()
            $reader.Close()
        } catch { }
        $location = $null
        try { $location = $response.Headers['Location'] } catch { }
        return [pscustomobject]@{ Status = [int]$response.StatusCode; Content = $content; Location = $location }
    }
    $why = 'no answer'
    if ($problems) { $why = (@($problems)[0]).ToString() }
    return [pscustomobject]@{ Status = 0; Content = ''; Location = $null; Why = $why }
}

function Api([string]$Pat, [string]$Path, [string]$Method = 'GET', $Body) {
    $json = $null
    if ($Body) { $json = ($Body | ConvertTo-Json -Depth 8 -Compress) }
    $answer = Invoke-Http -Uri "$BaseUrl/api/v1$Path" -Method $Method -Body $json `
        -Headers @{ Authorization = "Bearer $Pat" } -ContentType 'application/json'
    $parsed = $null
    if ($answer.Content) { try { $parsed = $answer.Content | ConvertFrom-Json } catch { } }
    return [pscustomobject]@{ Status = $answer.Status; Body = $parsed; Raw = $answer.Content }
}

function AgentApi([string]$Path, [string]$Method = 'GET', $Body) {
    $json = $null
    if ($Body) { $json = ($Body | ConvertTo-Json -Depth 8 -Compress) }
    $answer = Invoke-Http -Uri "$BaseUrl/agent/v1$Path" -Method $Method -Body $json `
        -Headers @{ Authorization = "Bearer $AgentToken"; 'X-Agent-Version' = '0.0.0-dev' } `
        -ContentType 'application/json'
    $parsed = $null
    if ($answer.Content) { try { $parsed = $answer.Content | ConvertFrom-Json } catch { } }
    return [pscustomobject]@{ Status = $answer.Status; Body = $parsed; Raw = $answer.Content }
}

# ------------------------------------------------------------------ 1. the stack

Step 'The stack'
$stackPath = $null
if (Test-Path $Stack) { $stackPath = (Resolve-Path $Stack).Path }
if (-not $stackPath) {
    throw "there is no clone of twinoak/meshmaster.dk at '$Stack'. Clone it beside this repository, or pass -Stack."
}
if ($Commit) {
    Say "  checking out $Commit"
    git -C $stackPath fetch --quiet origin
    git -C $stackPath checkout --quiet $Commit
}
$at = (git -C $stackPath rev-parse --short HEAD) | Out-String
$at = $at.Trim()
$dirty = git -C $stackPath status --porcelain
$dirtyNote = ''
if ($dirty) { $dirtyNote = ' (with uncommitted changes)' }
Say "  $stackPath at $at$dirtyNote"
if (-not $SkipCompose) {
    if (-not (Test-Path (Join-Path $stackPath '.env'))) {
        Copy-Item (Join-Path $stackPath '.env.example') (Join-Path $stackPath '.env')
        Say '  .env was copied from .env.example (development values, public in that repository)'
    }
    Say '  docker compose up -d'
    Push-Location $stackPath
    try { docker compose up -d | Out-Null } finally { Pop-Location }
}

$healthy = $false
for ($i = 0; ($i -lt 60) -and (-not $healthy); $i++) {
    $probe = $null
    try { $probe = Invoke-Http -Uri "$BaseUrl/" -MaximumRedirection 0 } catch { }
    if ($probe -and ($probe.Status -lt 500)) { $healthy = $true } else { Start-Sleep -Seconds 2 }
}
Check $healthy "$BaseUrl answers" 'the stack did not come up in two minutes'
if (-not $healthy) { exit 1 }

# ------------------------------------------------------------------ 2. a session and a token

Step 'The token'
$pat = $AdminToken
Check ($pat -like 'mm_*') 'a personal access token was given' "-AdminToken is '$pat'"

$repeater = $null
$agentId = 0
$me = Api $pat '/me'
Check ($me.Status -eq 200) 'the token answers on /api/v1/me' "http $($me.Status)"
if ($me.Status -eq 200) { Say "  $($me.Body.name), scopes: $($me.Body.scopes -join ', ')" }

# --------------------------------------------------------------- 3. the register

Step 'The register, and the exit node'
$scenarioPath = Join-Path $repo $Scenario
if (-not (Test-Path $scenarioPath)) { throw "there is no scenario at $scenarioPath" }
$scenarioDoc = Get-Content $scenarioPath -Raw | ConvertFrom-Json
$wantedKey = $scenarioDoc.repeaters[0].publicKey
$exitNodeKey = $scenarioDoc.exitNode.publicKey
Say "  the scenario's first repeater is $($wantedKey.Substring(0,12))..., this exit node is $($exitNodeKey.Substring(0,12))..."

$config = AgentApi '/config'
Check ($config.Status -eq 200) 'the agent token answers on /agent/v1/config' "http $($config.Status)"
if ($config.Status -ne 200) { throw 'the agent token was refused: pass -AgentToken' }
$agentId = $config.Body.agentId
Say "  exit node $agentId `"$($config.Body.label)`", budget $($config.Body.budget.maxTransactionsPerMinute)/min gap $($config.Body.budget.minGapSeconds) s, poll $($config.Body.pollIntervalS) s"

$repeaters = Api $pat '/repeaters?limit=200'
if ($repeaters.Status -eq 200) {
    $repeater = $repeaters.Body.items | Where-Object { $_.publicKey -eq $wantedKey } | Select-Object -First 1
}
Check ($null -ne $repeater) "the register holds a repeater with the scenario's key" `
    "no repeater has $wantedKey - point the scenario at one that exists, or adopt the key in the UI"
if ($repeater) { Say "  repeater $($repeater.id) `"$($repeater.label)`", stage $($repeater.stage)" }

# --------------------------------------------------------------- 4. the run

Step 'The run'
$journalPath = Join-Path $repo $Journal
New-Item -ItemType Directory -Force (Split-Path -Parent $journalPath) | Out-Null
if (Test-Path $journalPath) { Remove-Item $journalPath }
$logPath = Join-Path $repo 'out/host-run.log'
$before = Get-Date

if ($Docker) {
    $running = docker ps --filter "name=^/$Container$" --format '{{.Names}}'
    if (-not $running) {
        Say "  starting the container $Container from $Image"
        docker rm -f $Container 2>$null | Out-Null
        docker run -d --name $Container -v "$($repo):/w" -w /w $Image sleep infinity | Out-Null
        docker exec $Container bash -lc 'apt-get update -qq && apt-get install -y -qq g++ git >/dev/null 2>&1 && pip install -q platformio==6.1.19' | Out-Null
    }
    if (-not $SkipBuild) {
        Say '  pio run -e host (in the container)'
        docker exec $Container bash -lc 'pio run -e host'
        if ($LASTEXITCODE -ne 0) { throw 'pio run -e host failed in the container' }
    }
    # The stack is on this machine's loopback, which is not the container's: host.docker.internal
    # is how a container reaches it, and the client checks the address it resolves to like any
    # other (docs/host.md section 5).
    $containerUrl = $BaseUrl -replace '127\.0\.0\.1|localhost', 'host.docker.internal'
    # The two paths as the container sees them: the repository is mounted at /w, and a path typed
    # with backslashes is turned round for it.
    $relativeScenario = $Scenario.Replace('\', '/')
    $relativeJournal = $Journal.Replace('\', '/')
    Say "  the program, in $Container, against $containerUrl"
    docker exec $Container bash -lc "/w/.pio/build/host/program --url '$containerUrl' --token '$AgentToken' --board heltec_v3 --key '$exitNodeKey' --scenario '/w/$relativeScenario' --journal '/w/$relativeJournal' --idle-exits 2 --max-seconds $MaxSeconds --log-level info 2>&1" |
        Tee-Object -FilePath $logPath
    $programExit = $LASTEXITCODE
} else {
    if (-not $SkipBuild) {
        Say '  pio run -e host'
        & pio run -e host
        if ($LASTEXITCODE -ne 0) { throw 'pio run -e host failed' }
    }
    $program = Join-Path $repo '.pio/build/host/program'
    if (-not (Test-Path $program)) {
        throw 'there is no host program at .pio/build/host/program. On Windows there is no g++: pass -Docker (docs/host.md section 1).'
    }
    Say "  $program --url $BaseUrl --token <the exit node's> --board heltec_v3"
    & $program --url $BaseUrl --token $AgentToken --board heltec_v3 --key $exitNodeKey `
        --scenario $scenarioPath --journal $journalPath --idle-exits 2 --max-seconds $MaxSeconds `
        --log-level info 2>&1 | Tee-Object -FilePath $logPath
    $programExit = $LASTEXITCODE
}
$took = [int]((Get-Date) - $before).TotalSeconds
Say "  the program exited $programExit after $took s; its output is out/host-run.log"
# Exit 2 is --max-seconds with work still going, and with nothing queued that is what a run against
# a stack whose job doors are all behind the gate looks like. It is said rather than failed.
if ($programExit -eq 2) {
    Say '  (exit 2: nothing was queued for this exit node, so the cycle never started. The four'
    Say '  steps at the end of this run are what queue one.)'
} else {
    Check ($programExit -eq 0) 'the program exited 0' "it exited $programExit"
}

# --------------------------------------------------------------- 5. the assertions

Step 'What the server stored'
$exitNodeRecord = Api $pat "/agents/$agentId"
Check ($exitNodeRecord.Status -eq 200) "GET /agents/$agentId" "http $($exitNodeRecord.Status)"
if ($exitNodeRecord.Status -eq 200) {
    $reported = $exitNodeRecord.Body.reported
    # `transport` and `board`: the heartbeat carries both (spec/API.md section 3 - the record is
    # filled from it, and the exit node's page shows "onboard . Heltec V3"), so this is where they
    # should come back. Either the record or the DTO is where they are not: see
    # tasks/phase-1.1/FOLLOWUPS.md, which is meshmaster.dk's to settle.
    $reportedTransport = $reported.transport
    if (-not $reportedTransport) { $reportedTransport = $exitNodeRecord.Body.transport }
    Check ($reportedTransport -eq 'onboard') 'the exit node reports transport onboard' `
        "the record says '$reportedTransport', and its reported block has no transport key at all - tasks/phase-1.1/FOLLOWUPS.md"
    Check ($reported.board -eq 'heltec_v3') 'the exit node reports board heltec_v3' `
        'the reported block has no board key at all, though the heartbeat carried one - tasks/phase-1.1/FOLLOWUPS.md' 
    Check ($reported.companionPublicKey -eq $exitNodeKey) 'the companion key the heartbeat carried is stored' `
        "it stored $($reported.companionPublicKey)"
    Check ($reported.jobTypes.Count -eq 13) 'the heartbeat advertises all thirteen job types' `
        "it advertises $($reported.jobTypes.Count)"
    Check ($null -ne $reported.radio) 'the radio parameters the heartbeat carried are stored' ''
    Check ($reported.errors.PSObject.Properties.Name.Count -eq 6) 'the six error counters are stored' `
        "there are $($reported.errors.PSObject.Properties.Name.Count)"
    Say "  health: $($exitNodeRecord.Body.health), queueDepth $($reported.queueDepth), last at $($reported.at)"
}

if ($repeater) {
    $jobs = Api $pat "/jobs?repeater=$($repeater.id)&limit=50"
    if ($jobs.Status -eq 404) {
        # GET /api/v1/jobs is in the resource map of spec/API.md section 4 and is not built on this
        # stack: the jobs a person sees are task 24's pages. So there is nothing for a script to
        # read here, which is a thing to say rather than an assertion to fail.
        Say '  GET /api/v1/jobs is 404 on this build: the jobs resource of spec/API.md section 4 is'
        Say '  not there yet, so the job assertions of this step are the browser walk''s below.'
    } elseif ($jobs.Status -eq 200) {
        $mine = @($jobs.Body.items | Where-Object { ($_.lastAgentId -eq $agentId) -or ($_.agentId -eq $agentId) })
        Say "  the jobs page has $($jobs.Body.items.Count) job(s) for repeater $($repeater.id), $($mine.Count) through this exit node"
        foreach ($job in ($mine | Select-Object -First 20)) { Say "    $($job.id) $($job.type) -> $($job.state)" }
        Check ($mine.Count -gt 0) 'the server has at least one job this exit node attempted' `
            'nothing was queued for it - the four steps below are what queue them'
    }
    $associations = Api $pat "/repeaters/$($repeater.id)/associations"
    if ($associations.Status -eq 200) {
        $through = $associations.Body.items | Where-Object { $_.agentId -eq $agentId } | Select-Object -First 1
        if ($through) {
            Check ($through.state -eq 'active') "the association of repeater $($repeater.id) is active" "it is '$($through.state)'"
        } else {
            Check $false "an association of repeater $($repeater.id) through exit node $agentId" `
                'there is none: starting one is gated and needs the web UI'
        }
    }
    $changes = Api $pat "/repeaters/$($repeater.id)/changes?limit=20"
    if ($changes.Status -eq 200) {
        Say "  the change history has $($changes.Body.items.Count) row(s); the newest:"
        foreach ($change in ($changes.Body.items | Select-Object -First 5)) {
            Say "    $($change.key): $($change.from) -> $($change.to) ($($change.scope))"
        }
    }
}

Step 'What the fake saw'
if (Test-Path $journalPath) {
    # Only entries that are entries: ConvertFrom-Json in 5.1 turns an empty JSON array into
    # something @() counts as one, and an empty journal must read as empty.
    $journalDoc = Get-Content $journalPath -Raw | ConvertFrom-Json
    $journalEntries = @($journalDoc.transactions | Where-Object { $_ -and ($_.PSObject.Properties.Name -contains 'seq') })
    Say "  $($journalEntries.Count) transaction(s)"
    $rising = $true
    $last = 0
    foreach ($entry in $journalEntries) {
        if ($entry.timestamp -le $last) { $rising = $false; break }
        $last = $entry.timestamp
    }
    if ($journalEntries.Count -eq 0) {
        Say '  nothing reached the fake: no job ran, because none was queued (the four steps below)'
    } else {
        Check $rising 'the sender timestamps strictly increase' 'one repeats, which stock firmware reads as a retry'
    }
    # A `password <value>` line is journalled with its value starred (src/agent/Credentials.h);
    # a journal that held one would be a hole in the rail, so this is the grep for it.
    $leaked = @($journalEntries | Where-Object { $_.what -match 'password\s+[^*\s]' })
    Check ($leaked.Count -eq 0) 'no password reached the journal' `
        "$($leaked.Count) line(s) carry one: $(($leaked | ForEach-Object { $_.what }) -join '; ')"
    $cli = @($journalEntries | Where-Object { $_.kind -eq 'cli' } | ForEach-Object { $_.what })
    $repeated = @($cli | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    # A line may legitimately repeat across jobs (`ver` ends every apply); within one job it may
    # not, and tools/host-stub.py is where that is checked job by job.
    if ($repeated.Count -gt 0) { Say "  note: these lines appear in more than one job: $($repeated -join ', ')" }
} else {
    Check $false 'the journal was written' "$journalPath is not there"
}

# --------------------------------------------------------------- 6. the recording

if ($Record) {
    Step 'Refreshing the recording'
    $recordingPath = Join-Path $repo $Recording
    $doc = Get-Content $recordingPath -Raw | ConvertFrom-Json
    $doc.config.agentId = $config.Body.agentId
    $doc.config.label = $config.Body.label
    $doc.config.transport = $config.Body.transport
    # The budget stays as it is on purpose: loose, so a cycle against the stub is a minute and
    # not seven hours (docs/host.md section 6). Everything else is the stack's.
    $doc | ConvertTo-Json -Depth 20 | Set-Content $recordingPath -Encoding utf8
    Say "  $Recording was refreshed from the stack at $at"
    Say '  the claims themselves are not overwritten: the gated job types never reached this run.'
    Say '  Refresh them from a run that queued them in the UI.'
}

# --------------------------------------------------------------- the walk that is left

$repeaterId = 'the repeater'
if ($repeater) { $repeaterId = $repeater.id }
Step 'What is left, and must be walked in the browser'
Say "  meshmaster.dk answers a personal access token 'reauth-required' for every gated action, for"
Say '  ever (spec/API.md section 2, decision D-26), and does not accept a session cookie on'
Say '  /api/v1 at all. These four are therefore a person''s, in the web UI, with the second factor'
Say '  fresh - and each has its assertion written out, so that the walk is a matter of reading:'
Say ''
Say "  1. The association. /repeaters/$repeaterId -> Associate, exit node $agentId, ACL read-only."
Say '     Then run this script again: the association must reach ACTIVE, and'
Say "     GET /repeaters/$repeaterId/associations must say so."
Say '  2. Read now. The repeater''s page -> Read now. Then this script: the jobs page must show a'
Say "     status job SUCCEEDED through exit node $agentId, with 2 transactions the first time (the"
Say '     firmware version is read) and 1 after that.'
Say '  3. The configuration read, a derive from the dk profile, and an apply of one key. The'
Say '     repeater''s Config tab -> Read, Derive, Apply. Then this script: the read''s snapshot must'
Say '     end with aclRestored: true, and the change history must have a row for the key with'
Say '     scope "device".'
Say '  4. A rotation. The repeater''s Credentials tab -> Rotate the admin password. Then this'
Say '     script: the stored credential must be the new one and the job SUCCEEDED.'
Say ''
Say '  Everything above this line ran without a person. docs/host.md section 7 is the page.'

Write-Host ''
Write-Host "$script:passed assertion(s) held, $($script:failures.Count) did not"
foreach ($failure in $script:failures) { Write-Host "  FAILED  $failure" -ForegroundColor Red }
if ($script:failures.Count -gt 0) {
    Write-Host ''
    Write-Host 'Some of these are the four steps above, which no script may do: 3 says so.' -ForegroundColor Yellow
    exit 3
}
exit 0
