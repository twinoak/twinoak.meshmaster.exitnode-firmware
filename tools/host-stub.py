#!/usr/bin/env python3
"""The server's side of a host-harness run, replayed from a recording - and the assertions on it.

CI cannot run meshmaster.dk: no image of it is published and its own tests need MySQL. So this
script is the other half of `tools/host-run.ps1`: it answers the five calls of EXIT-NODES section 6
from `tests/fixtures/server/*.json` over plain HTTP on a loopback port, runs
`.pio/build/host/program` against it, and then asserts - on what came back from the agent and on the
fake repeater's own journal of what reached it.

    python tools/host-stub.py --recording tests/fixtures/server/one-cycle.json \
                              --scenario  tests/fixtures/host/two-repeaters.json \
                              --program   .pio/build/host/program \
                              --journal   out/host-journal.json

It exits 0 when every assertion held and 1 when one did not, naming the field that differed. That
makes a stale recording fail loudly rather than quietly passing: if the firmware's result shapes
move, the mismatch is printed with the job, the field and both values.

What it asserts
---------------
1.  **Every job ran, and came back as the recording says.** Outcome, and the transaction count
    where the recording names one - which is EXIT-NODES section 7's table, job by job.
2.  **Every result is its contract fixture's shape.** `data`'s keys are a subset of
    `tests/fixtures/contracts/<Type>Result.json`'s, so a field the firmware invented, renamed or
    misspelled is a failure here. `has`, `hasNot` and `resultIs` in the recording pin the fields
    that carry a verdict.
3.  **Every command was sent once.** The journal is walked job by job - each job owns as many
    entries as its result's `transactions` count, in order - and a repeated `cli:` line inside one
    job is a failure. A `set` sent twice is a change made twice (CONFIGURATION section 5a).
4.  **The sender timestamps strictly increase.** Stock firmware answers an equal one with nothing
    and runs nothing (`MyMesh::onPeerDataRecv`), so a run whose series ever repeats is a run whose
    later commands silently did not happen.
5.  **The restore is the last command of every admin session that owes one.** An admin login from
    this exit node's key rewrites its own ACL entry to admin, so an admin-class job through a pair
    that holds a grant must end with `setperm <own key> <granted level>` - except the three types
    that cannot (task p4.1-60: `password.set` restores before dropping its session, `reboot` not at
    all, `acl.set` on our own key *is* the level).
6.  **No password reached the port.** Every password the scenario and the recording hold is grepped
    for in the program's whole output and in the journal. The buffer itself is held to zero by
    `test/test_credentials` natively; this is the other half of the same rail - that nothing made
    from one was ever printed.

The recording holds no real token and no real password: the stub refuses to run one that looks like
it does (`mma_` plus 43 base64url characters is a real token's shape).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CONTRACTS = REPO / "tests" / "fixtures" / "contracts"

# The twelve result records of API section 3, by job type. The thirteenth, `trace`, has one too.
RESULT_FOR_TYPE = {
    "status": "StatusResult",
    "telemetry": "TelemetryResult",
    "neighbours": "NeighboursResult",
    "trace": "TraceResult",
    "discover": "DiscoverResult",
    "acl.set": "AclSetResult",
    "config.read": "ConfigReadResult",
    "config.apply": "ConfigApplyResult",
    "cli": "CliResult",
    "advert.request": "AdvertRequestResult",
    "clock.sync": "ClockSyncResult",
    "password.set": "PasswordSetResult",
    "reboot": "RebootResult",
}

# The types whose admin session does not end with the restore (task p4.1-60).
NO_RESTORE = {"password.set", "reboot", "acl.set"}


def iso(when: float) -> str:
    return datetime.fromtimestamp(int(when), tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Recording:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.doc = json.loads(path.read_text(encoding="utf-8"))
        self.token = self.doc["token"]
        self.exit_node_key = self.doc["exitNodeKey"]
        self.jobs = self.doc["jobs"]
        self.config = self.doc["config"]
        self.heartbeat = self.doc["heartbeat"]

    def check_redacted(self) -> list[str]:
        problems = []
        if re.fullmatch(r"mma_[A-Za-z0-9_-]{43}", self.token):
            problems.append(
                f"{self.path.name}: the token has the shape of a real agent token. A recording is "
                "committed to this repository and must hold a placeholder (tasks/README.md: secrets "
                "never in the repo)."
            )
        return problems


class Stub:
    """The five calls, and what the agent sent to each."""

    def __init__(self, recording: Recording, poll_interval: int) -> None:
        self.recording = recording
        self.poll_interval = poll_interval
        self.queue = list(recording.jobs)
        self.results: dict[int, dict] = {}
        self.result_order: list[int] = []
        self.heartbeats: list[dict] = []
        self.observations: list[dict] = []
        self.claims = 0
        self.lock = threading.Lock()

    def claim(self, want: int) -> dict:
        with self.lock:
            self.claims += 1
            handed = self.queue[: max(1, min(want, 2))]
            self.queue = self.queue[len(handed) :]
        jobs = []
        now = time.time()
        for job in handed:
            out = {k: v for k, v in job.items() if not k.startswith("_") and k not in ("expect", "leaseSeconds")}
            # A recorded lease is an absolute time that has long since passed. The server's rule is
            # forty seconds a transaction, two minutes at least and thirty at most (task p4.3-67);
            # the recording names the seconds it wants and this is where they become a time.
            out["leaseUntil"] = iso(now + job.get("leaseSeconds", 1800))
            out.setdefault("timeouts", {"perHopMs": 4000, "loginMs": 8000})
            jobs.append(out)
        return {"serverTime": iso(now), "jobs": jobs}


class Handler(BaseHTTPRequestHandler):
    stub: Stub = None  # set below

    def log_message(self, fmt, *args):  # the stub's own log is the assertions
        pass

    def _send(self, code: int, body: dict) -> None:
        raw = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("X-RateLimit-Remaining", "119")
        self.end_headers()
        self.wfile.write(raw)

    def _body(self) -> dict:
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            return json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            return {"_unparsed": raw.decode("utf-8", "replace")}

    def _authorised(self) -> bool:
        if self.headers.get("Authorization") != f"Bearer {self.stub.recording.token}":
            self._send(401, {"title": "Unauthorized"})
            return False
        if not self.headers.get("X-Agent-Version"):
            # The server's own rule: a missing X-Agent-Version is a 400 that names the header.
            self._send(400, {"title": "X-Agent-Version is required"})
            return False
        return True

    def do_GET(self):  # noqa: N802
        if not self._authorised():
            return
        if self.path != "/agent/v1/config":
            self._send(404, {"title": "Not Found"})
            return
        config = dict(self.stub.recording.config)
        config["serverTime"] = iso(time.time())
        config["pollIntervalS"] = self.stub.poll_interval
        self._send(200, config)

    def do_POST(self):  # noqa: N802
        if not self._authorised():
            return
        body = self._body()
        if self.path == "/agent/v1/heartbeat":
            self.stub.heartbeats.append(body)
            reply = dict(self.stub.recording.heartbeat)
            reply["serverTime"] = iso(time.time())
            reply["pollIntervalS"] = self.stub.poll_interval
            # Always true: it is how the program is told to keep claiming. The claim that comes back
            # empty is what ends the run (the program's --idle-exits).
            reply["jobsWaiting"] = True
            self._send(200, reply)
            return
        if self.path == "/agent/v1/jobs/claim":
            self._send(200, self.stub.claim(int(body.get("max", 1))))
            return
        if self.path == "/agent/v1/observations":
            self.stub.observations.append(body)
            self._send(200, {"accepted": len(body.get("items", [])), "duplicate": False})
            return
        match = re.fullmatch(r"/agent/v1/jobs/(\d+)/result", self.path)
        if match:
            job_id = int(match.group(1))
            duplicate = job_id in self.stub.results
            if not duplicate:
                self.stub.results[job_id] = body
                self.stub.result_order.append(job_id)
            self._send(200, {"jobId": job_id, "state": "succeeded", "duplicate": duplicate, "late": False})
            return
        self._send(404, {"title": "Not Found"})


# --------------------------------------------------------------------------------- the assertions


class Checks:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.passed = 0

    def ok(self, condition: bool, what: str, detail: str = "") -> None:
        if condition:
            self.passed += 1
        else:
            self.failures.append(f"{what}{(': ' + detail) if detail else ''}")

    def report(self) -> int:
        print()
        print(f"{self.passed} assertion(s) held, {len(self.failures)} did not")
        for failure in self.failures:
            print(f"  FAILED  {failure}")
        return 0 if not self.failures else 1


def contract_keys(name: str) -> set[str]:
    path = CONTRACTS / f"{name}.json"
    if not path.exists():
        return set()
    return set(json.loads(path.read_text(encoding="utf-8")).keys())


def check_results(checks: Checks, recording: Recording, stub: Stub) -> None:
    for job in recording.jobs:
        job_id = job["id"]
        expect = job.get("expect", {})
        where = f"job {job_id} {job['type']}"
        report = stub.results.get(job_id)
        if report is None:
            checks.ok(False, where, "no result was posted")
            continue
        checks.ok(True, f"{where}: a result was posted")
        if "outcome" in expect:
            checks.ok(
                report.get("outcome") == expect["outcome"],
                f"{where}: outcome",
                f"expected {expect['outcome']}, got {report.get('outcome')}",
            )
        if "transactions" in expect:
            checks.ok(
                report.get("transactions") == expect["transactions"],
                f"{where}: transactions (EXIT-NODES section 7)",
                f"expected {expect['transactions']}, got {report.get('transactions')}",
            )
        checks.ok("clientId" in report, f"{where}: the result carries a clientId")
        data = report.get("data")
        result_name = expect.get("result") or RESULT_FOR_TYPE.get(job["type"])
        if data is None:
            checks.ok(expect.get("noData", False), f"{where}: the result carries data")
            continue
        keys = contract_keys(result_name) if result_name else set()
        if keys:
            unknown = sorted(set(data.keys()) - keys)
            checks.ok(
                not unknown,
                f"{where}: the result's fields are {result_name}.json's",
                f"{unknown} are in the result and not in the fixture",
            )
        for field in expect.get("has", []):
            checks.ok(field in data, f"{where}: `{field}` is in the result")
        for field in expect.get("hasNot", []):
            checks.ok(field not in data, f"{where}: `{field}` is not in the result")
        for field, value in expect.get("resultIs", {}).items():
            checks.ok(
                data.get(field) == value,
                f"{where}: {field}",
                f"expected {value!r}, got {data.get(field)!r}",
            )
        if "aclRestored" in expect:
            checks.ok(
                data.get("aclRestored") == expect["aclRestored"],
                f"{where}: aclRestored (task p4.1-60)",
                f"expected {expect['aclRestored']}, got {data.get('aclRestored')}",
            )
        for prefix in expect.get("rawMentions", []):
            raw = data.get("raw") or []
            checks.ok(
                any(str(line).startswith(prefix) for line in raw),
                f"{where}: a raw line starting '{prefix}'",
                f"raw is {raw}",
            )


def check_journal(checks: Checks, recording: Recording, stub: Stub, journal_path: Path) -> None:
    if not journal_path.exists():
        checks.ok(False, "the fake's journal", f"{journal_path} was not written")
        return
    entries = json.loads(journal_path.read_text(encoding="utf-8"))["transactions"]
    checks.ok(bool(entries), "the fake saw traffic", "the journal is empty")

    # The sender timestamps of a run are one series and must strictly increase: a repeat is read by
    # stock firmware as a retry and runs nothing (MyMesh::onPeerDataRecv at d9296435).
    last = 0
    rising = True
    where = ""
    for entry in entries:
        if entry["timestamp"] <= last:
            rising = False
            where = f"seq {entry['seq']} ({entry['what']}) is {entry['timestamp']}, after {last}"
            break
        last = entry["timestamp"]
    checks.ok(rising, "the sender timestamps strictly increase", where)

    # Each job owns as many entries as its result's `transactions` count, in order: one `spend()` per
    # frame, so the counts partition the journal exactly.
    at = 0
    own_key = recording.exit_node_key
    for job_id in stub.result_order:
        job = next((j for j in recording.jobs if j["id"] == job_id), None)
        if job is None:
            continue
        count = stub.results[job_id].get("transactions", 0) or 0
        mine = entries[at : at + count]
        at += count
        where = f"job {job_id} {job['type']}"
        commands = [e["what"] for e in mine if e["kind"] == "cli"]
        repeated = sorted({c for c in commands if commands.count(c) > 1})
        checks.ok(
            not repeated,
            f"{where}: every command was sent once",
            f"{repeated} were sent more than once",
        )
        needs_restore = (
            job.get("credentialClass") == "admin"
            and job["type"] not in NO_RESTORE
            and isinstance(job.get("credential"), dict)
            and job["credential"].get("acl") in (1, 2)
        )
        if needs_restore and commands:
            level = job["credential"]["acl"]
            expected = f"cli:setperm {own_key} {level}"
            checks.ok(
                commands[-1] == expected,
                f"{where}: the restore is the session's last command (task p4.1-60)",
                f"the last command was {commands[-1]!r}, not {expected!r}",
            )
    checks.ok(at <= len(entries), "the journal accounts for every transaction reported",
              f"{at} claimed of {len(entries)} seen")


def check_no_secret(checks: Checks, recording: Recording, scenario_path: Path, output: str,
                    journal_path: Path) -> None:
    secrets: set[str] = set()
    scenario = json.loads(scenario_path.read_text(encoding="utf-8"))
    for repeater in scenario.get("repeaters", []):
        for field in ("adminPassword", "guestPassword"):
            value = repeater.get(field)
            if value:
                secrets.add(value)
        for value in (repeater.get("prefs") or {}).values():
            pass  # a pref is not a secret; guest.password is covered by the field above
    for job in recording.jobs:
        credential = job.get("credential") or {}
        for field in ("adminPassword", "guestPassword", "newPassword"):
            if credential.get(field):
                secrets.add(credential[field])
    journal = journal_path.read_text(encoding="utf-8") if journal_path.exists() else ""
    for secret in sorted(secrets):
        checks.ok(secret not in output, "no password reached the console", f"'{secret}' is in the output")
        checks.ok(secret not in journal, "no password reached the journal", f"'{secret}' is in the journal")


# --------------------------------------------------------------------------------- the run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recording", default="tests/fixtures/server/one-cycle.json")
    parser.add_argument("--scenario", default="tests/fixtures/host/two-repeaters.json")
    parser.add_argument("--program", default=".pio/build/host/program")
    parser.add_argument("--journal", default="out/host-journal.json")
    parser.add_argument("--log", default="out/host-stub.log")
    parser.add_argument("--port", type=int, default=0, help="0 asks the operating system for one")
    parser.add_argument("--poll-interval", type=int, default=2,
                        help="what the heartbeat reply says, in seconds: small, so a cycle is not "
                             "mostly the agent waiting for its next poll")
    parser.add_argument("--max-seconds", type=int, default=600)
    parser.add_argument("--log-level", default="info")
    args = parser.parse_args()

    recording = Recording(REPO / args.recording)
    problems = recording.check_redacted()
    if problems:
        for problem in problems:
            print(f"REFUSED  {problem}")
        return 1

    stub = Stub(recording, args.poll_interval)
    Handler.stub = stub
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    journal_path = REPO / args.journal
    journal_path.parent.mkdir(parents=True, exist_ok=True)
    if journal_path.exists():
        journal_path.unlink()
    log_path = REPO / args.log
    log_path.parent.mkdir(parents=True, exist_ok=True)

    command = [
        str(REPO / args.program),
        "--url", f"http://127.0.0.1:{port}",
        "--token", recording.token,
        "--board", "heltec_v3",
        "--key", recording.exit_node_key,
        "--scenario", str(REPO / args.scenario),
        "--journal", str(journal_path),
        "--idle-exits", "2",
        "--max-seconds", str(args.max_seconds),
        "--log-level", args.log_level,
    ]
    print("the stub is on http://127.0.0.1:%d; running:" % port)
    print("  " + " ".join(command))
    print()
    started = time.time()
    run = subprocess.run(command, capture_output=True, text=True, timeout=args.max_seconds + 120,
                         cwd=str(REPO))
    took = time.time() - started
    output = run.stdout + run.stderr
    log_path.write_text(output, encoding="utf-8")
    sys.stdout.write(output)
    print()
    print(f"the program exited {run.returncode} after {took:.0f} s; its output is {args.log}")

    checks = Checks()
    checks.ok(run.returncode == 0, "the program exited 0", f"it exited {run.returncode}")
    checks.ok(len(stub.heartbeats) > 0, "the server was heartbeated")
    checks.ok(
        all(hb.get("transport") == "onboard" for hb in stub.heartbeats),
        "every heartbeat says transport onboard",
    )
    checks.ok(
        all(hb.get("board") == "heltec_v3" for hb in stub.heartbeats),
        "every heartbeat carries the board",
        f"saw {sorted({hb.get('board') for hb in stub.heartbeats})}",
    )
    checks.ok(
        all(hb.get("companionPublicKey") == recording.exit_node_key for hb in stub.heartbeats),
        "every heartbeat carries this exit node's key",
    )
    ran_types = {job["type"] for job in recording.jobs if job["id"] in stub.results}
    missing = sorted(set(RESULT_FOR_TYPE) - ran_types)
    checks.ok(not missing, "all thirteen job types ran one cycle", f"{missing} did not run")

    check_results(checks, recording, stub)
    check_journal(checks, recording, stub, journal_path)
    check_no_secret(checks, recording, REPO / args.scenario, output, journal_path)

    server.shutdown()
    return checks.report()


if __name__ == "__main__":
    sys.exit(main())
