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

Reuse the existing inbound unserviceable-`1137` reject witness (033/038 W3 class): a session with a
**serviceable** own default that receives a Logon advertising a **different unserviceable** peer
version still emits `Reject(35=3, 371=1137, 373=5)` at runtime. Assert it is unaffected by the new
open() guard.

## Coverage

The new guard branch (disjunct #3 true arm) must show a covered lcov DA line + taken BRDA branch
(`[const §IX.1]`). Re-measure on the `fixpp_session_tests` binary after the witnesses land.
