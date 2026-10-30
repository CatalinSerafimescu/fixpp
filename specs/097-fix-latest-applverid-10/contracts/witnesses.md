# Contract: 097 witnesses and their RED arms

Every witness names its file, what it asserts, and how it is shown RED **for its stated reason**.

**Why most RED arms are mutants, not the unfixed tree.** A witness that names
`application_version::vlatest` does not compile on the unfixed tree. A compile failure is RED for the
wrong reason (it would be RED whatever the behaviour). So, except where noted, each witness's RED is a
**named mutant applied to the fixed tree**, built, and run unfiltered (`--gtest_filter` only narrows the
report, never the binary). The mutants are M-1..M-9h, plus M-3b (W-3) and M-8d (W-8); W-12 re-runs M-1, M-1n and M-2 against the fuzz
harness rather than adding one. A row whose rebuild failed proves nothing and is gated on build rc 0. The one
exception is M-9c, whose stated reason *is* a build failure. The verify record keeps a table: witness |
mutant | build rc | test rc | the failing assertion's message.

**Collateral.** Unless a row says "exactly one failing test", a mutant may also turn RED other tests
that reach the mutated site. The row is read on the assertions it names, and the record lists every
failing test the unfiltered run reports, so collateral is visible rather than assumed. A failing test
that does not reach the mutated site is investigated before the row is trusted.

**Where mutants run (`[const §XVI.6]`; Gate A round 1, O097-007).** Mutation proofs run in a scratch
copy of the tree, never in the PR's worktree. The main checkout holds this branch, and its
`build/linux-clang-debug` is the tree verify reads, so a mutant there would contaminate a peer's build.
The scratch copy is the reusable parallel worktree `~/Work/Programming/fixpp-parallel` (owner, T002,
2026-10-29: it is free; check `git -C ~/Work/Programming/fixpp-parallel status --porcelain` is empty and no
other session holds it first). Its build trees are already symlinked onto `/mnt/wsl/fixppbuild`. Procedure
(parent `research/G19-fix-fpml-iso20022/phases/phase-4/parallel-worktrees.md` §3a/§3b):

```
H=$(git -C <main checkout> rev-parse HEAD)          # the implementation head under test
df -h /mnt/wsl/fixppbuild                           # headroom for a Debug tree first
git -C ~/Work/Programming/fixpp-parallel switch --detach "$H"   # it is parked detached; never a branch
# ON tree = build/linux-clang-debug, ASan tree = build/linux-clang-asan: existing symlinks onto
# /mnt/wsl/fixppbuild/build/parallel-clang-{debug,asan}. The OFF tree is new:
mkdir -p /mnt/wsl/fixppbuild/build/parallel-097-off
ln -s /mnt/wsl/fixppbuild/build/parallel-097-off ~/Work/Programming/fixpp-parallel/build/097-off
export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache
cd ~/Work/Programming/fixpp-parallel
# The preset's CMAKE_TOOLCHAIN_FILE lives inside build/linux-clang-debug, so Conan runs first
# (CONTRIBUTING.md's minimum local cycle):
conan install . -pr conan/profiles/linux-clang-debug --build=missing -of build/linux-clang-debug
cmake --preset linux-clang-debug
# The OFF tree for M-9c/M-9d: same preset and toolchain, its own binary dir.
cmake --preset linux-clang-debug -B build/097-off -DFIXPP_CODEGEN_FIX_LATEST=OFF -DFIXPP_WERROR=ON
# The ASan tree for W-12's rows. The fuzz target exists only under FIXPP_BUILD_FUZZ, which the top-level
# CMakeLists.txt refuses without ASan or UBSan, so neither debug tree builds it:
conan install . -pr conan/profiles/linux-clang-asan --build=missing -of build/linux-clang-asan
cmake --preset linux-clang-asan
```

