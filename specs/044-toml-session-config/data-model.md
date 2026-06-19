# Phase 1 Data Model: Native TOML config-file loading

All types below are **C++-only loader-local value types** in namespace `fixpp::config`. None crosses the C ABI (`[const §X.4]`). The loader hydrates the *existing* `fixpp::session::SessionConfig` / `fixpp::core::EngineConfig` — it defines no parallel runtime config.

---

## E-1 — `ConfigBundle`

The validated output of one successful load.

| Field | Type | Notes |
|---|---|---|
| `engine` | `EngineEstablishment` (E-2) | the file-expressible engine-level establishment slice |
| `sessions` | `std::vector<SessionDefinition>` (E-3) | one per `[[session]]`; ≥1 required |

**Invariant:** a `ConfigBundle` is **deliberately incomplete** — it carries everything a file can express and nothing it cannot (E-6). The host completes it (inject `Application`, provide the executor) before opening (FR-010 / research D-4).

---

## E-2 — `EngineEstablishment`

The file-expressible subset of `EngineConfig` (the anchors a session references). Maps onto `EngineConfig` at host-completion time.

| Loader produces | Populates `EngineConfig` field | Source selector |
|---|---|---|
| `std::shared_ptr<Clock>` | `clock` | `clock.kind="system"` (D-5) — **buildable**: the loader receives the engine executor as a load input (DECISION-1) and constructs the live `system_clock_source(LoadOptions.engine_executor)`. **`clock.kind` is REQUIRED at load** (engine/default scope; the ONLY legal value is `system`): absent → `missing_required`, present-but-empty → `empty_required`, any non-`system` token → `unknown_enum`. `EngineConfig::clock` is mandatory (`engine_config.hpp:188-192`, `clock_not_set` at engine open), so a missing clock selector MUST be caught per-key at load — not escape to engine-open as one opaque failure (Codex Gate A round 2 #2; FR-006a parallel). |
| `std::shared_ptr<MessageStoreFactory>` | `default_store_factory` | `store=` (engine-default scope) |
| `std::shared_ptr<cert_source>` | `default_cert_source` | `cert_source=` (engine-default scope) |
| `std::shared_ptr<TransportFactory>` | `default_transport_factory` | `transport=` (engine-default scope) — **buildable**: a TLS factory embeds the SAME `Clock` shared_ptr in its `SslCtxConfig` (D-6), so it too needs the executor-derived clock |
| `std::vector<shared_ptr<const Dictionary>>` | `dictionaries` | `dictionary=` (by **path** only this step — D-5; by-version is `recognized_not_yet_supported_step2`, OQ-1 resolved option A) |

**Load inputs (DECISION-1 + Codex Gate A round 2 #1):** the loader takes `load_toml_config(path, LoadOptions opts)`, where `LoadOptions = { asio::any_io_executor engine_executor; std::pmr::memory_resource* resource = std::pmr::get_default_resource(); }`. This is the **closed at-load input set** (list (A) of the round-2 review):
- **`opts.engine_executor`** — the one host-supplied value available before load (the host builds its `io_context` first). The loader uses it to construct the executor-DEPENDENT objects above (the `system_clock_source`, and transitively the TLS `TransportFactory` via the clock). The host later sets this SAME executor instance on `EngineConfig::executor`; the loader does not populate that field.
- **`opts.resource`** — the **COLD-PATH load-time arena** the loader passes to its load-time object construction: `XmlLoader::load(path, mr)` (precondition `mr != nullptr`, UB-in-release on null — `xml_loader.hpp:51-58`) and `make_file_cert_source(cfg, mr)` (`file_cert_source.hpp:56`). This is a **load-time** allocation arena, **NOT** a session/message hot-path arena — it is distinct from `default_message_resource` / `default_session_resource` and the per-session arenas, which remain host-supplied AFTER load (step-2 / FR-009).

**Left for the host** (NOT set by the loader, injected AFTER load): `executor` (the host sets the `EngineConfig::executor` field — it is a load *input* the loader uses to build the clock/transport, but the loader does not write that field), `file_io_executor`, `default_message_resource`, `default_session_resource` (the post-load runtime arenas — distinct from `opts.resource`, the cold-path **load-time** arena above), `logger`, `tracer`, `meter`, `engine_trace_context`, `control_plane_factory`, **`application`**. (Observability fields = step-2; file_io_executor/runtime-arenas/application = host-supplied per FR-009/FR-010.)

---

## E-3 — `SessionDefinition`

A per-session unit: the merged-and-validated `SessionConfig` for one `[[session]]` (after `[default]` merge, D-8).

**Loader-SET `SessionConfig` fields (file-expressible — the in-scope surface):**

- **Bucket-A scalars (~28):** `sender_comp_id`, `target_comp_id`, `begin_string`, `role`, `mode`, `locks`, `already_serialized_executor` (the FR-011 guard), `heartbeat_interval`, `test_request_threshold`, `sending_time_threshold`, `reject_policy`, `app_backpressure`, `reset_seqnum_policy_field`, `reset_on_logon`, `reset_on_logout`, `reset_on_disconnect`, `refresh_on_logon`, `logout_disconnect_timeout_ms`, `redeliver_poss_dup`, `allow_pos_dup`, `sending_time_precision`, `enable_next_expected_msg_seq_num`, `check_comp_id`, `validate_sequence_numbers`, `validate_inbound_messages`, `default_appl_ver_id`, `username`, `password`.
- **Structured members:** `security_profile`, `compid_authorization_policy`, `reconnect_endpoint`, `reconnect_policy`.
  - **`security_profile.kind` is a REQUIRED per-session key** (after `[default]` merge): absent → `missing_required`, present-but-empty → `empty_required`. No-implicit-default (`[const §XII.5]`) is enforced **at the loader boundary, per-key** — this is the primary boundary; the `Session::open()` `kind::unset` reject (`session.cpp:938`) is retained as defense-in-depth, not the primary check. **Step-1 accepted profiles: `{mtls_ca, one_way_ca, insecure_plain_tcp}`.** `security_profile.kind="mtls_pinned"` is **recognized but NOT buildable from a file in step 1** → `recognized_not_yet_supported_step2` (see E-4 + research D-9a): `mtls_pinned` requires a non-empty `Pinset` at config-build (`make_ssl_ctx_config` hard-rejects a null pinset with `tls_invalid_security_profile`, `security_profile.cpp:72-78`), and the only pinset-population path is `Pinset::add(Certificate const&)` — a parsed runtime object with NO file channel (`pinset.hpp:112`). The programmatic path to `mtls_pinned` is unaffected; only the FILE cannot select it this step.
- **Object-selector results:** `store_factory`, `cert_source`, `dictionary` (**by path only** this step; by-version → `recognized_not_yet_supported_step2`, OQ-1 option A) (and `dialect_overlay` is **DEFERRED**, FR-007a).

**Required-at-load key set (Bucket B — Codex Gate A round 2 #2 + round-2 review list (B)):** after `[default]` merge, each of the following has **no struct default OR is mandatory-to-open**, so absent → `missing_required`, present-but-empty → `empty_required`, bad enum token → `unknown_enum`. Per-key at the loader boundary (the primary check), so a missing establishment key surfaces as a named per-key diagnostic, not a downstream opaque open-time failure (FR-013 / FR-017):

| Required-at-load key | Scope | Why mandatory (oracle) |
|---|---|---|
| `clock.kind` (`system` only) | engine/default | `EngineConfig::clock` mandatory — `clock_not_set` at engine open (`engine_config.hpp:188-192`) |
| `sender_comp_id` | per-session | bare `std::string`, no default (`session_config.hpp`); spec L41/L99 |
| `target_comp_id` | per-session | bare `std::string`, no default |
| `begin_string` | per-session | bare `std::string`, no default |
| `security_profile.kind` | per-session | `kind::unset`→open() reject; FR-006a (already enumerated above) |
| `store.kind` (selector) | per-session (engine-default scope) | no usable default factory; spec L41 "required … store" |
| `dictionary` (by-path selector) | per-session | `SessionConfig::dictionary` annotated `// required` |
| `transport.kind` (selector) | per-session | spec L41 "required … transport"; D-6 `kind()`↔profile |
| `transport.host` / `transport.port` | per-session, **initiator-conditional** | the connect target (`reconnect_endpoint`) — required **only when `role=initiator`**; an acceptor needs no connect target and legitimately omits it (default `Endpoint{host="", port=0}` = not configured) |
| `default_appl_ver_id` | per-session, **FIXT.1.1-conditional** | required **only when `begin_string=="FIXT.1.1"`** — a FIXT session has no fixed application version, so the default application version is mandatory (`session_config.hpp:446-455`; `Session::open()` rejects the missing case at `session.cpp:1003-1006`). Absent → `missing_required`; present-but-empty → `empty_required`; an unknown application-version token → `unknown_enum` (or `malformed_value`). **The load-time rule requires only the key's PRESENCE for a FIXT session** — whether the configured application version is *backed by the engine's loaded dictionary/version registry* is the existing open-time serviceability check (042 — `session.cpp`), which stays at `Session::open()`, not at load. A non-FIXT (`FIX.4.x`) session has its application version implied by `begin_string` and legitimately omits this key. |
| resolved `cert_source` | per-session (selector OR engine `default_cert_source`), **TLS-profile-conditional** | required **only when `security_profile.kind` is a TLS profile (`mtls_ca` or `one_way_ca`)**; NOT required for `insecure_plain_tcp`. The cert_source is resolved from the per-session `cert_source=` selector OR falls back to the engine `default_cert_source` (`engine_config.hpp:139`). A null resolved cert_source for a TLS profile is rejected by `make_ssl_ctx_config` BEFORE the profile switch (`security_profile.cpp:58-61`). Under DECISION-1 the loader builds the TLS factory **at load**, so — unlike the other conditional keys — there is **no `Session::open()` backstop**; the load diagnostic is the only clean surface. Absent (neither selector nor engine default) → `missing_required` attributed to the cert selector. (Positive-requirement companion to research **D-6**: D-6 detects the *contradiction* — a TLS profile with no cert material — at the selector; this row lists the *requirement* at the key boundary. The two are consistent, not duplicative.) |

**NOT required (documented explicit default — STAY optional, FR-013 first clause):** `role` (=initiator), `mode`/`locks`, the `std::optional` scalars `heartbeat_interval`, `test_request_threshold`, `sending_time_threshold` (**EXCEPT `default_appl_ver_id` — also a `std::optional` but FIXT.1.1-conditionally REQUIRED per the row above; it is the one optional-typed scalar that is NOT unconditionally optional**), `reset_seqnum_policy_field` (=bilateral_strict), the four reset/refresh bools (=false), `logout_disconnect_timeout_ms` (=2000), `reconnect_policy` (nullopt→`defaults_quickfix_compat()`), `username`/`password`. The loader MUST NOT over-require these. `transport.kind` is always required, but profile↔`kind()` consistency stays the single `Session::open()` 043 check (validation rule 8), not a duplicated loader rule.

**Loader-LEFT-DEFAULT `SessionConfig` fields (host-supplied / out-of-scope this step):**

`executor_override` (host executor instance — FR-010), `clock_override` (left default → the engine clock is used; the loader builds that engine `Clock` from the load-input executor per E-2 / D-5, so the effective-clock story is consistent: session-level override stays host/engine-resolved, never loader-produced), `message_arena`/`framer_carry_arena`/`session_arena` (arena selection = step-2, FR-009), `initial_trace_context`, `logger_override`/`tracer_override` (observability = step-2), `tap_consumer` (step-2), `transport_send` (legacy sink — superseded by factory), `engine_managed`/`engine_adopt_strand` (engine-internal).

> **`transport_factory_override` is conditionally loader-SET, not purely left** (D-6a): for a session whose cert/profile **matches** the engine default it is left default (the shared engine `default_transport_factory` is used); for a session whose cert/profile **diverges**, the loader mints a **freshly-minted per-session factory instance** and sets it on this field. Each such override is owned by exactly one session → `use_count()==1`, satisfying the `Session::open()` hygiene assertion (`session_config.hpp:298-307`); the loader MUST NEVER set the same override `shared_ptr` on multiple sessions. Only the engine `default_transport_factory` is shared (an engine anchor), never an override.

> This SET/LEFT split is the **host-completion contract** (research D-4). A reviewer or operator reading the bundle must be able to see it is not ready-to-open until the host fills the LEFT-DEFAULT host-supplied items.

---

## E-4 — `ObjectSelector` (internal)

The parsed `{kind, params}` form before resolution. Internal to the resolver; not part of the public bundle.

| Field | Type |
|---|---|
| `seam` | enum `{ store, cert_source, dictionary, transport, clock, dialect_overlay }` |
| `kind` | `std::string` (canonical token, e.g. `file`/`memory`, `tls`/`plaintext`, `system`; for `dictionary` the only accepted kind this step is `path` — `kind="version"` → `recognized_not_yet_supported_step2`, OQ-1 option A) |
| `params` | typed key→value map (paths, ports, caps, durations) |
| `location` | `{ line, col }` for diagnostics |

Resolution → the built-in factory per research **D-5** table. Unknown `kind` → `unknown_enum` (or `invalid_or_contradictory_selector`) diagnostic with the legal set (US3 AC-3).

**Recognized-not-yet-supported tokens (selection deferred to step 2):** `dictionary.kind="version"` (OQ-1 option A), the `dialect_overlay` selector (FR-007a), and `security_profile.kind="mtls_pinned"` (no file-based pinset/pin-material channel — research D-9a) all resolve to `recognized_not_yet_supported_step2`, NOT `unknown_enum` (a deferral reads differently from a typo). The programmatic paths to all three are unaffected.

---

## E-5 — `LoadDiagnostic` + `reason_class`

One structured per-failure report (FR-017). Closed `reason_class` enum:

```
enum class reason_class : std::uint8_t {
    parse_error,                          // TOML syntactically malformed
    unknown_key,                          // unrecognized key (likely typo)
    recognized_not_yet_supported_step2,   // known deferred selection (logger/tracer/meter/tap/arena/dialect_overlay; dictionary kind="version"; security_profile kind="mtls_pinned")
    missing_required,                     // required key absent
    empty_required,                       // present but empty (≠ absent→default)
    malformed_value,                      // type/format wrong (e.g. unitless duration)
    out_of_range,                         // type-correct but out of range (e.g. ≤0 timeout)
    unknown_enum,                         // enum token not a canonical spelling
    invalid_or_contradictory_selector,    // selector backing-resource invalid OR internally contradictory
};
```

| Field | Type | Notes |
|---|---|---|
| `key_path` | `std::string` | e.g. `session[0].heartbeat_interval`, dotted to the offending key |
| `reason` | `reason_class` | |
| `location` | `{ line, col }` | from tomlplusplus `source_region` (FR-017) |
| `message` | `std::string` | human text; **credential values redacted** (`***REDACTED***`) for `username`/`password` keys (FR-019) |

**Collect-ALL (FR-018):** validation accumulates diagnostics across the whole file in one pass; the loader returns the full vector, never stops at the first. `LoadResult = std::expected<ConfigBundle, std::vector<LoadDiagnostic>>` — `std::expected` directly, since `fixpp::core::expected_t<T>` is hard-bound to `error` (single param); no new `fixpp_error_t` (research D-3).

---

## E-6 — Step-2 deferred key set (FR-018a precision)

The known keys/selections that must be reported as `recognized_not_yet_supported_step2` (NOT `unknown_key`), so a deferral reads differently from a typo:

- **Observability/diagnostics keys:** `logger`, `log_sink`, `tracer`, `meter`, `otlp`, `prometheus`, `exporter`, `tap` / `tap_consumer` (FR-009).
- **Arena keys:** `arena` / `message_arena` / `session_arena` / `framer_carry_arena` (FR-009).
- **Deferred selectors:** the `dialect_overlay` selector (FR-007a); `dictionary.kind="version"` (OQ-1 option A); `security_profile.kind="mtls_pinned"` (no file-based pinset channel — research D-9a).
- **`reject_policy` (implementation-discovered, added during `/implement` T013).** `SessionConfig::reject_policy` is a recognized field, but its `RejectPolicy` enum is **forward-declared only** in `session_config.hpp` ("owned by 005"; **no enumerators exist in this checkout**), so no canonical token can be mapped from a file. A present `reject_policy` key therefore resolves to `recognized_not_yet_supported_step2` (recognized field, not yet file-selectable) rather than `unknown_key`. This is a step-1 **capability** limitation pending feature 005's enum; the programmatic path is unaffected. (Witnessed by `neg_multi.toml` in the negative battery.)

Any other unrecognized key → `unknown_key`.

> **NOT in this set — encrypted-PEM private keys.** Step 1 supports **plaintext (unencrypted) private keys only**: a file cannot express a passphrase securely and `file_cert_source`'s `password_cb` has no file channel (`file_cert_source.hpp:43-44`; research D-5). A config referencing an **encrypted** key is therefore NOT a `recognized_not_yet_supported_step2` deferral — it is a **runtime cert-load failure**: `make_file_cert_source` fails gracefully (it returns `expected_t`, noexcept — no terminate) and the loader converts that to an `invalid_or_contradictory_selector` diagnostic naming the cert selector (fail-closed, FR-012). See spec edge case + research D-5.

---

## Validation rules (cross-cutting)

1. **Fail-closed before open** (FR-012): any non-empty diagnostic vector ⇒ no `ConfigBundle` returned; no session is constructed.
2. **No implicit default** (FR-013 / `[const §XII.5]`): absent optional → documented explicit default; absent/empty required → error.
3. **Canonical enum spellings only** (FR-014): unknown token → `unknown_enum` + legal set.
4. **Frozen at open** (FR-015): the loader produces a value snapshot; later file edits have no effect until reload-and-reopen (the existing frozen-at-open invariant).
5. **Selector backing-resource validity** (FR-016): unreadable/unparseable/contradictory backing → `invalid_or_contradictory_selector` attributed to that selector.
6. **Relative-path base** (FR-016a / research D-7): resolve against the config file's directory; absolute verbatim.
7. **`direct_executor` attestation guard** (FR-011): `mode=direct_executor` without `already_serialized_executor=true` → fail closed (the file cannot bypass the serialization attestation).
7a. **`direct_executor` + `spin` guard** (research D-6b): `mode=direct_executor` with `locks=spin` → `invalid_or_contradictory_selector` on the threading key group **at load**, regardless of `already_serialized_executor`. This is a SECOND unsafe threading combination the real `Session::open()` rejects (`session.cpp:904`: a bare attested executor has no engine-internal serialisation for the always-mutex store-write path); the loader must reject it per-key at load so it never reaches `Session::open()` as one opaque failure (preserving the fail-closed-before-open promise, FR-012/FR-017).
8. **Profile↔transport consistency** (research D-6): file-contradiction → diagnostic; the resolved-pair invariant stays the single `Session::open()` 043 check (`session.cpp:946-966`).
9. **noexcept-boundary / throwing-site conversion** (research D-3 / FR-012): the `load_toml_config(...)` entry point is `noexcept`, but several construction/parse sites it must call **throw**: the toml++ parse, the `system_clock_source` ctor (NOT noexcept — `system_clock_source.hpp:55`; **live** because DECISION-1 builds the clock at load time), `XmlLoader::load` (throws `dict::xml_parse_error`/`unknown_version_error`/`xml_oom_error` — no noexcept variant), and the `file_cert_source` direct ctor. The loader MUST wrap every such site in `try/catch` (or `trap_throw`) and convert the throw to a `LoadDiagnostic` (fail-closed) — terminating on malformed input would defeat the feature (US2/FR-012). Where a noexcept `expected_t`-returning factory exists it MUST be preferred over the throwing path: `make_file_cert_source` (`file_cert_source.hpp:56`), `make_asio_{tls,plain}_transport_factory` and `make_ssl_ctx_config` (these already return `expected_t`, so check-the-`expected`, no try/catch). toml++ MUST be pinned to its exception-or-error mode and the chosen `parse_error` channel converted to a `parse_error` diagnostic without a throw escaping the `noexcept` boundary.
