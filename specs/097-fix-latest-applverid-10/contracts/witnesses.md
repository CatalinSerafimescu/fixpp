# Contract: 097 witnesses and their RED arms

Every witness names its file, what it asserts, and how it is shown RED **for its stated reason**.

**Why most RED arms are mutants, not the unfixed tree.** A witness that names
`application_version::vlatest` does not compile on the unfixed tree. A compile failure is RED for the
wrong reason (it would be RED whatever the behaviour). So, except where noted, each witness's RED is a
**named mutant applied to the fixed tree**, built, and run unfiltered (`--gtest_filter` only narrows the
report, never the binary). Each mutant is committed on the feature branch, built, run and recorded. It
is then dropped with `git reset --hard <head-before-mutant>` on that same branch: never `git stash`
(every worktree shares it), never `checkout`/`switch`, and never a detached head in the shared
checkout. A row whose rebuild failed proves nothing and is gated on build rc 0. The verify record keeps a
table: witness | mutant | build rc | test rc | the failing assertion's message.

Where unmodified code DOES compile the witness (string-level frames, no new enumerator), it is written
and run first (plan phase 2), and that run's result is recorded too.

## W-1 — parse (FR-001, SC-004)

- **File**: `tests/dictionary/version_profile_test.cpp`.
- **Asserts**: a table over every ApplVerID value `"0"`..`"10"`. `"2"`..`"9"` map as before, `"10"` maps to
  `vlatest`, `"0"`/`"1"` stay `dict_unknown_appl_ver_id`. The malformed values `"10 "`, `"010"`, `"1O"`,
  `"11"`, `"100"`, `"99"` stay `dict_unknown_appl_ver_id`. The existing "bad" lists drop `"10"`: here and
  in `tests/dictionary/reify_dispatch_test.cpp`.
- **RED**: on `origin/main`, the string-only half (`resolve("10")` has a value) fails: it returns
  `dict_unknown_appl_ver_id`. Mutant M-1: the `"10"` arm accepts any two-character string starting `'1'`,
  so `"11"` resolves.

## W-2 — render (FR-002, SC-004)

- **File**: `tests/dictionary/version_profile_test.cpp`.
- **Asserts**: `render(vlatest) == "10"`; render then parse is the identity over all ten members; the ten
  rendered strings are pairwise distinct; `render(Unknown)` stays an error.
- **RED**: mutant M-2, `render(vlatest)` returns `"9"`. The round-trip and the distinctness checks both
  fail.

## W-3 — config token (FR-003)

- **File**: `tests/config/` (beside `test_load_happy_path.cpp`'s ApplVerID cases; new fixture
  session `default_appl_ver_id = "10"`).
- **Asserts**: `"10"` loads as `vlatest`. An unknown token (`"11"`) yields `reason_class::unknown_enum`,
  and the message contains `"2".."10"`.
- **RED**: mutant M-3, delete the `"10"` branch: the load yields a diagnostic. The text arm is RED on
  `origin/main` (it reads `"2".."9"`).

## W-4 — registry coexistence (FR-004, FR-005, SC-002)

- **File**: `tests/dictionary/orchestra_loader_test.cpp`. `OrchestraFR010Guard.BothDictsFailLoud` is
  renamed `OrchestraRegistry097.BothDictsCoexist`. The FR-010 death test is deleted together with the
  abort.
- **Asserts**: building from {FIX50SP2, FIX Latest}, in both orders, returns. `get(v50sp2)` is the
  FIX50SP2 dict and `get(vlatest)` is the FIX Latest dict. `OrchestraVersionIdentity.DistinctVLatestMapsToV50SP2`
  is rewritten to assert the mapping to `vlatest`.
- **RED**: on `origin/main` the construction aborts with `FR-010`. This is recorded from the death test
  as it exists before the rename: its pass on `origin/main` IS the abort. Mutant M-4 restores
  `case vlatest: return v50sp2;` without the abort: `get(vlatest)` is empty and `get(v50sp2)` returns
  the last writer, so one order fails.

## W-5 — one-way fallback (FR-006)

- **File**: `tests/dictionary/orchestra_loader_test.cpp`. `SingleDictConfigsSucceed` is kept and extended
  with a new `OrchestraRegistry097.OneWayFallback`.
- **Asserts**: every row of data-model §A's lookup table. With FIX Latest alone, `get(v50sp2)` and
  `get(vlatest)` are both that dict (the fallback is kept). With FIX50SP2 alone, `get(vlatest)` is
  `dict_no_dictionary_for_application_version` (no reverse fallback). With both, each version gets its
  own dict (exact match beats the fallback). Empty gives none for both.
- **RED**: the FIX-Latest-alone `get(v50sp2)` arm passes on `origin/main` (a characterization pin).
  Mutant M-5a deletes the fallback, which turns that arm RED. Mutant M-5b adds the reverse fallback, so
  an empty vlatest slot answers from v50sp2; this turns the FIX50SP2-alone arm RED. Mutant M-5c makes the
  fallback win over an exact match, which turns the both-registered arm RED.

## W-6 — FIXT establishment (FR-007, SC-001)

