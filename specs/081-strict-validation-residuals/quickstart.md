# Quickstart — Strict-Validation-Path Residual Closeout

Runnable validation scenarios proving both concerns end-to-end. All tests are GoogleTest; isolation-safe cases go in existing grouped buckets selected by `ctest -L` (Article VII §8); exact-set completeness/census gates stay standalone. Build from the submodule root.

Prereqs: a configured build preset (clang-debug for TDD; the sanitizer + gcc-release + MSVC matrix runs at `/speckit-verify`).

## Scenario A1 — FIX50SPx application frame accepted standalone (Concern A, RED→GREEN)

**Goal**: SC-001. A well-formed FIX50SP2 app frame validated standalone against the vendored `FIX50SP2.xml` under strict validation is accepted.

- Load `dictionaries/FIX50SP2.xml` only (no FIXT11), enable `validate_inbound_messages`.
- Feed a well-formed TradeCaptureReport (AE) / NewOrderSingle carrying standard header (34/49/52/56) + trailer (10).
- **RED (pre-fix)**: rejected `wire_unexpected_tag` on **tag 8** (`ref_tag == 8`) — the first field reaching Step-1 (Step-1 walks the frame from byte 0, and the framer does not strip 8/9/10 from the validated bytes, so BeginString(8) is the first tag `validate()` examines against the empty valid-tag view). cf. the mechanism pin `tests/wire/validator_production_table_view_test.cpp:270` (`EXPECT_EQ(ref_tag, 8)` for an empty valid-tag view — the same condition an empty-`<header/>` FIX50SPx message presents).
- **GREEN**: the same frame is **accepted** — tag 8 (and the rest of the framing set) passes Step-1 via `is_fixt_framing_tag`, and the frame proceeds to required-field/enum/group checks.
- Repeat for `FIX50.xml` and `FIX50SP1.xml`.

Expected: accept. Pin: `tests/wire/fixt_header_validate_*` (new).

## Scenario A2 — accept-only guard + no false-accept (Concern A)

**Goal**: FR-003a / FR-011 / contract clauses 2–4.

- Same setup; feed a frame that omits a genuinely-required **application** field → still **rejected** (header acceptance did not weaken app-field checks).
- Feed a frame that omits a session-owned header field (e.g. 52) → **not** rejected by `validate()` for that (session FSM owns it).
- **Malformed numeric header → REJECT (RED→GREEN negative, F2 pin)**: feed a frame with `34=abc` and (separately) `1156=abc`. **RED (before `fixt_framing_types_`)**: accepted — framing was accepted at Step-1, but the framing tag's type defaults to `field_type::String` (no constraint), so the malformed value passes (the F2 false-accept). **GREEN (after)**: rejected `wire_field_value_out_of_range` at the Int arm (34=SeqNum→Int, 1156=ApplExtID→Int), asserting the **exact** error AND `ref_tag == 34` / `ref_tag == 1156` (not a generic "rejects" — a generic assertion is not RED, since pre-`fixt_framing_types_` the tag was already accepted, and the GREEN error/ref_tag is the meaningful pin).
- **Not a reject pin**: `52=notatime` stays **accepted** (UtcTimestamp→String is structurally undetectable to the Phase-1 validator, `field_type.hpp`) — document, do not pin as a reject.

Expected: app-field omission rejects; header-field omission not rejected by the dictionary validator; malformed Int-typed header field rejected; malformed UtcTimestamp header field accepted (documented limitation).

## Scenario A3 — FIXT framing census + parser-unchanged + no-regression (Concern A)

- **Census (standalone)**: the baked FIXT framing table == `dictionaries/FIXT11.xml` `<header>`+`<trailer>` field tags **and their datatypes** (through the `field_data_type → field_type` mapping), exact-set both directions. Pin: `tests/dictionary/fixt_header_merge_*` (new, standalone).
- **Parser-containment (RC#1) — pin the invariant DIRECTLY, not via a blind on/off compare**: on the `as_table_view()` output for FIX50/FIX50SP1/FIX50SP2, assert `table_view::field_valid_for(msg_type, T)` AND `valid_tags_for(msg_type).contains(T)` stay **false** for each framing tag `T ∈ {8, 9, 10, 34, 49, 52, 56, 1128, 1156, …}` that is not genuinely message-declared, WHILE `dictionary_driven_validator::validate()` **accepts** those same tags via `is_fixt_framing_tag`. A `valid_` re-widening (the exact round-1 regression this pin catches) flips `field_valid_for` to `true` → RED. (Do NOT rely on an `unknown_fields()` strict-on-vs-off comparison — it is near-vacuous: `inbound_tv_` is built flag-independently at `session.cpp:992`, so a both-sides `valid_` widening classifies identically on and off and the compare false-greens.)
- **No-regression**: FIX44 / FIXT11 full-frame `validate()` behavior unchanged; read/reify goldens byte-identical (run the codegen determinism / golden tests).

## Scenario B1 — optional-group member relaxed, required-group member held (Concern B, RED→GREEN)

**Goal**: SC-002 core behavior + contract clauses 1–2.

- Strict validation on. At a representative optional-group site (e.g. FIX44 PositionReport/AP NoUnderlyings(711) omitting 732/733):
  - **RED (pre-fix)**: rejected `wire_required_field_missing` (733).
  - **GREEN**: accepted (optional group, group-gated).
- At a representative **required**-group site: present-but-incomplete instance → still **rejected**.

Pin: `tests/wire/required_scope_two_tier_test.cpp` (flip the optional-group cases; keep/add required-group cases). Both tiers (runtime + typed `validate_<Msg>`) must agree.

## Scenario B2 — per-context census exact-set (Concern B)

**Goal**: SC-002. Across all in-scope dictionaries, the loaded per-group required-member store == the reworked QuickFIX-group-gated oracle, exact-set both directions; the 24 previously-divergent contexts → 0.

- Rework `tests/dictionary/required_scope_oracle.hpp` to gate on enclosing-group `required=`.
- Recount shrunk baselines in `tests/dictionary/required_scope_census_test.cpp` (RC5 max=6→new, total_contexts).
- Update `tests/dictionary/required_scope_test.cpp` AP 732/733 pins (contains→not-contains).

## Scenario B3 — quickfix-cpp parity golden (Concern B)

- **Prerequisite (N2)**: clone + build `reference-engines/quickfix-cpp` first (gitignored / absent on a fresh tree, [[project_reference_engines_setup]]) — a `/tasks` + `/verify` prerequisite, offline, not linked in CI.
- Regenerate the required-set parity golden with quickfix-cpp 1.16.0 (offline); assert loaded per-group required sets exact-equal to QuickFIX at the 24 contexts. Pin: `tests/wire/required_scope_parity_test.cpp`.
- **N3**: enumerate the 24 divergent contexts by `(version, message, group)`; each either carries a typed-tier agreement pin (v44/v50sp2/vlatest) or is explicitly runtime-only-by-scope (census + parity golden only, no typed tier — e.g. v42).

## Cross-cutting gates (run at /speckit-verify)

- `validate_inbound_messages=off` → byte-identical no-op (default path unchanged).
- abidiff 0-diff + `nm` symbol golden unchanged (no C-ABI change).
- Regenerated typed-validator goldens (v44/v50sp2/vlatest) are deterministic and match; read/reify goldens byte-identical.
- Coverage ≥95% line / ≥85% branch on touched `dictionary`/`wire` modules; ASan/UBSan/TSan + gcc-release + MSVC green.
- `bench/wire/validator_bench.cpp` within ±5% (no intended perf change).
