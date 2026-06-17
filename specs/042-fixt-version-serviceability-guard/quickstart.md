# Quickstart: FIXT version-registry serviceability guard at open()

## What this feature does

A FIXT session (acceptor or initiator) configured with a `default_appl_ver_id` the engine cannot serve
(no application dictionary registered for it) now **fails at `open()`** with `invalid_session_config`,
instead of opening and silently rejecting every inbound FIXT Logon. Closes L-033-5.

## Running the witnesses

All witnesses live in `tests/session/test_fixt_logon_establishment.cpp` (existing `FixtSetup` fixture).

```bash
# from the library submodule root
cmake --preset linux-clang-debug                     # if not already configured
cmake --build build/linux-clang-debug --target fixpp_session_tests -j2
ctest --test-dir build/linux-clang-debug -R FixtLogonEstablishment --output-on-failure
```

### RED-first / mutation discipline (W1)

1. Write W1 (acceptor, unserviceable own default → `open()` == `invalid_session_config`) and confirm it
   **fails on main** (before adding disjunct #3) — `open()` currently succeeds.
2. Add the disjunct (`src/session/session.cpp:940-943`) → W1 passes.
3. Mutation check: comment out disjunct #3 → rebuild → W1 must fail again (proves the witness
   discriminates the new arm, not a pre-existing guard).

### Building the unserviceable-own-default fixture

The fixture builds the registry from the dicts you pass:

```cpp
// serviceable: registry has v50sp2, config default = v50sp2  → open() succeeds (W3)
FixtSetup s{{ make_dict(kMinimalFix50sp2Xml) }};
auto cfg = s.make_acceptor_cfg(application_version::v50sp2);

// UNSERVICEABLE: registry has ONLY v44, config default = v50sp2 → open() == invalid_session_config (W1)
FixtSetup s{{ make_dict(kMinimalFix44Xml) }};
auto cfg = s.make_acceptor_cfg(application_version::v50sp2);   // registry can't serve v50sp2
// ... construct Session with &s.registry, call open(), assert std::unexpected(invalid_session_config)

// W2: identical to W1 but s.make_initiator_cfg(...) — role-agnostic, same result
```

> Note: this is the registry-present-but-cannot-serve arm (#3, production-reachable). Do NOT witness it
> via a null registry (that is the pre-existing, documented-prod-unreachable arm #2).

### Inbound non-deadness (W4, SC-003)

This is a **NEW three-version-registry witness**, NOT a reuse of the existing 033/038 inbound reject
witness. To keep this side's own default serviceable (so `open()` succeeds) AND still drive the inbound
reject, the registry must serve this side's own default but NOT the peer's advertised version — a
**three-distinct-version** setup:

```cpp
// registry serves {v44, v50sp2}; own default = v44 (serviceable → open() succeeds);
// peer Logon advertises a version the registry LACKS (e.g. 1137="8" = v50sp1, absent)
FixtSetup s{{ make_dict(kMinimalFix44Xml), make_dict(kMinimalFix50sp2Xml) }};
auto cfg = s.make_acceptor_cfg(application_version::v44);   // serviceable → open() succeeds
// ... open() succeeds; inject peer FIXT Logon with 1137="8" (absent) → assert Reject(35=3,371=1137,373=5)
```

Carry a mutation/non-deadness assertion that the inbound `373=5` path still fires (the new open() guard
does NOT subsume or make dead the inbound peer-version check).

> **The two inherited inbound witnesses MUST be REWRITTEN, not edited-green.**
> `W3_Unserviceable1137_AcceptorRejectsWithVII_NotActive` (`test_fixt_logon_establishment.cpp:887`) and
> `W_Unserviceable1137_ToAdminObserved_ValueIsIncorrect_Disconnected` (`:1302`) currently set this
> side's own default to the unserviceable v50sp2 against a v44-only registry — under 042 their `open()`
> now FAILS. Rewrite them so this side's own default is serviceable (preserving the 033 FR-004a inbound
> reject coverage); do NOT drop their inbound-reject assertions to make them green.

## Coverage

The new guard branch (disjunct #3 true arm) must show a covered lcov DA line + taken BRDA branch
(`[const §IX.1]`). Re-measure on the `fixpp_session_tests` binary after the witnesses land.
