# Quickstart: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

How to configure, exercise, and validate the feature.

## Configure a FIXT / 5.0 SP2 session

```cpp
fixpp::session::SessionConfig c;
c.begin_string = "FIXT.1.1";                                  // transport version
c.default_appl_ver_id = fixpp::dict::application_version::v50sp2;  // std::optional<application_version> (E3)
// optional credentials:
// c.username = "...";  c.password = "...";
// ... existing fields (comp ids, dictionary/registry, transport, etc.)
```

Retrieve the negotiated application version from a `fromApp` handler (FR-005 / SC-006):

```cpp
auto s = engine.lookup(sid);                            // existing Engine::lookup spine
auto vp = s->negotiated_version_profile();              // NEW accessor — {vt11, default_appl=negotiated}
// The fixpp session delivers app messages dict-free and never reifies (R4). A downstream
// consumer that chooses to reify uses vp (the negotiated default dictionary). Note: reify
// honors a present ApplVerID(1128) per its normal rules — opting into per-message override is
// the consumer's choice, outside 033's session scope (S-026). 033 exposes only the negotiated default.
```

- `begin_string == "FIXT.1.1"` + `default_appl_ver_id` set ⇒ FIXT session (`is_fixt()`).
- Omit `default_appl_ver_id` (or use `8=FIX.4.4`) for the unchanged FIX.4.x path (byte-identical, C2).
- Version-general: set `default_appl_ver_id` to FIX.4.4 to run FIX.4.4 *over* FIXT.1.1 (C6).

## What happens on establishment

- **Outbound Logon** carries `8=FIXT.1.1` + `1137` (+ optional `553`/`554`) — both initiator and acceptor
  reply (C1).
- **Inbound Logon** must carry `1137`; missing ⇒ `Reject(35=3, 373=RequiredTagMissing=1)` (C4);
  present-but-unserviceable version ⇒ `Reject(35=3, 371=1137, 373=ValueIsIncorrect=5)` then refuse (C5).
  The negotiated version is recorded + exposed (`negotiated_version_profile()`) so downstream reify
  call-sites select the application dictionary (C3); session-layer delivery to `fromApp` stays dict-free.
- `Password(554)` is redacted in logs/transcripts/goldens (C8).

## Validate (unit)

```sh
cd research/G19-fix-fpml-iso20022/library
# build the FIXT establishment suites (debug)
cmake --build build/linux-clang-debug --target fixpp-tests -j2     # confirm exact targets at /tasks
ctest --test-dir build/linux-clang-debug -R 'fixt' --output-on-failure
```

Witnesses (research.md R8): W1 round-trip, W2 missing-1137 reject, W3 unserviceable refuse, W4 FIX.4.x
byte-identical regression guard, W5 4.4-over-FIXT, W6 credentials, W7 554 redaction.

## Validate (live interop, both engines, both roles)

Run from the parent harness (live cells need real TCP — run outside the sandbox / with sockets allowed,
the 032 close-out pattern):

```sh
cd research/G19-fix-fpml-iso20022/phase-9-harness
# capture goldens (2-pass), then verify flag-free (example: one of the 8 cells):
python3 tools/run_interop_cell.py HP-fixt50sp2-qfcpp-init --config normal --update-goldens   # x2
python3 tools/run_interop_cell.py HP-fixt50sp2-qfcpp-init --config normal                    # expect: pass, golden match
```

**The 8 cells** (2 dialect families × 2 roles × {QFcpp, QFJ}):

| family | role | QFcpp cell | QFJ cell |
|--------|------|-----------|----------|
| FIX.5.0SP2 | fixpp initiator | `HP-fixt50sp2-qfcpp-init` | `HP-fixt50sp2-qfj-init` |
| FIX.5.0SP2 | fixpp acceptor | `HP-fixt50sp2-qfcpp-acc` | `HP-fixt50sp2-qfj-acc` |
| FIXT-carrying-4.4 | fixpp initiator | `HP-fixt44-qfcpp-init` | `HP-fixt44-qfj-init` |
| FIXT-carrying-4.4 | fixpp acceptor | `HP-fixt44-qfcpp-acc` | `HP-fixt44-qfj-acc` |

**Counterparty config templates**: `TransportDataDictionary=FIXT11.xml`;
`AppDataDictionary=FIX50SP2.xml` (50sp2 family) / `FIX44.xml` (4.4 family);
`DefaultApplVerID=9` (50sp2) / `6` (4.4). Exact cell ids/paths registered at /tasks/implement. After they
pass, flip the manifest from `deferred:fixt-routing` to live and bank goldens (C10). `Password(554)` is
redacted by `run_interop_cell.py`'s shared tag-554 redactor before any golden is written (C8).

## Done when

- All FRs map to a passing witness; SC-001..SC-006 met; FIX.4.x suites byte-identical (SC-002).
- The `HP-fixt11-fix50sp2-cells` axis is no longer `deferred:fixt-routing`; live cells pass both engines.
- Catalogue/coverage/B&L deltas applied (plan §VI delta).
