# Phase 1 Data Model: FIXT version-registry serviceability guard at open()

**No new entities, fields, or types.** This feature adds one boolean predicate evaluation at an
existing validation site. The "data model" is the guard's truth table over existing state.

## Existing state consumed (read-only)

| Symbol | Type | Source | Role |
|---|---|---|---|
| `cfg_.begin_string` | `std::string` | `SessionConfig` | FIXT gate (`== "FIXT.1.1"`) |
| `cfg_.default_appl_ver_id` | `std::optional<dict::application_version>` | `SessionConfig` (`:455`) | this side's own configured default app version (already the resolved enum — no wire-string step) |
| `app_version_registry_` | `const dict::version_registry*` (nullable, non-owning) | engine-built, set on the Session | serviceability authority |
| `version_registry::get(application_version) const noexcept` | `→ expected_t<Dictionary const*>` | `version_registry.hpp:56` | "serviceable" ⇔ `.has_value()` |

## Serviceability predicate

```
serviceable(v) := app_version_registry_ != nullptr
                  && app_version_registry_->get(v).has_value()
```

Identical to the inbound runtime check at `session.cpp:2194-2195` (one notion of serviceability).

## Guard truth table (the FQ-1 block, `session.cpp:940-943`, post-extension)

Outer gate: `cfg_.begin_string == "FIXT.1.1"`. If false → guard skipped entirely (non-FIXT
byte-identical, FR-004).

| `default_appl_ver_id` | `registry` | `registry.get(default)` | open() result | Disjunct | New? |
|---|---|---|---|---|---|
| absent | — | — | `unexpected(invalid_session_config)` | #1 | existing |
| present | `nullptr` | — | `unexpected(invalid_session_config)` | #2 (documented prod-unreachable) | existing |
| present | non-null | **empty** (unserviceable) | `unexpected(invalid_session_config)` | **#3** | **NEW (prod-reachable)** |
| present | non-null | has value (serviceable) | proceed (open succeeds) | — | existing (byte-identical) |

**Reachability note**: arm #2 is documented "structurally unreachable in production" (engine always
passes a non-null registry). Arm **#3 is production-reachable** — a real engine whose registry simply
lacks the dictionary for this session's configured default. The witness exercises #3 with a real
non-null registry missing the dict (NOT via the null-registry #2 arm).

## State transitions

None. This is a pre-establishment config-load validation; on failure the session never leaves the
pre-open state (no FSM transition, no wire emission, no store mutation — fail-closed before any
observable effect, FR-002).

## Invariants

- **INV-042-1 (one serviceability notion)**: the open() predicate and the inbound runtime predicate
  (`:2195`) call the same `version_registry::get(...).has_value()` — they never diverge.
- **INV-042-2 (inbound non-deadness)**: the open() guard validates this side's **own** configured
  default; it does NOT subsume the inbound **peer-advertised**-`1137` serviceability reject (a peer can
  advertise a version different from this side's serviceable default). The :2186-2200 path stays live.
- **INV-042-3 (fail-closed ordering)**: the guard sits in the shared open() validation block before any
  observable mutation/emission, consistent with the sibling FQ-1 / security-profile / credential guards.
