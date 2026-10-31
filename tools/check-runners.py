# Where every job of every workflow runs, in one place, checked on every run.
#
#   python tools/check-runners.py
#   python tools/check-runners.py --self-test        the rule against planted workflows
#   python tools/check-runners.py --dir <dir>        a directory of workflow files
#
# THE RULE. Every job of every workflow here runs on a GitHub-hosted runner. None of them runs on a
# self-hosted one: this repository is public, and a fork's pull request would run on the host.
#
#   ci.yml, release.yml, upstream.yml, and any workflow added later    runs-on: ubuntu-latest
#
# WHY, at more length than one sentence. **This repository is public** - meshmaster.dk and the
# repeater firmware are private - and that one fact gives both halves of the reason:
#
#   Running here costs nothing. Actions on standard GitHub-hosted runners are free in a public
#   repository, so this repository's CI has never spent a credit. The minutes that got out of hand
#   in October 2026 are the private repositories', and their CI is the one that moved home to
#   Thomas's Unraid runners (myoung34/github-runner, containers unraid-1..5, labels `self-hosted`
#   and `unraid`).
#
#   A self-hosted runner on a public repository runs a stranger's code. Anyone may fork this
#   repository and open a pull request, and a pull request can change a workflow file; on a
#   self-hosted runner that would execute on the Unraid host, with its docker socket, its LAN and
#   its Tailscale. GitHub's own guidance: self-hosted runners "should almost never be used for
#   public repositories". The runner group's *Allow public repositories* is not ticked, which is
#   what stops it today; this guard is what stops it being worked around from inside the repository.
#
# So there is no `release.yml` exception and no file that is special: one rule, every file. If this
# repository is ever made private, the move home is commit `dc0861b` (*CI runs at home, the release
# does not*) with its own guard, ready to take back - README's *Where CI runs* says so too.
#
# A job with no `runs-on`, one whose `runs-on` is an expression this cannot read, and a label that
# is neither GitHub's nor an admitted `self-hosted` (a bare `unraid`, or a misspelt
# `ubunut-latest`) are failures too: the rule is only worth having if it cannot be stepped around
# by accident.
#
# Python 3 and the standard library, because this runs as an early step of ci.yml's native job -
# before PlatformIO is installed and before anything is built - and so that it keeps working
# wherever it is run from. The reader below understands the slice of YAML these workflows are
# written in: a `jobs:` mapping at column 0, one job per key two spaces in, and that job's
# `runs-on` two spaces further. It is block-scalar aware (a `run: |` body is skipped whole), so a
# heredoc cannot be misread as a job, and it fails loudly rather than quietly when a file is not in
# that shape.

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOWS = os.path.join(ROOT, ".github", "workflows")

# The label GitHub puts on every self-hosted runner of its own accord, and so the one that names
# what must not happen here.
SELF_HOSTED = "self-hosted"

# GitHub's own runner images, as the label prefixes they are named with. Checking for the prefix
# rather than for the absence of `self-hosted` is what catches a self-hosted runner addressed by
# its own label alone (`runs-on: unraid`) and a typo that would queue for ever ("ubunut-latest").
HOSTED_PREFIXES = ("ubuntu-", "windows-", "macos-", "macOS-")

# One sentence, which is what a red step prints.
REASON = (
    "a public repository; a fork's pull request would run on the host. "
    "Every job here runs on a GitHub-hosted runner, which is free for a public repository."
)

JOB_KEY = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_-]*):\s*(.*)$")
BLOCK_SCALAR = re.compile(r"^[|>][+-]?\d*$")

QUOTES = "'" + '"'


def _strip_comment(value):
    # Enough for these files: a `#` that is not inside quotes ends the value.
    out = []
    quote = None
    for i, ch in enumerate(value):
        if quote:
            if ch == quote:
                quote = None
        elif ch in QUOTES:
            quote = ch
        elif ch == "#" and (i == 0 or value[i - 1] in " \t"):
            break
        out.append(ch)
    return "".join(out).strip()


def labels_of(raw):
    # The two forms these workflows use; a block sequence is joined into the second by the reader.
    #   ubuntu-latest         -> ["ubuntu-latest"]
    #   [self-hosted, unraid] -> ["self-hosted", "unraid"]
    raw = raw.strip()
    if raw.startswith("[") and raw.endswith("]"):
        inner = raw[1:-1]
        return [p.strip().strip(QUOTES) for p in inner.split(",") if p.strip()]
    return [raw.strip(QUOTES)]