- **File**: `tests/session/test_fixt_logon_establishment.cpp`. Cloned from the existing W5 v50sp2
  acceptor case, with an Orchestra-dictionary helper beside `make_dict` (the minimal repository skeleton
  pattern of `orchestra_loader_test.cpp`, or the vendored file).
- **Asserts**, one test per row:
  - (a) Acceptor, registry {FIX Latest}, inbound `1137=10`: Logon reply, Active, and the negotiated
    profile reads `vlatest`.
  - (b) Acceptor, registry {FIX50SP2}, inbound `1137=10`: Reject 373=5, 371=1137, then disconnect.
  - (c) Acceptor, registry {FIX Latest}, inbound `1137=9`: established, negotiated `v50sp2`. This is the
    one-way fallback and B-097-1's "still admits".
  - (d) Acceptor, registry {FIX50SP2, FIX Latest}: one session per peer, `1137=9` and `1137=10`, both
    established, each negotiating its own version.
  - (e) Initiator, Logon-ack `1137=10`: negotiated `vlatest`.
  - (f) `open()` with default `"10"` and {FIX Latest} succeeds. `open()` with default `"9"` and
    {FIX Latest} still succeeds (fallback). `open()` with default `"10"` and {FIX50SP2} returns
    `invalid_session_config`.
- **RED**: (a) and (e) are string-level on input. On `origin/main`, (a)'s assertion "reply is a Logon,
  not a Reject" is recorded RED (Reject 373=5). (c) passes on `origin/main` (a characterization pin).
  M-1 (parse) turns (a), (d) and (e) RED. M-5a (no fallback) turns (c) and (f)'s second case RED. M-5b
  (reverse fallback) turns (b) and (f)'s third case RED.

## W-7 — 1407 / 1156 accepted and ignored (FR-008)

- **File**: `tests/session/test_fixt_logon_establishment.cpp`.
- **Asserts**:
  - (a) `1137=9` + `1407=303` against {FIX50SP2}: established, negotiated `v50sp2`. This is a
    characterization pin.
  - (b) `1137=10` + `1407=303` against {FIX Latest}: established, negotiated `vlatest`.
  - (c) The false-positive arm: `1137=10` + `1407=303` against {FIX50SP2}: Reject 373=5.
  - (d) An inbound application message with header `1156=303` on an Active session reaches `fromApp`
    exactly as the same message without 1156 does, with no Reject.
- **RED**: (a) and (d) pass on `origin/main`. Their RED is mutant M-7a, a Logon whose 1407 is present
  is refused (373=5), and mutant M-7b, a header carrying 1156 is session-rejected. (b) is RED on
  `origin/main` (the `1137=10` refusal). Mutant M-7c, 1407 presence makes the version serviceable
  (treat `1407=303` as `vlatest`), turns (c) RED.

## W-8 — no 1407 or header 1128 emitted (FR-008, SC-005)

- **File**: `tests/session/test_fixt_logon_establishment.cpp`. One helper counts occurrences of
  `"\x01" "1407="` and `"\x01" "1128="` in a captured outbound frame.
- **Asserts**: across every outbound frame W-6 and W-7 capture (Logon replies, Rejects, heartbeats sent
  in those tests), both counts are 0. The FIX Latest default's outbound Logon carries `1137=10`.
- **RED (the instrument)**: the counter is shown non-zero on two seeded frames, one carrying `1407=303`
  and one carrying `1128=9`. These are positives built inside the test, asserted to count 1 each. The
  `1137=10` assertion is RED under mutant M-2.

## W-9 — FIX Latest reify arm and typed version (FR-009, FR-012(c))

- **Files**: `tests/codegen/vlatest_dispatch_exclusion_test.cpp` (rewritten header and halves),
  `tests/dictionary/reify_dispatch_test.cpp`, `tests/codegen/vlatest_compile_smoke_test.cpp`,
  `tests/codegen/determinism_test.cpp`'s `VlatestGeneratedMatchesGolden` (golden regenerated).
- **Asserts**:
  - (a1) and (a2) are kept: under a v50sp2 profile, `D` reifies as `v50sp2` and `EC` fails loud.
  - (a3) is new: under a `vlatest` resolution, `EC` (FIX-Latest-only) and `D` reify with handle
    application version `vlatest`, and a MsgType absent from FIX Latest gives
    `dict_reify_unknown_msg_type`.
  - (b): the generated outer switch has exactly one `case …::v50sp2:` and exactly one `case …::vlatest:`.
  - The smoke test: `vlatest::owning_Heartbeat::which() == vlatest`.
  - The golden matches.
- **RED**: mutant M-9a deletes the `kAppVersions` vlatest row, which makes (a3) and (b) RED. Mutant
  M-9b reverts `app_version_enum("vlatest")` to `"v50sp2"`, which makes the smoke test and the golden RED.
- **Golden diff rule**: `git diff --stat` on the golden shows one file, and `git diff -U0` shows only
  lines where `application_version::v50sp2;` becomes `application_version::vlatest;`. Their count equals
  `grep -c` of the pre-change file. Quickstart §3 gives the check and its seeded positive.

## W-10 — no regression (FR-013, SC-006)

The full `linux-clang-debug` ctest at the final head, with zero failures outside the tests this contract
names as rewritten. The expected-ctest list is recorded by `/speckit-tasks`.
