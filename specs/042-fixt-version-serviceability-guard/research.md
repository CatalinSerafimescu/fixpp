# Phase 0 Research: FIXT version-registry serviceability guard at open()

All `/speckit-clarify` items resolved (the single role-scope question, recorded in spec.md
Clarifications). The decisions below pin the mechanism, the role scope, the error disposition, and the
byte-identity boundary.

## D-1 — Mechanism: a third disjunct on the existing FQ-1 guard

**Decision**: Extend the existing FIXT config-load guard at `src/session/session.cpp:940-943`:

```cpp
// existing:
if (cfg_.begin_string == "FIXT.1.1" &&
    (!cfg_.default_appl_ver_id.has_value() || app_version_registry_ == nullptr)) {
    co_return std::unexpected(error::invalid_session_config);
}
// becomes (third disjunct added):
if (cfg_.begin_string == "FIXT.1.1" &&
    (!cfg_.default_appl_ver_id.has_value() || app_version_registry_ == nullptr ||
     !app_version_registry_->get(*cfg_.default_appl_ver_id).has_value())) {
    co_return std::unexpected(error::invalid_session_config);
}
```

**Rationale**:
- Reuses the exact serviceability predicate the inbound runtime path already uses
  (`app_version_registry_->get(...).has_value()`, `session.cpp:2195`) — one notion of "serviceable",
  no divergence.
- Short-circuit safety: the `*cfg_.default_appl_ver_id` deref in the third term is only evaluated when
  the first term (`!has_value()`) is false (⇒ `has_value()` is true) **and** the second term
  (`registry == nullptr`) is false (⇒ registry non-null). So the deref and the call are both guarded by
  the preceding disjuncts. No new null-deref risk.
- `version_registry::get` is `const noexcept` → no new throw from the `co_return` path; consistent with
  the sibling fail-closed guards.
- Lands at the existing shared validation block (alongside the FIXT FQ-1, security-profile sentinel,
  and credential guards) — a guard *extension*, not a new validation phase.

**Alternatives considered**:
- *Self-register / fall back to the configured default* (the L-033-5 "open question"): **rejected** —
  adjudicated against in `REMAINING-WORK.md` line 51 and `library/CLAUDE.md`; it would mask the
  misconfiguration rather than surface it (the opposite of the footgun fix).
- *A separate `validate_config()` method*: rejected — over-engineered for one disjunct; breaks the
  established "all open()-time guards live in the one block" pattern.

## D-2 — Role scope: role-agnostic

**Decision**: The guard applies to **both** acceptor and initiator (no role gate). See spec.md
Clarifications (2026-06-17).

**Rationale**:
- **QuickFIX-cpp** `SessionFactory::create` validates the FIXT `AppDataDictionary` for the configured
  `DefaultApplVerID` at config-load **role-independently** (verified:
  `reference-engines/quickfix-cpp/src/C++/SessionFactory.cpp:38-68` requires `DEFAULT_APPLVERID` for
  any FIXT session and runs `processFixtDataDictionaries` :279-307, which throws `ConfigError` on a
  missing/malformed app dictionary; the only role-specific check there is the unrelated
  `SessionQualifier`). "Like QuickFIX" ⇒ role-agnostic.
- **Orthogonal to 033 FR-004a** (`specs/033-fixt-fix50sp2-session/spec.md:100`): FR-004a scopes the
  *runtime* refuse on a **peer-advertised** unserviceable `1137` to the acceptor (the initiator does
  not *refuse* on the peer's version). 042 validates **this** side's **own configured** default at
  config-load — a different axis. Role-agnostic 042 does not contradict acceptor-scoped FR-004a.
- **Least code** (one shared path, no role branch) and closes the initiator misconfiguration footgun
  too.

**Empirical obligation (implement-time)**: confirm the existing initiator FIXT session tests stay
green. The `FixtSetup` fixture (`tests/session/test_fixt_logon_establishment.cpp:631-680`) registers
the matching dictionary for both `make_acceptor_cfg` and `make_initiator_cfg`, so the serviceable path
holds and no regression is expected. Any existing test that opens a FIXT session with an
**unserviceable own configured default** must be reconciled to the new fail-closed contract (none
expected; the unserviceable-version tests inject the bad version on the **peer's wire frame**, not the
session's own config).

## D-3 — Error disposition: reuse `error::invalid_session_config`

**Decision**: Return the existing `error::invalid_session_config` (slot 53) — the same value the
sibling FQ-1 / security-profile / credential open() guards already return.

**Rationale**: No new error slot is warranted — this is a configuration-invalid condition at
config-load, exactly what `invalid_session_config` denotes. Minting a distinct code would expand the
frozen error taxonomy for no operator benefit (the failure is at `open()`, with the config in hand).
Consistent with FR-007 (no new surface).

**Alternative**: a dedicated `error::version_unserviceable` — rejected (taxonomy bloat; the open()
caller already handles `invalid_session_config` as "fix your config").

## D-4 — Byte-identity boundary (regression safety)

**Decision**: Three invariants pinned as witnesses:
1. **Correctly-configured FIXT** (serviceable default): `open()` succeeds; establishment byte-identical
   to pre-feature (FR-003 / SC-002).
2. **Non-FIXT** (`begin_string != "FIXT.1.1"`): the outer `begin_string == "FIXT.1.1"` gate
   short-circuits ⇒ the new disjunct is never evaluated ⇒ byte-identical (FR-004).
3. **Inbound peer-`1137` reject still live** (SC-003): a session with a *serviceable* own default that
   receives a Logon advertising a *different, unserviceable* peer version still hits the runtime
   `Reject(35=3, 371=1137, 373=5)` at :2186-2200 — the new open() guard does not make that path dead.

**Rationale**: These are the exact non-regression and non-deadness claims that distinguish a sound
guard from one that over-rejects (FR-003/FR-004) or silently subsumes the inbound check (FR-005).

## D-5 — Comment reachability correction

**Decision**: Update the `session.cpp:923-939` guard comment. The existing comment documents arm #2
(`app_version_registry_ == nullptr`) as "**Structurally unreachable in production** (engine always
passes non-null)". The new arm is the **opposite**: it is **production-reachable** — a real engine with
a non-null registry that simply lacks the dictionary for this session's configured default. The comment
must state the new arm's reachability so the witness's use of a real non-null registry isn't read as
contradicting the documented-unreachable #2 arm (this repo punishes comment drift —
[[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]]).
