# Quickstart: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

How to configure, exercise, and validate the feature.

## Configure a FIXT / 5.0 SP2 session

```cpp
fixpp::session::SessionConfig c;
c.begin_string = "FIXT.1.1";              // transport version
c.default_appl_ver_id = /* FIX.5.0SP2 */; // negotiated application version (E3)
// optional credentials:
// c.username = "...";  c.password = "...";
// ... existing fields (comp ids, dictionary/registry, transport, etc.)
```

- `begin_string == "FIXT.1.1"` + `default_appl_ver_id` set ⇒ FIXT session (`is_fixt()`).
- Omit `default_appl_ver_id` (or use `8=FIX.4.4`) for the unchanged FIX.4.x path (byte-identical, C2).
- Version-general: set `default_appl_ver_id` to FIX.4.4 to run FIX.4.4 *over* FIXT.1.1 (C6).

## What happens on establishment

- **Outbound Logon** carries `8=FIXT.1.1` + `1137` (+ optional `553`/`554`) — both initiator and acceptor
  reply (C1).
- **Inbound Logon** must carry `1137`; missing ⇒ `Reject(35=3, 373=1)` (C4); unserviceable version ⇒
  refuse (C5). The negotiated version selects the application dictionary for inbound app messages (C3).
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
# capture goldens (2-pass), then verify flag-free:
python3 tools/run_interop_cell.py <FIXT-cell-id> --config normal --update-goldens   # x2
python3 tools/run_interop_cell.py <FIXT-cell-id> --config normal                    # expect: pass, golden match
```

Cell families (both roles × QFcpp/QFJ): a FIX.5.0SP2 family and a FIXT-carrying-FIX.4.4 family;
counterparty configured with a FIXT.1.1 transport dictionary + the matching application dictionary
(exact cell ids + configs registered at /tasks/implement). After they pass, flip the manifest from
`deferred:fixt-routing` to live and bank goldens (C10).

## Done when

- All FRs map to a passing witness; SC-001..SC-006 met; FIX.4.x suites byte-identical (SC-002).
- The `HP-fixt11-fix50sp2-cells` axis is no longer `deferred:fixt-routing`; live cells pass both engines.
- Catalogue/coverage/B&L deltas applied (plan §VI delta).