Each mutant is committed on that scratch worktree's **detached head**, built there, run and recorded.
Every mutant row but M-9c/M-9d and W-12's builds in the ON tree (`cmake --build --preset
linux-clang-debug --target <t> -j8`); M-9c and M-9d build in the OFF tree (`cmake --build
build/097-off --target <t> -j8`), since the preset build names the ON tree only; W-12's rows
build `fuzz_appl_ver_id` in the ASan tree (`cmake --build --preset linux-clang-asan --target
fuzz_appl_ver_id -j8`).
It is then dropped with `git reset --hard "$H"` in the scratch worktree. Never use `git stash`
(every worktree shares it), never `checkout`/`switch` in the main checkout, and never put a mutant
commit on the feature branch. After the last row, park `fixpp-parallel` detached at `origin/main`
(`git -C ~/Work/Programming/fixpp-parallel switch --detach origin/main`, clean status), remove the
`build/097-off` symlink and delete `/mnt/wsl/fixppbuild/build/parallel-097-off`. Never retire the
worktree itself (standing owner rule). Detaching `fixpp-parallel` does not touch the main checkout's branch.

Where unmodified code DOES compile the witness (string-level frames, no new enumerator), it is written
and run first (plan phase 2), and that run's result is recorded too.

## W-1 — parse (FR-001, SC-004)

- **File**: `tests/dictionary/version_profile_test.cpp`.
- **Asserts**: a table over every ApplVerID value `"0"`..`"10"`. `"2"`..`"9"` map as before, `"10"` maps to
  `vlatest`, `"0"`/`"1"` stay `dict_unknown_appl_ver_id`. The malformed values `"10 "`, `"010"`, `"1O"`,
  `"11"`, `"100"`, `"99"` stay `dict_unknown_appl_ver_id`. The existing "bad" lists drop `"10"`: here and
  in `tests/dictionary/reify_dispatch_test.cpp`.
- **RED**: on `origin/main`, the string-only half (`resolve("10")` has a value) fails: it returns
  `dict_unknown_appl_ver_id`. Mutant M-1 *widens* the `"10"` arm to accept any two-character string
  starting `'1'`, so `"11"` resolves: the `"11"` row turns RED. Mutant M-1n *narrows* it, mapping `"10"`
  to `v50sp2`, which turns the `"10"` → `vlatest` row RED.

## W-2 — render (FR-002, SC-004)

- **File**: `tests/dictionary/version_profile_test.cpp`.
- **Asserts**: `render(vlatest) == "10"`. Over the nine renderable members `v40`..`vlatest`, render then
  parse is the identity, and the nine rendered strings are pairwise distinct. As a separate assertion,
  `render(Unknown)` is `dict_unknown_appl_ver_id`. (W-1's eleven wire inputs `"0"`..`"10"` are a different
  set from these nine members.)
- **RED**: mutant M-2, `render(vlatest)` returns `"9"`. The round-trip and the distinctness checks both
  fail.

## W-3 — config token (FR-003)

- **Files** (both in `config_044_tests`, `tests/config/CMakeLists.txt`): the accepted-token arm in
  `tests/config/test_load_happy_path.cpp`, beside its `pos_fixt_appl_ver_ids.toml` case, loading a new
  fixture `tests/config/fixtures/pos_fixt_appl_ver_id_10.toml` (one FIXT session,
  `default_appl_ver_id = "10"`); the unknown-token arm in `tests/config/test_load_negative_battery.cpp`,
  beside its `neg_fixt_empty_appl_ver_id.toml` case, loading a new fixture
  `tests/config/fixtures/neg_fixt_unknown_appl_ver_id.toml` (`default_appl_ver_id = "11"`). New files,
  in the directory's `pos_*` / `neg_*` naming: `pos_fixt_appl_ver_ids.toml` is not extended, because
  its test iterates an expected array.
- **Asserts**: `"10"` loads as `vlatest`. An unknown token (`"11"`) yields `reason_class::unknown_enum`,
  and the message contains `"2".."10"`.
- **RED**: mutant M-3, delete the `"10"` branch: the load yields a diagnostic. The text arm is RED on
  `origin/main` (it reads `"2".."9"`).
- **RED, the value**: mutant M-3b, the `"10"` branch in `src/config/scalar_mappers.cpp` maps to `v50sp2`
  instead of `vlatest`. The load succeeds, so the accepted-token arm turns RED on its value assertion
  alone (`"10"` loads as `vlatest`). M-3's RED lands on the load's `has_value()` precondition and never
  reaches that assertion (re-run 2 ruling).
- The existing comments that restate the `"2".."9"` range for the old fixture are rewritten to point at
  the mapper (FR-012(c)). That is checked by quickstart §4's comment-claims run, not by a test.

## W-4 — registry coexistence (FR-004, FR-005, SC-002)

- **File**: `tests/dictionary/orchestra_loader_test.cpp`. `OrchestraFR010Guard.BothDictsFailLoud` (the
  FR-010 death test) becomes the coexistence test `OrchestraRegistry097.BothDictsCoexist`; the death
  assertion is deleted together with the abort.
- **Asserts**: building from {FIX50SP2, FIX Latest}, in both orders, returns. `get(v50sp2)` is the
  FIX50SP2 dict and `get(vlatest)` is the FIX Latest dict. `BothDictsCoexist` captures stderr around
  both constructions (`testing::internal::CaptureStderr()` / `GetCapturedStderr()`) and asserts it is
  empty: no diagnostic is written (US2 AS-1). Quickstart §2's `grep -c FR-010` is a secondary check. `OrchestraVersionIdentity.DistinctVLatestMapsToV50SP2`
  is rewritten to assert the mapping to `vlatest` and renamed `OrchestraVersionIdentity.DistinctVLatestMapsToVlatest`.
- **RED**: on `origin/main` the construction aborts with `FR-010`. This is recorded from the death test
  as it exists before the rename: its pass on `origin/main` IS the abort. Mutant M-4 restores
  `case vlatest: return v50sp2;` without the abort: `get(vlatest)` is empty and `get(v50sp2)` returns
  the last writer. So `get(vlatest)` fails in both orders (the fallback answers `v50sp2` only), and
  `get(v50sp2)` is also wrong in one order.

## W-5 — one-way fallback (FR-006)

- **File**: `tests/dictionary/orchestra_loader_test.cpp`. `SingleDictConfigsSucceed` is kept and extended
  with a new `OrchestraRegistry097.OneWayFallback`.
- **Asserts**: every row of data-model §A's lookup table. With FIX Latest alone, `get(v50sp2)` and
  `get(vlatest)` are both that dict (the fallback is kept), and `get(v44)` is none (the fallback
  answers v50sp2 only). With FIX50SP2 alone, `get(vlatest)` is
  `dict_no_dictionary_for_application_version` (no reverse fallback). With both, each version gets its
  own dict (exact match beats the fallback). Empty gives none for both.
- **RED**: the FIX-Latest-alone `get(v50sp2)` arm passes on `origin/main` (a characterization pin).
  Mutant M-5a deletes the fallback, which turns that arm RED. Mutant M-5b adds the reverse fallback, so
  an empty vlatest slot answers from v50sp2; this turns the FIX50SP2-alone arm RED. Mutant M-5c makes the
  fallback win over an exact match, which turns the both-registered arm RED.

## W-6 — FIXT establishment (FR-007, SC-001)

- **File**: `tests/session/test_fixt_logon_establishment.cpp`. Cloned from the existing W5 v50sp2
  acceptor case, with an Orchestra-dictionary helper beside `make_dict` (the minimal repository skeleton
  pattern of `orchestra_loader_test.cpp`, or the vendored file). The registry is the dictionary set
  under test. `validate_inbound_messages` stays at its default (**off**), so the `FixtSetup` session
  dictionary (`kMinimalFix44Xml`, Heartbeat only) takes no part. Validation-on behaviour is W-7's.
- **Asserts**, one test per row:
  - (a) Acceptor, registry {FIX Latest}, inbound `1137=10`: Logon reply, Active, and the negotiated
    profile reads `vlatest`.
  - (b) Acceptor, registry {FIX50SP2}, inbound `1137=10`: Reject 373=5, 371=1137, then disconnect.
  - (c) Acceptor, registry {FIX Latest}, inbound `1137=9`: established, negotiated `v50sp2`. This is the
    one-way fallback and B-097-1's "still admits".
    - (c2) Admission arm: the same registry and peer, but the session's own default is `"10"`:
      established, negotiated `v50sp2`. It exists because (c)'s own default `"9"` already needs the
      fallback at `open()`. With default `"10"`, `open()` hits the exact vlatest slot, so only the
      inbound Logon's lookup uses the fallback, and a missing fallback shows as a refused Logon with
      `open()` succeeding. (c) stays as it is: it is the characterization pin that passes on
      `origin/main`.
  - (d) Acceptor, registry {FIX50SP2, FIX Latest}: one session per peer, `1137=9` and `1137=10`, both
    established, each negotiating its own version. One arm uses a session whose own dictionary is
    FIX50SP2 receiving `1137=10`: admitted, which pins the engine-wide rule (spec Clarifications,
    `/speckit-clarify`).
  - (e) Initiator, Logon-ack `1137=10`: negotiated `vlatest` (recorded, no registry lookup).
  - (f) `open()` with default `"10"` and {FIX Latest} succeeds. `open()` with default `"9"` and
    {FIX Latest} still succeeds (fallback). `open()` with default `"10"` and {FIX50SP2} returns
    `invalid_session_config`.
- **RED** (Gate A round 1, O097-002: M-1 widens the parse and cannot turn any W-6 arm RED, so it is not
  used here):
  - (a) is string-level on input. On `origin/main` its assertion "reply is a Logon, not a Reject" is
    recorded RED (Reject 373=5). The negotiated-version assertion is added once the enumerator exists.
  - (c) passes on `origin/main`; it is a characterization pin.
  - (b) passes on `origin/main` for a different reason than at the head: there `"10"` does not
    parse; at the head it parses and the {FIX50SP2} registry does not serve it. It feeds `1137=10`, so
    it is inside FR-013's exception (i), not one of its pins. M-5b proves the head's reason.
  - **M-1n** (narrowing: `"10"` → `v50sp2`) turns (a), (d)'s `1137=10` sessions and (e) RED, each on
    its negotiated-version assertion. (e) has no RED on `origin/main`: the initiator never consults the
    registry, and naming `vlatest` does not compile there. M-1n is its only RED.
  - **M-4** turns (d)'s both-registered arm RED. It also turns (a) and (f)'s first case RED: the FIX
    Latest dictionary lands in the v50sp2 slot, so `get(vlatest)` is empty, (a)'s `1137=10` gets
    Reject 373=5, and (f)'s `open()` with default `"10"` and {FIX Latest} returns
    `invalid_session_config`.
  - **M-5a** (no fallback) turns (c2) RED on its admission assertion (the Logon is refused while
    `open()` succeeds), and (f)'s second case RED. (c) also goes RED under it, at its `open()`
    precondition, which is the lookup (f)'s second case pins; (c)'s M-5a RED is read on (c2).
  - **M-5b** (reverse fallback) turns (b) and (f)'s third case RED.

## W-7 — 1407 / 1156: no rule, dictionary-decided validation (FR-008)

- **File**: `tests/session/test_fixt_logon_establishment.cpp`. Every arm runs with
  `validate_inbound_messages` **on** and names its **session dictionary** (`SessionConfig::dictionary`),
  which is what validates (research R-6). The registry is named separately. The existing `FixtSetup`
  dictionary (`kMinimalFix44Xml`) defines only Heartbeat, so under validation it rejects every Logon.
  It is therefore **not** used here (Gate A round 1, O097-001).
- **Control arms.** Every arm `(x)` has a control `(x0)`: the same frame without 1407/1156, the same
  session dictionary and registry. Each control has an expected outcome for **each** tree it runs on,
  and must show that outcome before its arm is read. A control that departs from its expected outcome
  means the fixture is wrong (plan phase 2's stop rule, first part), and the arm proves nothing until it
  matches. This is the instrument's proof that it can report a pass. The one control whose outcome
  differs between the trees is (b0): on `origin/main`, `"10"` does not parse.
- **Asserts**:
  - (a) Session dictionary `dictionaries/FIXT11.xml`, registry {FIX50SP2}: Logon `1137=9` + `1407=303`
    is established, negotiated `v50sp2`. This is a characterization pin. Control (a0): `1137=9`
    without 1407.
  - (b) Session dictionary `FIXT11.xml`, registry {FIX Latest}: `1137=10` + `1407=303` is established,
    negotiated `vlatest`. Control (b0): `1137=10` without 1407. **On `origin/main`** (plan phase 2),
    (b) and (b0) both get the same 1137 Reject (373=5, 371=1137). The Logon passed validation and was
    refused at the 1137 check, so 1407 is not the cause. **At the implementation head** both are
    established and negotiated `vlatest`.
  - (c) The false-positive arm. Session dictionary `FIXT11.xml`, registry {FIX50SP2}: `1137=10` +
    `1407=303` gets Reject 373=5, 371=1137, then disconnect (W-6(b)'s shape), so 1407 does not make
    the version serviceable. Control (c0): `1137=10` without 1407, the same session dictionary and
    registry, the same Reject.
  - (d) Session dictionary the Orchestra FIX Latest file, registry {FIX Latest}, Logon `1137=9`
    (admitted today through the fallback, so this arm runs on `origin/main`). On the Active session, an
    inbound application message of a MsgType the Orchestra file defines, with its required fields and
    header `1156=303`, reaches `fromApp` with no Reject. Control (d0): the same message without 1156.
    - Header 1156 under a FIX50SP2 session dictionary is **not** a session arm, because `FIX50SP2.xml`
      defines no Logon and a validated session using it cannot reach Active. That case is covered by
      081's existing validator-level witness, `tests/wire/fixt_header_validate_test.cpp`, whose frames
      carry `1156=` through the FIXT framing surface. It is cited, not rewritten.
  - (e) The dictionary-decided reject (B-097-3, owner ruling, Gate A round 1). Session dictionary
    FIX4.x-shaped with 1137 injected: `fix44_dictionary_with_1137()`, which lives in
    `tests/support/validation_test_dictionary.hpp` and is called by both the 097 test and
    `tests/session/engine_reset_unit_stop_test.cpp` (moved there at T068 F3; before it, the 097 test
    held a copy of the latter file's helper).
    Registry {FIX50SP2}. Logon `1137=9` + `1407=303` gets a session Reject (35=3) and is never Active.
    The validate-first gate passes the validator's `ref_tag` and
    `wire_error_to_session_reject_reason(...)` into the Reject: 371 is expected to be 1407, and 373 is
    what `wire_unexpected_tag` maps to. Both are read from the phase-2 run and asserted as recorded.
    This is a characterization pin. Control (e0): `1137=9` without 1407, established. If (e)'s observed
    shape departs from this text while (e0) passes, record the observed shape (the pin is of today's
    behaviour), correct this text, and change no production code.
  - (f) The dictionary-decided reject of an undeclared header 1156 (FR-008 "rejects both"; B-097-3;
    Gate A round 2, G097-014). On (e0)'s Active session (the same FIX4.x-shaped session dictionary and
    registry {FIX50SP2}), an inbound application message `D` (NewOrderSingle, which that dictionary
    defines) with its required fields plus header `1156=303` gets a session Reject (35=3) and does not
    reach `fromApp`. That dictionary is a FIX 4.4 session version, so the FIXT framing surface
    (installed only for FIX50/SP1/SP2 dictionaries) does not whitelist 1156 for it. 371 is expected to
    be 1156 and 373 what `wire_unexpected_tag` maps to; both are read from the phase-2 run and asserted
    as recorded. This is a characterization pin. Control (f0): the same `D` without 1156, delivered to
    `fromApp`. If (f)'s observed shape departs from this text while (f0) passes, record the observed
    shape, correct this text, and change no production code (as for (e)).
  - (v1) and (v2), the non-vacuity controls of (a)-(d) (T012 ruling). (a)-(d) pass when the validator
    accepts 1407 or 1156, and would also pass if no validation ran under their session dictionaries.
    (v1) uses session dictionary `FIXT11.xml` and registry {FIX50SP2} ((a)'s); (v2) uses the Orchestra
    FIX Latest file and registry {FIX Latest} ((d)'s). Each feeds (a0)'s Logon (`1137=9`) plus one
    field neither file declares, and gets a session Reject (35=3) with 371 = that tag and 373 = what
    `wire_unexpected_tag` maps to, never Active. Without the validator that Logon would establish, so
    the Reject shows the gate live under exactly those dictionaries. The tag is checked undeclared by
    `grep -c` on both files, which prints 0, while the same greps for 1407 print non-zero.
- **RED**:
  - (a), (d), (e) and (f) pass on `origin/main`. They are characterization pins, each with a mutant.
  - (c) passes on `origin/main` for a different reason than at the head (`"10"` does not parse there;
    at the head the registry does not serve it). It is inside FR-013's exception (i). M-7c proves the
    head's reason.
  - (b) is RED on `origin/main` (the `1137=10` refusal, 373=5). Its negotiated-version assertion is
    added once the enumerator exists. **M-1n** (`"10"` → `v50sp2`) turns that negotiated-`vlatest`
    assertion RED, and (b0)'s too: the FIX Latest dictionary answers `v50sp2` through the fallback, so
    both stay established, negotiated `v50sp2`.
  - M-7a: the acceptor refuses a Logon that carries 1407 (373=5). Site: the acceptor's
    DefaultApplVerID block in `Session::on_inbound_not_connected_` (`src/session/session.cpp`). The
    `logon_interpret_result` that `interpret_logon` returns does not carry 1407, so the mutant reads the
    raw inbound frame for `"\x01" "1407="` itself. It turns (a) and (b) RED while (a0) and (b0) stay
    green.
  - M-7b: a header carrying 1156 is session-rejected. Site: the inbound application path of
    `Session::on_inbound_active_` (`src/session/session.cpp`), after its validate-first gate and before
    the `fromApp` dispatch: when the raw inbound frame contains `"\x01" "1156="`, the mutant sends a
    session Reject instead of dispatching. Binary: `session_fixt_logon_establishment`. It turns (d) RED
    while (d0) stays green. (f) is rejected by the validator before that site, so it stays rejected;
    if its recorded 371/373 move under the mutant, record the observed shape.
  - M-7c, a serviceability bypass (Gate A round 2, G097-014). Same site as M-7a: when the raw inbound
    Logon contains `"\x01" "1407="`, `serviceable` is `resolved.has_value()` alone, so the registry
    lookup is skipped. Under it (c) is established (RED: `"10"` parses to `vlatest` at the head) while
    (c0) still gets Reject 373=5, 371=1137. Derivation: (c)'s Reject comes from the registry lookup,
    not from the parse. A mutant that only changes which version 1407 maps to (round 1's M-7c, "treat
    `1407=303` as `vlatest`") cannot turn (c) RED, because a FIX50SP2-only registry answers no FIX
    Latest lookup (FR-006 forbids the reverse fallback; `version_registry::get` returns
    `dict_no_dictionary_for_application_version` for an empty slot).
  - M-7d: the validator's unexpected-tag check (`include/fixpp/wire/validator.hpp`, the
    `wire_unexpected_tag` test of the field walk) accepts 1407 for every dictionary. It turns (e) RED
    (established instead of rejected) while (e0) stays green. Why one exemption suffices: the walk's
    later checks pass an undeclared tag. `table_view::enum_valid` accepts a tag with no enum entry,
    the type check reads `field_type_of_with_framing`, which defaults an undeclared tag outside the
    framing surface to String (`303` passes), and the required-field and group-structure steps do not
    consider it.
  - M-7e (Gate A round 2, G097-014): the same check accepts 1156 for every dictionary. It turns (f) RED
    (delivered instead of rejected) while (f0) stays green; the later checks pass it for the reason
    given under M-7d. It exempts the one tag at the check rather than installing the framing table for
    every version (`src/dictionary/dictionary.cpp`): the table whitelists every FIXT framing tag and
    retypes them, so a RED under it would not isolate 1156.

## W-8 — nothing session-originated carries 1407, 1156 or header 1128 (FR-008, SC-005)

- **File**: `tests/session/test_fixt_logon_establishment.cpp`. One helper counts occurrences of
  `"\x01" "1407="`, `"\x01" "1156="` and `"\x01" "1128="` in a captured outbound frame; a second reads
  its MsgType (35). Frames are captured from the session's outbound transport double, as the file's
  existing FIXT handshake tests do.
- **Frame sources (Gate A round 2, G097-015)**, each a named session, for both defaults SC-005 names:
  - (i) An initiator configured `default_appl_ver_id = "10"` (`vlatest`), registry {FIX Latest}. Its
    registry must hold FIX Latest: otherwise `open()` returns `invalid_session_config` (W-6(f)),
    nothing is captured, and every count reads 0 for the wrong reason.
  - (ii) An initiator configured `"9"` (`v50sp2`), registry {FIX50SP2}.
  - For each of (i) and (ii): its Logon (35=A) is captured at `open()`. A peer Logon reply carrying
    its own 1137 is then fed, and the session is Active. On that session: an inbound TestRequest draws a
    Heartbeat (35=0); the liveness loop's TestRequest (35=1) is driven by letting the heartbeat interval
    pass with no inbound traffic, in the pattern of `tests/session/heartbeat_testrequest_test.cpp`
    (time moves on the session's `mock_clock`, never by a wall-clock wait, and each pump is that file's
    bounded pump, `tests/support/pump_until_ready.hpp`; the clock is advanced past the configured
    heartbeat interval, so a TestRequest that never comes fails the non-vacuity assertion instead of
    hanging); and
    `close()` sends a Logout (35=5).
  - (iii) The acceptor's Logon reply (35=A) of W-6(a) (peer `1137=10`) and of W-6(c) (peer `1137=9`),
    and the acceptor's Reject (35=3) of W-6(b). These acceptors are configured `"9"`: W-6 clones the
    W5 v50sp2 acceptor case.
  - (iv) For the `"10"` default (SC-005's "both defaults"): an acceptor configured `"10"`, registry
    {FIX Latest}. Its Logon reply (35=A) to a peer `1137=10`, and, on a second session, its Reject
    (35=3; 373=5, 371=1137) to a peer `1137=6`, which that registry does not serve.
  - Frames W-8 does not capture: ResendRequest, SequenceReset-GapFill, BusinessMessageReject and
    Reject-with-text. Their builders in `src/session/admin_messages.cpp` write no 1128, 1156 or 1407
    (`grep -n "1128\|1156\|1407" src/session/admin_messages.cpp` is empty), and 097 changes no
    `src/session/` file (tasks T075), so the source fact covers them.
- **Asserts**:
  - **Non-vacuity first.** Every frame source listed above yields its frame: the initiator Logon,
    Heartbeat, TestRequest and Logout for each of (i) and (ii); the acceptor Logon reply for each of
    W-6(a) and W-6(c); the Reject for W-6(b); and (iv)'s Logon reply and Reject. A missing frame fails
    the test before any count is read.
  - In every captured frame, the three counts are 0.
  - (i)'s Logon carries `1137=10`; (ii)'s carries `1137=9`.
  - No W-8 session sends an application payload, so the payload pass-through of 1128/1156 (B-097-3) is
    outside this count.
- **RED**:
  - The instrument: the counter is shown non-zero on three seeded frames, carrying `1407=303`,
    `1156=303` and `1128=9`. These are positives built inside the test, asserted to count 1 each. This
    proves the counter counts; it is not the witness's RED.
  - The witness is a characterization pin (no-origination holds on `origin/main`), so FR-010 requires
    a production mutant. The admin builders in `src/session/admin_messages.cpp` share no header writer:
    each `build_*` writes its own standard header. So there is one mutant per tag, each in a different
    builder:
    - M-8a: `build_logout` appends `1156=303` after TargetCompID. The Logout's 1156 count turns RED.
    - M-8b: `build_logon` appends `1407=303`. (i)'s and (ii)'s Logon 1407 counts turn RED, and so do
      the 1407 counts of (iii)'s and (iv)'s acceptor Logon replies, which the same `build_logon`
      builds.
    - M-8c: `build_heartbeat` appends `1128=9`. The Heartbeat's 1128 count turns RED.
  - The non-vacuity assertion's own RED (re-run 2 ruling): mutant M-8d deletes the mechanism it
    observes. The session no longer answers an inbound TestRequest with a Heartbeat:
    `Session::inbound_heartbeat_reply_leaf_` in `src/session/session.cpp` (called only from
    `on_inbound_active_test_request_`) builds the Heartbeat and emits nothing. Both W-8 tests turn RED
    on `ASSERT_TRUE(has_w8_test_request_answer(...))`, the check that a Heartbeat echoing the inbound
    TestRequest's `112=W8TR` is present. A liveness Heartbeat carries no 112, so it cannot satisfy
    that check. Other tests that need a TestRequest answer may also turn RED; the record lists them.
  - The `1137=10` assertion on (i)'s Logon turns RED under mutant M-2 (`render(vlatest)` returns
    `"9"`).
- **Phase split**: (ii), and (iii)'s W-6(b)/(c) frames, compile on `origin/main` and run in plan
  phase 2 as characterization pins (all counts 0, every family present). (i), W-6(a)'s reply and (iv)
  name `"10"` / `vlatest` and run from plan phase 4.

## W-9 — FIX Latest reify arm and typed version (FR-009, FR-012(c), US3)

- **Files**: `tests/codegen/vlatest_dispatch_exclusion_test.cpp` (rewritten header and halves; the
  (a3) census, the (a3) samples and (a4)), `tests/dictionary/reify_dispatch_test.cpp` (the (a3) admin
  sub-case, the (a3) in-frame 1128 sub-case and (c)), `tests/codegen/vlatest_compile_smoke_test.cpp`,
  `tests/codegen/determinism_test.cpp`'s `VlatestGeneratedMatchesGolden` (golden regenerated),
  `tests/codegen/CMakeLists.txt` (`FIXPP_DICT_DATA_DIR` added to `codegen_vlatest_tests`'s
  `target_compile_definitions`, for the census; `codegen_vlatest_census_test` in the same file is the
  precedent), `tests/codegen/test_067_emit_builders_unit.cpp`'s
  `VersionIr097.FixLatestHasOwnApplicationVersion` (in `codegen_067_emit_builders_unit_test`, which
  links `ir.cpp` directly and is not gated on `FIXPP_CODEGEN_FIX_LATEST`; Gate B round 2, G097-B2-002).
- **Asserts (FIX Latest tier ON, the default)**:
  - (a1) and (a2) are kept: under a v50sp2 profile, `D` reifies as `v50sp2` and `EC` fails loud. Their
    comments and assertion messages are rewritten (tasks T040); the RED of this witness is (a3)/(a4).
  - (a3) is new, the runtime outcome FR-009(a), whose partition it restates: under a `vlatest`
    resolution, every MsgType the FIX Latest dictionary defines other than the seven FIXT admin
    MsgTypes `dict::reify` intercepts first reifies to an `owning_message_handle` whose
    `version().application == vlatest`, and a MsgType that dictionary does not define gives
    `dict_reify_unknown_msg_type`. Its sub-cases, each with its file and call path (Gate A round 4,
    G097-021 and O097-301):
    - **(a3) census**, in `tests/codegen/vlatest_dispatch_exclusion_test.cpp`, through direct
      `dispatch::dispatch_application` with an empty `MV`. Its binary, `codegen_vlatest_tests`, is
      built only when the FIX Latest tier is ON (`tests/codegen/CMakeLists.txt`,
      `if(FIXPP_CODEGEN_FIX_LATEST)`) and already includes the dispatch header. It is not in
      `reify_dispatch_test.cpp`, because that binary also hosts the OFF-only (c). It loads
      `dictionaries/orchestra/OrchestraFIXLatest.xml` (under `FIXPP_DICT_DATA_DIR`) with
      `OrchestraLoader`. For every `MessageEntry` in `Dictionary::messages()` whose MsgType is not one
      of the seven `is_fixt_admin` MsgTypes, it calls
      `dispatch_application(mv, msg_type, application_version::vlatest, profile, &arena)` and asserts
      `has_value()`, `version().k == kind::application` and `version().application == vlatest`; each
      failure message names the MsgType. Non-vacuity: it asserts that all seven admin MsgTypes were
      found in `messages()` and skipped, and that the dispatched count equals `messages().size()`
      minus the seven skipped. Any comment states that derivation, never a literal count. Scope: the
      vlatest IR is built from the same `OrchestraLoader` `Dictionary::messages()`
      (`tools/codegen/fixpp-codegen/ir.cpp`), so the census guards the emitter loop, not the loader.
      Loader drops stay `VlatestCompletenessCensus`'s and `OrchestraLoad.LoadsEP303`'s.
    - **(a3) samples**, in `tests/codegen/vlatest_dispatch_exclusion_test.cpp`, through the same direct
      `dispatch_application` with an empty `MV`: `EC` (FIX-Latest-only) and `D` reify with
      `application == vlatest`; `ZZ`, a MsgType absent from FIX Latest, gives
      `dict_reify_unknown_msg_type`. `ZZ` is absent from every arm, not only from FIX Latest:
      `grep -c 'msgType="ZZ"' dictionaries/orchestra/OrchestraFIXLatest.xml` and
      `grep -c "msgtype='ZZ'" dictionaries/*.xml` print 0 for every file, while the same greps for `D`
      print non-zero (the known positive). It is two characters, so under the `vlatest` arm it reaches
      that arm's two-character `default:`.
    - **(a3) admin interception**, in `tests/dictionary/reify_dispatch_test.cpp`, through `dict::reify`
      on a framed message (that file's `ReifyFixture`): a FIXT admin MsgType under the same profile
      keeps its session-admin label. It is witnessed only through `dict::reify`, because only
      `dict::reify` intercepts the admin MsgTypes: the emitter does not filter them, so a direct
      `dispatch_application` of one returns `application` / `vlatest`. The case is compiled only when
      the FIX Latest tier is ON, behind the compile definition (c) asks `/speckit-tasks` to name, with
      the opposite polarity. The case loops over all seven FIXT admin MsgTypes (`0 1 2 3 4 5 A`), each
      in its own framed frame, and asserts `has_value()` and `version().k == kind::session_admin` for
      each, the failure message naming the MsgType.
    - **(a3) in-frame 1128**, in `tests/dictionary/reify_dispatch_test.cpp`, ON-guarded like the admin
      case: `ReifyRoundTrip.ApplVerId10InFrameResolvesToVlatest`, on the pattern of
      `ReifyRoundTrip.ApplVerIdInFrameDrivesResolution`. A framed `35=D` carrying `1128=10`, built
      inline with `fixpp::test_support::assemble_frame` (no new helper), through `dict::reify` under a v50sp2-default
      profile, gives `version().k == kind::application` and `version().application == vlatest`: the
      in-frame value, not the profile default, selects FIX Latest (B-097-4's condition).
  - (a4) is new (Gate A round 3, G097-019), in `tests/codegen/vlatest_dispatch_exclusion_test.cpp`,
    through the same direct `dispatch_application` with an empty `MV` as the (a3) samples (Gate A
    round 4, O097-301): under a `vlatest` resolution,
    XMLnonFIX (35=n) reifies to a handle with `version().k == kind::application` and
    `version().application == vlatest`. It is the one MsgType where reify's partition and the
    Orchestra message category differ (the Orchestra file files it under `category="Session"`, and the
    IR's `is_application` is false for it).
  - (b), the anchor test's own (b) half, not FR-009(b): the generated outer switch has exactly one
    `case …::v50sp2:` and exactly one `case …::vlatest:`.
  - The static outcome FR-009(b) is the smoke test: `vlatest::owning_Heartbeat::which() == vlatest`.
  - The golden matches.
  - `build_ir()` on `dictionaries/orchestra/OrchestraFIXLatest.xml` returns a `VersionIR` whose
    `session` and `application` are both `vlatest` and whose `ns` is `"vlatest"`
    (`VersionIr097.FixLatestHasOwnApplicationVersion`; Gate B round 2, G097-B2-002).
- **(c) FIX Latest tier OFF (US3 scenario 4; Gate A round 1, O097-003)**: one case compiled only when
  `FIXPP_CODEGEN_FIX_LATEST` is OFF. It sits in a dictionary test binary that the OFF build compiles
  (`tests/dictionary/reify_dispatch_test.cpp` if it builds under OFF; `/speckit-tasks` confirms this
  and names the compile definition `tests/dictionary/CMakeLists.txt` sets from
  `NOT FIXPP_CODEGEN_FIX_LATEST`; no new executable). It asserts that, under a `vlatest` resolution,
  `dict::reify` returns `dict_reify_unknown_msg_type` for `D` and for `EC`, and that the generated
  `_dispatch/reify_dispatch_application.hpp` holds no `case …::vlatest:`. The OFF build itself
  configures and builds with `FIXPP_WERROR=ON`. **No CI lane builds OFF**
  (`git grep FIX_LATEST .github CMakePresets.json` finds no OFF setting). So (c) runs only in the one
  OFF configure+build that plan phase 7 and `/speckit-verify` perform, its tree on
  `/mnt/wsl/fixppbuild`, and its result is recorded in the verify record.
- **RED**:
  - M-9a deletes the `kAppVersions` vlatest row. It turns the (a3) census, the (a3) `EC`/`D` samples,
    the (a3) in-frame 1128 case, (a4) and (b) RED.
  - M-1n (the US1 mutant, `"10"` → `v50sp2`) is re-run here against `dictionary_reify_tests`: the (a3)
    in-frame 1128 case turns RED on its `application == vlatest` assertion.
  - M-9b reverts `app_version_enum("vlatest")` to `"v50sp2"`. It turns the smoke test and the golden RED.
  - M-9c restores emitting an outer `case` for a row whose IR is absent: today's shape, an unread
    `rmv_app`. The OFF build fails with `-Werror=unused-variable` in the generated header. Its stated
    reason is a build failure, so the build-rc gate is inverted for this row: RED = build rc ≠ 0 with
    that diagnostic.
  - M-9d makes the OFF emitter write a `case …::vlatest:` that dispatches through the v50sp2 MsgType
    switch. It turns (c) RED: a handle is returned where the error was expected.
  - M-9e (Gate A round 3, G097-019) applies the category filter Codex offered as an alternative: when
    `emit_dispatch_application` writes the `vlatest` arm, and only that arm, it skips every message
    whose `is_application` is false. It builds in the ON tree. Gate the row on build rc 0 and on the
    mutant having taken: the regenerated `_dispatch/reify_dispatch_application.hpp`'s `vlatest` arm
    holds no `XMLnonFIX` case, while its v44 arm still does. It turns (a4) RED: 35=n falls to the
    `vlatest` arm's inner default and gets `dict_reify_unknown_msg_type` where an `application` /
    `vlatest` handle was expected. The (a3) census is RED too, and names `n`. The (a3) samples stay
    green, since `EC` and `D` are application messages, and so does the (a3) admin interception case,
    since `dict::reify` intercepts the admin MsgTypes before dispatch; that is why M-9a cannot play
    this role.
  - M-9f (Gate A round 4, G097-021): in `emit_dispatch_application`, skip `m.msg_type == "8"` only
    when `av.ns == "vlatest"`. It builds in the ON tree. Gate the row, as for M-9e, on build rc 0 and
    on the mutant having taken: the regenerated `_dispatch/reify_dispatch_application.hpp`'s `vlatest`
    arm holds no `case '8'`, while its v50sp2 arm keeps one. It turns the (a3) census RED, naming `8`.
    The (a3) samples (`D`, `EC`) and (a4) stay green, which is why M-9a and M-9e cannot play this
    role.
  - M-9g (analyze C1), the (a3) admin case's RED. In `dict::reify` (`src/dictionary/reify.cpp`), at
    Step 2's admin-interception call site (not `is_fixt_admin` itself), the condition becomes
    `mt_sv.size() == 1 && is_fixt_admin(mt_sv.front()) && profile.default_appl != application_version::vlatest`.
    It builds in the ON tree; it is not a codegen mutant, so no reconfigure gate. Under a `vlatest`
    profile an admin MsgType then skips interception, resolves to `vlatest` (no 1128 in the frame) and
    reaches the `vlatest` arm, which has a case for each of the seven, because the emitter has no admin
    filter and the Orchestra file defines all seven. It turns the (a3) admin case RED on
    `version().k == kind::session_admin` (record which assertion fired; a `has_value()` failure also
    counts). Isolation: `is_fixt_admin`'s only caller is `dict::reify`, which has no `src/` caller, and
    the existing admin reify witnesses use v50sp2 profiles or `dispatch_fixt` directly. Re-derive at the
    head: `grep -rn "is_fixt_admin(" src include tools` shows its definition and one call, in
    `dict::reify`; and `grep -n "application_version::vlatest"` over the sources that
    `add_executable(dictionary_reify_tests …)` lists in `tests/dictionary/CMakeLists.txt` (derive the
    list from that block at the head) shows only the 097 cases. So run
    `dictionary_reify_tests` UNFILTERED, gated on build rc 0, and expect exactly one failing test, the
    (a3) admin case.
  - M-9h (analyze C2), the absent-MsgType sample's RED. In `emit_dispatch_application`
    (`tools/codegen/fixpp-codegen/emit_dispatch.cpp`), for `av.ns == "vlatest"` only, the single-char
    `default:`, the two-char `default:` and the arm's trailing return emit
    `::fixpp::dict::detail::owning_message_handle_from_frame(rmv_app, view, mr)` instead of the error.
    It builds in the ON tree. Gate the row, as for M-9e, on build rc 0, reconfigure rc 0 and the
    mutant having taken: in the regenerated `_dispatch/reify_dispatch_application.hpp`, the `vlatest`
    arm has no `dict_reify_unknown_msg_type` after its empty-MsgType guard, while the v50sp2 arm keeps
    it. It turns the `ZZ` sample RED (a handle where the error was expected). The census, `D`/`EC`, the
    in-frame 1128 case, the admin case, (a4) and (b) stay green. Before this mutant the `ZZ` sample has
    no RED: at T043 (no `vlatest` arm yet) it passes, because the outer `default:` returns the same
    error. That pass is for the wrong reason and proves nothing about the arm.
  - M-9i (Gate B round 2, G097-B2-002) sets the vlatest kCodegenVersions row's `.a` back to `v50sp2`. It turns `VersionIr097.FixLatestHasOwnApplicationVersion` (codegen_067_emit_builders_unit_test) RED on its application assertion.
- **Golden diff rule**: `git diff --stat` on the golden shows one file, and `git diff -U0` shows only
  lines where `application_version::v50sp2;` becomes `application_version::vlatest;`. Their count equals
  `grep -c` of the pre-change file. Quickstart §3 gives the check and its seeded positive.

## W-10 — no regression (FR-013, SC-006)

The full `linux-clang-debug` ctest at the final head, with zero failures outside the tests this contract
names as rewritten. The expected-ctest list is recorded by `/speckit-tasks`. FR-013's two exceptions
(value `"10"` wherever parsed; registry contents that aborted on `origin/main`) are what W-1, W-4 and
W-6(a)/(d)/(e) assert. The pins that show the rest unchanged are W-5's FIX-Latest-alone arm, W-6(c),
W-6(f)'s second case, W-7(a)/(d)/(e)/(f) and W-8's phase-2 arms.

Rewritten at implement (T046, orchestrator ruling): `DeterminismTest.AdditiveOffOnByteDiff` (076 V-7),
clause (b). It required every OFF-run file to be byte-identical to its ON-run copy, which includes the
shared `_dispatch/reify_dispatch_application.hpp`; 097 adds FIX Latest's own dispatch arm only when the
tier is ON (W-9, research R-5), so that one file, exempted by its exact relative path, is now checked
structurally: the ON copy equals the OFF copy plus one contiguous inserted block, the block holds exactly
one `case ::fixpp::dict::application_version::vlatest:`, and the OFF copy holds none. Every other OFF-run
file stays byte-identical.

## W-11 — the record (FR-011, FR-012; Gate A round 1, G097-009 / RC-3)

A records witness, not a test (FR-010 covers the behaviour FRs only). It is quickstart §4, run at the
final head and recorded in the verify record:
- the superseded-phrase sweep, with its seeded positive and its seeded negative (`origin/main` must
  report unannotated hits). Its exclusions are pathspecs and a line range in the command itself, and
  its pass condition is executable: zero hit lines that do not name `097`, other than the named
  current-rule list (quickstart §4.1; Gate A round 2, G097-018);
- the positive-presence checks, each with its seeded negative: the same check run against
  `origin/main`'s copy of the file must report the addition **absent**. This proves the check can fail.
  They include the count of the canonical residue sentence (data-model §E.0) at each locus, and the
  `.specify/2c-codegen.md` notes row, whose expected value is one line per note data-model §C
  prescribes (Gate A round 3, G097-020). The Status-line row runs at the RATIFIED flip, not at verify;
- the comment-claims and line-citation checks.

A positive-presence check whose `origin/main` run also reports "present" proves nothing and is
rewritten before it is trusted.

## W-12 — the ApplVerID parse under a fuzzer (`[const §VII.7]`; FR-001, FR-002; analyze D1)

`[const §VII.7]`: "New parser-touching code without a fuzz harness is a Gate B blocker." T018's
two-character arm in `resolve_application_version` (`src/dictionary/version_profile.cpp`) parses a
peer-supplied DefaultApplVerID(1137) or ApplVerID(1128), and no existing harness reaches that function
(research R-11).

- **Files**: `tests/fuzz/fuzz_appl_ver_id.cpp` (libFuzzer target `fuzz_appl_ver_id`), registered in
  `tests/fuzz/CMakeLists.txt` in its siblings' shape: `add_executable`, `target_link_libraries(... PRIVATE
  fixpp_dictionary fixpp_core)`, `-fsanitize=fuzzer,address,undefined` as both compile and link
  options, `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`, and a comment above it naming 097 and
  `[const §VII.7]` (no count, no value list). Its seed corpus is `tests/fuzz/corpus/appl_ver_id/`. The
  directory is named after the target minus `fuzz_`, because the corpus loop in that file registers
  `fuzz_replay_<dir>` against `fuzz_<dir>`. A harness with no replay fails the configure
  (`fixpp_assert_every_fuzz_harness_replays`, `cmake/Helpers.cmake`), and an empty corpus directory
  fails it too (the corpus loop's `FATAL_ERROR` in `tests/fuzz/CMakeLists.txt`). The target exists only under `FIXPP_BUILD_FUZZ` (the `linux-clang-asan` preset; the
  top-level `CMakeLists.txt` refuses it without ASan or UBSan), so it adds nothing to the
  `linux-clang-debug` ctest list.
- **Entry point**: `extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)`. The input
  bytes, as a `std::string_view`, are the ApplVerID value. It is resolved under two profiles: `{session
  = vt11, default_appl = Unknown, has_per_message_override = true}` and the same with `default_appl =
  v50sp2`.
- **Asserts**. Each violation is `__builtin_trap()`, unconditional so it fires under `NDEBUG`, as
  `tests/fuzz/fuzz_decimal_parse.cpp` does:
  - **Empty input** follows the profile-default rule: under the Unknown default the result is the error
    `dict_unresolved_application_version`; under the v50sp2 default it is the value `v50sp2`.
  - **Non-empty input** gives the same outcome under both profiles (the same value, or the same
    error): a non-empty value never consults the default.
  - **Non-empty input refused**: the error is `dict_unknown_appl_ver_id` (FR-001: a refused value keeps
    its error).
  - **Non-empty input accepted**: `render_appl_ver_id(result)` has a value, and it equals the input
    bytes exactly. Exact byte equality is the strict form. Parse accepts only the strings render
    produces, so a parse that accepts a second spelling, or a render that moves, fails it. Re-parse
    equality alone would not: under M-1, `"11"` parses to `vlatest`, renders `"10"`, and `"10"` parses
    back to `vlatest`. The harness does not re-parse the rendering: once the rendered bytes equal the
    input, the parse is a pure function on the same bytes and profile, so a re-parse check cannot fire.
    It was removed at T068 F7.
  - No input aborts, reads out of bounds or reaches UB (ASan and UBSan).
- **Seeds** (`tests/fuzz/corpus/appl_ver_id/`, named `seed_<what>` per the provenance rule in
  `tests/fuzz/CMakeLists.txt`, each written without a trailing newline, e.g. `printf '%s'`): `seed_10`
  (`10`), `seed_010` (`010`), `seed_1O` (`1O`, letter O), `seed_10_trailing_space` (`10 `), `seed_11`
  (`11`), `seed_9` (`9`), `seed_0` (`0`), `seed_empty` (zero bytes).
- **Phase**: the harness names no `vlatest` symbol. It compiles and passes on `origin/main` too, where
  `"10"` is refused like `"11"`, so like a characterization pin its RED comes from mutants only.
- **RED** (re-runs of US1's mutants; no new mutant id; scratch worktree's ASan tree; each row gated on
  build rc 0). Command per row: `build/linux-clang-asan/bin/fuzz_appl_ver_id
  -artifact_prefix=<scratch dir>/ <seed file>` exits non-zero with libFuzzer's `deadly signal`, and the
  whole-corpus replay `build/linux-clang-asan/bin/fuzz_appl_ver_id -artifact_prefix=<scratch dir>/
  -runs=0 tests/fuzz/corpus/appl_ver_id` exits non-zero. Record the seed that fired.
  - M-1 (two-character strings starting `'1'` widened to `vlatest`): `seed_11` traps on the byte-equality
    assertion (`"11"` renders `"10"`), and so does `seed_1O`.
  - M-1n (`"10"` → `v50sp2`): `seed_10` traps (`v50sp2` renders `"9"`).
  - M-2 (`render(vlatest)` returns `"9"`): `seed_10` traps.
- **Non-vacuity**: unmutated, at the same head, `build/linux-clang-asan/bin/fuzz_appl_ver_id
  -artifact_prefix=<scratch dir>/ tests/fuzz/corpus/appl_ver_id/*` exits 0, and every seed file the
  directory holds (`ls`) is named in an `Executed` line of its output. That is the condition; no count
  is written down. libFuzzer's `-runs=0` replay also runs an implicit empty input
  (`fixpp_add_fuzz_replay`'s header in `cmake/Helpers.cmake`), which is one more reason to compare
  names, not totals. The M-1 row proves the trap path can fire.
- **Where it runs**:
  - On every PR, CI's `linux-clang-asan` lane (`FIXPP_BUILD_FUZZ=ON`) replays the seeds as the ctest
    `fuzz_replay_appl_ver_id` (`-runs=0`, label `fuzz`).
  - `[const §VII.7]` also asks for a libFuzzer corpus run of at least ten minutes on every PR. No CI
    job runs a timed libFuzzer campaign (`git grep -n -e '-max_total_time' -- .github` finds none, while `git grep -c -e fuzz -- .github`
    finds the replay lane's lines), so
    that run is `/speckit-verify` Step 7 with `--fuzz-duration=600` on this target (tasks T073),
    recorded in the verify record. The gap between §VII.7's wording and CI's replay-only lane predates
    097; it is reported, not fixed here.
