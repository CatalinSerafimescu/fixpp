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
- **fixpp's own grounding (authoritative)**: the MANDATORY fail-closed serviceability requirement is
  grounded in fixpp's own **L-033-5 disposition** (the adjudicated open()-guard fix), the **symmetric
  `open()` path** (one shared validation block, no role gate), and **Constitution §XII.5** fail-closed
  establishment — NOT in QuickFIX.
- **QuickFIX-cpp (corroborating reference-engine evidence for role-independence only)**:
  `SessionFactory::create` performs FIXT app-dictionary config-load processing **role-independently when
  data dictionaries are enabled/configured** (verified:
  `reference-engines/quickfix-cpp/src/C++/SessionFactory.cpp:38-68` requires `DEFAULT_APPLVERID` for
  any FIXT session role-independently and, when `UseDataDictionary` is true, runs
  `processFixtDataDictionaries` :279-307; the only role-specific check there is the unrelated
  `SessionQualifier`). This corroborates the *role-independence* of FIXT config-load processing; it does
  NOT by itself mandate that the configured default be serviceable in every FIXT session
  (`UseDataDictionary=N` skips processing; `processFixtDataDictionaries` iterates only *present*
  `AppDataDictionary` keys). The `reference-engines/...` path is **parent-repo-relative**. ⇒ role-agnostic.
- **Orthogonal to 033 FR-004a** (`specs/033-fixt-fix50sp2-session/spec.md:100`): FR-004a scopes the
  *runtime* refuse on a **peer-advertised** unserviceable `1137` to the acceptor (the initiator does
  not *refuse* on the peer's version). 042 validates **this** side's **own configured** default at
  config-load — a different axis. Role-agnostic 042 does not contradict acceptor-scoped FR-004a.
- **Least code** (one shared path, no role branch) and closes the initiator misconfiguration footgun
  too.

**Empirical obligation (implement-time)**: confirm the existing initiator FIXT session tests stay
green. The `FixtSetup` fixture (`tests/session/test_fixt_logon_establishment.cpp:631-680`) registers
the matching dictionary for both `make_acceptor_cfg` and `make_initiator_cfg`, so the serviceable path
holds and no regression is expected. **Two existing inbound witnesses DO configure an unserviceable own
default and MUST be rewritten** under 042 (Gate A round-1 correction — an earlier draft wrongly claimed
"none expected" and that the unserviceable-version tests inject the bad version only on the peer wire
frame): `W3_Unserviceable1137_AcceptorRejectsWithVII_NotActive`
(`test_fixt_logon_establishment.cpp:887`) and
`W_Unserviceable1137_ToAdminObserved_ValueIsIncorrect_Disconnected`
(`test_fixt_logon_establishment.cpp:1302`) both build `FixtSetup s{{v44_dict}}` (v44-only registry),
configure this side's own `make_acceptor_cfg(application_version::v50sp2)`, and call `open()` (result
discarded) **relying on it succeeding** before injecting the peer frame `1137=9`. Under 042's third
disjunct `open()` now FAILS for both (v44-only registry cannot serve the configured v50sp2 default), so
`on_inbound_frame` would run against an unopened session and the 033 FR-004a inbound reject path goes
unwitnessed. They MUST be rewritten so **this side's own default is serviceable** (own default = a
registry-served version), NOT edited-green by dropping their inbound-reject assertions (that would
silently erode merged 033 FR-004a coverage). To keep the own default serviceable AND still drive the
inbound reject requires a **three-distinct-version registry** — see D-2a.

## D-2a — Inbound-non-deadness (SC-003) witness: a new three-version-registry witness

**Decision**: The SC-003 inbound-non-deadness witness is a **NEW** witness, not a reuse of the existing
033/038 inbound reject witness. To keep this side's own default serviceable (so `open()` succeeds) AND
still drive the inbound `Reject(373=5)`, the registry must serve this side's own default but NOT the
peer's advertised version — a **three-distinct-version** setup: e.g. registry `{v44, v50sp2}`, own
default = v44 (serviceable ⇒ `open()` succeeds), peer Logon advertises a version the registry does NOT
hold (e.g. `1137="8"` = v50sp1, absent). `FixtSetup`'s ctor takes a `vector` of dicts, so the
multi-dict registry is constructible. The witness MUST carry a mutation/non-deadness assertion that the
inbound `Reject(35=3, 371=1137, 373=5)` path still fires (so 042's open() guard is shown NOT to subsume
or make dead the inbound peer-version check).

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

**Stale-ref correction (Gate A round-1)**: the existing comment at `session.cpp:932` points the inbound
serviceability gate at `session.cpp:1986`, which is **stale** (pre-existing, not 042's fault). The real
inbound `app_version_registry_->get` reject lives at ~`session.cpp:2172-2221` (call at `:2195`). The
D-5 rewrite MUST correct the inbound ref to the real line and MUST NOT propagate the stale `:1986` ref
forward.
