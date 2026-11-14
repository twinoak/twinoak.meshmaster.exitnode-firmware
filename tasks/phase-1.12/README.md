# Pile 1.12 — the release says what is left

Written on 14 November.

**What happened.** On 13 November the first release from the button published v0.7.1 and ended
red, with a summary that blamed a setting nobody had touched. The pull request that files the
notes was open and waiting on two required checks. Thomas asked whether it was ok. It was.

One task. The repeater's firmware gets the same in its phase 3.12; the Walter's already ends
green and needs one sentence corrected.

## Order and dependencies

| # | Task | Depends on | What it leaves behind |
|---|---|---|---|
| 54 | [the-release-says-what-is-left](54-the-release-says-what-is-left.md) | — | a release that published is a green run that names the one pull request left to merge; the summary and the pull request say what is true about `master`'s checks |

```powershell
.\tasks\run-all.ps1 -Phase '1.12'
```

## Models

- **Opus** — 54.

## Afterwards (Thomas)

- Nothing. It shows at the next release.

## Follow-ups

Review findings go into [FOLLOWUPS.md](FOLLOWUPS.md).
