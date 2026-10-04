# `tests/fixtures/partitions/` — the tables `tools/check-partitions.py` must refuse

Two partition tables that are wrong on purpose, for the script's own tests
(`python tools/check-partitions.py --self-test`). The good tables are the real
ones, `boards/partitions/meshmaster_8MB.csv` and `meshmaster_16MB.csv`: the
self-test runs those first, so there is no copy here to drift from them.

| File | What is wrong | Why it is here |
|---|---|---|
| `spiffs-moved_8MB.csv` | `nvs` took `app1`'s whole region and `spiffs` was pulled down after it | the mistake task p1.2-11 warns against: spiffs holds the identity, and a spiffs that moves by one sector makes every repeater's ACL entry for the board a stranger's key |
| `nvs-in-the-gap_8MB.csv` | the framework's `default_8MB.csv` as installed on 5 October 2026: `nvs` at `0x9000` | the table that erased a provisioned board's settings on the bench (4 October 2026): nvs is inside the span a merged image fills with `0xFF`, and the check must say so twice - nvs below app0's end, and any merged image reaching it |

These are not meshmaster.dk's fixtures and `tools/sync-fixtures.ps1` does not
touch them; they are this repository's own, written by hand, and a change to
one is a change to what the check refuses.