def jobs_of(path):
    # [(job_id, runs_on_or_None, line_number)] for one workflow file, in file order.
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    jobs = []
    in_jobs = False
    job_indent = None
    current = None
    skip_deeper_than = None

    index = 0
    while index < len(lines):
        line = lines[index]
        index += 1
        stripped = line.strip()
        indent = len(line) - len(line.lstrip(" "))

        # Inside a block scalar (`run: |`): every line indented deeper than its key, and every
        # blank line, belongs to the body and is not YAML.
        if skip_deeper_than is not None:
            if not stripped or indent > skip_deeper_than:
                continue
            skip_deeper_than = None

        if not stripped or stripped.startswith("#"):
            continue

        if not in_jobs:
            if stripped == "jobs:" and indent == 0:
                in_jobs = True
            continue

        if indent == 0:
            in_jobs = False
            current = None
            continue

        match = JOB_KEY.match(line)
        if not match:
            continue
        key = match.group(2)
        value = _strip_comment(match.group(3))

        if BLOCK_SCALAR.match(value):
            skip_deeper_than = indent
            continue

        if not value:
            if job_indent is None:
                job_indent = indent
            if indent == job_indent:
                current = [key, None, index]
                jobs.append(current)
                continue

        if (
            current is not None
            and job_indent is not None
            and indent == job_indent + 2
            and key == "runs-on"
        ):
            if value:
                current[1] = value
            else:
                # A block sequence: the `- label` lines that follow, joined into the flow form.
                seq = []
                while index < len(lines):
                    nxt = lines[index]
                    nxt_stripped = nxt.strip()
                    nxt_indent = len(nxt) - len(nxt.lstrip(" "))
                    if not nxt_stripped or nxt_stripped.startswith("#"):
                        index += 1
                        continue
                    if not nxt_stripped.startswith("- ") or nxt_indent <= indent:
                        break
                    seq.append(_strip_comment(nxt_stripped[2:]))
                    index += 1
                current[1] = "[" + ", ".join(seq) + "]"

    return [(job, runs_on, line_no) for job, runs_on, line_no in jobs]


def check(directory):
    names = sorted(
        name
        for name in os.listdir(directory)
        if name.endswith(".yml") or name.endswith(".yaml")
    )
    if not names:
        sys.stderr.write("%s holds no workflow file. Nothing was checked.\n" % directory)
        return 1

    failures = []
    for name in names:
        path = os.path.join(directory, name)
        jobs = jobs_of(path)
        if not jobs:
            failures.append(
                "%s: no job found. The rule could not be applied to this file, which is a "
                "failure and not a pass (is `jobs:` at column 0?)." % name
            )
            continue
        for job, runs_on, line_no in jobs:
            where = ".github/workflows/%s:%d, job `%s`" % (name, line_no, job)
            print("  %-14s %-28s %s" % (name, job, runs_on if runs_on else "(none)"))
            if runs_on is None:
                failures.append("%s: no `runs-on`." % where)
                continue
            if "${{" in runs_on:
                failures.append(
                    "%s: `runs-on: %s` is an expression. Where a job runs is not something to "
                    "decide at run time - an expression can evaluate to a self-hosted runner and "
                    "this guard cannot see that it does not: write the labels." % (where, runs_on)
                )
                continue
            labels = labels_of(runs_on)
            if SELF_HOSTED in labels:
                failures.append(
                    "%s: `runs-on: %s` is a self-hosted runner. No job here may run on one - %s"
                    % (where, runs_on, REASON)
                )
            elif not (len(labels) == 1 and labels[0].startswith(HOSTED_PREFIXES)):
                failures.append(
                    "%s: `runs-on: %s` is not a GitHub-hosted runner label (one of %s). If it is a "
                    "self-hosted runner addressed by its own label, the rule is the same - %s"
                    % (
                        where,
                        runs_on,
                        ", ".join("`%s*`" % p for p in HOSTED_PREFIXES[:3]),
                        REASON,
                    )
                )

    if failures:
        sys.stderr.write("\n")
        for failure in failures:
            sys.stderr.write("::error::%s\n" % failure)
        sys.stderr.write(
            "\n%d job(s) run in the wrong place. The rule, and why it is the rule, is at the top "
            "of tools/check-runners.py.\n" % len(failures)
        )
        return 1

    print(
        "every job of %d workflow(s) runs on a GitHub-hosted runner: free for a public "
        "repository, and no fork's pull request on the Unraid host" % len(names)
    )
    return 0


