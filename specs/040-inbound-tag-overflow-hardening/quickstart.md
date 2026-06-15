# Quickstart: Inbound tag-overflow hardening (040)

Security fix — close forged-tag overflow aliasing at all five live-inbound tag scanners via one shared
bounded-tag helper. Build from a configured preset (e.g. `linux-clang-debug`); run from the library
submodule root.

## Helper boundary unit test

```bash
ctest --test-dir build/<preset> -R 'tag_scan|accumulate_tag' -V
```
Expect: `65535` ok; `65536` reject; wrap-and-continue `429496729649` reject; zero-padded
`000000000034` → 34.

## US1 — scan_frame_header (the defective guard)

```bash
ctest --test-dir build/<preset> -R 'scan_frame_header.*overflow|frame_header.*alias' -V
```
Expect: `429496729649` not surfaced as SenderCompID(49); `429496729652` not surfaced as
SendingTime(52) (no 038-guard regression vector); naive `4294967330` still rejected; `65535` parses.

## US2 — the other four scanners

```bash
# wire twins (Index + Scan)
ctest --test-dir build/<preset> -R 'offset_table.*overflow|field_iterator.*overflow|tag.*alias' -V
# session/engine scanners
ctest --test-dir build/<preset> -R 'interpret_logon.*overflow|first_frame.*overflow' -V
```
Expect: forged wrap-aliased tokens rejected at each site; `interpret_logon` does not consume a
wrap-aliased `1137`/`49`/`56`; `scan_first_frame_ids` does not resolve a wrap-aliased `49`/`56`.
Conforming corpora unchanged.

## US3 — build_replay_frame exclusion

```bash
grep -n 'build_replay_frame' src/session/session.cpp   # confirm the exclusion comment is present
```
Expect: a comment explaining it parses stored own-outbound frames (not live-inbound) → exempt.

## Verification

`/speckit-verify` runs the 6-preset Tier-1 matrix (incl. TSan) + lcov DA/BRDA. The helper's overflow
branch and each site's reject branch must be covered by the wrap-and-continue witnesses. Live
cross-engine witness is DEFERRED (038 L-038-2 family). Re-run `tools/check_layers.py` for the new
`include/fixpp/wire/tag_scan.hpp` header.
