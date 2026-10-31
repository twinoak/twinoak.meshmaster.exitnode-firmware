# Where every job of every workflow runs, in one place, checked on every run.
#
#   python tools/check-runners.py
#   python tools/check-runners.py --self-test        the rule against planted workflows
#   python tools/check-runners.py --dir <dir>        a directory of workflow files
#
# THE RULE. Everything that **checks** runs at home; everything that **ships** stays away.
#
#   ci.yml, upstream.yml, and any workflow added later    runs-on: [self-hosted, unraid]
#   release.yml                                           runs-on: <a GitHub-hosted label>
#
# WHY. The runners are Thomas's, self-hosted on Unraid at the organisation level
# (myoung34/github-runner, containers unraid-1..5, labels `self-hosted` and `unraid`), and
# GitHub-hosted minutes got out of hand: every pull request a task session opens runs the board
# matrix and the native tests, several times a day. meshmaster.dk moved its CI to those runners on
# 29 October 2026 and this repository follows (task p1.8-29).
#
# And release.yml does not, deliberately. A self-hosted runner **keeps its home between jobs** -
# `~/.platformio` and every toolchain in it - and is shared with every branch a task session
# pushes, so an image built there is only as clean as the last branch that ran on that container.
# The images people flash are built on a machine that is new every time. release.yml runs at a tag,
# a few times a month, so the minutes it costs are few.
#
# A job with no `runs-on`, or one whose `runs-on` is an expression this cannot read, is a failure
# too: the rule is only worth having if it cannot be stepped around by accident.
#
# Python 3 and the standard library, because this runs as an early step of ci.yml's native job -
# before PlatformIO is installed and before anything is built - and a self-hosted runner has no
# PyYAML. The reader below understands the slice of YAML these workflows are written in: a `jobs:`
# mapping at column 0, one job per key two spaces in, and that job's `runs-on` two spaces further.
# It is block-scalar aware (a `run: |` body is skipped whole), so a heredoc cannot be misread as a
# job, and it fails loudly rather than quietly when a file is not in that shape.

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOWS = os.path.join(ROOT, ".github", "workflows")

# The one file that ships, and so the one file that stays away from home.
SHIPS = "release.yml"

# What a job that checks must say, as a set of labels.
HOME = frozenset(("self-hosted", "unraid"))

# GitHub's own runner images, as the label prefixes they are named with. A hosted label never
# includes `self-hosted`, which is the half that actually matters; the prefixes keep a typo
# ("ubunut-latest") from passing as hosted.
HOSTED_PREFIXES = ("ubuntu-", "windows-", "macos-", "macOS-")

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
                    "decide at run time: write the labels." % (where, runs_on)
                )
                continue
            labels = labels_of(runs_on)
            if name == SHIPS:
                if not (len(labels) == 1 and labels[0].startswith(HOSTED_PREFIXES)):
                    failures.append(
                        "%s: `runs-on: %s`. %s builds the images people flash and publishes "
                        "them, so it runs on a GitHub-hosted runner - a machine that is new "
                        "every time - and not on a self-hosted runner that keeps its home "
                        "between jobs." % (where, runs_on, SHIPS)
                    )
            elif frozenset(labels) != HOME:
                failures.append(
                    "%s: `runs-on: %s`. Everything that checks runs at home: "
                    "`runs-on: [self-hosted, unraid]`." % (where, runs_on)
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
        "every job of %d workflow(s) runs where it must: %s on a GitHub-hosted runner, "
        "everything else on [self-hosted, unraid]" % (len(names), SHIPS)
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
        "    runs-on: [self-hosted, unraid]",
        "    strategy:",
        "      matrix:",
        "        env: [heltec_v3, heltec_v4]",
        "    steps:",
        "      - uses: actions/checkout@v4",
        "      - name: A heredoc that must not be read as a job",
        "        run: |",
        "          cat <<'XEOFX'",
        "          native:",
        "            runs-on: ubuntu-latest",
        "          XEOFX",
        "  native:",
        "    runs-on:",
        "      - self-hosted",
        "      - unraid",
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

HOME_LINE = "runs-on: [self-hosted, unraid]"

SELF_TEST_CASES = [
    ("what this repository has", {"ci.yml": GOOD_CI, "release.yml": GOOD_RELEASE}, 0),
    (
        "a ci.yml job on a GitHub-hosted runner",
        {
            "ci.yml": GOOD_CI.replace(HOME_LINE, "runs-on: ubuntu-latest", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "a release.yml job at home",
        {
            "ci.yml": GOOD_CI,
            "release.yml": GOOD_RELEASE.replace("runs-on: ubuntu-latest", HOME_LINE),
        },
        1,
    ),
    (
        "a job with no runs-on",
        {
            "ci.yml": GOOD_CI.replace(
                "    runs-on:\n      - self-hosted\n      - unraid\n", ""
            ),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "a runs-on decided at run time",
        {
            "ci.yml": GOOD_CI.replace(HOME_LINE, "runs-on: ${{ vars.RUNNER }}", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "half the labels",
        {
            "ci.yml": GOOD_CI.replace(HOME_LINE, "runs-on: [self-hosted]", 1),
            "release.yml": GOOD_RELEASE,
        },
        1,
    ),
    (
        "a label that looks hosted and is a typo",
        {
            "ci.yml": GOOD_CI,
            "release.yml": GOOD_RELEASE.replace(
                "runs-on: ubuntu-latest", "runs-on: ubunut-latest"
            ),
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
            "self-test %-5s %-40s expected %d, got %d"
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