# The planted workflows: the shapes the reader must still read, and what the rule must refuse.
GOOD_CI = "\n".join(
    [
        "name: ci",
        "on: [push]",
        "jobs:",
        "  build:",
        "    name: build (${{ matrix.env }})",
        "    runs-on: ubuntu-latest",
        "    strategy:",
        "      matrix:",
        "        env: [heltec_v3, heltec_v4]",
        "    steps:",
        "      - uses: actions/checkout@v4",
        "      - name: A heredoc that must not be read as a job",
        "        run: |",
        "          cat <<'XEOFX'",
        "          native:",
        "            runs-on: [self-hosted, unraid]",
        "          XEOFX",
        "  native:",
        "    runs-on:",
        "      - ubuntu-latest",
        "    steps:",
        "      - run: echo hi",
        "",
    ]
)

GOOD_RELEASE = "\n".join(
    [
        "name: release",
        "on:",
        "  push:",
        '    tags: ["v*"]',
        "jobs:",
        "  build:",
        "    runs-on: ubuntu-latest",
        "    steps:",
        "      - run: echo hi",
        "",
    ]
)

HOSTED_LINE = "runs-on: ubuntu-latest"

SELF_TEST_CASES = [
    ("what this repository has", {"ci.yml": GOOD_CI, "release.yml": GOOD_RELEASE}, 0),
    (
        "a ci.yml job at home",
        {
            "ci.yml": GOOD_CI.replace(HOSTED_LINE, "runs-on: [self-hosted, unraid]", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "self-hosted with any other label, not only unraid",
        {
            "ci.yml": GOOD_CI.replace(HOSTED_LINE, "runs-on: [self-hosted, linux, x64]", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "self-hosted on its own",
        {
            "ci.yml": GOOD_CI.replace(HOSTED_LINE, "runs-on: self-hosted", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "release.yml at home, which is no different",
        {
            "ci.yml": GOOD_CI,
            "release.yml": GOOD_RELEASE.replace(
                HOSTED_LINE, "runs-on: [self-hosted, unraid]"
            ),
        },
        1,
    ),
    (
        "a runner group's own label, with no self-hosted beside it",
        {
            "ci.yml": GOOD_CI.replace(HOSTED_LINE, "runs-on: unraid", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "a job with no runs-on",
        {
            "ci.yml": GOOD_CI.replace("    runs-on:\n      - ubuntu-latest\n", ""),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "a runs-on decided at run time",
        {
            "ci.yml": GOOD_CI.replace(HOSTED_LINE, "runs-on: ${{ vars.RUNNER }}", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "a label that looks hosted and is a typo",
        {
            "ci.yml": GOOD_CI,
            "release.yml": GOOD_RELEASE.replace(HOSTED_LINE, "runs-on: ubunut-latest"),
        },
        1,
    ),
    (
        "a workflow with no job at all",
        {
            "ci.yml": GOOD_CI,
            "release.yml": GOOD_RELEASE,
            "stray.yml": "name: stray\non: [push]\n",
        },
        1,
    ),
]


def self_test():
    bad = 0
    verdicts = []
    for label, files, expected in SELF_TEST_CASES:
        with tempfile.TemporaryDirectory() as directory:
            for name, body in files.items():
                with open(
                    os.path.join(directory, name), "w", encoding="utf-8"
                ) as handle:
                    handle.write(body)
            print("--- self-test: %s" % label)
            got = check(directory)
        if got != expected:
            bad += 1
        verdicts.append(
            "self-test %-5s %-55s expected %d, got %d"
            % ("ok" if got == expected else "WRONG", label, expected, got)
        )
    print("")
    for line in verdicts:
        print(line)
    if bad:
        sys.stderr.write("\n%d self-test case(s) wrong.\n" % bad)
        return 1
    print("self-test: %d cases, all as expected" % len(SELF_TEST_CASES))
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    directory = WORKFLOWS
    if "--dir" in argv:
        directory = argv[argv.index("--dir") + 1]
    return check(directory)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
