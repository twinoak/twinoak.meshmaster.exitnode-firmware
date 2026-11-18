# `tests/fixtures/partitions/` — the tables `tools/check-partitions.py` must refuse

Five partition tables that are wrong on purpose, for the script's own tests
(`python tools/check-partitions.py --self-test`). The good tables are the real
ones, `boards/partitions/meshmaster_8MB.csv` and `meshmaster_16MB.csv`: the
self-test runs those first, so there is no copy here to drift from them. The
table boards in the field hold is a real file too
(`boards/partitions/meshmaster_8MB_one_slot.csv`) and the self-test uses it as
the *no second slot* case, for the same reason.

| File | What is wrong | Why it is here |
|---|---|---|
| `spiffs-moved_8MB.csv` | `nvs` took `app1`'s whole region and `spiffs` was pulled down after it | the mistake task p1.2-11 warns against: spiffs holds the identity, and a spiffs that moves by one sector makes every repeater's ACL entry for the board a stranger's key |
| `nvs-in-the-gap_8MB.csv` | the framework's `default_8MB.csv` as installed on 5 October 2026: `nvs` at `0x9000` | the table that erased a provisioned board's settings on the bench (4 October 2026): nvs is inside the span a merged image fills with `0xFF`, and the check must say so twice - nvs below app0's end, and any merged image reaching it |
| `app1-over-nvs_8MB.csv` | `app1` back where the framework's default has it, `0x340000`, which is where `nvs` went | task p1.15-57's second slot in the one place it must never be: an update fetched into it would write over the WiFi, the server and the agent token on its way in |
| `app1-over-spiffs_8MB.csv` | `app1` given `app0`'s size, so it runs `0x10000` past where `spiffs` begins | the same mistake as `spiffs-moved`, made from the other end: the first update fetched into the slot would take the identity, and the identity cannot be re-provisioned |
| `app1-off-the-boundary_8MB.csv` | `app1` at `0x34F000`: inside the region, overlapping nothing, but not on a 64 kB boundary | an application partition has to be aligned (the ESP-IDF's own `gen_esp32part.py` refuses it); the check says so before a build ever reaches that script |

These are not meshmaster.dk's fixtures and `tools/sync-fixtures.ps1` does not
touch them; they are this repository's own, written by hand, and a change to
one is a change to what the check refuses.
